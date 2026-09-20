// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// \file
/// \brief serialization::yaml - one dependency-free YAML reader and writer for every cx project: the recipes
/// cx-algebra builds graphs from, agenticx-ncortex's `ncortex.yaml`, and the one-line flow form its command-line
/// tokens carry.
///
/// Deliberately an explicit grammar rather than a general YAML parser (the same call json.hpp makes): block
/// mappings, block sequences (including the compact `- key: value` item form), flow collections on one line
/// (`{a: 1, b: [x, y]}`), plain and quoted scalars (`true`/`false`, `null`/`~`, integers, floats, `.inf`/`.nan`,
/// strings), the core-schema tags that pin a scalar's type (`!!str`, `!!int`, `!!float`, `!!bool`, `!!null`, and
/// the non-specific `!`), and `#` comments. Explicitly **unsupported**, rejected with a positioned error rather
/// than silently misread: anchors/aliases, any other tag, flow collections that span lines, multi-document
/// streams, block scalars (`|`, `>`), multi-line plain scalars.
///
/// parse() and dump()/flow() are each other's exact inverse: a string whose plain form would read back as
/// something else is double-quoted, and a double is written in its shortest round-trip form with a fraction or
/// exponent, so `1.0` never comes back as the integer `1`.

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace cx::core::serialization::yaml {

struct error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class value {
public:
  enum class kind : std::uint8_t { null, boolean, integer, floating, string, sequence, mapping };
  using sequence_type = std::vector<value>;
  using mapping_type = std::vector<std::pair<std::string, value>>;

  value() = default;
  static value null() { return value(); }
  static value boolean(bool b) {
    value v;
    v.kind_ = kind::boolean;
    v.data_ = b;
    return v;
  }
  static value integer(long long i) {
    value v;
    v.kind_ = kind::integer;
    v.data_ = i;
    return v;
  }
  static value floating(double d) {
    value v;
    v.kind_ = kind::floating;
    v.data_ = d;
    return v;
  }
  static value string(std::string s) {
    value v;
    v.kind_ = kind::string;
    v.data_ = std::move(s);
    return v;
  }
  static value sequence(sequence_type s = {}) {
    value v;
    v.kind_ = kind::sequence;
    v.data_ = std::move(s);
    return v;
  }
  static value mapping(mapping_type m = {}) {
    value v;
    v.kind_ = kind::mapping;
    v.data_ = std::move(m);
    return v;
  }

  kind type() const { return kind_; }
  bool is_null() const { return kind_ == kind::null; }
  bool is_bool() const { return kind_ == kind::boolean; }
  bool is_int() const { return kind_ == kind::integer; }
  bool is_float() const { return kind_ == kind::floating; }
  bool is_number() const { return is_int() || is_float(); }
  bool is_string() const { return kind_ == kind::string; }
  bool is_sequence() const { return kind_ == kind::sequence; }
  bool is_mapping() const { return kind_ == kind::mapping; }
  bool is_scalar() const { return !is_sequence() && !is_mapping(); }

  bool as_bool() const {
    if (!is_bool()) {
      throw error("yaml: expected a boolean, got " + type_name());
    }
    return std::get<bool>(data_);
  }
  long long as_int() const {
    if (is_int()) {
      return std::get<long long>(data_);
    }
    if (is_float()) {
      const double d = std::get<double>(data_);
      if (d == static_cast<double>(static_cast<long long>(d))) {
        return static_cast<long long>(d);
      }
    }
    throw error("yaml: expected an integer, got " + type_name());
  }
  double as_double() const {
    if (is_float()) {
      return std::get<double>(data_);
    }
    if (is_int()) {
      return static_cast<double>(std::get<long long>(data_));
    }
    throw error("yaml: expected a number, got " + type_name());
  }
  const std::string &as_string() const {
    if (!is_string()) {
      throw error("yaml: expected a string, got " + type_name());
    }
    return std::get<std::string>(data_);
  }
  /// \brief Any scalar rendered as text (what a `{...}` pattern expansion or a name needs).
  std::string to_string() const {
    switch (kind_) {
    case kind::null:
      return "null";
    case kind::boolean:
      return as_bool() ? "true" : "false";
    case kind::integer:
      return std::to_string(as_int());
    case kind::floating: {
      char buf[32];
      std::snprintf(buf, sizeof buf, "%.9g", as_double());
      return buf;
    }
    case kind::string:
      return as_string();
    case kind::sequence:
      return "[sequence]";
    case kind::mapping:
      return "{mapping}";
    }
    return "";
  }

  const sequence_type &as_sequence() const {
    if (!is_sequence()) {
      throw error("yaml: expected a sequence, got " + type_name());
    }
    return std::get<sequence_type>(data_);
  }
  sequence_type &as_sequence() {
    if (!is_sequence()) {
      throw error("yaml: expected a sequence, got " + type_name());
    }
    return std::get<sequence_type>(data_);
  }
  const mapping_type &as_mapping() const {
    if (!is_mapping()) {
      throw error("yaml: expected a mapping, got " + type_name());
    }
    return std::get<mapping_type>(data_);
  }
  mapping_type &as_mapping() {
    if (!is_mapping()) {
      throw error("yaml: expected a mapping, got " + type_name());
    }
    return std::get<mapping_type>(data_);
  }

  std::size_t size() const {
    if (is_sequence()) {
      return as_sequence().size();
    }
    if (is_mapping()) {
      return as_mapping().size();
    }
    return 0;
  }
  const value &operator[](std::size_t i) const { return as_sequence().at(i); }

  /// \brief Mapping lookup; nullptr when absent (or when this is not a mapping).
  const value *find(std::string_view key) const {
    if (!is_mapping()) {
      return nullptr;
    }
    for (const auto &[k, v] : as_mapping()) {
      if (k == key) {
        return &v;
      }
    }
    return nullptr;
  }
  bool has(std::string_view key) const { return find(key) != nullptr; }
  const value &at(std::string_view key) const {
    if (const value *v = find(key)) {
      return *v;
    }
    throw error("yaml: missing key '" + std::string(key) + "'");
  }
  void set(std::string key, value v) {
    auto &m = as_mapping();
    for (auto &e : m) {
      if (e.first == key) {
        e.second = std::move(v);
        return;
      }
    }
    m.emplace_back(std::move(key), std::move(v));
  }

  std::string type_name() const {
    switch (kind_) {
    case kind::null:
      return "null";
    case kind::boolean:
      return "boolean";
    case kind::integer:
      return "integer";
    case kind::floating:
      return "float";
    case kind::string:
      return "string";
    case kind::sequence:
      return "sequence";
    case kind::mapping:
      return "mapping";
    }
    return "?";
  }

  friend bool operator==(const value &a, const value &b) { return a.kind_ == b.kind_ && a.data_ == b.data_; }

private:
  kind kind_ = kind::null;
  std::variant<std::monostate, bool, long long, double, std::string, sequence_type, mapping_type> data_;
};

namespace detail {

struct line {
  std::size_t number; // 1-based
  std::size_t indent;
  std::string text; // comment-stripped, right-trimmed, no leading indent
};

inline std::string trim(std::string_view s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t')) {
    ++b;
  }
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) {
    --e;
  }
  return std::string(s.substr(b, e - b));
}

/// \brief Strips a trailing `# comment` that is not inside quotes (a `#` must be preceded by whitespace or start the line).
inline std::string strip_comment(std::string_view s) {
  char quote = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (quote != 0) {
      if (c == '\\' && quote == '"') {
        ++i;
      } else if (c == quote) {
        quote = 0;
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
      return std::string(s.substr(0, i));
    }
  }
  return std::string(s);
}

inline std::vector<line> split_lines(std::string_view text) {
  std::vector<line> out;
  std::size_t number = 0;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    const std::size_t nl = text.find('\n', pos);
    std::string_view raw = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    ++number;
    pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
    std::size_t indent = 0;
    while (indent < raw.size() && raw[indent] == ' ') {
      ++indent;
    }
    if (indent < raw.size() && raw[indent] == '\t') {
      throw error("yaml:" + std::to_string(number) + ": tabs are not allowed for indentation");
    }
    std::string body = trim(strip_comment(raw.substr(indent)));
    if (body.empty()) {
      continue;
    }
    if (body == "---" || body == "...") {
      if (!out.empty()) {
        throw error("yaml:" + std::to_string(number) + ": multi-document streams are not supported");
      }
      continue;
    }
    if (body.starts_with("%")) {
      throw error("yaml:" + std::to_string(number) + ": directives are not supported");
    }
    out.push_back({number, indent, std::move(body)});
  }
  return out;
}

inline std::string where(const line &l) { return "yaml:" + std::to_string(l.number) + ": "; }

inline constexpr std::size_t max_depth = 128;

inline void append_utf8(std::string &out, unsigned long code) {
  if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xC0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | ((code >> 18) & 0x07));
    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  }
}

inline value parse_flow(std::string_view text, const line &l);
inline value parse_tagged(std::string_view s, const line &l);

inline value parse_scalar(std::string_view s, const line &l) {
  if (s.empty() || s == "~" || s == "null" || s == "Null" || s == "NULL") {
    return value::null();
  }
  if (s.front() == '&' || s.front() == '*') {
    throw error(where(l) + "anchors/aliases are not supported");
  }
  if (s.front() == '!') {
    return parse_tagged(s, l);
  }
  if (s.front() == '|' || s.front() == '>') {
    throw error(where(l) + "block scalars are not supported");
  }
  if (s.front() == '{' || s.front() == '[') {
    return parse_flow(s, l);
  }
  if (s.front() == '"' || s.front() == '\'') {
    const char q = s.front();
    if (s.size() < 2 || s.back() != q) {
      throw error(where(l) + "unterminated quoted string");
    }
    std::string out;
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
      const char c = s[i];
      if (q == '"' && c == '\\' && i + 2 < s.size()) {
        const char e = s[++i];
        switch (e) {
        case 'n':
          out += '\n';
          break;
        case 't':
          out += '\t';
          break;
        case 'r':
          out += '\r';
          break;
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case '0':
          out += '\0';
          break;
        case '/':
          out += '/';
          break;
        case 'a':
          out += '\a';
          break;
        case 'b':
          out += '\b';
          break;
        case 'e':
          out += '\x1b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'v':
          out += '\v';
          break;
        case 'x':
        case 'u':
        case 'U': {
          const std::size_t digits = e == 'x' ? 2 : e == 'u' ? 4 : 8;
          if (i + digits + 1 >= s.size()) {
            throw error(where(l) + "truncated \\" + std::string(1, e) + " escape");
          }
          unsigned long code = 0;
          for (std::size_t d = 0; d < digits; ++d) {
            const char h = s[++i];
            if (!std::isxdigit(static_cast<unsigned char>(h))) {
              throw error(where(l) + "bad hexadecimal digit in an escape");
            }
            code = code * 16 + static_cast<unsigned long>(std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (std::tolower(h) - 'a' + 10));
          }
          append_utf8(out, code);
          break;
        }
        default:
          out += '\\';
          out += e;
        }
      } else if (q == '\'' && c == '\'' && i + 2 < s.size() && s[i + 1] == '\'') {
        out += '\'';
        ++i;
      } else {
        out += c;
      }
    }
    return value::string(std::move(out));
  }
  if (s == "true" || s == "True" || s == "TRUE") {
    return value::boolean(true);
  }
  if (s == "false" || s == "False" || s == "FALSE") {
    return value::boolean(false);
  }
  // Numbers: an optional sign, digits, optional fraction/exponent; anything else is a plain string.
  {
    std::size_t i = 0;
    if (s[i] == '-' || s[i] == '+') {
      ++i;
    }
    bool digits = false;
    bool is_float = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      ++i;
      digits = true;
    }
    if (i < s.size() && s[i] == '.') {
      is_float = true;
      ++i;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        ++i;
        digits = true;
      }
    }
    if (digits && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
      std::size_t j = i + 1;
      if (j < s.size() && (s[j] == '-' || s[j] == '+')) {
        ++j;
      }
      if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
        is_float = true;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
          ++j;
        }
        i = j;
      }
    }
    if (digits && i == s.size()) {
      const std::string str(s);
      if (!is_float) {
        long long integer = 0;
        const char *first = str.data() + (str.front() == '+' ? 1 : 0);
        const auto [end, failure] = std::from_chars(first, str.data() + str.size(), integer);
        if (failure == std::errc{} && end == str.data() + str.size()) {
          return value::integer(integer);
        }
      }
      // A float, or an integer too large for one: either way the nearest double.
      return value::floating(std::strtod(str.c_str(), nullptr));
    }
    if (s == ".inf" || s == "+.inf" || s == ".Inf") {
      return value::floating(std::numeric_limits<double>::infinity());
    }
    if (s == "-.inf" || s == "-.Inf") {
      return value::floating(-std::numeric_limits<double>::infinity());
    }
    if (s == ".nan" || s == ".NaN" || s == ".NAN") {
      return value::floating(std::numeric_limits<double>::quiet_NaN());
    }
  }
  return value::string(std::string(s));
}

/// \brief A scalar whose type a core-schema tag pins: `!!float 1`, `!!str true`, `! 007`. Any other tag is refused.
inline value parse_tagged(std::string_view s, const line &l) {
  const std::size_t space = s.find(' ');
  const std::string_view tag = s.substr(0, space);
  const std::string rest = space == std::string_view::npos ? std::string() : trim(s.substr(space + 1));
  const bool quoted = !rest.empty() && (rest.front() == '"' || rest.front() == '\'');
  // The text the tag applies to: a quoted scalar's content, or the plain text as written.
  const std::string text = quoted ? parse_scalar(rest, l).as_string() : rest;
  if (tag == "!" || tag == "!!str") {
    return value::string(text);
  }
  if (tag == "!!null") {
    return value::null();
  }
  const value plain = parse_scalar(text, l);
  if (tag == "!!float" && plain.is_number()) {
    return value::floating(plain.as_double());
  }
  if (tag == "!!int" && plain.is_int()) {
    return plain;
  }
  if (tag == "!!bool" && plain.is_bool()) {
    return plain;
  }
  if (tag == "!!float" || tag == "!!int" || tag == "!!bool") {
    throw error(where(l) + "'" + text + "' is not a " + std::string(tag.substr(2)));
  }
  throw error(where(l) + "tag '" + std::string(tag) + "' is not supported");
}

/// \brief A flow collection on one line: `{key: value, ...}` / `[item, ...]`, nested freely, scalars by the block rules.
class flow_parser {
public:
  flow_parser(std::string_view text, const line &l) : text_(text), line_(l) {}

  value parse_document() {
    value v = parse_value(0);
    skip_spaces();
    if (pos_ != text_.size()) {
      throw error(where(line_) + "unexpected text after a flow collection");
    }
    return v;
  }

private:
  void skip_spaces() {
    while (pos_ < text_.size() && text_[pos_] == ' ') {
      ++pos_;
    }
  }

  // The raw text of one scalar: a quoted string with its quotes (escapes skipped over), or plain text up to the
  // indicator that ends it - `,` `}` `]` always, and `: ` / a trailing `:` when it is a mapping key.
  std::string_view scan_scalar(bool is_key) {
    const std::size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '!') { // a tag travels with its scalar
      while (pos_ < text_.size() && text_[pos_] != ' ' && text_[pos_] != ',' && text_[pos_] != '}' && text_[pos_] != ']') {
        ++pos_;
      }
      skip_spaces();
    }
    if (pos_ < text_.size() && (text_[pos_] == '"' || text_[pos_] == '\'')) {
      const char quote = text_[pos_++];
      for (;; ++pos_) {
        if (pos_ >= text_.size()) {
          throw error(where(line_) + "unterminated quoted string");
        }
        if (quote == '"' && text_[pos_] == '\\') {
          ++pos_;
        } else if (text_[pos_] == quote) {
          if (quote == '\'' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '\'') {
            ++pos_;
            continue;
          }
          ++pos_;
          break;
        }
      }
      return text_.substr(start, pos_ - start);
    }
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ',' || c == '}' || c == ']') {
        break;
      }
      if (is_key && c == ':' && (pos_ + 1 == text_.size() || text_[pos_ + 1] == ' ')) {
        break;
      }
      if (c == '{' || c == '[') {
        throw error(where(line_) + "unexpected '" + std::string(1, c) + "' inside a plain scalar");
      }
      ++pos_;
    }
    return text_.substr(start, pos_ - start);
  }

  value parse_value(std::size_t depth) {
    if (depth > max_depth) {
      throw error(where(line_) + "the document exceeds the maximum nesting depth");
    }
    skip_spaces();
    if (pos_ >= text_.size()) {
      throw error(where(line_) + "unterminated flow collection (flow collections may not span lines)");
    }
    if (text_[pos_] == '{') {
      return parse_mapping(depth);
    }
    if (text_[pos_] == '[') {
      return parse_sequence(depth);
    }
    return parse_scalar(trim(scan_scalar(false)), line_);
  }

  value parse_mapping(std::size_t depth) {
    ++pos_; // {
    value::mapping_type entries;
    for (;;) {
      skip_spaces();
      if (pos_ >= text_.size()) {
        throw error(where(line_) + "unterminated flow mapping (flow collections may not span lines)");
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return value::mapping(std::move(entries));
      }
      const value key_value = parse_scalar(trim(scan_scalar(true)), line_);
      if (!key_value.is_scalar() || key_value.is_null()) {
        throw error(where(line_) + "empty mapping key");
      }
      std::string key = key_value.to_string();
      for (const auto &e : entries) {
        if (e.first == key) {
          throw error(where(line_) + "duplicate key '" + key + "'");
        }
      }
      skip_spaces();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        throw error(where(line_) + "expected ':' after the key '" + key + "'");
      }
      ++pos_;
      skip_spaces();
      if (pos_ < text_.size() && (text_[pos_] == ',' || text_[pos_] == '}')) {
        entries.emplace_back(std::move(key), value::null());
      } else {
        entries.emplace_back(std::move(key), parse_value(depth + 1));
      }
      skip_spaces();
      if (pos_ < text_.size() && text_[pos_] == ',') {
        ++pos_;
      } else if (pos_ >= text_.size() || text_[pos_] != '}') {
        throw error(where(line_) + "expected ',' or '}' in a flow mapping");
      }
    }
  }

  value parse_sequence(std::size_t depth) {
    ++pos_; // [
    value::sequence_type items;
    for (;;) {
      skip_spaces();
      if (pos_ >= text_.size()) {
        throw error(where(line_) + "unterminated flow sequence (flow collections may not span lines)");
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return value::sequence(std::move(items));
      }
      items.push_back(parse_value(depth + 1));
      skip_spaces();
      if (pos_ < text_.size() && text_[pos_] == ',') {
        ++pos_;
      } else if (pos_ >= text_.size() || text_[pos_] != ']') {
        throw error(where(line_) + "expected ',' or ']' in a flow sequence");
      }
    }
  }

  std::string_view text_;
  const line &line_;
  std::size_t pos_ = 0;
};

inline value parse_flow(std::string_view text, const line &l) { return flow_parser(text, l).parse_document(); }

/// \brief Finds the `:` that separates a mapping key from its value (outside quotes, followed by space/EOL).
inline std::size_t find_key_colon(std::string_view s) {
  char quote = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (quote != 0) {
      if (c == quote) {
        quote = 0;
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      if (i == 0) {
        quote = c;
      }
      continue;
    }
    if (c == ':' && (i + 1 == s.size() || s[i + 1] == ' ')) {
      return i;
    }
  }
  return std::string_view::npos;
}

class parser {
public:
  explicit parser(std::vector<line> lines) : lines_(std::move(lines)) {}

  value parse_document() {
    if (lines_.empty()) {
      return value::null();
    }
    value v = is_inline(lines_[0].text) ? parse_inline() : parse_block(lines_[0].indent, 0);
    if (pos_ < lines_.size()) {
      throw error(where(lines_[pos_]) + "unexpected content (bad indentation?)");
    }
    return v;
  }

private:
  // Text that is a whole value on its own line rather than the start of a block: a flow collection, a quoted or
  // tagged scalar, or a plain scalar (no `key: `, no `- `).
  static bool is_inline(const std::string &text) {
    if (text == "-" || text.starts_with("- ")) {
      return false;
    }
    const char c = text.front();
    if (c == '{' || c == '[' || c == '!') {
      return true;
    }
    return find_key_colon(text) == std::string_view::npos;
  }

  value parse_inline() {
    const line &l = lines_[pos_++];
    return parse_scalar(l.text, l);
  }

  value parse_block(std::size_t indent, std::size_t depth) {
    if (depth > max_depth) {
      throw error(where(lines_[pos_]) + "the document exceeds the maximum nesting depth");
    }
    const line &first = lines_[pos_];
    if (first.indent != indent) {
      throw error(where(first) + "unexpected indentation");
    }
    if (first.text == "-" || first.text.starts_with("- ")) {
      return parse_sequence(indent, depth);
    }
    return parse_mapping(indent, depth);
  }

  value parse_sequence(std::size_t indent, std::size_t depth) {
    value::sequence_type items;
    while (pos_ < lines_.size() && lines_[pos_].indent == indent) {
      line &l = lines_[pos_];
      if (!(l.text == "-" || l.text.starts_with("- "))) {
        throw error(where(l) + "expected a '- ' sequence item");
      }
      const std::string rest = trim(std::string_view(l.text).substr(1));
      if (rest.empty()) {
        ++pos_;
        if (pos_ < lines_.size() && lines_[pos_].indent > indent) {
          items.push_back(parse_block(lines_[pos_].indent, depth + 1));
        } else {
          items.push_back(value::null());
        }
        continue;
      }
      const std::size_t colon = find_key_colon(rest);
      if (colon != std::string_view::npos && rest.front() != '"' && rest.front() != '\'' && rest.front() != '{' &&
          rest.front() != '[' && rest.front() != '!') {
        // Compact `- key: value` form: the item is a mapping whose first entry sits on this line, at the column
        // where `key` starts; rewrite the line in place and parse that mapping.
        const std::size_t column = indent + 2;
        l.indent = column;
        l.text = rest;
        items.push_back(parse_mapping(column, depth + 1));
        continue;
      }
      items.push_back(parse_scalar(rest, l));
      ++pos_;
    }
    return value::sequence(std::move(items));
  }

  value parse_mapping(std::size_t indent, std::size_t depth) {
    value::mapping_type entries;
    while (pos_ < lines_.size() && lines_[pos_].indent == indent) {
      const line &l = lines_[pos_];
      if (l.text == "-" || l.text.starts_with("- ")) {
        break;
      }
      const std::size_t colon = find_key_colon(l.text);
      if (colon == std::string_view::npos) {
        throw error(where(l) + "expected 'key: value' (multi-line plain scalars are not supported)");
      }
      std::string key = trim(std::string_view(l.text).substr(0, colon));
      if (!key.empty() && (key.front() == '"' || key.front() == '\'')) {
        key = parse_scalar(key, l).as_string();
      }
      if (key.empty()) {
        throw error(where(l) + "empty mapping key");
      }
      for (const auto &e : entries) {
        if (e.first == key) {
          throw error(where(l) + "duplicate key '" + key + "'");
        }
      }
      const std::string rest = trim(std::string_view(l.text).substr(colon + 1));
      ++pos_;
      if (rest.empty()) {
        if (pos_ < lines_.size() && lines_[pos_].indent > indent) {
          entries.emplace_back(std::move(key), parse_block(lines_[pos_].indent, depth + 1));
        } else {
          entries.emplace_back(std::move(key), value::null());
        }
      } else {
        entries.emplace_back(std::move(key), parse_scalar(rest, l));
        if (pos_ < lines_.size() && lines_[pos_].indent > indent) {
          throw error(where(lines_[pos_]) + "unexpected indentation after a scalar value");
        }
      }
    }
    return value::mapping(std::move(entries));
  }

  std::vector<line> lines_;
  std::size_t pos_ = 0;
};

inline void dump_value(const value &v, std::string &out, std::size_t indent, bool in_sequence_item);

/// \brief A double as text that reads back as the same double *and as a float*: the shortest round-trip form,
/// given a fraction when it has neither fraction nor exponent (`1` would read back as an integer).
inline std::string float_text(double d) {
  if (std::isnan(d)) {
    return ".nan";
  }
  if (std::isinf(d)) {
    return d < 0 ? "-.inf" : ".inf";
  }
  char buffer[32];
  const auto [end, failure] = std::to_chars(buffer, buffer + sizeof buffer, d);
  std::string text(buffer, failure == std::errc{} ? end : buffer);
  if (text.find_first_of(".en") == std::string::npos) {
    text += ".0";
  }
  return text;
}

/// \brief `in_flow`: inside `{}` / `[]`, where `,` and the brackets end a plain scalar and so force quotes.
inline std::string quote_if_needed(const std::string &s, bool in_flow = false) {
  if (s.empty()) {
    return "\"\"";
  }
  bool control = false;
  for (const char c : s) {
    control = control || (static_cast<unsigned char>(c) < 0x20) || c == 0x7f;
  }
  bool needs = control || (in_flow && s.find_first_of(",[]{}") != std::string::npos) || s.front() == ' ' || s.back() == ' ' || s.front() == '{' || s.front() == '[' || s.front() == '&' ||
               s.front() == '*' || s.front() == '!' || s.front() == '|' || s.front() == '>' || s.front() == '\'' ||
               s.front() == '"' || s.front() == '#' || s.front() == '-' || s.find(": ") != std::string::npos ||
               s.find(" #") != std::string::npos || s.back() == ':' || s == "null" || s == "~" || s == "true" ||
               s == "false" || s.front() == '%' || s.front() == '@' || s.front() == '`' || s.front() == '?';
  if (!needs) {
    const value parsed = parse_scalar(s, line{0, 0, ""});
    needs = !parsed.is_string();
  }
  if (!needs) {
    return s;
  }
  std::string out = "\"";
  for (const char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\r':
      out += "\\r";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
        char escaped[8];
        std::snprintf(escaped, sizeof escaped, "\\x%02x", static_cast<unsigned>(static_cast<unsigned char>(c)));
        out += escaped;
      } else {
        out += c;
      }
    }
  }
  return out + "\"";
}

inline std::string scalar_text(const value &v, bool in_flow = false) {
  switch (v.type()) {
  case value::kind::string:
    return quote_if_needed(v.as_string(), in_flow);
  case value::kind::floating:
    return float_text(v.as_double());
  case value::kind::mapping:
    return "{}";
  case value::kind::sequence:
    return "[]";
  default:
    return v.to_string();
  }
}

inline void dump_value(const value &v, std::string &out, std::size_t indent, bool in_sequence_item) {
  const std::string pad(indent, ' ');
  if (v.is_mapping() && v.size() > 0) {
    bool first = true;
    for (const auto &[k, e] : v.as_mapping()) {
      if (!(first && in_sequence_item)) {
        out += pad;
      }
      first = false;
      out += quote_if_needed(k) + ":";
      if ((e.is_mapping() || e.is_sequence()) && e.size() > 0) {
        out += "\n";
        dump_value(e, out, indent + 2, false);
      } else {
        out += " " + scalar_text(e) + "\n";
      }
    }
  } else if (v.is_sequence() && v.size() > 0) {
    for (const auto &e : v.as_sequence()) {
      out += pad + "- ";
      if (e.is_mapping() && e.size() > 0) {
        dump_value(e, out, indent + 2, true);
      } else if (e.is_sequence() && e.size() > 0) {
        out += "\n";
        dump_value(e, out, indent + 2, false);
      } else {
        out += scalar_text(e) + "\n";
      }
    }
  } else {
    out += pad + scalar_text(v) + "\n";
  }
}

} // namespace detail

/// \brief Parses one document in the scoped subset; throws yaml::error with a line number otherwise.
inline value parse(std::string_view text) {
  detail::parser p(detail::split_lines(text));
  return p.parse_document();
}

namespace detail {
inline void flow_value(const value &v, std::string &out) {
  if (v.is_mapping()) {
    out += "{";
    bool first = true;
    for (const auto &[k, e] : v.as_mapping()) {
      out += first ? "" : ", ";
      first = false;
      out += quote_if_needed(k, true) + ": ";
      flow_value(e, out);
    }
    out += "}";
  } else if (v.is_sequence()) {
    out += "[";
    bool first = true;
    for (const auto &e : v.as_sequence()) {
      out += first ? "" : ", ";
      first = false;
      flow_value(e, out);
    }
    out += "]";
  } else {
    out += scalar_text(v, true);
  }
}
} // namespace detail

/// \brief The whole value on one line, flow style (`{a: 1, b: [x, y]}`) - the form a command-line token carries.
inline std::string flow(const value &v) {
  std::string out;
  detail::flow_value(v, out);
  return out;
}

/// \brief Serializes back into the same subset (block style, two-space indentation).
inline std::string dump(const value &v) {
  std::string out;
  detail::dump_value(v, out, 0, false);
  return out;
}

} // namespace cx::core::serialization::yaml
