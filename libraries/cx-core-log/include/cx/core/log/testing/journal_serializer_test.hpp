// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cx/core/log/journal_serializer.hpp>
#include <cx/core/testing/test_group.hpp>

#include <chrono>
#include <sstream>
#include <string>

namespace cx::core::testing {

struct journal_serializer_test : public test_group {
  journal_serializer_test() : test_group("journal_serializer", {
    {"an ostream with no serializer streamed onto it renders plain by default", [](test_context &ctx) {
      log::journal_entry entry{.level = log::journal_level::info, .line = 7, .file = "a.cpp", .function = "f",
                           .message = "hi"};
      std::ostringstream out;
      out << entry;
      ctx.check(out.str().find("a.cpp:7") != std::string::npos, "plain output should contain file:line");
      ctx.check(out.str().find("hi") != std::string::npos, "plain output should contain the message");
    }},
    {"streaming a serializer onto a stream sticks for every entry streamed afterwards", [](test_context &ctx) {
      log::journal_entry entry{.level = log::journal_level::error, .line = 1, .file = "f.cpp", .function = "g",
                           .message = "boom"};
      std::ostringstream out;
      out << std::make_shared<log::json_journal_serializer>();
      out << entry;
      out << entry; // second entry should still render as JSON, not fall back to plain
      std::string rendered = out.str();
      ctx.check_equal(std::count(rendered.begin(), rendered.end(), '{'), 2L);
      ctx.check(rendered.find(R"("level":"error")") != std::string::npos, "json output should carry the level");
    }},
    {"json_journal_serializer escapes quotes and backslashes in the message", [](test_context &ctx) {
      log::journal_entry entry{.message = R"(say "hi"\bye)"};
      std::ostringstream out;
      log::json_journal_serializer{}.write(entry, out);
      ctx.check(out.str().find(R"(say \"hi\"\\bye)") != std::string::npos,
                 "quotes and backslashes should be escaped");
    }},
    {"xml_journal_serializer escapes reserved characters in the message", [](test_context &ctx) {
      log::journal_entry entry{.message = "a < b & b > c"};
      std::ostringstream out;
      log::xml_journal_serializer{}.write(entry, out);
      ctx.check(out.str().find("a &lt; b &amp; b &gt; c") != std::string::npos,
                 "<, & and > should be escaped in the element body");
    }},
    {"csv_journal_serializer quotes a message containing a comma", [](test_context &ctx) {
      log::journal_entry entry{.message = "hello, world"};
      std::ostringstream out;
      log::csv_journal_serializer{}.write(entry, out);
      ctx.check(out.str().find("\"hello, world\"") != std::string::npos,
                 "a field containing the delimiter should be quoted");
    }},
    {"plain_journal_serializer is used for a fresh log::journal_entry", [](test_context &ctx) {
      log::journal_entry entry{.message = "plain check"};
      std::ostringstream out;
      log::plain_journal_serializer{}.write(entry, out);
      ctx.check(out.str().find("plain check") != std::string::npos, "plain output should contain the message");
      ctx.check(out.str().find('\n') != std::string::npos, "plain output should be newline-terminated");
    }},
  }) {}
};

inline static journal_serializer_test journal_serializer_test_instance;

} // namespace cx::core::testing
