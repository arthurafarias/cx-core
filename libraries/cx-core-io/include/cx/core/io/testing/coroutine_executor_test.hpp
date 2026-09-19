// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// @file
/// @ingroup testing
/// @brief Tests for cx::core::threading::coroutine_executor and the
/// cx::core::threading::coro vocabulary (SRS-011 §9).

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <cx/core/threading/coro/task.hpp>
#include <cx/core/threading/coroutine_executor.hpp>
#include <cx/core/io/event_loop.hpp>
#include <cx/core/threading/thread_pool.hpp>
#include <cx/core/testing/test_group.hpp>

namespace cx::core::testing {


namespace coro_detail {

inline threading::coro::task<int> add(int a, int b) { co_return a + b; }

inline threading::coro::task<int> sum_to(int n) {
  if (n <= 0) {
    co_return 0;
  }
  int rest = co_await sum_to(n - 1);
  co_return n + rest;
}

inline threading::coro::task<int> throws() {
  throw std::runtime_error("boom");
  co_return 0; // unreachable
}

inline threading::coro::task<void> hop_and_record(std::shared_ptr<threading::thread_pool> pool,
                                       std::thread::id *worker_seen,
                                       std::thread::id start_thread,
                                       std::atomic<bool> *hopped) {
  *worker_seen = start_thread; // overwritten after the hop
  co_await threading::coro::resume_on(pool);
  *worker_seen = std::this_thread::get_id();
  hopped->store(true);
}

} // namespace coro_detail

struct coroutine_executor_test : public test_group {
  coroutine_executor_test()
      : test_group(
            "threading::coroutine_executor",
            {
                {"advertises the serialized model and its name",
                 [](test_context &ctx) {
                   auto ex = threading::coroutine_executor::create();
                   ctx.check_equal(std::string(ex->name()), std::string("coroutine_executor"));
                   ctx.check(ex->model() == threading::concurrency::serialized, "serialized");
                 }},

                {"deferred work runs only when the pump is driven",
                 [](test_context &ctx) {
                   auto ex = threading::coroutine_executor::create();
                   ex->start();
                   std::atomic<int> ran{0};
                   ex->defer([&] { ran.fetch_add(1); });
                   ctx.check_equal(ran.load(), 0, "nothing runs before a pump");
                   std::size_t n = ex->run_once();
                   ctx.check_equal(n, std::size_t{1});
                   ctx.check_equal(ran.load(), 1);
                 }},

                {"run_once runs exactly the batch present at entry",
                 [](test_context &ctx) {
                   auto ex = threading::coroutine_executor::create();
                   ex->start();
                   std::atomic<int> ran{0};
                   for (int i = 0; i < 3; ++i) {
                     ex->defer([&] {
                       ran.fetch_add(1);
                       if (ran.load() == 1) {
                         ex->defer([&] { ran.fetch_add(1); }); // re-queued during the batch
                       }
                     });
                   }
                   ctx.check_equal(ex->run_once(), std::size_t{3});
                   ctx.check_equal(ran.load(), 3);
                   ctx.check_equal(ex->run_once(), std::size_t{1});
                   ctx.check_equal(ran.load(), 4);
                 }},

                {"sync_wait returns a task's value",
                 [](test_context &ctx) {
                   auto ex = threading::coroutine_executor::create();
                   int v = threading::coro::sync_wait(ex, coro_detail::add(2, 3));
                   ctx.check_equal(v, 5);
                 }},

                {"sync_wait rethrows a task's exception",
                 [](test_context &ctx) {
                   auto ex = threading::coroutine_executor::create();
                   ctx.check_throws<std::runtime_error>(
                       [&] { threading::coro::sync_wait(ex, coro_detail::throws()); }, "task exception propagates");
                 }},

                {"a deep co_await chain does not overflow the stack",
                 [](test_context &ctx) {
                   auto ex = threading::coroutine_executor::create();
                   int v = threading::coro::sync_wait(ex, coro_detail::sum_to(20000));
                   ctx.check_equal(v, 20000 * 20001 / 2);
                 }},

                {"spawn runs a task fire-and-forget on the executor",
                 [](test_context &ctx) {
                   auto ex = threading::coroutine_executor::create();
                   ex->start();
                   std::atomic<int> ran{0};
                   threading::coro::spawn(ex, [](std::atomic<int> *r) -> threading::coro::task<> {
                     r->fetch_add(1);
                     co_return;
                   }(&ran));
                   ctx.check_equal(ran.load(), 0, "spawn is lazy until pumped");
                   ex->run_until_idle();
                   ctx.check_equal(ran.load(), 1);
                 }},

                {"resume_on hops the coroutine onto another executor's thread",
                 [](test_context &ctx) {
                   auto ex = threading::coroutine_executor::create();
                   auto pool = threading::thread_pool::create(2);
                   std::thread::id worker_seen;
                   std::atomic<bool> hopped{false};
                   auto caller = std::this_thread::get_id();

                   threading::coro::spawn(ex, coro_detail::hop_and_record(pool, &worker_seen, caller, &hopped));
                   ex->start();
                   ex->run_once(); // start the task; it suspends at resume_on(pool)

                   auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                   while (!hopped.load() && std::chrono::steady_clock::now() < deadline) {
                     std::this_thread::sleep_for(std::chrono::milliseconds(1));
                   }
                   ctx.require(hopped.load(), "the coroutine resumed after the hop");
                   ctx.check(worker_seen != caller, "the tail ran on a pool worker, not the pump thread");
                 }},
            }) {}
};

inline static coroutine_executor_test coroutine_executor_test_instance;

} // namespace cx::core::testing
