// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cx/core/testing/test_group.hpp>
#include <cx/core/signals/signal.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cx::core::testing {

struct signal_test : public test_group {
  signal_test() : test_group("signal", {
    {"connect()/emit() invoke slots in connection order with the emitted args", [](test_context &ctx) {
      cx::core::signals::signal<int> sig;
      std::vector<int> seen;
      sig.connect([&](int v) { seen.push_back(v * 10); });
      sig.connect([&](int v) { seen.push_back(v * 100); });
      sig(7);
      ctx.check_equal(seen.size(), std::size_t{2});
      ctx.check_equal(seen[0], 70);
      ctx.check_equal(seen[1], 700);
    }},
    {"disconnect() stops future emissions for that slot only", [](test_context &ctx) {
      cx::core::signals::signal<int> sig;
      int a_count = 0, b_count = 0;
      auto a = sig.connect([&](int) { ++a_count; });
      sig.connect([&](int) { ++b_count; });

      sig(1);
      a.disconnect();
      sig(1);

      ctx.check_equal(a_count, 1);
      ctx.check_equal(b_count, 2);
    }},
    {"scoped_connection disconnects automatically on destruction", [](test_context &ctx) {
      cx::core::signals::signal<> sig;
      int count = 0;
      {
        cx::core::signals::signal<>::scoped_connection guard(sig.connect([&] { ++count; }));
        sig();
        ctx.check_equal(count, 1);
      }
      sig();
      ctx.check_equal(count, 1, "the slot should not fire after its scoped_connection was destroyed");
    }},
    {"operator+= connects; slot_count() reflects live connections", [](test_context &ctx) {
      cx::core::signals::signal<> sig;
      ctx.check_equal(sig.slot_count(), std::size_t{0});
      sig += [] {};
      sig += [] {};
      ctx.check_equal(sig.slot_count(), std::size_t{2});
    }},
    {"a slot disconnecting itself mid-emission does not crash", [](test_context &ctx) {
      cx::core::signals::signal<> sig;
      cx::core::signals::signal<>::connection self_connection;
      int count = 0;
      self_connection = sig.connect([&] {
        ++count;
        self_connection.disconnect();
      });

      sig(); // must not deadlock/crash while the slot disconnects itself mid-call
      sig(); // the slot removed itself, so this should not increment count again

      ctx.check_equal(count, 1);
      ctx.check(!self_connection.connected(), "a slot that disconnects itself mid-emission should end up disconnected");
    }},
    {"emit_async() runs the slot on the given pool", [](test_context &ctx) {
      cx::core::signals::signal<> sig;
      cx::core::threading::thread_pool pool(2);
      std::atomic<bool> ran{false};
      sig.connect([&] { ran = true; });
      sig.emit_async(pool);

      for (int i = 0; i < 200 && !ran.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      ctx.check(ran.load(), "emit_async() should eventually run the connected slot");
    }},
    {"emit_async(pool, priority, ...) dispatches the slot at the given priority", [](test_context &ctx) {
      cx::core::signals::signal<const char *> sig;
      cx::core::threading::thread_pool pool(1);
      std::mutex gate_mutex;
      std::condition_variable gate_cond;
      bool release = false;
      std::atomic<bool> blocker_started{false};

      // Occupy the sole worker so the emit_async() calls below queue up
      // instead of racing straight to a free worker in emission order.
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
      sig.connect([&](const char *label) {
        std::unique_lock lock(order_mutex);
        order.emplace_back(label);
      });

      // Emitted low -> high: the worst case for plain FIFO, so this only
      // passes if the priority argument - not emission order - wins.
      sig.emit_async(pool, cx::core::threading::task_priority::low, "low");
      sig.emit_async(pool, cx::core::threading::task_priority::high, "high");

      {
        std::unique_lock lock(gate_mutex);
        release = true;
      }
      gate_cond.notify_one();

      for (int i = 0; i < 200 && order.size() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }

      if (ctx.check_equal(order.size(), std::size_t{2})) {
        ctx.check_equal(order[0], std::string("high"));
        ctx.check_equal(order[1], std::string("low"));
      }
    }},
  }) {}
};

inline static signal_test signal_test_instance;

} // namespace cx::core::testing
