// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/log/bunyan.hpp>
#include <cx/core/serialization/json.hpp>
#include <cx/core/testing/test_group.hpp>

#include <cstdio>
#include <string>

namespace cx::core::testing {

inline test_group bunyan_tests{
    "bunyan",
    {
        {"format writes the fixed Bunyan fields, then the caller's", [](test_context &ctx) {
           namespace bunyan = log::bunyan;
           const std::string line =
               bunyan::format(bunyan::level::warn, "svc", "disk low", bunyan::field("free_mb", 12).json + bunyan::field("ok", false).json);
           const auto record = serialization::json::parse(line);
           ctx.check(record.number("v", -1) == 0 && record.number("level", 0) == 40, "v is 0 and warn is 40");
           ctx.check(record.text("name") == "svc" && record.text("msg") == "disk low", "name and msg are carried");
           ctx.check(record.find("pid") != nullptr && record.find("hostname") != nullptr, "pid and hostname are present");
           ctx.check(record.text("time").size() == 24 && record.text("time").back() == 'Z', "time is ISO 8601 UTC with milliseconds");
           ctx.check(record.number("free_mb", 0) == 12 && !record.flag("ok", true), "a field keeps its JSON type");
         }},
        {"hostile text cannot split or forge a record", [](test_context &ctx) {
           namespace bunyan = log::bunyan;
           const std::string hostile = "msg\n{\"level\":60}\x01\x1b[2J";
           const std::string line = bunyan::format(bunyan::level::info, "n\"\n", hostile, "");
           ctx.check(line.find('\n') == std::string::npos && line.find('\x01') == std::string::npos, "no raw newline or control byte");
           const auto record = serialization::json::parse(line);
           ctx.check(record.text("msg") == hostile && record.number("level", 0) == 30, "the text is the message; the level is its own");
           ctx.check(serialization::json::parse(bunyan::format(bunyan::level::info, "n", "m", bunyan::field("x", 0.0 / 0.0).json))
                         .find("x")->is<std::nullptr_t>(), "NaN is written as null, not as a token JSON lacks");
         }},
        {"a record is written once, when it ends, and only at or above the minimum", [](test_context &ctx) {
           namespace bunyan = log::bunyan;
           bunyan::settings &current = bunyan::configuration();
           const bunyan::settings saved = current;
           std::FILE *sink = std::tmpfile();
           ctx.require(sink != nullptr, "a temporary sink is needed");
           current.sink = sink;
           current.name = "test";
           current.minimum = bunyan::level::info;
           bunyan::debug() << "dropped";
           bunyan::info() << "kept " << 7 << bunyan::field("k", "v");
           current = saved;
           std::rewind(sink);
           std::string written;
           for (int c; (c = std::fgetc(sink)) != EOF;)
             written += static_cast<char>(c);
           std::fclose(sink);
           ctx.require(!written.empty() && written.back() == '\n', "one newline-terminated line");
           ctx.check(written.find('\n') == written.size() - 1, "debug was below the minimum, so exactly one line");
           const auto record = serialization::json::parse(written);
           ctx.check(record.text("msg") == "kept 7" && record.text("k") == "v" && record.text("name") == "test",
                     "text and numbers concatenate into msg; the field rides along");
           ctx.check(bunyan::parse_level("error", bunyan::level::info) == bunyan::level::error &&
                         bunyan::parse_level("loud", bunyan::level::info) == bunyan::level::info,
                     "parse_level knows the six names and falls back otherwise");
         }},
    }};

} // namespace cx::core::testing
