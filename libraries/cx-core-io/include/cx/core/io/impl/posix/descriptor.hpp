// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// @file
/// @ingroup core
/// @brief POSIX backend for cx::core::io::descriptor (SRS-019 §3.3).
///
/// Included only through `core/descriptor.hpp`. A `state` is an owning
/// `int fd`; `read` / `write` translate `-1` to `{ -1, std::errc(errno) }`
/// and `EINTR` is returned to the caller, not retried.

#include <cerrno>
#include <cstddef>
#include <span>
#include <system_error>
#include <utility>

#include <unistd.h>

#include <cx/core/io/io_result.hpp>

namespace cx::core::io::descriptor::impl::posix {

/// @brief The raw handle type: a POSIX file descriptor.
using native_handle = int;
/// @brief The sentinel for "no open handle".
inline constexpr native_handle invalid_handle = -1;

/// @ingroup core
/// @brief Move-only RAII owner of one file descriptor; closes it on
/// destruction unless released.
class state {
public:
  state() = default;
  /// @brief Take ownership of an already-open descriptor.
  explicit state(native_handle fd) : fd_(fd) {}

  state(const state &) = delete;
  state &operator=(const state &) = delete;

  state(state &&other) noexcept : fd_(std::exchange(other.fd_, invalid_handle)) {}
  state &operator=(state &&other) noexcept {
    if (this != &other) {
      reset();
      fd_ = std::exchange(other.fd_, invalid_handle);
    }
    return *this;
  }

  ~state() { reset(); }

  /// @brief The raw descriptor, or #invalid_handle.
  native_handle fd() const { return fd_; }
  /// @brief Hand ownership out; the state becomes invalid without closing.
  native_handle release() { return std::exchange(fd_, invalid_handle); }

  /// @brief `close()` the descriptor if open and become invalid. Idempotent.
  void reset() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = invalid_handle;
    }
  }

private:
  native_handle fd_ = invalid_handle;
};

/// @brief Wrap an already-open descriptor.
inline state adopt(native_handle fd) { return state(fd); }

/// @brief `::read` into @p buf. `count == 0` is EOF.
inline io_result read(state &d, std::span<std::byte> buf) {
  ssize_t n = ::read(d.fd(), buf.data(), buf.size());
  if (n < 0) {
    return {-1, std::errc(errno)};
  }
  return {static_cast<std::ptrdiff_t>(n), std::errc{}};
}

/// @brief `::write` from @p buf.
inline io_result write(state &d, std::span<const std::byte> buf) {
  ssize_t n = ::write(d.fd(), buf.data(), buf.size());
  if (n < 0) {
    return {-1, std::errc(errno)};
  }
  return {static_cast<std::ptrdiff_t>(n), std::errc{}};
}

/// @brief Close @p d's descriptor. Idempotent; leaves @p d invalid.
inline void close(state &d) { d.reset(); }
/// @brief Whether @p d holds an open descriptor.
inline bool valid(const state &d) { return d.fd() >= 0; }
/// @brief The raw descriptor of @p d, or #invalid_handle.
inline native_handle native(const state &d) { return d.fd(); }
/// @brief Hand @p d's descriptor out without closing it.
inline native_handle release(state &d) { return d.release(); }

} // namespace cx::core::io::descriptor::impl::posix
