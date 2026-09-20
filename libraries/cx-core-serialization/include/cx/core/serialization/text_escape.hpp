// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

// Escaping a string into the three text formats a record is commonly written
// in. Writers, not encoders: each streams straight into an ostream, so a
// serializer never builds the escaped copy it is about to throw away.

#include <format>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

namespace cx::core::serialization {

// RFC 8259 §7: quote, backslash and every control character below U+0020 must
// be escaped; a raw one makes the document unparseable. Bytes from 0x80 up are
// UTF-8 and pass through.
inline void write_json_escaped(std::ostream &out, std::string_view text) {
  for (char c : text) {
    switch (c) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        out << std::format("\\u{:04x}", static_cast<unsigned>(c));
      } else {
        out << c;
      }
    }
  }
}

// The escaped copy itself, for a caller assembling a line rather than streaming one.
inline std::string json_escaped(std::string_view text) {
  std::ostringstream out;
  write_json_escaped(out, text);
  return std::move(out).str();
}

inline void write_xml_escaped(std::ostream &out, const std::string &text) {
  for (char c : text) {
    switch (c) {
    case '&':
      out << "&amp;";
      break;
    case '<':
      out << "&lt;";
      break;
    case '>':
      out << "&gt;";
      break;
    case '"':
      out << "&quot;";
      break;
    case '\'':
      out << "&apos;";
      break;
    default:
      out << c;
    }
  }
}

// RFC 4180: a field containing the delimiter, a quote, or a newline must be
// quoted, with embedded quotes doubled.
inline void write_csv_field(std::ostream &out, const std::string &field) {
  bool needs_quoting = field.find_first_of(",\"\n\r") != std::string::npos;
  if (!needs_quoting) {
    out << field;
    return;
  }
  out << '"';
  for (char c : field) {
    if (c == '"') {
      out << "\"\"";
    } else {
      out << c;
    }
  }
  out << '"';
}

} // namespace cx::core::serialization
