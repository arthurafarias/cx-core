// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// @file
/// @ingroup containers
/// @brief The byte container handed to every `data` / `message` listener - the
/// analogue of Node.js's `Buffer`.
///
/// cx::core::containers::buffer is a thin owning `std::vector<std::byte>`. DNS
/// and most line protocols are handled as bytes rather than text, so the
/// string-interop helpers here are explicit conversions rather than implicit
/// behaviour.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cx::core::containers {

/// @ingroup containers
/// @brief Owning byte sequence passed to socket `data` / `message` listeners.
using buffer = std::vector<std::byte>;

/// @ingroup containers
/// @brief Copy the bytes of @p text into a fresh buffer.
/// @param text Characters to copy verbatim; no encoding conversion is done.
/// @return A buffer of `text.size()` bytes.
inline buffer make_buffer(std::string_view text) {
  buffer out(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    out[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
  }
  return out;
}

/// @ingroup containers
/// @brief Copy a span of bytes into a fresh buffer.
/// @param bytes Source bytes.
/// @return An owning copy of @p bytes.
inline buffer make_buffer(std::span<const std::byte> bytes) { return buffer(bytes.begin(), bytes.end()); }

/// @ingroup containers
/// @brief Reinterpret raw bytes as a `std::string` (one `char` per byte).
/// @param bytes Source bytes.
/// @return A string of `bytes.size()` characters; no encoding conversion.
inline std::string to_string(std::span<const std::byte> bytes) {
  std::string out(bytes.size(), '\0');
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    out[i] = static_cast<char>(bytes[i]);
  }
  return out;
}

/// @ingroup containers
/// @brief Append @p bytes to the end of @p dst.
/// @param[in,out] dst   Buffer to grow.
/// @param         bytes Bytes to append.
inline void append(buffer &dst, std::span<const std::byte> bytes) { dst.insert(dst.end(), bytes.begin(), bytes.end()); }

} // namespace cx::core::containers
