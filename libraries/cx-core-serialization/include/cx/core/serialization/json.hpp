// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <cx/core/serialization/text_escape.hpp>

// JSON as a wire format: a document tree, a strict RFC 8259 reader and a
// compact writer. Deliberately small. Brought in from agenticx-ncortex, whose
// HTTP applications read request bodies and write responses with it; it is not
// an archiver (see tags.hpp for that protocol).
namespace cx::core::serialization::json {

struct value;
using array = std::vector<value>;
using object = std::map<std::string, value, std::less<>>;

struct value : std::variant<std::nullptr_t, bool, double, std::string, array, object> {
  using variant::variant;

  // An integer is a JSON number: std::variant alone refuses int -> double as narrowing, which would leave
  // `{"index", 0}` without a constructor. bool stays bool.
  template <typename integer>
    requires(std::is_integral_v<integer> && !std::is_same_v<integer, bool>)
  value(integer number) : variant(static_cast<double>(number)) {}

  template <typename type> bool is() const { return std::holds_alternative<type>(*this); }
  template <typename type> const type &as() const { return std::get<type>(*this); }

  // Member `key` of an object, or nullptr when this is no object or has none.
  const value *find(std::string_view key) const {
    if (!is<object>())
      return nullptr;
    const auto found = as<object>().find(key);
    return found == as<object>().end() ? nullptr : &found->second;
  }

  std::string text(std::string_view key, std::string fallback = {}) const {
    const value *member = find(key);
    return member != nullptr && member->is<std::string>() ? member->as<std::string>() : std::move(fallback);
  }

  double number(std::string_view key, double fallback) const {
    const value *member = find(key);
    return member != nullptr && member->is<double>() ? member->as<double>() : fallback;
  }

  bool flag(std::string_view key, bool fallback) const {
    const value *member = find(key);
    return member != nullptr && member->is<bool>() ? member->as<bool>() : fallback;
  }
};

inline std::string escape(std::string_view text) { return json_escaped(text); }

inline std::string quote(std::string_view text) { return "\"" + escape(text) + "\""; }

// Compact serialization of a document tree.
inline std::string dump(const value &document) {
  if (document.is<std::nullptr_t>())
    return "null";
  if (document.is<bool>())
    return document.as<bool>() ? "true" : "false";
  if (document.is<double>()) {
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.17g", document.as<double>());
    return buffer;
  }
  if (document.is<std::string>())
    return quote(document.as<std::string>());
  std::string out;
  if (document.is<array>()) {
    for (const auto &item : document.as<array>())
      out += (out.empty() ? "" : ",") + dump(item);
    return "[" + out + "]";
  }
  for (const auto &[key, member] : document.as<object>())
    out += (out.empty() ? "" : ",") + quote(key) + ":" + dump(member);
  return "{" + out + "}";
}

namespace detail {
struct reader {
  std::string_view text;
  std::size_t at = 0;

  [[noreturn]] void fail(const char *what) const {
    throw std::invalid_argument("json: " + std::string(what) + " at byte " + std::to_string(at));
  }
  void skip() {
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
      ++at;
  }
  bool take(char c) {
    skip();
    if (at < text.size() && text[at] == c) {
      ++at;
      return true;
    }
    return false;
  }
  void literal(std::string_view word) {
    if (text.substr(at, word.size()) != word)
      fail("unexpected token");
    at += word.size();
  }
  static void utf8(std::string &out, std::uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  std::uint32_t hex4() {
    if (at + 4 > text.size())
      fail("short \\u escape");
    std::uint32_t cp = 0;
    const auto [end, failure] = std::from_chars(text.data() + at, text.data() + at + 4, cp, 16);
    if (failure != std::errc{} || end != text.data() + at + 4)
      fail("bad \\u escape");
    at += 4;
    return cp;
  }
  std::string string() {
    if (!take('"'))
      fail("expected a string");
    std::string out;
    while (at < text.size() && text[at] != '"') {
      char c = text[at++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (at >= text.size())
        fail("unterminated escape");
      switch (c = text[at++]) {
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': out += '\r'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'u': {
        std::uint32_t cp = hex4();
        if (cp >= 0xD800 && cp < 0xDC00 && text.substr(at, 2) == "\\u") {
          at += 2;
          cp = 0x10000 + ((cp - 0xD800) << 10) + (hex4() - 0xDC00);
        }
        utf8(out, cp);
        break;
      }
      default: out += c; // \" \\ \/
      }
    }
    if (at >= text.size())
      fail("unterminated string");
    ++at;
    return out;
  }
  value parse() {
    skip();
    if (at >= text.size())
      fail("unexpected end");
    const char c = text[at];
    if (c == '{') {
      ++at;
      object members;
      if (take('}'))
        return members;
      do {
        std::string key = string();
        if (!take(':'))
          fail("expected ':'");
        members.insert_or_assign(std::move(key), parse());
      } while (take(','));
      if (!take('}'))
        fail("expected '}'");
      return members;
    }
    if (c == '[') {
      ++at;
      array items;
      if (take(']'))
        return items;
      do
        items.push_back(parse());
      while (take(','));
      if (!take(']'))
        fail("expected ']'");
      return items;
    }
    if (c == '"')
      return string();
    if (c == 't') {
      literal("true");
      return true;
    }
    if (c == 'f') {
      literal("false");
      return false;
    }
    if (c == 'n') {
      literal("null");
      return nullptr;
    }
    double number = 0;
    const auto [end, failure] = std::from_chars(text.data() + at, text.data() + text.size(), number);
    if (failure != std::errc{})
      fail("unexpected token");
    at = static_cast<std::size_t>(end - text.data());
    return number;
  }
};
} // namespace detail

inline value parse(std::string_view text) {
  detail::reader reader{text};
  value document = reader.parse();
  reader.skip();
  if (reader.at != text.size())
    reader.fail("trailing bytes");
  return document;
}

} // namespace cx::core::serialization::json
