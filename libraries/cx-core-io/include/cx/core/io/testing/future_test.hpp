// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <cx/core/io/event_loop.hpp>
#include <cx/core/threading/future.hpp>
#include <cx/core/io/testing/loop_harness.hpp>
#include <cx/core/testing/test_group.hpp>

namespace cx::core::testing {


struct core_future_test : public test_group {
  core_future_test()
      : test_group(
            "threading::future",
            {
                {"then() after set_value() runs with the value",
                 [](test_context &ctx) {
                   threading::promise<int> p;
                   auto f = p.get_future();
                   p.set_value(42);
                   int seen = 0;
                   f.then([&](int v) { seen = v; });
                   ctx.check_equal(seen, 42);
                 }},
                {"set_value() after then() runs the continuation",
                 [](test_context &ctx) {
                   threading::promise<std::string> p;
                   auto f = p.get_future();
                   std::string seen;
                   f.then([&](std::string v) { seen = std::move(v); });
                   ctx.check(seen.empty(), "not fired yet");
                   p.set_value("hello");
                   ctx.check_equal(seen, std::string{"hello"});
                 }},
                {"the continuation fires exactly once",
                 [](test_context &ctx) {
                   threading::promise<int> p;
                   auto f = p.get_future();
                   int calls = 0;
                   f.then([&](int) { ++calls; });
                   p.set_value(1);
                   p.set_value(2);
                   ctx.check_equal(calls, 1);
                 }},
                {"void promise/future",
                 [](test_context &ctx) {
                   threading::promise<void> p;
                   auto f = p.get_future();
                   bool ran = false;
                   f.then([&] { ran = true; });
                   ctx.check(!ran, "not yet");
                   p.set_value();
                   ctx.check(ran, "ran after set_value");
                 }},
                {"make_ready_future is immediately ready",
                 [](test_context &ctx) {
                   auto f = threading::make_ready_future<int>(7);
                   ctx.check(f.is_ready(), "ready");
                   int seen = 0;
                   f.then([&](int v) { seen = v; });
                   ctx.check_equal(seen, 7);
                 }},
                {"an executor-bound promise dispatches the continuation on the loop",
                 [](test_context &ctx) {
                   auto loop = std::make_shared<io::event_loop>();
                   loop->start();
                   threading::promise<int> p(loop);
                   auto f = p.get_future();
                   std::promise<int> bridge;
                   auto bfut = bridge.get_future();
                   f.then([&](int v) { bridge.set_value(v); });
                   std::thread([&] { p.set_value(99); }).join();
                   auto got = await(bfut, std::chrono::seconds(2));
                   loop->stop();
                   ctx.require(got.has_value(), "continuation ran");
                   ctx.check_equal(*got, 99);
                 }},
            }) {}
};

inline static core_future_test core_future_test_instance;

} // namespace cx::core::testing
