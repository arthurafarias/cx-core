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
/// @brief cx::core::io::coro::pump_with - drive a
/// cx::core::threading::coroutine_executor from a
/// cx::core::io::event_loop.
///
/// After `pump_with(cex, loop)`, coroutines spawned on @p cex are pumped on
/// the loop thread: whenever work is queued (from the loop thread or another
/// thread), the executor posts a drain onto the loop. This is how a
/// threadless coroutine scheduler runs alongside socket I/O.
///
/// For socket-only coroutines you can skip it and spawn straight onto the
/// `event_loop` (it is itself an cx::core::threading::async_executor); the
/// coroutine then resumes via `loop->defer`.

#include <memory>
#include <utility>

#include <cx/core/threading/coroutine_executor.hpp>
#include <cx/core/io/event_loop.hpp>

namespace cx::core::io::coro {

/// @ingroup core
/// @brief Wire @p cex to drain on @p loop's thread whenever it has work, and
/// start it. Call once, before spawning onto @p cex.
inline void pump_with(std::shared_ptr<threading::coroutine_executor> cex, std::shared_ptr<event_loop> loop) {
  auto weak_cex = std::weak_ptr<threading::coroutine_executor>(cex);
  cex->set_wake([loop = std::move(loop), weak_cex] {
    if (auto c = weak_cex.lock()) {
      loop->defer([weak_cex] {
        if (auto c = weak_cex.lock()) {
          c->run_until_idle();
        }
      });
    }
  });
  cex->start();
}

} // namespace cx::core::io::coro
