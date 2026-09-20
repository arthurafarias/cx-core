// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

// Two processes editing one file: take `<file>.lock`, re-read, write a scratch
// copy, rename it over the target. file_lock is the first step as a RAII type
// and atomic_replace the last two - the protocol agenticx-ncortex's workspace
// save() spells out inline. POSIX only: the lock is flock(2), advisory, held
// by the open file description, so it also excludes another file_lock on the
// same path inside this process, and the kernel drops it if the holder dies.

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace cx::core::filesystem {

class file_lock {
public:
  // Blocks until the lock is held; throws if the lock file cannot be opened
  // or locked. The lock file is created if missing and never removed -
  // unlinking it would let two holders lock two different inodes.
  explicit file_lock(std::filesystem::path path) : path_(std::move(path)) {
    descriptor_ = open_lock_file(path_);
    if (::flock(descriptor_, LOCK_EX) != 0) {
      ::close(descriptor_);
      throw std::runtime_error("cannot lock " + path_.string());
    }
  }

  // std::nullopt when someone else holds it, instead of waiting.
  static std::optional<file_lock> try_acquire(std::filesystem::path path) {
    const int descriptor = open_lock_file(path);
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
      ::close(descriptor);
      return std::nullopt;
    }
    return file_lock(std::move(path), descriptor);
  }

  file_lock(const file_lock &) = delete;
  file_lock &operator=(const file_lock &) = delete;

  file_lock(file_lock &&other) noexcept : path_(std::move(other.path_)), descriptor_(std::exchange(other.descriptor_, -1)) {}
  file_lock &operator=(file_lock &&other) noexcept {
    if (this != &other) {
      release();
      path_ = std::move(other.path_);
      descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
  }

  ~file_lock() { release(); }

  void release() noexcept {
    if (descriptor_ >= 0) {
      ::flock(descriptor_, LOCK_UN);
      ::close(descriptor_);
      descriptor_ = -1;
    }
  }

  bool held() const noexcept { return descriptor_ >= 0; }
  const std::filesystem::path &path() const noexcept { return path_; }

private:
  file_lock(std::filesystem::path path, int descriptor) : path_(std::move(path)), descriptor_(descriptor) {}

  static int open_lock_file(const std::filesystem::path &path) {
    if (path.has_parent_path()) {
      std::filesystem::create_directories(path.parent_path());
    }
    const int descriptor = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (descriptor < 0) {
      throw std::runtime_error("cannot open lock file " + path.string());
    }
    return descriptor;
  }

  std::filesystem::path path_;
  int descriptor_ = -1;
};

// Writes through `write` into a scratch file beside `target`, then renames it
// over `target`: a reader sees the old content or the new, never half of
// either. The scratch name carries the pid so concurrent writers do not share
// one. A failed write removes the scratch and leaves `target` untouched.
inline void atomic_replace(const std::filesystem::path &target, const std::function<void(std::ostream &)> &write) {
  if (target.has_parent_path()) {
    std::filesystem::create_directories(target.parent_path());
  }
  const std::filesystem::path scratch = target.string() + ".tmp." + std::to_string(::getpid());
  try {
    {
      std::ofstream out(scratch, std::ios::trunc | std::ios::binary);
      write(out);
      out.flush();
      if (!out) {
        throw std::runtime_error("cannot write " + scratch.string());
      }
    }
    std::filesystem::rename(scratch, target);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(scratch, ignored);
    throw;
  }
}

} // namespace cx::core::filesystem
