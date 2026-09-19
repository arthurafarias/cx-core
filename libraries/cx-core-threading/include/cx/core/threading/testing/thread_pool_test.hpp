// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cx/core/testing/test_group.hpp>
#include <cx/core/threading/thread_pool.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace cx::core::testing {

struct thread_pool_test : public test_group {
  thread_pool_test() : test_group("thread_pool", {
    {"submit() executes the task asynchronously", [](test_context &ctx) {
      cx::core::threading::thread_pool pool(2);
      std::atomic<bool> ran{false};
      pool.submit([&] { ran = true; });

      for (int i = 0; i < 200 && !ran.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      ctx.check(ran.load(), "the submitted task should have run");
    }},
    {"multiple submissions all run", [](test_context &ctx) {
      cx::core::threading::thread_pool pool(4);
      constexpr int total = 20;
      std::atomic<int> count{0};
      for (int i = 0; i < total; ++i) {
        pool.submit([&] { ++count; });
      }

      for (int i = 0; i < 200 && count.load() < total; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      ctx.check_equal(count.load(), total);
    }},
    {"an exception thrown by one task does not break the worker or later submissions", [](test_context &ctx) {
      cx::core::threading::thread_pool pool(2);
      pool.submit([] { throw std::runtime_error("boom"); });

      std::atomic<bool> ran{false};
      pool.submit([&] { ran = true; });

      for (int i = 0; i < 200 && !ran.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      ctx.check(ran.load(), "a later submission should still run after a prior task threw");
    }},
    {"instance() returns the same reference across calls", [](test_context &ctx) {
      ctx.check(&cx::core::threading::thread_pool::instance() == &cx::core::threading::thread_pool::instance(), "instance() should return the same reference across calls");
    }},
    {"a free worker drains high before normal before low, regardless of submission order", [](test_context &ctx) {
      cx::core::threading::thread_pool pool(1);
      std::mutex gate_mutex;
      std::condition_variable gate_cond;
      bool release = false;
      std::atomic<bool> blocker_started{false};

      // Occupy the sole worker so the submissions below queue up instead of
      // racing straight to a free worker in submission order.
      pool.submit([&] {
        blocker_started = true;
        std::unique_lock lock(gate_mutex);
        gate_cond.wait(lock, [&] { return release; });
      });
      for (int i = 0; i < 200 && !blocker_started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }

      std::mutex order_mutex;
      std::vector<std::string> order;
      auto record = [&](const char *label) {
        std::unique_lock lock(order_mutex);
        order.emplace_back(label);
      };

      // Submitted low -> normal -> high: the worst case for plain FIFO, so
      // this only passes if priority - not submission order - wins.
      pool.submit([&] { record("low"); }, cx::core::threading::task_priority::low);
      pool.submit([&] { record("normal"); }, cx::core::threading::task_priority::normal);
      pool.submit([&] { record("high"); }, cx::core::threading::task_priority::high);

      {
        std::unique_lock lock(gate_mutex);
        release = true;
      }
      gate_cond.notify_one();

      for (int i = 0; i < 200 && order.size() < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }

      if (ctx.check_equal(order.size(), std::size_t{3})) {
        ctx.check_equal(order[0], std::string("high"));
        ctx.check_equal(order[1], std::string("normal"));
        ctx.check_equal(order[2], std::string("low"));
      }
    }},
    {"tasks submitted at the same priority level still run in FIFO order", [](test_context &ctx) {
      cx::core::threading::thread_pool pool(1);
      std::mutex gate_mutex;
      std::condition_variable gate_cond;
      bool release = false;
      std::atomic<bool> blocker_started{false};

      pool.submit([&] {
        blocker_started = true;
        std::unique_lock lock(gate_mutex);
        gate_cond.wait(lock, [&] { return release; });
      });
      for (int i = 0; i < 200 && !blocker_started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }

      constexpr int total = 5;
      std::mutex order_mutex;
      std::vector<int> order;
      for (int i = 0; i < total; ++i) {
        pool.submit(
            [&, i] {
              std::unique_lock lock(order_mutex);
              order.push_back(i);
            },
            cx::core::threading::task_priority::low);
      }

      {
        std::unique_lock lock(gate_mutex);
        release = true;
      }
      gate_cond.notify_one();

      for (int i = 0; i < 200 && static_cast<int>(order.size()) < total; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }

      if (ctx.check_equal(order.size(), static_cast<std::size_t>(total))) {
        for (int i = 0; i < total; ++i) {
          ctx.check_equal(order[static_cast<std::size_t>(i)], i, "same-priority tasks should run in submission order");
        }
      }
    }},
  }) {}
};

inline static thread_pool_test thread_pool_test_instance;

} // namespace cx::core::testing
