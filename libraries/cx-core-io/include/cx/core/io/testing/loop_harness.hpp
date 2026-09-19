// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// @file
/// @ingroup testing
/// @brief cx::core::testing::await - block a test thread on a future
/// with a hard timeout.
///
/// Shared helper for the networking test groups: block the test thread on a
/// `std::future` produced by a listener running on the event loop, with a
/// hard timeout so a broken socket path fails the case instead of hanging the
/// run.

#include <chrono>
#include <future>
#include <optional>

namespace cx::core::testing {

/// @ingroup testing
/// @brief Wait up to @p timeout for @p future, then take its value.
/// @tparam T Future's value type.
/// @param future  The future to wait on.
/// @param timeout Maximum wait; defaults to 2000 ms.
/// @return The future's value, or `std::nullopt` if it was not ready in time.
template <typename T>
std::optional<T> await(std::future<T> &future,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
  if (future.wait_for(timeout) != std::future_status::ready) {
    return std::nullopt;
  }
  return future.get();
}

} // namespace cx::core::testing
