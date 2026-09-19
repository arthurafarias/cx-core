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
/// @brief cx::core::threading::coroutine_executor - a threadless,
/// cooperatively-scheduled cx::core::threading::async_executor.
///
/// The third scheduling model (SRS-011 §9): `serialized` like
/// cx::core::io::event_loop, but it owns **no thread**. The caller
/// drives it - `run()` blocks pumping until stop(), `run_until_idle()` drains
/// what is ready and returns, `run_once()` steps it once. `defer()` /
/// `schedule()` queue work (a functor, or a `std::coroutine_handle` to
/// resume); `offload()` still hands blocking work to a backing
/// cx::core::threading::thread_pool so it never stalls the pump.
///
/// It is the natural scheduler for cx::core::threading::coro coroutines: a
/// deterministic pump for tests and simple programs, and - via set_wake() - a
/// coroutine scheduler that another loop drives on its own thread.

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#include <cx/core/threading/async_executor.hpp>
#include <cx/core/threading/task_priority.hpp>
#include <cx/core/threading/thread_pool.hpp>

namespace cx::core::threading {

/// @ingroup core
/// @brief Cooperative, single-consumer task/coroutine scheduler with no
/// dedicated thread. Not copyable or movable.
class coroutine_executor : public async_executor {
public:
  /// @brief Allocate a scheduler on the heap.
  static std::shared_ptr<coroutine_executor> create() {
    return std::make_shared<coroutine_executor>();
  }

  /// @brief Process-wide shared scheduler (Meyers singleton over a
  /// `shared_ptr`).
  static std::shared_ptr<coroutine_executor> default_instance() {
    static std::shared_ptr<coroutine_executor> instance = create();
    return instance;
  }

  coroutine_executor() = default;
  coroutine_executor(const coroutine_executor &) = delete;
  coroutine_executor &operator=(const coroutine_executor &) = delete;

  ~coroutine_executor() override { stop(); }

  // --- async_executor -------------------------------------------------

  /// @brief Queue @p fn to run on the next pump, on the pumping thread.
  /// @note Thread-safe.
  void defer(task fn) override {
    {
      std::lock_guard lock(mutex_);
      queue_.push_back(std::move(fn));
    }
    signal_work();
  }

  /// @brief Queue a coroutine to resume on the next pump.
  /// @note Thread-safe; the primary way an awaiter hands a continuation back.
  void schedule(std::coroutine_handle<> handle) {
    if (!handle || handle.done()) {
      return;
    }
    defer([handle] { handle.resume(); });
  }

  /// @brief Run @p fn on the backing cx::core::threading::thread_pool - never
  /// on the pump thread. Hand the result back with defer().
  /// @note Thread-safe.
  void offload(task fn, task_priority priority = task_priority::normal) override {
    offload_pool()->submit(std::move(fn), priority);
  }

  /// @brief Mark the scheduler runnable. Idempotent. Spawns no thread.
  void start() override { running_.store(true); }

  /// @brief Mark the scheduler stopped and wake a blocked run(). Idempotent.
  /// Queued work is left in place for a later run.
  void stop() override {
    running_.store(false);
    idle_cv_.notify_all();
  }

  /// @brief Whether the scheduler is runnable (start() called, stop() not
  /// since).
  bool running() const override { return running_.load(); }

  /// @brief `"coroutine_executor"`.
  std::string_view name() const noexcept override { return "coroutine_executor"; }

  /// @brief cx::core::threading::concurrency::serialized - the pump is
  /// single-threaded.
  concurrency model() const noexcept override { return concurrency::serialized; }

  // --- driving the pump ---------------------------------------------

  /// @brief Run every task queued as of entry, once each, on the calling
  /// thread. Work queued during the batch waits for the next call.
  /// @return The number of tasks run.
  std::size_t run_once() {
    std::deque<task> batch;
    {
      std::lock_guard lock(mutex_);
      batch.swap(queue_);
    }
    for (auto &fn : batch) {
      fn();
    }
    return batch.size();
  }

  /// @brief Pump until the queue is empty (or stop()), then return - does not
  /// wait for future work.
  /// @return The total number of tasks run.
  std::size_t run_until_idle() {
    std::size_t total = 0;
    while (pending() && running_.load()) {
      total += run_once();
    }
    return total;
  }

  /// @brief start(), then block the calling thread pumping until another
  /// thread calls stop() - the equivalent of running an event loop to
  /// completion. Waits on a condition variable while idle.
  void run() {
    start();
    while (running_.load()) {
      if (run_once() == 0) {
        std::unique_lock lock(mutex_);
        idle_cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
      }
    }
  }

  /// @brief Whether any task is queued.
  bool pending() const {
    std::lock_guard lock(mutex_);
    return !queue_.empty();
  }

  /// @brief Install a callback fired (outside the internal lock) whenever work
  /// is queued from any thread. An event_loop driver uses it to post a pump;
  /// pass `nullptr` to clear.
  /// @note Set once before the scheduler is shared across threads.
  void set_wake(std::function<void()> on_work) { on_work_ = std::move(on_work); }

private:
  void signal_work() {
    idle_cv_.notify_one();
    if (on_work_) {
      on_work_();
    }
  }

  std::shared_ptr<thread_pool> offload_pool() {
    std::lock_guard lock(pool_mutex_);
    if (!offload_pool_) {
      offload_pool_ = thread_pool::default_instance();
    }
    return offload_pool_;
  }

  mutable std::mutex mutex_;
  std::condition_variable idle_cv_;
  std::deque<task> queue_;
  std::function<void()> on_work_;

  std::atomic<bool> running_{false};

  std::mutex pool_mutex_;
  std::shared_ptr<thread_pool> offload_pool_;
};

} // namespace cx::core::threading
