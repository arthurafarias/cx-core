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
#include <string>

namespace cx::core::serialization {

inline void write_json_escaped(std::ostream &out, const std::string &text) {
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
      out << c;
    }
  }
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
