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
/// @brief Tests for cx::core::threading::async_executor (SRS-011): the
/// event_loop and thread_pool as interchangeable schedulers, plus
/// cx::core::threading::async_signal.

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <cx/core/threading/async_executor.hpp>
#include <cx/core/signals/async_signal.hpp>
#include <cx/core/io/event_loop.hpp>
#include <cx/core/signals/signal.hpp>
#include <cx/core/threading/thread_pool.hpp>
#include <cx/core/io/testing/loop_harness.hpp>
#include <cx/core/testing/test_group.hpp>

namespace cx::core::testing {


// signal is synchronous-only: emit_async lives on async_signal, not signal
// (SRS-011 §5). These detectors document the split; the negative one is what
// the M4 change enforces.
template <typename S>
concept has_emit_async =
    requires(S s, std::shared_ptr<threading::async_executor> e) { s.emit_async(e); };
static_assert(!has_emit_async<signals::signal<>>,
              "signals::signal must not expose emit_async - use signals::async_signal");
static_assert(has_emit_async<signals::async_signal<>>,
              "signals::async_signal exposes emit_async");

struct async_executor_test : public test_group {
  async_executor_test()
      : test_group(
            "threading::async_executor",
            {
                {"event_loop advertises the serialized model",
                 [](test_context &ctx) {
                   auto loop = io::event_loop::create();
                   threading::async_executor &ex = *loop;
                   ctx.check_equal(std::string(ex.name()), std::string("event_loop"));
                   ctx.check(ex.model() == threading::concurrency::serialized,
                             "the event loop is a serialized executor");
                 }},

                {"thread_pool advertises the concurrent model",
                 [](test_context &ctx) {
                   auto pool = threading::thread_pool::create(2);
                   threading::async_executor &ex = *pool;
                   ctx.check_equal(std::string(ex.name()), std::string("thread_pool"));
                   ctx.check(ex.model() == threading::concurrency::concurrent,
                             "the thread pool is a concurrent executor");
                 }},

                {"event_loop::defer through the async_executor interface runs on the loop thread",
                 [](test_context &ctx) {
                   auto loop = io::event_loop::create();
                   loop->start();
                   threading::async_executor &ex = *loop;

                   std::promise<std::thread::id> first, second;
                   auto f1 = first.get_future();
                   auto f2 = second.get_future();
                   ex.defer([&] { first.set_value(std::this_thread::get_id()); });
                   ex.defer([&] { second.set_value(std::this_thread::get_id()); });

                   auto a = await(f1);
                   auto b = await(f2);
                   loop->stop();
                   ctx.require(a.has_value() && b.has_value(), "both deferred tasks ran");
                   ctx.check(*a != std::this_thread::get_id(),
                             "deferred work does not run on the caller thread");
                   ctx.check(*a == *b, "every deferred task runs on the one loop thread");
                 }},

                {"event_loop::offload runs work off the loop thread, defer marshals it back",
                 [](test_context &ctx) {
                   auto loop = io::event_loop::create();
                   loop->start();

                   std::thread::id loop_thread;
                   {
                     std::promise<std::thread::id> p;
                     auto f = p.get_future();
                     loop->defer([&] { p.set_value(std::this_thread::get_id()); });
                     auto id = await(f);
                     ctx.require(id.has_value(), "probed the loop thread");
                     loop_thread = *id;
                   }

                   std::promise<void> done;
                   auto f = done.get_future();
                   std::thread::id offload_thread;
                   std::thread::id back_thread;
                   loop->offload([&] {
                     offload_thread = std::this_thread::get_id();
                     loop->defer([&] {
                       back_thread = std::this_thread::get_id();
                       done.set_value();
                     });
                   });

                   bool ok = f.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
                   loop->stop();
                   ctx.require(ok, "the offload -> defer round trip completed");
                   ctx.check(offload_thread != loop_thread,
                             "offloaded work does not run on the loop thread");
                   ctx.check(offload_thread != std::this_thread::get_id(),
                             "offloaded work does not run on the caller thread");
                   ctx.check(back_thread == loop_thread,
                             "the deferred continuation lands back on the loop thread");
                 }},

                {"thread_pool runs defer() and offload() work",
                 [](test_context &ctx) {
                   auto pool = threading::thread_pool::create(3);
                   threading::async_executor &ex = *pool;

                   std::atomic<int> ran{0};
                   std::promise<void> done;
                   auto f = done.get_future();
                   ex.offload([&] { ran.fetch_add(1); }, threading::task_priority::high);
                   ex.defer([&] {
                     ran.fetch_add(1);
                     done.set_value();
                   });

                   ctx.require(f.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                               "the pool ran the submitted work");
                   // give the other task a beat to retire
                   std::this_thread::sleep_for(std::chrono::milliseconds(20));
                   ctx.check_equal(ran.load(), 2);
                 }},

                {"thread_pool is restartable: stop() then start() runs fresh work",
                 [](test_context &ctx) {
                   auto pool = threading::thread_pool::create(2);
                   ctx.check(pool->running(), "a freshly created pool is running");
                   pool->stop();
                   ctx.check(!pool->running(), "stop() leaves the pool not running");
                   pool->start();
                   ctx.check(pool->running(), "start() brings the pool back");

                   std::promise<void> done;
                   auto f = done.get_future();
                   pool->defer([&] { done.set_value(); });
                   ctx.check(f.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                             "the restarted pool runs new work");
                 }},

                {"async_signal::emit() invokes every slot synchronously on the caller thread",
                 [](test_context &ctx) {
                   signals::async_signal<int> sig;
                   std::vector<int> seen;
                   auto caller = std::this_thread::get_id();
                   bool same_thread = true;
                   sig += [&](int v) {
                     seen.push_back(v);
                     same_thread = same_thread && std::this_thread::get_id() == caller;
                   };
                   sig += [&](int v) { seen.push_back(v * 10); };
                   sig.emit(3);
                   ctx.require_equal(seen.size(), std::size_t{2}, "both slots fired");
                   ctx.check_equal(seen[0], 3);
                   ctx.check_equal(seen[1], 30);
                   ctx.check(same_thread, "emit() runs slots on the calling thread");
                 }},

                {"async_signal::emit_async dispatches every slot through the executor",
                 [](test_context &ctx) {
                   for (bool use_pool : {false, true}) {
                     std::shared_ptr<threading::async_executor> ex;
                     std::shared_ptr<io::event_loop> loop;
                     std::shared_ptr<threading::thread_pool> pool;
                     if (use_pool) {
                       pool = threading::thread_pool::create(4);
                       ex = pool;
                     } else {
                       loop = io::event_loop::create();
                       loop->start();
                       ex = loop;
                     }

                     signals::async_signal<int> sig;
                     constexpr int slots = 8;
                     std::atomic<int> total{0};
                     std::atomic<int> fired{0};
                     for (int i = 0; i < slots; ++i) {
                       sig += [&](int v) {
                         total.fetch_add(v);
                         fired.fetch_add(1);
                       };
                     }

                     sig.emit_async(ex, 5);

                     auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                     while (fired.load() < slots && std::chrono::steady_clock::now() < deadline) {
                       std::this_thread::sleep_for(std::chrono::milliseconds(1));
                     }
                     if (loop) {
                       loop->stop();
                     }
                     ctx.check_equal(fired.load(), slots, use_pool ? "pool: every slot ran" : "loop: every slot ran");
                     ctx.check_equal(total.load(), slots * 5, use_pool ? "pool: payload delivered" : "loop: payload delivered");
                   }
                 }},
            }) {}
};

inline static async_executor_test async_executor_test_instance;

} // namespace cx::core::testing
