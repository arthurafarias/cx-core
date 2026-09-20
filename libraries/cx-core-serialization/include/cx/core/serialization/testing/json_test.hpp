// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/serialization/json.hpp>
#include <cx/core/testing/test_group.hpp>

#include <stdexcept>
#include <string>

namespace cx::core::testing {

inline test_group json_tests{
    "json",
    {
        {"parse reads nested arrays, objects, numbers, escapes and surrogate pairs", [](test_context &ctx) {
           namespace json = serialization::json;
           const json::value document =
               json::parse(R"( {"a": [1, 2.5, true, null], "s": "q\"\\\né😀", "o": {"k": -3e2}} )");
           ctx.require(document.find("a") != nullptr && document.find("o") != nullptr, "both members should be found");
           ctx.check(document.find("a")->as<json::array>().size() == 4, "the array keeps its four items");
           ctx.check(document.find("o")->number("k", 0) == -300.0, "an exponent is read");
           ctx.check(document.text("s") == "q\"\\\n\xc3\xa9\xf0\x9f\x98\x80", "\\u escapes and a surrogate pair become UTF-8");
           ctx.check(document.text("absent", "fallback") == "fallback" && document.flag("absent", true),
                     "a missing member yields the fallback");
         }},
        {"dump round-trips through parse, and quote escapes control bytes", [](test_context &ctx) {
           namespace json = serialization::json;
           const json::value document = json::parse(R"({"a":[1,2.5,true,null],"o":{"k":"v\n"}})");
           ctx.check(json::parse(json::dump(document)) == document, "dump then parse is the identity");
           ctx.check(json::quote("a\"b\n\x01") == "\"a\\\"b\\n\\u0001\"", "quote escapes quotes and control bytes");
         }},
        {"parse is strict: malformed documents throw", [](test_context &ctx) {
           namespace json = serialization::json;
           for (const char *broken : {"", "{", "[1,]", "{\"a\" 1}", "tru", "{} x", "\"unterminated"}) {
             bool threw = false;
             try {
               (void)json::parse(broken);
             } catch (const std::invalid_argument &) {
               threw = true;
             }
             ctx.check(threw, "a malformed document should be rejected");
           }
         }},
    }};

} // namespace cx::core::testing
