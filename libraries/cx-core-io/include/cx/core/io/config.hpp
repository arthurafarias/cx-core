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
/// @brief Compile-time backend selection for the cx::core::io facility façades -
/// cx::core::io::descriptor and cx::core::io::poller here, and cx-networking's
/// socket_ops and resolver, which read the same switch (SRS-019 §2.2).
///
/// Exactly one of `CX_CORE_BACKEND_POSIX` / `CX_CORE_BACKEND_STANDALONE` is
/// defined to `1` and the other to `0`. Override on the command line with
/// `-DCX_CORE_BACKEND_STANDALONE=1` (or `=POSIX`); otherwise `POSIX` is chosen
/// on any Unix and `STANDALONE` everywhere else.
///
/// `cx::core::io::poller` uses this switch too. Its `posix` backend
/// then makes a second, internal choice between the `poll(2)` and `epoll(7)`
/// engines (automatic, or forced by `CX_NETWORKING_IO_BACKEND`) — that engine
/// selection exists only under `CX_CORE_BACKEND_POSIX` (SRS-019 §4).

// The pre-cx-core spellings, from when this selection lived in cx-networking.
#if defined(CX_NET_BACKEND_STANDALONE) && !defined(CX_CORE_BACKEND_STANDALONE)
#define CX_CORE_BACKEND_STANDALONE CX_NET_BACKEND_STANDALONE
#endif
#if defined(CX_NET_BACKEND_POSIX) && !defined(CX_CORE_BACKEND_POSIX)
#define CX_CORE_BACKEND_POSIX CX_NET_BACKEND_POSIX
#endif

#if defined(CX_CORE_BACKEND_STANDALONE) && (CX_CORE_BACKEND_STANDALONE + 0)
#undef CX_CORE_BACKEND_STANDALONE
#define CX_CORE_BACKEND_STANDALONE 1
#define CX_CORE_BACKEND_POSIX 0
#elif defined(CX_CORE_BACKEND_POSIX) && (CX_CORE_BACKEND_POSIX + 0)
#undef CX_CORE_BACKEND_POSIX
#define CX_CORE_BACKEND_POSIX 1
#define CX_CORE_BACKEND_STANDALONE 0
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#define CX_CORE_BACKEND_POSIX 1
#define CX_CORE_BACKEND_STANDALONE 0
#else
#define CX_CORE_BACKEND_POSIX 0
#define CX_CORE_BACKEND_STANDALONE 1
#endif
