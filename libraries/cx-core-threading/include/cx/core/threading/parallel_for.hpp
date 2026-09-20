// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// @file
/// @brief cx::core::threading::parallel_for - fork-join over an index range.
///
/// thread_pool is a queue of independent jobs: each submission allocates a
/// std::function, takes the queue mutex and wakes a worker - some hundreds of
/// nanoseconds, which is nothing next to the work it carries and far too much
/// for a compute kernel cut into a hundred chunks of a few microseconds. This
/// is the other shape: one caller, one job, every core, return when the last
/// chunk is done. Nothing is allocated and no lock is taken on the way in or
/// out:
///
///  - the job lives on the caller's stack and is published through one atomic
///    pointer; chunks are claimed with a fetch_add, so which thread runs a
///    chunk never changes what the chunk computes;
///  - a worker spins briefly for the next job (the ops of one compute step
///    arrive back to back) and then sleeps on the generation counter, so an
///    idle process burns nothing;
///  - the caller always takes chunk 0 and helps drain the rest, a parallel_for
///    issued from inside a chunk runs inline, and concurrent callers take turns.
///
/// The thread count comes from `CX_CORE_THREADS` (0 or unset: every hardware
/// thread) and the spin budget from `CX_CORE_SPIN`, both read once.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
namespace cx::core::threading::config {
/// @brief One step of a busy-wait: tells the core it is spinning, without entering the kernel.
inline void pause() noexcept { _mm_pause(); }
} // namespace cx::core::threading::config
#else
namespace cx::core::threading::config {
inline void pause() noexcept { std::this_thread::yield(); }
} // namespace cx::core::threading::config
#endif

namespace cx::core::threading {

/// @brief The process-wide workers behind parallel_for; `run(chunks, job)`
/// calls `job(t)` once for every t in [0, chunks) and returns when all have.
class fork_join_pool {
public:
  static std::size_t thread_count() {
    static const std::size_t n = [] {
      if (const char *env = std::getenv("CX_CORE_THREADS")) {
        const long v = std::strtol(env, nullptr, 10);
        if (v > 0) {
          return static_cast<std::size_t>(v);
        }
      }
      const unsigned hw = std::thread::hardware_concurrency();
      return hw == 0 ? std::size_t{1} : static_cast<std::size_t>(hw);
    }();
    return n;
  }

  /// @brief Pauses a worker spins through before it sleeps (0 = sleep at once).
  static long spin_budget() {
    static const long n = [] {
      const char *env = std::getenv("CX_CORE_SPIN");
      const long v = env != nullptr ? std::strtol(env, nullptr, 10) : 2000L;
      return v < 0 ? 0L : v;
    }();
    return n;
  }

  static fork_join_pool &instance() {
    static fork_join_pool pool(thread_count());
    return pool;
  }

  explicit fork_join_pool(std::size_t threads) {
    workers_.reserve(threads > 0 ? threads - 1 : 0);
    for (std::size_t t = 1; t < threads; ++t) {
      workers_.emplace_back([this] { serve(); });
    }
  }

  fork_join_pool(const fork_join_pool &) = delete;
  fork_join_pool &operator=(const fork_join_pool &) = delete;

  ~fork_join_pool() {
    stop_.store(true);
    generation_.fetch_add(1);
    generation_.notify_all();
    for (auto &w : workers_) {
      w.join();
    }
  }

  /// @brief Whether the calling thread is already inside a chunk - a pool worker, or a caller running its share.
  static bool inside() { return inside_flag(); }

  std::size_t size() const { return workers_.size() + 1; }

  template <class job_type> void run(std::size_t chunks, job_type &&fn) {
    if (chunks == 0) {
      return;
    }
    std::lock_guard<std::mutex> turn(callers_);

    // The caller runs chunks too, so for the length of this job it is inside
    // the pool like any worker: a parallel_for it issues from a chunk must run
    // inline, not come back here for a turn it already holds.
    struct inside_scope {
      bool previous = inside_flag();
      inside_scope() { inside_flag() = true; }
      ~inside_scope() { inside_flag() = previous; }
    } inside_this_job;

    struct bound_type {
      job_type *fn;
      static void invoke(void *self, std::size_t t) { (*static_cast<bound_type *>(self)->fn)(t); }
    } bound{&fn};

    job current;
    current.invoke = &bound_type::invoke;
    current.context = &bound;
    current.chunks = chunks;
    current.next.store(1, std::memory_order_relaxed); // chunk 0 is the caller's
    current.pending.store(chunks - 1, std::memory_order_relaxed);

    job_.store(&current);
    generation_.fetch_add(1);
    generation_.notify_all();

    try {
      fn(std::size_t{0});
    } catch (...) {
      current.record(std::current_exception());
    }
    drain(current); // the caller helps with whatever is still unclaimed
    for (std::size_t spin = 0; current.pending.load(std::memory_order_acquire) != 0; ++spin) {
      if (spin < 20000) {
        config::pause();
      } else {
        std::this_thread::yield(); // a chunk is taking long: stop burning the core
      }
    }

    // `current` dies with this frame, so no worker may still hold it. A worker
    // announces itself in active_ *before* it reads job_, and job_ is cleared
    // here *before* active_ is read: whichever order the two threads interleave
    // in, either the worker sees nullptr or this loop sees the worker.
    job_.store(nullptr);
    while (active_.load() != 0) {
      config::pause();
    }

    if (current.failure) {
      std::rethrow_exception(current.failure);
    }
  }

private:
  struct job {
    void (*invoke)(void *, std::size_t) = nullptr;
    void *context = nullptr;
    std::size_t chunks = 0;
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> pending{0};
    std::atomic<bool> failed{false};
    std::exception_ptr failure; ///< written by whichever thread wins `failed`

    void record(std::exception_ptr e) {
      if (!failed.exchange(true)) {
        failure = std::move(e);
      }
    }
  };

  static bool &inside_flag() {
    static thread_local bool flag = false;
    return flag;
  }

  /// @brief Claims and runs chunks until none is left unclaimed.
  static void drain(job &j) {
    for (;;) {
      const std::size_t t = j.next.fetch_add(1, std::memory_order_acq_rel);
      if (t >= j.chunks) {
        return;
      }
      try {
        j.invoke(j.context, t);
      } catch (...) {
        j.record(std::current_exception());
      }
      j.pending.fetch_sub(1, std::memory_order_release);
    }
  }

  void serve() {
    inside_flag() = true;
    std::size_t seen = generation_.load();
    for (;;) {
      // Checked only after `seen` was read: a stop requested later than this
      // also bumps the generation past `seen`, so the wait below cannot sleep
      // through it - including for a worker that starts after its pool died.
      if (stop_.load()) {
        return;
      }
      // Spin briefly first: inside a compute step the next job is microseconds
      // away, and a sleeping worker costs a futex wake-up per job. Briefly is
      // the point - idle hyper-threads spinning next to the working ones cost
      // them clock and power budget.
      for (long spin = 0; spin < spin_budget() && generation_.load(std::memory_order_acquire) == seen; ++spin) {
        config::pause();
      }
      generation_.wait(seen); // returns at once when it has already moved on
      seen = generation_.load();
      active_.fetch_add(1);
      if (job *j = job_.load()) {
        drain(*j);
      }
      active_.fetch_sub(1);
    }
  }

  std::vector<std::thread> workers_;
  std::mutex callers_;
  std::atomic<std::size_t> generation_{0};
  std::atomic<job *> job_{nullptr};
  std::atomic<std::size_t> active_{0}; ///< workers between announcing themselves and leaving drain()
  std::atomic<bool> stop_{false};
};

/// @brief How many chunks a range is cut into per thread: cores are not equal
/// (performance and efficiency cores, hyper-thread siblings), and with one
/// equal slice each a range lasts as long as its slowest core.
inline constexpr std::size_t slices_per_thread = 4;

/// @brief Runs `fn(begin, end)` over `[0, n)` split into contiguous chunks
/// across threads; `min_per_thread` caps the split so small ranges stay on
/// the calling thread.
template <class function_type> void parallel_for(std::size_t n, std::size_t min_per_thread, function_type &&fn) {
  const std::size_t grain = std::max<std::size_t>(1, min_per_thread);
  const std::size_t threads = std::min(fork_join_pool::thread_count(), std::max<std::size_t>(1, n / grain));
  if (threads <= 1 || n == 0 || fork_join_pool::inside()) {
    fn(std::size_t{0}, n);
    return;
  }
  const std::size_t pieces = std::min(threads * slices_per_thread, std::max<std::size_t>(1, n / grain));
  const std::size_t chunk = (n + pieces - 1) / pieces;
  const std::size_t chunks = (n + chunk - 1) / chunk;
  fork_join_pool::instance().run(chunks, [&fn, n, chunk](std::size_t t) {
    const std::size_t begin = t * chunk;
    fn(begin, std::min(n, begin + chunk));
  });
}

} // namespace cx::core::threading
