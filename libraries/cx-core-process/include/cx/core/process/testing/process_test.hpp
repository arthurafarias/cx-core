// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/process/process.hpp>
#include <cx/core/testing/test_group.hpp>

#include <string>

namespace cx::core::testing {

inline test_group process_tests{
    "process",
    {
        {"which() resolves through PATH and rejects what is not there", [](test_context &ctx) {
           auto sh = process::which("sh");
           ctx.require(sh.has_value(), "sh should be on PATH");
           ctx.check(sh->is_absolute() || sh->string().starts_with("."), "the result should be a usable path");
           ctx.check(!process::which("cx-core-no-such-binary").has_value(), "an unknown name should resolve to nothing");
           ctx.check(process::which(sh->string()) == sh, "a name with a slash is checked as given");
         }},
        {"run() returns the child's exit code", [](test_context &ctx) {
           ctx.check(process::run("/bin/sh", {"sh", "-c", "exit 7"}) == 7, "an exit code should come back as is");
           ctx.check(process::run("/bin/sh", {"sh", "-c", "kill -TERM $$"}) == 128 + 15, "a signal death should be 128 + signal");
           ctx.check(process::run("/cx-core/no/such/binary", {"x"}) == 126, "a failed exec should report 126");
         }},
        {"run_captured() separates stdout from stderr", [](test_context &ctx) {
           auto r = process::run_captured("/bin/sh", {"sh", "-c", "echo to-out; echo to-err 1>&2; exit 3"});
           ctx.check(r.status == 3, "the exit code should be captured");
           ctx.check(r.out == "to-out\n" && r.err == "to-err\n", "each stream should land in its own buffer");
         }},
        {"run_captured() survives a child that fills both pipes", [](test_context &ctx) {
           // 1 MiB on each stream, far past a 64 KiB pipe: a reader that
           // drains stdout to the end before touching stderr never returns.
           auto r = process::run_captured(
               "/bin/sh", {"sh", "-c", "i=0; while [ $i -lt 16 ]; do head -c 65536 /dev/zero | tr '\\0' e 1>&2; head -c 65536 /dev/zero | tr '\\0' o; i=$((i+1)); done"});
           ctx.check(r.status == 0, "the child should run to completion");
           ctx.check(r.out.size() == 16 * 65536 && r.err.size() == 16 * 65536, "every byte of both streams should be captured");
           ctx.check(r.out.find_first_not_of('o') == std::string::npos && r.err.find_first_not_of('e') == std::string::npos,
                     "the streams should not be interleaved into each other");
         }},
    }};

} // namespace cx::core::testing
