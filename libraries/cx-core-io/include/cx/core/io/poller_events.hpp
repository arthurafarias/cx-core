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
/// @brief cx::core::io::io_event - the backend-neutral readiness mask.
///
/// The one vocabulary type cx::core::io::poller trades in. Callers
/// register interest and receive results in terms of `io_event` rather than
/// the kernel's readiness-syscall constants, so `event_loop` and the
/// protocol layer never depend on which readiness engine the `posix` poller
/// backend picked (SRS-019 §4).

#include <type_traits>

namespace cx::core::io {

/// @ingroup core
/// @brief A set of readiness conditions on a file descriptor.
///
/// Values are flags and combine with the bitwise operators below. `readable`
/// and `writable` are the interests a caller asks for; `error` and `hangup`
/// are report-only - a backend may deliver them even when they were not
/// requested.
enum class io_event : unsigned {
  none = 0,             ///< No condition.
  readable = 1u << 0,   ///< Data (or an incoming connection) can be read now.
  writable = 1u << 1,   ///< The descriptor can accept more output now.
  error = 1u << 2,      ///< An asynchronous error is pending on the descriptor.
  hangup = 1u << 3,     ///< The peer closed or the descriptor was hung up.
};

/// @brief Union of two readiness masks.
constexpr io_event operator|(io_event a, io_event b) noexcept {
  using u = std::underlying_type_t<io_event>;
  return static_cast<io_event>(static_cast<u>(a) | static_cast<u>(b));
}

/// @brief Intersection of two readiness masks.
constexpr io_event operator&(io_event a, io_event b) noexcept {
  using u = std::underlying_type_t<io_event>;
  return static_cast<io_event>(static_cast<u>(a) & static_cast<u>(b));
}

/// @brief Symmetric difference of two readiness masks.
constexpr io_event operator^(io_event a, io_event b) noexcept {
  using u = std::underlying_type_t<io_event>;
  return static_cast<io_event>(static_cast<u>(a) ^ static_cast<u>(b));
}

/// @brief Complement of a readiness mask.
constexpr io_event operator~(io_event a) noexcept {
  using u = std::underlying_type_t<io_event>;
  return static_cast<io_event>(~static_cast<u>(a));
}

/// @brief Add the bits of @p b into @p a in place.
constexpr io_event &operator|=(io_event &a, io_event b) noexcept { return a = a | b; }
/// @brief Keep only the bits of @p a that are also in @p b, in place.
constexpr io_event &operator&=(io_event &a, io_event b) noexcept { return a = a & b; }

/// @brief Whether @p e carries at least one condition.
/// @param e Mask to test, typically the result of an `&` with the flags of
///        interest, e.g. `any(revents & io_event::writable)`.
constexpr bool any(io_event e) noexcept { return e != io_event::none; }

} // namespace cx::core::io
