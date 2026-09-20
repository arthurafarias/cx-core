// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

// Dispatch cost of every cx-core asynchronous-call and event-driven mechanism,
// each next to what the STL alone gives for the same job. No benchmark
// framework (cx-core stays dependency-free): every case runs `ops` operations
// per repetition, and the reported figure is the median repetition's ns/op.
//
//   A  synchronous event dispatch     signal / event / async_signal::emit
//   B  fire-and-forget async call     thread_pool / event_loop / coroutine_executor
//   C  async call returning a value   promise+future / coro::task
//   D  asynchronous signal dispatch   signal::emit_async / async_signal::emit_async
//   E  coroutine signal dispatch      a coroutine suspended on a signal emission
//   F  fork-join over all cores       parallel_for against queue-shaped dispatch
//
// Within a section the first row is the baseline the `x` column is relative to.
// Usage: cx-core-benchmark-dispatch [repetitions=9] [scale=1.0]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cx/core/events/event.hpp>
#include <cx/core/io/event_loop.hpp>
#include <cx/core/signals/async_signal.hpp>
#include <cx/core/signals/signal.hpp>
#include <cx/core/threading/coro/task.hpp>
#include <cx/core/threading/coroutine_executor.hpp>
#include <cx/core/threading/future.hpp>
#include <cx/core/threading/parallel_for.hpp>
#include <cx/core/threading/thread_pool.hpp>

namespace {

namespace threading = cx::core::threading;
namespace signals = cx::core::signals;
namespace events = cx::core::events;
namespace io = cx::core::io;
namespace coro = cx::core::threading::coro;

int repetitions = 9;
double scale = 1.0;
double baseline_ns = 0.0;

std::size_t scaled(std::size_t ops) { return std::max<std::size_t>(1, static_cast<std::size_t>(ops * scale)); }

void section(const char *title) {
  baseline_ns = 0.0;
  std::printf("\n%s\n%-58s %12s %10s\n", title, "mechanism", "ns/op", "x");
}

// body(ops) performs `ops` operations and returns only once all of them have
// been delivered, so asynchronous cases pay for their completion too.
template <typename body_type> void measure(const char *name, std::size_t ops, body_type body) {
  ops = scaled(ops);
  body(std::max<std::size_t>(1, ops / 10)); // warm-up: threads started, allocator primed
  std::vector<double> samples;
  for (int r = 0; r < repetitions; ++r) {
    const auto begin = std::chrono::steady_clock::now();
    body(ops);
    const std::chrono::duration<double, std::nano> elapsed = std::chrono::steady_clock::now() - begin;
    samples.push_back(elapsed.count() / static_cast<double>(ops));
  }
  std::ranges::sort(samples);
  const double median = samples[samples.size() / 2];
  if (baseline_ns == 0.0) baseline_ns = median;
  std::printf("%-58s %12.1f %9.2fx\n", name, median, median / baseline_ns);
  std::fflush(stdout);
}

// Counts deliveries and lets the submitting thread block until all arrived.
struct completion {
  std::atomic<std::size_t> delivered{0};
  void hit() {
    delivered.fetch_add(1, std::memory_order_relaxed);
  }
  void wait_for(std::size_t expected) {
    while (delivered.load(std::memory_order_relaxed) < expected) std::this_thread::yield();
    delivered.store(0);
  }
};

std::atomic<std::uint64_t> sink{0};
void consume(int v) { sink.fetch_add(static_cast<std::uint64_t>(v), std::memory_order_relaxed); }

// What "a thread-safe signal" costs when written with the STL only: the same
// snapshot-under-lock-then-call-unlocked contract signals::signal gives.
struct stl_locked_slots {
  std::mutex mutex;
  std::vector<std::function<void(int)>> slots;
  void emit(int v) {
    std::vector<std::function<void(int)>> snapshot;
    {
      std::unique_lock lock(mutex);
      snapshot = slots;
    }
    for (auto &slot : snapshot) slot(v);
  }
};

// What "a worker thread" costs when written with the STL only: one jthread
// draining a mutex + condition_variable queue of std::function.
class stl_worker {
public:
  stl_worker() : thread_([this](std::stop_token stop) { run(stop); }) {}
  void post(std::function<void()> fn) {
    {
      std::unique_lock lock(mutex_);
      queue_.push_back(std::move(fn));
    }
    cond_.notify_one();
  }

private:
  void run(std::stop_token stop) {
    while (true) {
      std::function<void()> fn;
      {
        std::unique_lock lock(mutex_);
        cond_.wait(lock, stop, [this] { return !queue_.empty(); });
        if (queue_.empty()) return;
        fn = std::move(queue_.front());
        queue_.pop_front();
      }
      fn();
    }
  }
  std::mutex mutex_;
  std::condition_variable_any cond_;
  std::deque<std::function<void()>> queue_;
  std::jthread thread_; // last: joins before the queue it drains is destroyed
};

// --- A ---------------------------------------------------------------------

void synchronous_event_dispatch(std::size_t slot_count) {
  const std::string title = "A  synchronous event dispatch, " + std::to_string(slot_count) + " slot(s)";
  section(title.c_str());
  constexpr std::size_t ops = 2'000'000;

  std::vector<std::function<void(int)>> plain(slot_count, consume);
  measure("stl  vector<std::function> loop (not thread-safe)", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
      for (auto &slot : plain) slot(1);
  });

  stl_locked_slots locked;
  locked.slots.assign(slot_count, consume);
  measure("stl  mutex + snapshot + loop (thread-safe equivalent)", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) locked.emit(1);
  });

  signals::signal<int> sig;
  events::event<int> ev;
  signals::async_signal<int> async_sig;
  for (std::size_t i = 0; i < slot_count; ++i) {
    sig.connect(consume);
    ev += consume;
    async_sig.connect(consume);
  }
  measure("cx   signals::signal::emit", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) sig.emit(1);
  });
  measure("cx   events::event::emit", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) ev.emit(1);
  });
  measure("cx   signals::async_signal::emit (synchronous path)", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) async_sig.emit(1);
  });
}

// --- B ---------------------------------------------------------------------

void fire_and_forget() {
  section("B  fire-and-forget asynchronous call (submit + delivery)");
  completion done;

  {
    stl_worker worker;
    measure("stl  jthread + mutex/condvar queue, 1 worker", 500'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) worker.post([&] { done.hit(); });
      done.wait_for(n);
    });
  }
  measure("stl  std::async(launch::async), future kept then waited", 20'000, [&](std::size_t n) {
    std::vector<std::future<void>> futures;
    futures.reserve(n);
    for (std::size_t i = 0; i < n; ++i) futures.push_back(std::async(std::launch::async, [&] { done.hit(); }));
    for (auto &f : futures) f.wait();
    done.wait_for(n);
  });
  measure("stl  std::jthread per call", 20'000, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) std::jthread([&] { done.hit(); });
    done.wait_for(n);
  });

  {
    threading::thread_pool pool(1);
    measure("cx   threading::thread_pool::submit, 1 worker", 500'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) pool.submit([&] { done.hit(); });
      done.wait_for(n);
    });
  }
  {
    threading::thread_pool pool(4);
    measure("cx   threading::thread_pool::submit, 4 workers", 500'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) pool.submit([&] { done.hit(); });
      done.wait_for(n);
    });
    measure("cx   thread_pool::submit, 4 workers, priority high", 500'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) pool.submit([&] { done.hit(); }, threading::task_priority::high);
      done.wait_for(n);
    });
  }
  {
    auto loop = std::make_shared<io::event_loop>();
    loop->start();
    measure("cx   io::event_loop::defer (cross-thread, wakes the poller)", 200'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) loop->defer([&] { done.hit(); });
      done.wait_for(n);
    });
    loop->stop();
  }
  {
    auto exec = threading::coroutine_executor::create();
    exec->start();
    measure("cx   coroutine_executor::defer + run_until_idle (same thread)", 500'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) exec->defer([&] { done.hit(); });
      exec->run_until_idle();
      done.wait_for(n);
    });
  }
}

// --- C ---------------------------------------------------------------------

coro::task<int> leaf(int v) { co_return v; }
coro::task<std::uint64_t> await_chain(std::size_t n) {
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < n; ++i) total += static_cast<std::uint64_t>(co_await leaf(1));
  co_return total;
}

void call_returning_a_value() {
  section("C  asynchronous call returning a value (produce + consume)");

  measure("stl  std::promise/std::future, set_value then get", 500'000, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      std::promise<int> p;
      auto f = p.get_future();
      p.set_value(1);
      consume(f.get());
    }
  });
  measure("stl  std::packaged_task invoked inline, then get", 500'000, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      std::packaged_task<int()> t([] { return 1; });
      auto f = t.get_future();
      t();
      consume(f.get());
    }
  });
  measure("stl  std::async(launch::deferred).get()", 500'000, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) consume(std::async(std::launch::deferred, [] { return 1; }).get());
  });
  measure("stl  std::async(launch::async).get()", 20'000, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) consume(std::async(std::launch::async, [] { return 1; }).get());
  });

  measure("cx   threading::promise/future::then, inline continuation", 500'000, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      threading::promise<int> p;
      p.get_future().then(consume);
      p.set_value(1);
    }
  });
  {
    auto exec = threading::coroutine_executor::create();
    exec->start();
    measure("cx   threading::promise/future::then via coroutine_executor", 500'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        threading::promise<int> p(exec);
        p.get_future().then(consume);
        p.set_value(1);
      }
      exec->run_until_idle();
    });
  }
  {
    auto pool = threading::thread_pool::create(1);
    completion done;
    measure("cx   threading::promise/future::then via thread_pool", 500'000, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        threading::promise<int> p(pool);
        p.get_future().then([&](int) { done.hit(); });
        p.set_value(1);
      }
      done.wait_for(n);
    });
  }
  measure("cx   co_await coro::task<int> (symmetric transfer)", 2'000'000, [&](std::size_t n) {
    auto exec = threading::coroutine_executor::create();
    consume(static_cast<int>(coro::sync_wait(exec, await_chain(n)) & 1));
  });
}

// --- D ---------------------------------------------------------------------

void asynchronous_signal_dispatch(std::size_t slot_count) {
  const std::string title = "D  asynchronous signal dispatch, " + std::to_string(slot_count) + " slot(s), per emission";
  section(title.c_str());
  constexpr std::size_t ops = 200'000;
  completion done;
  const auto slot = [&done](int) { done.hit(); };

  {
    stl_worker worker;
    stl_locked_slots locked;
    locked.slots.assign(slot_count, slot);
    measure("stl  snapshot, then post each slot to a jthread queue", ops, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        std::vector<std::function<void(int)>> snapshot;
        {
          std::unique_lock lock(locked.mutex);
          snapshot = locked.slots;
        }
        for (auto &s : snapshot) worker.post([s] { s(1); });
      }
      done.wait_for(n * slot_count);
    });
  }

  signals::signal<int> sig;
  signals::async_signal<int> async_sig;
  for (std::size_t i = 0; i < slot_count; ++i) {
    sig.connect(slot);
    async_sig.connect(slot);
  }
  {
    threading::thread_pool pool(1);
    measure("cx   signal::emit_async(thread_pool), 1 worker", ops, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) sig.emit_async(pool, 1);
      done.wait_for(n * slot_count);
    });
  }
  {
    auto pool = threading::thread_pool::create(4);
    measure("cx   async_signal::emit_async(thread_pool), 4 workers", ops, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) async_sig.emit_async(pool, 1);
      done.wait_for(n * slot_count);
    });
  }
  {
    auto loop = std::make_shared<io::event_loop>();
    loop->start();
    measure("cx   async_signal::emit_async(event_loop)", ops, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) async_sig.emit_async(loop, 1);
      done.wait_for(n * slot_count);
    });
    loop->stop();
  }
  {
    auto exec = threading::coroutine_executor::create();
    exec->start();
    measure("cx   async_signal::emit_async(coroutine_executor) + drain", ops, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) async_sig.emit_async(exec, 1);
      exec->run_until_idle();
      done.wait_for(n * slot_count);
    });
  }
}

// --- E ---------------------------------------------------------------------

// Suspends the awaiting coroutine until the signal's next emission. `via`
// null resumes inside emit() itself; otherwise the resume is scheduled on the
// executor, which is how a coroutine stays on its home thread.
struct next_emission {
  signals::signal<int> &source;
  std::shared_ptr<threading::coroutine_executor> via;
  completion *armed = nullptr; // hit once connected, for a producer on another thread
  signals::signal<int>::connection link;
  int value = 0;

  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) {
    link = source.connect([this, h](int v) {
      value = v;
      link.disconnect();
      if (via) via->schedule(h);
      else h.resume();
    });
    if (armed) armed->hit();
  }
  int await_resume() const noexcept { return value; }
};

coro::task<void> listen(signals::signal<int> &source, std::shared_ptr<threading::coroutine_executor> via,
                        std::size_t n, completion &done, completion *armed = nullptr) {
  for (std::size_t i = 0; i < n; ++i) {
    consume(co_await next_emission{source, via, armed});
    done.hit();
  }
}

// The bare-STL floor: a coroutine parked on a handle the producer resumes.
struct parked {
  std::coroutine_handle<> *slot;
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) const noexcept { *slot = h; }
  void await_resume() const noexcept {}
};
coro::task<void> listen_parked(std::coroutine_handle<> *slot, std::size_t n, completion &done) {
  for (std::size_t i = 0; i < n; ++i) {
    co_await parked{slot};
    done.hit();
  }
}

void coroutine_signal_dispatch() {
  section("E  coroutine signal dispatch (one emission wakes one suspended coroutine)");
  constexpr std::size_t ops = 500'000;
  completion done;

  measure("stl  raw std::coroutine_handle::resume, no signal", ops, [&](std::size_t n) {
    std::coroutine_handle<> waiting;
    auto listener = listen_parked(&waiting, n, done);
    listener.handle().resume();
    for (std::size_t i = 0; i < n; ++i) waiting.resume();
    done.wait_for(n);
  });
  measure("stl  condition_variable handoff to a waiting thread", 200'000, [&](std::size_t n) {
    std::mutex m;
    std::condition_variable cv;
    std::size_t produced = 0, consumed = 0;
    std::jthread waiter([&] {
      for (std::size_t i = 0; i < n; ++i) {
        std::unique_lock lock(m);
        cv.wait(lock, [&] { return produced > consumed; });
        ++consumed;
        cv.notify_all();
      }
    });
    for (std::size_t i = 0; i < n; ++i) {
      std::unique_lock lock(m);
      ++produced;
      cv.notify_all();
      cv.wait(lock, [&] { return consumed == produced; });
    }
  });
  measure("stl  std::promise/std::future handoff to a waiting thread", 100'000, [&](std::size_t n) {
    std::vector<std::promise<int>> events(n);
    std::vector<std::promise<void>> acks(n);
    std::jthread waiter([&] {
      for (std::size_t i = 0; i < n; ++i) {
        consume(events[i].get_future().get());
        acks[i].set_value();
      }
    });
    for (std::size_t i = 0; i < n; ++i) {
      events[i].set_value(1);
      acks[i].get_future().wait();
    }
  });

  measure("cx   signal::emit resumes the coroutine inline", ops, [&](std::size_t n) {
    signals::signal<int> source;
    auto listener = listen(source, nullptr, n, done);
    listener.handle().resume();
    for (std::size_t i = 0; i < n; ++i) source.emit(1);
    done.wait_for(n);
  });
  measure("cx   signal::emit schedules it on coroutine_executor", ops, [&](std::size_t n) {
    signals::signal<int> source;
    auto exec = threading::coroutine_executor::create();
    exec->start();
    auto listener = listen(source, exec, n, done);
    listener.handle().promise().executor_ = exec;
    listener.handle().resume();
    for (std::size_t i = 0; i < n; ++i) {
      source.emit(1);
      exec->run_until_idle();
    }
    done.wait_for(n);
  });
  measure("cx   emit on one thread, coroutine resumed on event_loop", 200'000, [&](std::size_t n) {
    signals::signal<int> source;
    auto loop = std::make_shared<io::event_loop>();
    loop->start();
    auto exec = threading::coroutine_executor::create();
    exec->start();
    exec->set_wake([&] { loop->defer([&] { exec->run_until_idle(); }); });
    // The listener re-arms on the loop thread, so the producer waits for the
    // connection to exist before emitting - an emission with no slot is lost.
    completion armed;
    auto listener = listen(source, exec, n, done, &armed);
    listener.handle().promise().executor_ = exec;
    listener.handle().resume();
    for (std::size_t i = 0; i < n; ++i) {
      armed.wait_for(1);
      source.emit(1);
    }
    done.wait_for(n);
    loop->stop();
  });
}

// --- F ---------------------------------------------------------------------

void burn(long iterations) {
  volatile double x = 1.0;
  for (long i = 0; i < iterations; ++i) x = x * 1.0000001 + 1e-9;
}

// A chunk as a coroutine that hops onto the pool: multicore dispatch through
// the coroutine vocabulary (resume_on), one frame and one queue entry a chunk.
struct detached {
  struct promise_type {
    detached get_return_object() { return {}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::abort(); }
  };
};
detached chunk_on(std::shared_ptr<threading::thread_pool> pool, long iterations, std::latch &done) {
  co_await coro::resume_on(pool);
  burn(iterations);
  done.count_down();
}

// The shape of a compute kernel: cut a range into 4 chunks a thread, run them
// on every core, return when the last is done. ns/op is one whole fork-join.
void fork_join(long iterations, const char *label) {
  const std::size_t threads = threading::fork_join_pool::thread_count();
  const std::size_t chunks = threads * threading::slices_per_thread;
  const std::string title = "F  fork-join, " + std::to_string(chunks) + " chunks of " + label + " on " +
                            std::to_string(threads) + " threads, per fork-join";
  section(title.c_str());
  const std::size_t ops = iterations >= 30000 ? 500 : 5000;
  auto pool = threading::thread_pool::create(static_cast<unsigned>(threads));

  measure("     serial loop (no dispatch)", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
      for (std::size_t t = 0; t < chunks; ++t) burn(iterations);
  });
  measure("stl  std::jthread per chunk, joined", std::max<std::size_t>(ops / 50, 20), [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      std::vector<std::jthread> workers;
      for (std::size_t t = 0; t < chunks; ++t) workers.emplace_back([&] { burn(iterations); });
    }
  });
  measure("cx   thread_pool::submit per chunk + std::latch", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      std::latch done(static_cast<std::ptrdiff_t>(chunks));
      for (std::size_t t = 0; t < chunks; ++t) pool->submit([&] { burn(iterations); done.count_down(); });
      done.wait();
    }
  });
  measure("cx   coroutine per chunk, resume_on(thread_pool)", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      std::latch done(static_cast<std::ptrdiff_t>(chunks));
      for (std::size_t t = 0; t < chunks; ++t) chunk_on(pool, iterations, done);
      done.wait();
    }
  });
  {
    signals::signal<long> sig;
    std::latch *current = nullptr;
    for (std::size_t t = 0; t < chunks; ++t) sig.connect([&](long it) { burn(it); current->count_down(); });
    measure("cx   signal::emit_async(thread_pool), a slot per chunk", ops, [&](std::size_t n) {
      for (std::size_t i = 0; i < n; ++i) {
        std::latch done(static_cast<std::ptrdiff_t>(chunks));
        current = &done;
        sig.emit_async(*pool, iterations);
        done.wait();
      }
    });
  }
  measure("cx   threading::parallel_for", ops, [&](std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
      threading::parallel_for(chunks, 1, [&](std::size_t begin, std::size_t end) {
        for (std::size_t t = begin; t < end; ++t) burn(iterations);
      });
  });
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1) repetitions = std::max(1, std::atoi(argv[1]));
  if (argc > 2) scale = std::max(0.001, std::atof(argv[2]));

  std::printf("cx-core dispatch benchmark: %d repetitions (median), scale %.3f, %u hardware threads\n", repetitions,
              scale, std::thread::hardware_concurrency());

  synchronous_event_dispatch(1);
  synchronous_event_dispatch(8);
  fire_and_forget();
  call_returning_a_value();
  asynchronous_signal_dispatch(1);
  asynchronous_signal_dispatch(8);
  coroutine_signal_dispatch();
  fork_join(300, "~0.6 us");
  fork_join(3000, "~7 us");
  fork_join(30000, "~70 us");

  std::printf("\nchecksum %llu\n", static_cast<unsigned long long>(sink.load()));
  return 0;
}
