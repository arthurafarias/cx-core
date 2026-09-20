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
/// @brief cx::core::io::descriptor - the façade over one owned OS
/// handle and its raw I/O (SRS-019 §3).
///
/// Selects a backend from `core/config.hpp` by namespace alias; every
/// consumer names only this header. `descriptor::state` is a move-only RAII
/// owner - raw use is always spelled `descriptor::native(s)`. No virtual
/// dispatch: the forwarding calls devirtualise to the backend functions
/// directly.

#include <concepts>
#include <cstddef>
#include <span>
#include <system_error>

#include <cx/core/io/config.hpp>
#include <cx/core/io/io_result.hpp>

#if CX_CORE_BACKEND_POSIX
#include <cx/core/io/impl/posix/descriptor.hpp>
#elif CX_CORE_BACKEND_STANDALONE
#error "cx::core::io::descriptor: the standalone backend is not implemented yet (SRS-019 M3)"
#endif

/// @ingroup core
/// @brief The owned-descriptor facility: an RAII handle plus its raw
/// read/write. See SRS-019 §3.
namespace cx::core::io::descriptor {

#if CX_CORE_BACKEND_POSIX
namespace backend = impl::posix;
#endif

/// @brief The backend's raw handle type (`int` on posix).
using native_handle = backend::native_handle;
/// @brief The move-only RAII owner of one handle.
using state = backend::state;
/// @brief The backend's "no open handle" sentinel.
inline constexpr native_handle invalid_handle = backend::invalid_handle;
using io::would_block;

/// @brief Wrap an already-open handle.
inline state adopt(native_handle h) { return backend::adopt(h); }
/// @brief Read into @p b; `count == 0` is EOF, `error` set on failure.
inline io_result read(state &d, std::span<std::byte> b) { return backend::read(d, b); }
/// @brief Write from @p b; `error` set on failure.
inline io_result write(state &d, std::span<const std::byte> b) { return backend::write(d, b); }
/// @brief Close @p d. Idempotent; leaves it invalid.
inline void close(state &d) { backend::close(d); }
/// @brief Whether @p d holds an open handle.
inline bool valid(const state &d) { return backend::valid(d); }
/// @brief The raw handle of @p d, or #invalid_handle.
inline native_handle native(const state &d) { return backend::native(d); }
/// @brief Hand @p d's handle out; @p d becomes invalid without closing.
inline native_handle release(state &d) { return backend::release(d); }

// --- backend contract (SRS-019 §2.3) -------------------------------------

static_assert(
    requires(state s, const state cs, native_handle h, std::span<std::byte> w, std::span<const std::byte> r) {
      { backend::adopt(h) } -> std::same_as<state>;
      { backend::read(s, w) } -> std::same_as<io_result>;
      { backend::write(s, r) } -> std::same_as<io_result>;
      { backend::close(s) };
      { backend::valid(cs) } -> std::same_as<bool>;
      { backend::native(cs) } -> std::same_as<native_handle>;
    },
    "selected cx::core::io::descriptor backend is incomplete (SRS-019 §2.3)");

} // namespace cx::core::io::descriptor
