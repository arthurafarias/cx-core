// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// @file
/// @ingroup core
/// @brief cx::core::io::event_loop - the readiness reactor at the
/// heart of the networking layer.
///
/// The analogue of libuv's loop behind Node.js. It is a single
/// cx::core::threading::task driving one readiness cycle per iteration: fd
/// readiness callbacks, deferred functions (`queueMicrotask` /
/// `process.nextTick`), and timers (`setTimeout`) all run on that one loop
/// thread, in that order, so socket listeners never race each other.
///
/// The syscall that blocks for readiness - `poll` or `epoll` - lives behind
/// cx::core::io::poller (SRS-019 §4); the loop owns one
/// `poller::state` and never names a syscall family. The `posix` poller
/// backend auto-selects the engine (`epoll` on Linux, `poll` elsewhere;
/// `CX_NETWORKING_IO_BACKEND` overrides). io_backend() reports which is live.
///
/// The loop is the `serialized` cx::core::threading::async_executor (SRS-011):
/// defer() is its next-tick queue, and offload() hands blocking work
/// (`getaddrinfo`, disk) to a backing cx::core::threading::thread_pool so it
/// never runs on the loop thread - the result is marshalled back with defer().

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include <cx/core/threading/async_executor.hpp>
#include <cx/core/io/poller.hpp>
#include <cx/core/io/poller_events.hpp>
#include <cx/core/threading/task.hpp>
#include <cx/core/threading/task_priority.hpp>
#include <cx/core/threading/thread_pool.hpp>

namespace cx::core::io {

/// @ingroup core
/// @brief Single-threaded readiness reactor: fd watches, deferred work, and
/// timers.
///
/// One instance owns one dedicated thread (via cx::core::threading::task) and
/// one cx::core::io::poller state. The watch / defer / timer entry
/// points are thread-safe and may be called from any thread; their callbacks
/// always run on the loop thread. Not copyable.
class event_loop : public threading::async_executor {
public:
  /// @brief Callback for an fd watch. @p revents carries the backend-neutral
  /// readiness mask (cx::core::io::io_event).
  using io_callback = poller::callback;
  /// @brief A function queued with defer() or set_timeout().
  using deferred = std::function<void()>;
  /// @brief Opaque handle identifying a scheduled timer; pass to
  /// clear_timeout().
  using timer_id = std::uint64_t;

  /// @brief Allocate an idle loop on the heap.
  /// @return A `shared_ptr` owning the new loop.
  static std::shared_ptr<event_loop> create() {
    return std::make_shared<event_loop>();
  }

  /// @brief Process-wide shared loop (Meyers singleton over a `shared_ptr`).
  ///
  /// The default loop used by every protocol `create()` factory when no
  /// explicit loop is passed.
  static std::shared_ptr<event_loop> default_instance() {
    static std::shared_ptr<event_loop> loop = create();
    return loop;
  }

  /// @brief Construct an idle loop. The thread is not spawned until start();
  /// the poller's OS resources (a wake `eventfd`, and on Linux an `epoll`
  /// instance) are created now.
  /// @param offload_pool Pool that runs offload() work; `nullptr` (the
  ///        default) resolves lazily to
  ///        cx::core::threading::thread_pool::default_instance() on first
  ///        use, so a loop that never offloads never touches the shared pool.
  explicit event_loop(std::shared_ptr<threading::thread_pool> offload_pool = nullptr)
      : task_([this] { iterate(); }), offload_pool_(std::move(offload_pool)) {}

  /// @brief Stop the loop if it is still running.
  ~event_loop() override { stop(); }

  event_loop(const event_loop &) = delete;
  event_loop &operator=(const event_loop &) = delete;

  // --- fd watches (all thread-safe; callback runs on the loop thread) ----

  /// @brief Start (or replace) a readiness watch on @p fd.
  /// @param fd     File descriptor to watch.
  /// @param events Conditions to wait for (cx::core::io::io_event,
  ///        e.g. `io_event::readable | io_event::writable`).
  /// @param cb     Invoked on the loop thread whenever @p fd is ready.
  /// @note Thread-safe; wakes the loop so the new watch takes effect at once.
  void watch(int fd, io_event events, io_callback cb) { poller::add(poller_, fd, events, std::move(cb)); }

  /// @brief Change the interest mask of an existing watch.
  /// @param fd     Descriptor previously passed to watch(). No-op if unknown.
  /// @param events The new cx::core::io::io_event mask.
  /// @note Thread-safe.
  void modify(int fd, io_event events) { poller::modify(poller_, fd, events); }

  /// @brief Remove the watch on @p fd. Does not close the descriptor.
  /// @param fd Descriptor to stop watching. No-op if unknown.
  /// @note Thread-safe.
  void unwatch(int fd) { poller::remove(poller_, fd); }

  // --- deferred work & timers ------------------------------------------

  /// @brief Queue @p fn to run once, on the loop thread, on the next
  /// iteration - `process.nextTick` / `queueMicrotask`.
  /// @param fn Function to run. Deferred functions run before timers.
  /// @note Thread-safe; the primary way to hand thread-pool results back to
  ///       the loop.
  void defer(deferred fn) override {
    {
      std::unique_lock lock(mutex_);
      deferred_.push_back(std::move(fn));
    }
    wake();
  }

  /// @brief Run @p fn on a backing cx::core::threading::thread_pool - never
  /// on the loop thread. The way blocking calls (`getaddrinfo`) leave the
  /// reactor; hand the result back with defer().
  /// @param fn       Blocking work to run off the loop.
  /// @param priority Pool queue level.
  /// @note Thread-safe.
  void offload(deferred fn, threading::task_priority priority = threading::task_priority::normal) override {
    offload_pool()->submit(std::move(fn), priority);
  }

  /// @brief Schedule @p fn to run once after @p delay - `setTimeout`.
  /// @param delay Minimum delay before the callback fires; it may fire later
  ///        under load but never early.
  /// @param fn    Function to run on the loop thread.
  /// @return A #timer_id for clear_timeout().
  /// @note Thread-safe.
  timer_id set_timeout(std::chrono::milliseconds delay, deferred fn) {
    timer_id id;
    {
      std::unique_lock lock(mutex_);
      id = next_timer_id_++;
      timers_.push_back(timer{id, std::chrono::steady_clock::now() + delay, std::move(fn)});
    }
    wake();
    return id;
  }

  /// @brief Cancel a pending timer - `clearTimeout`.
  /// @param id Handle from set_timeout(). No-op if the timer already fired or
  ///        never existed.
  /// @note Thread-safe.
  void clear_timeout(timer_id id) {
    std::unique_lock lock(mutex_);
    std::erase_if(timers_, [id](const timer &t) { return t.id == id; });
  }

  // --- lifecycle -----------------------------------------------------

  /// @brief Spawn the loop thread and begin iterating. No-op if already
  /// started.
  void start() override {
    if (started_.exchange(true)) {
      return;
    }
    quitting_.store(false);
    task_.start();
  }

  /// @brief Request shutdown, wake the loop, and join its thread. No-op if not
  /// started. Also called by the destructor.
  void stop() override {
    if (!started_.exchange(false)) {
      return;
    }
    quitting_.store(true);
    wake();
    task_.stop(); // requests stop + joins the loop thread
    {
      std::unique_lock lock(run_mutex_);
      stop_requested_ = true;
    }
    run_cv_.notify_all();
  }

  /// @brief start() the loop, then block the calling thread until stop() is
  /// called from elsewhere - the equivalent of running Node's event loop to
  /// completion.
  void run() {
    start();
    std::unique_lock lock(run_mutex_);
    run_cv_.wait(lock, [this] { return stop_requested_; });
    stop_requested_ = false;
  }

  /// @brief Whether the loop thread is currently running.
  bool running() const override { return started_.load(); }
  /// @brief Identifier of the active readiness engine (`"poll"` / `"epoll"`),
  /// for logs and diagnostics.
  std::string_view io_backend() const { return poller::backend_name(poller_); }

  // --- async_executor identity ----------------------------------------

  /// @brief `"event_loop"`.
  std::string_view name() const noexcept override { return "event_loop"; }
  /// @brief cx::core::io::concurrency::serialized - every defer()
  /// callback runs on the one loop thread.
  threading::concurrency model() const noexcept override { return threading::concurrency::serialized; }

private:
  /// @brief The backing pool for offload(), materialised on first use.
  std::shared_ptr<threading::thread_pool> offload_pool() {
    std::unique_lock lock(offload_mutex_);
    if (!offload_pool_) {
      offload_pool_ = threading::thread_pool::default_instance();
    }
    return offload_pool_;
  }

  /// @brief One scheduled timer: id, deadline, and callback.
  struct timer {
    timer_id id;
    std::chrono::steady_clock::time_point when;
    deferred fn;
  };

  /// @brief Interrupt the poller so a newly queued watch, timer, or deferred
  /// function is picked up without waiting out the current block.
  void wake() const { poller::interrupt(poller_); }

  /// @brief Compute the dispatch timeout in ms: `0` if deferred work is
  /// pending, `-1` if no timers, otherwise time until the soonest timer.
  int next_timeout_ms() {
    std::unique_lock lock(mutex_);
    if (!deferred_.empty()) {
      return 0;
    }
    if (timers_.empty()) {
      return -1;
    }
    auto soonest = timers_.front().when;
    for (const timer &t : timers_) {
      soonest = std::min(soonest, t.when);
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(soonest - std::chrono::steady_clock::now()).count();
    return ms < 0 ? 0 : static_cast<int>(ms);
  }

  /// @brief Run and remove every timer whose deadline has passed.
  void fire_due_timers() {
    std::vector<deferred> due;
    {
      std::unique_lock lock(mutex_);
      auto now = std::chrono::steady_clock::now();
      for (auto it = timers_.begin(); it != timers_.end();) {
        if (it->when <= now) {
          due.push_back(std::move(it->fn));
          it = timers_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (auto &fn : due) {
      fn();
    }
  }

  /// @brief One loop iteration: block the poller for at most the next timer's
  /// delay (running ready fd callbacks), then run deferred work and due
  /// timers, in that order.
  void iterate() {
    if (quitting_.load()) {
      return;
    }

    poller::dispatch(poller_, std::chrono::milliseconds(next_timeout_ms()));

    std::vector<deferred> run_now;
    {
      std::unique_lock lock(mutex_);
      run_now.swap(deferred_);
    }
    for (auto &fn : run_now) {
      fn();
    }

    fire_due_timers();
  }

  // Fully qualified: async_executor injects a `task` type alias that would
  // otherwise shadow the cx::core::threading::task class here.
  threading::task task_;
  mutable poller::state poller_;

  std::mutex offload_mutex_;
  std::shared_ptr<threading::thread_pool> offload_pool_;

  mutable std::mutex mutex_;
  std::vector<deferred> deferred_;
  std::vector<timer> timers_;
  timer_id next_timer_id_ = 1;

  std::atomic<bool> started_{false};
  std::atomic<bool> quitting_{false};

  std::mutex run_mutex_;
  std::condition_variable run_cv_;
  bool stop_requested_ = false;
};

} // namespace cx::core::io
