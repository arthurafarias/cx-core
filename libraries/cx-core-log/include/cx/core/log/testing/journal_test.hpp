// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cx/core/log/journal.hpp>
#include <cx/core/testing/test_group.hpp>
#include <cx/core/threading/thread_pool.hpp>

#include <chrono>
#include <memory>
#include <sstream>
#include <thread>

namespace cx::core::testing {

namespace {

// Every case gets a private, disconnected log::journal_stream + serializer -
// log::journal is a process-wide static façade (SRS-006 §6.3), so tests must not
// leak state (level, serializer, dispatched slot) into one another.
struct journal_fixture {
  std::ostringstream out;

  journal_fixture() {
    log::journal_stream::set(out);
    log::journal::set_serializer(std::make_shared<log::plain_journal_serializer>());
    log::journal::level_set(log::journal_level::debug);
  }

  ~journal_fixture() { log::journal_stream::set(std::cout); }
};

} // namespace

struct journal_test : public test_group {
  journal_test() : test_group("journal", {
    {"a log::journal nobody has called set_serializer() on still emits through the default plain serializer",
     [](test_context &ctx) {
      std::ostringstream out;
      log::journal_stream::set(out);
      // Deliberately no set_serializer() call - this is what every core
      // instrumentation call site relies on before anything ever touches
      // log::journal::set_serializer().
      log::journal::info("zero-setup {}", 1);
      log::journal_stream::set(std::cout);
      ctx.check(out.str().find("zero-setup 1") != std::string::npos,
                 "info() should be dispatched even with no serializer ever explicitly selected");
    }},
    {"info()/warn()/debug() dispatch a rendered log::journal_entry at the default (most permissive) level",
     [](test_context &ctx) {
      journal_fixture fixture;
      log::journal::info("info {}", 1);
      log::journal::warn("warn {}", 2);
      log::journal::debug("debug {}", 3);
      ctx.check(fixture.out.str().find("info 1") != std::string::npos, "info() should have been emitted");
      ctx.check(fixture.out.str().find("warn 2") != std::string::npos, "warn() should have been emitted");
      ctx.check(fixture.out.str().find("debug 3") != std::string::npos, "debug() should have been emitted");
    }},
    {"level_set(warn) silences debug but keeps warn/info/error", [](test_context &ctx) {
      journal_fixture fixture;
      log::journal::level_set(log::journal_level::warn);

      log::journal::debug("hidden");
      ctx.check(fixture.out.str().empty(), "debug() should be filtered out below warn");

      log::journal::warn("visible warn");
      log::journal::info("visible info");
      log::journal::error("visible error");
      ctx.check(fixture.out.str().find("hidden") == std::string::npos,
                 "the filtered debug() call should never have reached the stream");
      ctx.check(fixture.out.str().find("visible warn") != std::string::npos, "warn() should still be emitted");
      ctx.check(fixture.out.str().find("visible info") != std::string::npos, "info() should still be emitted");
      ctx.check(fixture.out.str().find("visible error") != std::string::npos, "error() should still be emitted");
    }},
    {"level_set(info) additionally silences warn", [](test_context &ctx) {
      journal_fixture fixture;
      log::journal::level_set(log::journal_level::info);

      log::journal::warn("hidden warn");
      log::journal::debug("hidden debug");
      ctx.check(fixture.out.str().empty(), "warn()/debug() should both be filtered out at level info");

      log::journal::info("visible info");
      ctx.check(fixture.out.str().find("visible info") != std::string::npos, "info() should still be emitted");
    }},
    {"error() is never gated, even at the strictest level", [](test_context &ctx) {
      journal_fixture fixture;
      log::journal::level_set(log::journal_level::info);
      log::journal::error("always shown");
      ctx.check(fixture.out.str().find("always shown") != std::string::npos, "error() must ignore level_set()");
    }},
    {"each entry point stamps the caller's own file/line, not journal.hpp's", [](test_context &ctx) {
      journal_fixture fixture;
      log::journal::info("here");
      ctx.check(fixture.out.str().find(__FILE__) != std::string::npos,
                 "the captured source_location should point at the call site");
    }},
    {"set_serializer() replaces the previously selected serializer", [](test_context &ctx) {
      journal_fixture fixture;
      log::journal::set_serializer(std::make_shared<log::json_journal_serializer>());
      log::journal::info("as json");
      ctx.check(fixture.out.str().find('{') != std::string::npos, "output should now be JSON-shaped");
    }},
    {"emit_async() dispatches through the given thread_pool and honors level_set()", [](test_context &ctx) {
      journal_fixture fixture;
      threading::thread_pool pool(1);
      log::journal::level_set(log::journal_level::warn);

      log::journal::emit_async(pool, threading::task_priority::normal, log::journal_level::debug, "hidden async");
      log::journal::emit_async(pool, threading::task_priority::normal, log::journal_level::error, "visible async");

      std::string rendered;
      for (int i = 0; i < 200; ++i) {
        rendered = fixture.out.str();
        if (rendered.find("visible async") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      ctx.check(rendered.find("visible async") != std::string::npos, "the error-level async entry should land");
      ctx.check(rendered.find("hidden async") == std::string::npos,
                 "the filtered-out debug-level async entry should never have been dispatched");
    }},
  }) {}
};

inline static journal_test journal_test_instance;

} // namespace cx::core::testing
