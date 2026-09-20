// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cx/core/testing/test_group.hpp>
#include <cx/core/threading/parallel_for.hpp>

#include <atomic>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace cx::core::testing {

inline test_group parallel_for_tests{
    "parallel_for",
    {
        {"every index of the range is visited exactly once", [](test_context &ctx) {
           std::vector<int> hits(100'003, 0);
           threading::parallel_for(hits.size(), 1, [&](std::size_t begin, std::size_t end) {
             for (std::size_t i = begin; i < end; ++i) ++hits[i];
           });
           bool once = true;
           for (int h : hits) once = once && h == 1;
           ctx.check(once, "each index should be written by exactly one chunk");
         }},
        {"a range below min_per_thread runs inline on the calling thread", [](test_context &ctx) {
           const auto caller = std::this_thread::get_id();
           std::size_t calls = 0;
           bool same_thread = true;
           threading::parallel_for(10, 1000, [&](std::size_t begin, std::size_t end) {
             ++calls;
             same_thread = same_thread && std::this_thread::get_id() == caller && begin == 0 && end == 10;
           });
           ctx.check(calls == 1 && same_thread, "a small range should be one inline call over the whole range");
         }},
        {"an empty range calls fn once with an empty interval", [](test_context &ctx) {
           std::size_t calls = 0;
           threading::parallel_for(0, 1, [&](std::size_t begin, std::size_t end) { calls += (begin == end) ? 1 : 100; });
           ctx.check(calls == 1, "n == 0 should still be a single empty call, as the serial path is");
         }},
        {"back-to-back jobs never see a previous job's state", [](test_context &ctx) {
           // The job lives on run()'s stack: a worker still holding the last
           // one while the next is published would corrupt these sums.
           bool exact = true;
           for (std::size_t round = 1; round <= 3000; ++round) {
             std::atomic<std::size_t> sum{0};
             const std::size_t n = 64 + round % 97;
             threading::parallel_for(n, 1, [&](std::size_t begin, std::size_t end) {
               std::size_t local = 0;
               for (std::size_t i = begin; i < end; ++i) local += i + round;
               sum.fetch_add(local);
             });
             exact = exact && sum.load() == n * (n - 1) / 2 + n * round;
           }
           ctx.check(exact, "every round should sum exactly its own range");
         }},
        {"a parallel_for issued from inside a chunk runs inline", [](test_context &ctx) {
           std::atomic<std::size_t> total{0};
           threading::parallel_for(256, 1, [&](std::size_t begin, std::size_t end) {
             for (std::size_t i = begin; i < end; ++i) {
               threading::parallel_for(8, 1, [&](std::size_t b, std::size_t e) { total.fetch_add(e - b); });
             }
           });
           ctx.check(total.load() == 256 * 8, "nested ranges should complete without deadlocking the pool");
         }},
        {"concurrent callers take turns and each gets its own result", [](test_context &ctx) {
           std::vector<std::size_t> sums(6, 0);
           {
             std::vector<std::jthread> callers;
             for (std::size_t c = 0; c < sums.size(); ++c) {
               callers.emplace_back([&sums, c] {
                 for (int round = 0; round < 200; ++round) {
                   std::atomic<std::size_t> sum{0};
                   threading::parallel_for(1000, 1, [&](std::size_t begin, std::size_t end) { sum.fetch_add(end - begin); });
                   sums[c] += sum.load();
                 }
               });
             }
           }
           bool exact = true;
           for (std::size_t s : sums) exact = exact && s == 200 * 1000;
           ctx.check(exact, "each caller should see exactly its own range covered");
         }},
        {"an exception thrown by a chunk reaches the caller, and the pool keeps working", [](test_context &ctx) {
           ctx.check_throws<std::runtime_error>(
               [] {
                 threading::parallel_for(1024, 1, [](std::size_t begin, std::size_t) {
                   if (begin != 0) throw std::runtime_error("chunk failed");
                 });
               },
               "a worker chunk's exception should be rethrown on the calling thread");
           std::atomic<std::size_t> covered{0};
           threading::parallel_for(1024, 1, [&](std::size_t begin, std::size_t end) { covered.fetch_add(end - begin); });
           ctx.check(covered.load() == 1024, "the next job should run normally after a failed one");
         }},
        {"a private pool joins its workers on destruction", [](test_context &ctx) {
           std::atomic<std::size_t> ran{0};
           {
             threading::fork_join_pool pool(4);
             ctx.check(pool.size() == 4, "size() should count the caller as one of the threads");
             pool.run(64, [&](std::size_t) { ran.fetch_add(1); });
           }
           ctx.check(ran.load() == 64, "run() should call the job once per chunk");
         }},
    }};

} // namespace cx::core::testing
