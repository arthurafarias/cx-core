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
/// @brief Shared, backend-independent result vocabulary for the `core::*`
/// facility façades (SRS-019 §2.5).
///
/// In its own header so a backend under `core/impl/` can use it without
/// pulling in - or circularly including - a façade. A value-initialised
/// `std::errc{}` means success everywhere in `core::*`.

#include <cstddef>
#include <system_error>

namespace cx::core::io {

/// @ingroup core
/// @brief cx::core::io::descriptor vocabulary that a backend needs
/// before the façade header is available.
namespace descriptor {

/// @ingroup core
/// @brief The outcome of a raw descriptor read/write.
///
/// `error == std::errc{}` is success. For a stream read, `count == 0` with no
/// error is end-of-file; a failure sets #error and leaves `count < 0`.
struct io_result {
  std::ptrdiff_t count = 0;    ///< Bytes transferred; `0` == EOF for a stream read.
  std::errc error = std::errc{}; ///< `std::errc{}` on success.
};

} // namespace descriptor

/// @ingroup core
/// @brief Whether @p e is the "try again later" condition a non-blocking
/// socket reports when it would otherwise block.
/// @param e An error from a `core::descriptor` / `core::socket_ops` call.
/// @return `true` for `operation_would_block` / `resource_unavailable_try_again`.
inline bool would_block(std::errc e) {
  return e == std::errc::operation_would_block || e == std::errc::resource_unavailable_try_again;
}

} // namespace cx::core::io
