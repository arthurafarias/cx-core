// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// @file
/// @ingroup serialization
/// @brief cx::core::serialization::valid_utf8 - a strict UTF-8 well-formedness
/// check. Shared by MQTT string fields (SRS-008 §3.1 / `[MQTT-1.5.4]`) and
/// WebSocket text frames + close reasons (SRS-018 §8.1 / `[RFC6455-8.1]`).
///
/// Rejects overlong encodings, surrogate code points, code points above
/// U+10FFFF, and (unless `allow_nul`) U+0000. Incremental validation
/// (`utf8_validator`) is provided for a text message reassembled across
/// frames.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cx::core::serialization {

/// @ingroup serialization
/// @brief Whether @p bytes is well-formed UTF-8.
/// @param allow_nul permit the U+0000 code point (MQTT forbids it, RFC 6455
///        permits it).
inline bool valid_utf8(std::span<const std::byte> bytes, bool allow_nul = true) {
  std::size_t i = 0;
  const std::size_t n = bytes.size();
  auto at = [&](std::size_t k) { return static_cast<std::uint8_t>(bytes[k]); };
  while (i < n) {
    std::uint8_t c = at(i);
    if (c == 0x00 && !allow_nul) {
      return false;
    }
    if (c < 0x80) {
      ++i;
      continue;
    }
    std::size_t extra;
    std::uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
      extra = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + extra >= n) {
      return false;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      std::uint8_t cc = at(i + k);
      if ((cc & 0xC0) != 0x80) {
        return false;
      }
      cp = (cp << 6) | (cc & 0x3F);
    }
    static constexpr std::uint32_t min_for[4] = {0, 0x80, 0x800, 0x10000};
    if (cp < min_for[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      return false;
    }
    i += extra + 1;
  }
  return true;
}

inline bool valid_utf8(std::string_view s, bool allow_nul = true) {
  return valid_utf8(std::span<const std::byte>(reinterpret_cast<const std::byte *>(s.data()), s.size()), allow_nul);
}

/// @ingroup serialization
/// @brief Streaming UTF-8 validator: `feed()` chunks (a sequence may split a
/// multi-byte character across chunks), then `done()` checks it ended on a
/// character boundary and never saw an error.
class utf8_validator {
public:
  /// @return `false` once an ill-formed sequence has been seen.
  bool feed(std::span<const std::byte> chunk) {
    if (bad_) {
      return false;
    }
    for (std::byte b : chunk) {
      if (!step(static_cast<std::uint8_t>(b))) {
        bad_ = true;
        return false;
      }
    }
    return true;
  }
  /// @return `true` iff every byte was well-formed and no partial character
  /// is pending.
  bool done() const { return !bad_ && need_ == 0; }

private:
  bool step(std::uint8_t c) {
    if (need_ == 0) {
      if (c < 0x80) {
        return true;
      }
      if ((c & 0xE0) == 0xC0) {
        need_ = 1;
        cp_ = c & 0x1F;
        min_ = 0x80;
      } else if ((c & 0xF0) == 0xE0) {
        need_ = 2;
        cp_ = c & 0x0F;
        min_ = 0x800;
      } else if ((c & 0xF8) == 0xF0) {
        need_ = 3;
        cp_ = c & 0x07;
        min_ = 0x10000;
      } else {
        return false;
      }
      seen_ = 0;
      return true;
    }
    if ((c & 0xC0) != 0x80) {
      return false;
    }
    cp_ = (cp_ << 6) | (c & 0x3F);
    if (++seen_ == need_) {
      need_ = 0;
      if (cp_ < min_ || cp_ > 0x10FFFF || (cp_ >= 0xD800 && cp_ <= 0xDFFF)) {
        return false;
      }
    }
    return true;
  }

  std::uint32_t cp_ = 0, min_ = 0;
  int need_ = 0, seen_ = 0;
  bool bad_ = false;
};

} // namespace cx::core::serialization
