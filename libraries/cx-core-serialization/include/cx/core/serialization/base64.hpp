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
/// @brief Base64 (RFC 4648) — standard alphabet, used by PEM (SRS-016 §6.2)
/// and `Sec-WebSocket-Accept` (SRS-018 §4).

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cx::core::serialization {

inline constexpr std::string_view base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @ingroup serialization
/// @brief RFC 4648 base64 with `=` padding.
inline std::string base64_encode(std::span<const std::byte> in) {
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 3 <= in.size(); i += 3) {
    std::uint32_t n = (std::uint32_t(std::uint8_t(in[i])) << 16) | (std::uint32_t(std::uint8_t(in[i + 1])) << 8) |
                      std::uint32_t(std::uint8_t(in[i + 2]));
    out.push_back(base64_alphabet[(n >> 18) & 63]);
    out.push_back(base64_alphabet[(n >> 12) & 63]);
    out.push_back(base64_alphabet[(n >> 6) & 63]);
    out.push_back(base64_alphabet[n & 63]);
  }
  std::size_t rem = in.size() - i;
  if (rem == 1) {
    std::uint32_t n = std::uint32_t(std::uint8_t(in[i])) << 16;
    out.push_back(base64_alphabet[(n >> 18) & 63]);
    out.push_back(base64_alphabet[(n >> 12) & 63]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    std::uint32_t n = (std::uint32_t(std::uint8_t(in[i])) << 16) | (std::uint32_t(std::uint8_t(in[i + 1])) << 8);
    out.push_back(base64_alphabet[(n >> 18) & 63]);
    out.push_back(base64_alphabet[(n >> 12) & 63]);
    out.push_back(base64_alphabet[(n >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

inline std::string base64_encode(std::string_view in) {
  return base64_encode(std::span<const std::byte>(reinterpret_cast<const std::byte *>(in.data()), in.size()));
}

/// @ingroup serialization
/// @brief RFC 4648 base64 decode. Whitespace is skipped; anything else
/// outside the alphabet (bar `=` padding) is an error.
inline std::expected<std::vector<std::byte>, std::string> base64_decode(std::string_view in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') {
      return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
      return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
      return c - '0' + 52;
    }
    if (c == '+') {
      return 62;
    }
    if (c == '/') {
      return 63;
    }
    return -1;
  };
  std::vector<std::byte> out;
  std::uint32_t acc = 0;
  int bits = 0;
  int pad = 0;
  for (char c : in) {
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
      continue;
    }
    if (c == '=') {
      ++pad;
      continue;
    }
    if (pad != 0) {
      return std::unexpected("base64: data after padding");
    }
    int v = val(c);
    if (v < 0) {
      return std::unexpected(std::string("base64: illegal character '") + c + "'");
    }
    acc = (acc << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(std::byte((acc >> bits) & 0xFF));
    }
  }
  return out;
}

} // namespace cx::core::serialization
