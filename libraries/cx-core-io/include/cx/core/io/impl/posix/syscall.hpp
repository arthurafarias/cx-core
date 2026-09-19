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
/// @brief Bare POSIX descriptor helpers for the raw layer-2 surface
/// (SRS-019 §5.4).
///
/// cx::core::io::socket_ops covers the INET stream/datagram surface
/// (`tcp::*` / `udp::peer` / `dns::*`). The `AF_PACKET` / TAP endpoints -
/// cx::networking::protocol::raw::endpoint and
/// cx::networking::netstack::device - are an explicitly POSIX-only facility
/// with no `standalone` counterpart: they own a raw `int fd` and issue
/// `ioctl` / `recvmsg` directly. These three helpers are all they need from
/// the old `core/native.hpp`, kept here under `core/impl/posix/` so no
/// header above it defines an OS-syscall wrapper.

#include <cerrno>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cx::core::io::impl::posix {

/// @brief Put @p fd into non-blocking mode (`O_NONBLOCK`).
/// @return `true` on success, `false` if either `fcntl` call failed.
inline bool set_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  return flags != -1 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

/// @brief `close()` @p fd if open and set it to `-1`. No-op when negative.
inline void close_fd(int &fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

/// @brief Format the current `errno` as `"<prefix>: <strerror>"`.
inline std::string last_error(const char *prefix) {
  return std::string(prefix) + ": " + std::strerror(errno);
}

} // namespace cx::core::io::impl::posix
