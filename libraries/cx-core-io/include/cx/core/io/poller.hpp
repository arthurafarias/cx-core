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
/// @brief cx::core::io::poller - the façade over readiness
/// multiplexing and the cross-thread wake (SRS-019 §4).
///
/// Selects a backend from `core/config.hpp` by namespace alias, like every
/// other `core::*` facility. The `posix` backend is a single file that folds
/// `poll(2)` and `epoll(7)` together and auto-selects the engine; nothing
/// above this header names a syscall family. `event_loop` owns one
/// `poller::state` as a direct member and calls only these free functions.

#include <chrono>
#include <concepts>
#include <cstddef>
#include <functional>
#include <string_view>

#include <cx/core/io/config.hpp>
#include <cx/core/io/poller_events.hpp>

#if CX_CORE_BACKEND_POSIX
#include <cx/core/io/impl/posix/poller.hpp>
#elif CX_CORE_BACKEND_STANDALONE
#error "cx::core::io::poller: the standalone backend is not implemented yet (SRS-019 M3)"
#endif

/// @ingroup core
/// @brief The readiness-multiplexing facility behind
/// cx::core::io::event_loop. See SRS-019 §4.
namespace cx::core::io::poller {

#if CX_CORE_BACKEND_POSIX
namespace backend = impl::posix;
#endif

/// @brief The poll set plus its internal wake channel. Non-copyable and
/// non-movable; hold it as a direct data member.
using state = backend::state;
/// @brief The callback fired on the loop thread for a ready descriptor.
using callback = backend::callback;

/// @brief Register (or replace) interest in @p fd and the callback for it.
/// Takes effect on the next dispatch(), which is interrupted so a concurrent
/// block returns promptly. Thread-safe.
inline void add(state &s, int fd, io_event interest, callback cb) { backend::add(s, fd, interest, std::move(cb)); }
/// @brief Change @p fd's interest mask. No-op if @p fd is not registered.
/// Thread-safe.
inline void modify(state &s, int fd, io_event interest) { backend::modify(s, fd, interest); }
/// @brief Drop @p fd's registration. Does not close the descriptor.
/// Thread-safe.
inline void remove(state &s, int fd) { backend::remove(s, fd); }
/// @brief Block up to @p timeout for readiness (negative = forever), then run
/// the callback of every ready descriptor. Returns the number invoked. Loop
/// thread only.
inline std::size_t dispatch(state &s, std::chrono::milliseconds timeout) { return backend::dispatch(s, timeout); }
/// @brief Make a concurrent or next dispatch() return promptly. Any thread.
inline void interrupt(state &s) { backend::interrupt(s); }
/// @brief Short identifier of the live engine (`"poll"` / `"epoll"`), for
/// logs and diagnostics.
inline std::string_view backend_name(const state &s) { return backend::backend_name(s); }

// --- backend contract (SRS-019 §2.3) -----------------------------------

static_assert(
    requires(state s, const state cs, int fd, io_event e, callback cb, std::chrono::milliseconds t) {
      { backend::add(s, fd, e, std::move(cb)) };
      { backend::modify(s, fd, e) };
      { backend::remove(s, fd) };
      { backend::dispatch(s, t) } -> std::same_as<std::size_t>;
      { backend::interrupt(s) };
      { backend::backend_name(cs) } -> std::convertible_to<std::string_view>;
    },
    "selected cx::core::io::poller backend is incomplete (SRS-019 §2.3)");

} // namespace cx::core::io::poller
