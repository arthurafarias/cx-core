// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/filesystem/file_lock.hpp>
#include <cx/core/testing/test_group.hpp>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace cx::core::testing {

namespace file_lock_test_detail {
inline std::filesystem::path scratch_directory(const char *name) {
  auto dir = std::filesystem::temp_directory_path() / ("cx-core-" + std::string(name) + "-" + std::to_string(::getpid()));
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}
inline std::string slurp(const std::filesystem::path &p) {
  std::ifstream in(p);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}
} // namespace file_lock_test_detail

inline test_group file_lock_tests{
    "file_lock",
    {
        {"a held lock excludes a second holder until it is released", [](test_context &ctx) {
           const auto dir = file_lock_test_detail::scratch_directory("lock");
           const auto path = dir / "state.yaml.lock";
           {
             filesystem::file_lock first(path);
             ctx.check(first.held(), "construction should acquire");
             ctx.check(!filesystem::file_lock::try_acquire(path).has_value(), "a second holder should be refused while the first holds it");
           }
           ctx.check(filesystem::file_lock::try_acquire(path).has_value(), "destruction should release");
           std::filesystem::remove_all(dir);
         }},
        {"a blocking acquire waits for the holder", [](test_context &ctx) {
           const auto dir = file_lock_test_detail::scratch_directory("wait");
           const auto path = dir / "a.lock";
           std::atomic<bool> released{false}, acquired_after_release{false};
           auto holder = std::make_unique<filesystem::file_lock>(path);
           std::jthread waiter([&] {
             filesystem::file_lock second(path);
             acquired_after_release = released.load();
           });
           std::this_thread::sleep_for(std::chrono::milliseconds(50));
           released = true;
           holder.reset();
           waiter.join();
           ctx.check(acquired_after_release.load(), "the waiter should only get the lock once the holder let go");
           std::filesystem::remove_all(dir);
         }},
        {"moving a lock moves ownership, not a second release", [](test_context &ctx) {
           const auto dir = file_lock_test_detail::scratch_directory("move");
           const auto path = dir / "a.lock";
           filesystem::file_lock a(path);
           filesystem::file_lock b(std::move(a));
           ctx.check(!a.held() && b.held(), "the moved-from lock should hold nothing");
           ctx.check(!filesystem::file_lock::try_acquire(path).has_value(), "the lock should still be held through the new owner");
           b.release();
           ctx.check(filesystem::file_lock::try_acquire(path).has_value(), "release() should free it");
           std::filesystem::remove_all(dir);
         }},
        {"atomic_replace swaps in complete content and cleans up after a failed write", [](test_context &ctx) {
           const auto dir = file_lock_test_detail::scratch_directory("replace");
           const auto target = dir / "nested" / "state.yaml";
           filesystem::atomic_replace(target, [](std::ostream &out) { out << "first\n"; });
           filesystem::atomic_replace(target, [](std::ostream &out) { out << "second\n"; });
           ctx.check(file_lock_test_detail::slurp(target) == "second\n", "the target should hold the last complete write");
           ctx.check_throws<std::runtime_error>(
               [&] { filesystem::atomic_replace(target, [](std::ostream &) { throw std::runtime_error("writer failed"); }); },
               "a throwing writer should propagate");
           ctx.check(file_lock_test_detail::slurp(target) == "second\n", "a failed write should leave the target untouched");
           std::size_t entries = 0;
           for (const auto &entry : std::filesystem::directory_iterator(target.parent_path())) { (void)entry; ++entries; }
           ctx.check(entries == 1, "no scratch file should be left behind");
           std::filesystem::remove_all(dir);
         }},
    }};

} // namespace cx::core::testing
