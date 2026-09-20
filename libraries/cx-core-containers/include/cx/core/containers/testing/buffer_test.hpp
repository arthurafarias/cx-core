// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cx/core/containers/buffer.hpp>
#include <cx/core/testing/test_group.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace cx::core::testing {

inline test_group buffer_tests{
    "buffer",
    {
        {"text survives a round trip through bytes, embedded NULs and high bytes included", [](test_context &ctx) {
           const std::string text{"a\0b\xff\x80z", 6};
           const containers::buffer bytes = containers::make_buffer(std::string_view(text));
           ctx.check(bytes.size() == 6 && bytes[1] == std::byte{0} && bytes[3] == std::byte{0xff},
                     "make_buffer() should copy every char verbatim, one byte each");
           ctx.check(containers::to_string(bytes) == text, "to_string() should give back exactly the original chars");
         }},
        {"make_buffer(span) is an owning copy", [](test_context &ctx) {
           containers::buffer source = containers::make_buffer("abc");
           const containers::buffer copy = containers::make_buffer(std::span<const std::byte>(source));
           source[0] = std::byte{'x'};
           ctx.check(containers::to_string(copy) == "abc", "changing the source should not reach the copy");
         }},
        {"append() grows the destination in order, and an empty span is a no-op", [](test_context &ctx) {
           containers::buffer dst = containers::make_buffer("GET ");
           containers::append(dst, containers::make_buffer("/index"));
           containers::append(dst, {});
           ctx.check(containers::to_string(dst) == "GET /index", "appended bytes should follow the existing ones");
         }},
    }};

} // namespace cx::core::testing
