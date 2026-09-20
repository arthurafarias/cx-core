// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/serialization/base64.hpp>
#include <cx/core/serialization/text_escape.hpp>
#include <cx/core/serialization/utf8.hpp>
#include <cx/core/testing/test_group.hpp>

#include <sstream>
#include <string>
#include <string_view>

namespace cx::core::testing {

inline test_group serialization_tests{
    "serialization",
    {
        {"base64 encodes the RFC 4648 test vectors", [](test_context &ctx) {
           using serialization::base64_encode;
           ctx.check(base64_encode(std::string_view{""}) == "", "empty input");
           ctx.check(base64_encode(std::string_view{"f"}) == "Zg==", "one byte pads twice");
           ctx.check(base64_encode(std::string_view{"fo"}) == "Zm8=", "two bytes pad once");
           ctx.check(base64_encode(std::string_view{"foo"}) == "Zm9v", "three bytes need no padding");
           ctx.check(base64_encode(std::string_view{"foobar"}) == "Zm9vYmFy", "two full groups");
         }},
        {"base64 decode round-trips and rejects malformed input", [](test_context &ctx) {
           auto decoded = serialization::base64_decode("Zm9vYmE=");
           ctx.require(decoded.has_value(), "a padded group should decode");
           ctx.check(serialization::base64_encode(*decoded) == "Zm9vYmE=", "decode then encode should be the identity");
           ctx.check(!serialization::base64_decode("Zm9v!mFy").has_value(), "a character outside the alphabet is an error");
         }},
        {"valid_utf8 accepts well-formed text and rejects the classic malformations", [](test_context &ctx) {
           using serialization::valid_utf8;
           ctx.check(valid_utf8(std::string_view{"plain ascii"}), "ascii is utf-8");
           ctx.check(valid_utf8(std::string_view{"a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"}), "2, 3 and 4 byte sequences");
           ctx.check(!valid_utf8(std::string_view{"\xC0\xAF"}), "an overlong encoding is rejected");
           ctx.check(!valid_utf8(std::string_view{"\xED\xA0\x80"}), "a surrogate half is rejected");
           ctx.check(!valid_utf8(std::string_view{"\xE2\x82"}), "a truncated sequence is rejected");
           ctx.check(!valid_utf8(std::string_view{"a\0b", 3}, false), "NUL is rejected when not allowed");
         }},
        {"the text escapers neutralise each format's metacharacters", [](test_context &ctx) {
           std::ostringstream json, xml, csv;
           serialization::write_json_escaped(json, "a\"b\\c\nd");
           serialization::write_xml_escaped(xml, "<a & \"b\">");
           serialization::write_csv_field(csv, "x,\"y\"");
           ctx.check(json.str() == "a\\\"b\\\\c\\nd", "json escapes quote, backslash and newline");
           ctx.check(serialization::json_escaped(std::string_view{"a\x01\x1f\0z", 5}) == "a\\u0001\\u001f\\u0000z",
                     "json escapes every control character, which RFC 8259 forbids raw");
           ctx.check(serialization::json_escaped("\xc3\xa9\x7f") == "\xc3\xa9\x7f", "json leaves UTF-8 and DEL alone");
           ctx.check(xml.str().find('<') == std::string::npos && xml.str().find("&amp;") != std::string::npos,
                     "xml replaces markup characters with entities");
           ctx.check(csv.str() == "\"x,\"\"y\"\"\"", "csv quotes the field and doubles embedded quotes");
         }},
    }};

} // namespace cx::core::testing
