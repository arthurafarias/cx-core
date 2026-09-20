// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <unistd.h>

#include <cx/core/io/descriptor.hpp>
#include <cx/core/testing/test_group.hpp>

namespace cx::core::testing {

inline test_group descriptor_tests{
    "descriptor",
    {
        {"bytes written to one end of a pipe are read from the other", [](test_context &ctx) {
           int fds[2] = {-1, -1};
           ctx.require(::pipe(fds) == 0, "pipe() should succeed");
           auto reader = io::descriptor::adopt(fds[0]);
           auto writer = io::descriptor::adopt(fds[1]);
           ctx.check(io::descriptor::valid(reader) && io::descriptor::native(writer) == fds[1],
                     "an adopted descriptor should be valid and report the handle it owns");

           constexpr std::string_view text = "cx-core";
           const auto sent = io::descriptor::write(writer, std::as_bytes(std::span(text)));
           ctx.check(sent.error == std::errc{} && sent.count == static_cast<std::ptrdiff_t>(text.size()),
                     "write() should report every byte as transferred");

           std::array<std::byte, 16> buffer{};
           const auto got = io::descriptor::read(reader, buffer);
           ctx.check(got.count == static_cast<std::ptrdiff_t>(text.size()) &&
                         std::string_view(reinterpret_cast<const char *>(buffer.data()), text.size()) == text,
                     "read() should return the bytes that were written");
         }},
        {"a closed writer reads as end-of-file, and close() invalidates the descriptor", [](test_context &ctx) {
           int fds[2] = {-1, -1};
           ctx.require(::pipe(fds) == 0, "pipe() should succeed");
           auto reader = io::descriptor::adopt(fds[0]);
           auto writer = io::descriptor::adopt(fds[1]);
           io::descriptor::close(writer);
           ctx.check(!io::descriptor::valid(writer), "close() should leave the descriptor invalid");

           std::array<std::byte, 4> buffer{};
           const auto got = io::descriptor::read(reader, buffer);
           ctx.check(got.error == std::errc{} && got.count == 0, "count == 0 with no error is end-of-file");
         }},
        {"release() hands the handle back without closing it", [](test_context &ctx) {
           int fds[2] = {-1, -1};
           ctx.require(::pipe(fds) == 0, "pipe() should succeed");
           auto reader = io::descriptor::adopt(fds[0]);
           const auto raw = io::descriptor::release(reader);
           ctx.check(raw == fds[0] && !io::descriptor::valid(reader), "release() should return the handle and disown it");
           ctx.check(::close(raw) == 0, "the released handle should still be open, so closing it succeeds");
           ::close(fds[1]);
         }},
        {"would_block() recognises both spellings of try-again", [](test_context &ctx) {
           ctx.check(io::descriptor::would_block(std::errc::operation_would_block) &&
                         io::would_block(std::errc::resource_unavailable_try_again) &&
                         !io::would_block(std::errc::broken_pipe),
                     "only EAGAIN/EWOULDBLOCK are try-again");
         }},
    }};

} // namespace cx::core::testing
