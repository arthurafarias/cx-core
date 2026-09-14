// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace cx::core {

// Priority attached to a thread_pool submission (SRS-007 §5.1). A free
// worker always drains the highest non-empty level first - strict priority,
// not weighted fair queuing (SRS-007 §7.1) - so e.g. logging (low) can never
// delay media events (normal) queued behind it. `normal` is the default and
// matches today's single-queue FIFO behavior exactly.
enum class task_priority { high = 0, normal = 1, low = 2 };

// General-purpose pool for short-lived, fire-and-forget work (e.g. explicit
// signal::emit_async submissions). Long-running/repeating loops (pad
// streaming threads) must use task instead - submitting a repeating loop
// here would starve the pool for everyone else, since it is a fixed-size
// worker set.
class thread_pool {
public:
  using task_type = std::function<void()>;

  explicit thread_pool(unsigned worker_count = std::thread::hardware_concurrency());

  thread_pool(const thread_pool &) = delete;
  thread_pool &operator=(const thread_pool &) = delete;
  thread_pool(thread_pool &&) = delete;
  thread_pool &operator=(thread_pool &&) = delete;

  ~thread_pool() = default;

  void submit(task_type task, task_priority priority = task_priority::normal);

  static thread_pool &instance();

private:
  void worker_loop(std::stop_token stop_token);
  bool has_pending() const;
  task_type dequeue_highest();

  std::mutex mutex_;
  std::condition_variable_any queue_cond_;
  std::array<std::queue<task_type>, 3> queues_;
  std::vector<std::jthread> workers_;
};

inline thread_pool::thread_pool(unsigned worker_count) {
  // hardware_concurrency() may legitimately return 0 (unspecified per the
  // standard on some platforms/containers); a zero-worker pool would accept
  // submissions forever without ever running them.
  worker_count = std::max(1u, worker_count);

  workers_.reserve(worker_count);
  for (unsigned i = 0; i < worker_count; ++i) {
    workers_.emplace_back([this](std::stop_token stop_token) { worker_loop(stop_token); });
  }
}

inline void thread_pool::submit(task_type task, task_priority priority) {
  {
    std::unique_lock lock(mutex_);
    queues_[static_cast<std::size_t>(priority)].push(std::move(task));
  }
  queue_cond_.notify_one();
}

inline thread_pool &thread_pool::instance() {
  static thread_pool pool;
  return pool;
}

inline bool thread_pool::has_pending() const {
  return std::ranges::any_of(queues_, [](const auto &queue) { return !queue.empty(); });
}

inline thread_pool::task_type thread_pool::dequeue_highest() {
  for (auto &queue : queues_) { // task_priority::high (0) checked first
    if (!queue.empty()) {
      task_type task = std::move(queue.front());
      queue.pop();
      return task;
    }
  }
  return nullptr; // unreachable when called under has_pending()
}

inline void thread_pool::worker_loop(std::stop_token stop_token) {
  while (true) {
    task_type task;
    {
      std::unique_lock lock(mutex_);
      queue_cond_.wait(lock, stop_token, [this] { return has_pending(); });

      if (!has_pending()) {
        // Only reachable when stop was requested with nothing left to run.
        return;
      }

      task = dequeue_highest();
    }

    try {
      task();
    } catch (const std::exception &e) {
      std::cerr << "cx::core::thread_pool: task threw: " << e.what() << '\n';
    } catch (...) {
      std::cerr << "cx::core::thread_pool: task threw a non-exception value\n";
    }
  }
}

} // namespace cx::core
