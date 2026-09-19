// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cx/core/testing/test_group.hpp>
#include <cx/core/threading/task.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace cx::core::testing {

struct task_test : public test_group {
  task_test() : test_group("task", {
    {"start() runs the loop repeatedly on another thread until stop()", [](test_context &ctx) {
      std::atomic<int> count{0};
      threading::task t([&] {
        ++count;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      });

      ctx.check(!t.is_running(), "a task should not be running before start()");
      t.start();
      ctx.check(t.is_running(), "a task should be running after start()");

      for (int i = 0; i < 200 && count.load() < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      ctx.check(count.load() >= 3, "the loop should have run several times while started");

      t.stop();
      ctx.check(!t.is_running(), "a task should not be running after stop()");

      int after_stop = count.load();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      ctx.check_equal(count.load(), after_stop, "no further iterations should run once stopped");
    }},
    {"pause() gates the loop before its next iteration, resume() releases it", [](test_context &ctx) {
      std::atomic<int> count{0};
      threading::task t([&] { ++count; });
      t.start();

      for (int i = 0; i < 200 && count.load() < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      ctx.require(count.load() >= 1, "the loop should have run at least once before pausing");

      t.pause();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      int paused_count = count.load();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      ctx.check_equal(count.load(), paused_count, "no iterations should run while paused");

      t.resume();
      for (int i = 0; i < 200 && count.load() <= paused_count; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      ctx.check(count.load() > paused_count, "resume() should let the loop run again");

      t.stop();
    }},
    {"the destructor stops the loop without hanging", [](test_context &ctx) {
      std::atomic<int> count{0};
      {
        threading::task t([&] { ++count; });
        t.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      } // if the destructor failed to join, the test binary itself would hang here
      ctx.check(true, "reaching this point means the destructor returned");
    }},
  }) {}
};

inline static task_test task_test_instance;

} // namespace cx::core::testing
