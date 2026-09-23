#pragma once

/// @file
/// @brief cx::core::serialization::ordered_json - a small RFC 8259 JSON value (parse + serialize) for lossless configuration and wire protocols.
///
/// Objects keep insertion order so configuration files round-trip unchanged.
/// Integers that fit in int64 are kept exact; everything else is a double.

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cx::core::serialization {

class ordered_json {
public:
  using array = std::vector<ordered_json>;
  using member = std::pair<std::string, ordered_json>;
  using object = std::vector<member>;

  enum class kind { null, boolean, integer, number, string, array, object };

  struct parse_error : std::runtime_error {
    std::size_t offset;
    parse_error(const std::string &what, std::size_t at) : std::runtime_error(what), offset(at) {}
  };

  ordered_json() = default;
  ordered_json(std::nullptr_t) {}
  ordered_json(bool b) : v_(b) {}
  /// @brief Any integral type (except bool) becomes an exact integer.
  template <typename I, std::enable_if_t<std::is_integral_v<I> && !std::is_same_v<I, bool>, int> = 0>
  ordered_json(I i) : v_(static_cast<std::int64_t>(i)) {}
  template <typename F, std::enable_if_t<std::is_floating_point_v<F>, int> = 0> ordered_json(F d) : v_(static_cast<double>(d)) {}
  ordered_json(const char *s) : v_(std::string(s)) {}
  ordered_json(std::string s) : v_(std::move(s)) {}
  ordered_json(std::string_view s) : v_(std::string(s)) {}
  ordered_json(array a) : v_(std::move(a)) {}
  ordered_json(object o) : v_(std::move(o)) {}

  static ordered_json make_object() { return ordered_json(object{}); }
  static ordered_json make_array() { return ordered_json(array{}); }
  /// @brief `{ "k": v, ... }` literal helper.
  static ordered_json obj(std::initializer_list<member> members) { return ordered_json(object(members)); }
  static ordered_json arr(std::initializer_list<ordered_json> items) { return ordered_json(array(items)); }

  kind type() const { return static_cast<kind>(v_.index()); }
  bool is_null() const { return type() == kind::null; }
  bool is_bool() const { return type() == kind::boolean; }
  bool is_number() const { return type() == kind::integer || type() == kind::number; }
  bool is_integer() const { return type() == kind::integer; }
  bool is_string() const { return type() == kind::string; }
  bool is_array() const { return type() == kind::array; }
  bool is_object() const { return type() == kind::object; }

  bool as_bool(bool fallback = false) const { return is_bool() ? std::get<bool>(v_) : fallback; }
  std::int64_t as_int(std::int64_t fallback = 0) const {
    if (is_integer()) {
      return std::get<std::int64_t>(v_);
    }
    if (type() == kind::number) {
      return static_cast<std::int64_t>(std::get<double>(v_));
    }
    return fallback;
  }
  double as_double(double fallback = 0.0) const {
    if (is_integer()) {
      return static_cast<double>(std::get<std::int64_t>(v_));
    }
    if (type() == kind::number) {
      return std::get<double>(v_);
    }
    return fallback;
  }
  const std::string &as_string() const {
    static const std::string empty;
    return is_string() ? std::get<std::string>(v_) : empty;
  }
  std::string as_string(std::string fallback) const { return is_string() ? std::get<std::string>(v_) : fallback; }
  const array &as_array() const {
    static const array empty;
    return is_array() ? std::get<array>(v_) : empty;
  }
  array &as_array() {
    if (!is_array()) {
      v_ = array{};
    }
    return std::get<array>(v_);
  }
  const object &as_object() const {
    static const object empty;
    return is_object() ? std::get<object>(v_) : empty;
  }
  object &as_object() {
    if (!is_object()) {
      v_ = object{};
    }
    return std::get<object>(v_);
  }

  /// @brief Member lookup; nullptr when absent or not an object.
  const ordered_json *find(std::string_view key) const {
    if (!is_object()) {
      return nullptr;
    }
    for (const auto &[k, v] : std::get<object>(v_)) {
      if (k == key) {
        return &v;
      }
    }
    return nullptr;
  }
  ordered_json *find(std::string_view key) { return const_cast<ordered_json *>(std::as_const(*this).find(key)); }
  bool contains(std::string_view key) const { return find(key) != nullptr; }

  /// @brief Read-only member access; a shared null when absent.
  const ordered_json &operator[](std::string_view key) const {
    static const ordered_json null_value;
    const ordered_json *j = find(key);
    return j ? *j : null_value;
  }
  /// @brief Member access that inserts null when absent (turns null into {}).
  ordered_json &operator[](std::string_view key) {
    if (ordered_json *j = find(key)) {
      return *j;
    }
    auto &o = as_object();
    o.emplace_back(std::string(key), ordered_json());
    return o.back().second;
  }
  /// @brief Array element access; integral indices only (a literal 0 must not
  /// select the string_view key overload).
  template <typename I, std::enable_if_t<std::is_integral_v<I>, int> = 0> const ordered_json &operator[](I i) const {
    static const ordered_json null_value;
    const auto &a = as_array();
    auto idx = static_cast<std::size_t>(i);
    return idx < a.size() ? a[idx] : null_value;
  }
  template <typename I, std::enable_if_t<std::is_integral_v<I>, int> = 0> ordered_json &operator[](I i) {
    auto &a = as_array();
    auto idx = static_cast<std::size_t>(i);
    if (idx >= a.size()) {
      a.resize(idx + 1);
    }
    return a[idx];
  }

  void push_back(ordered_json v) { as_array().push_back(std::move(v)); }
  bool erase(std::string_view key) {
    if (!is_object()) {
      return false;
    }
    auto &o = std::get<object>(v_);
    for (auto it = o.begin(); it != o.end(); ++it) {
      if (it->first == key) {
        o.erase(it);
        return true;
      }
    }
    return false;
  }
  std::size_t size() const {
    if (is_array()) {
      return std::get<array>(v_).size();
    }
    if (is_object()) {
      return std::get<object>(v_).size();
    }
    return 0;
  }

  /// @brief Typed getters with defaults, for reading RPC params.
  std::string get_string(std::string_view key, std::string fallback = {}) const {
    const ordered_json *j = find(key);
    return j && j->is_string() ? j->as_string() : fallback;
  }
  std::int64_t get_int(std::string_view key, std::int64_t fallback = 0) const {
    const ordered_json *j = find(key);
    return j && j->is_number() ? j->as_int() : fallback;
  }
  double get_double(std::string_view key, double fallback = 0.0) const {
    const ordered_json *j = find(key);
    return j && j->is_number() ? j->as_double() : fallback;
  }
  bool get_bool(std::string_view key, bool fallback = false) const {
    const ordered_json *j = find(key);
    return j && j->is_bool() ? j->as_bool() : fallback;
  }

  friend bool operator==(const ordered_json &a, const ordered_json &b) {
    if (a.is_number() && b.is_number() && (a.type() != b.type())) {
      return a.as_double() == b.as_double();
    }
    return a.v_ == b.v_;
  }

  // --- serialization ---------------------------------------------------

  /// @brief Serialize. @p indent < 0 gives the compact form.
  std::string dump(int indent = -1) const {
    std::string out;
    write(out, indent, 0);
    return out;
  }

  static ordered_json parse(std::string_view text) {
    parser p{text, 0};
    p.skip_ws();
    ordered_json v = p.value(0);
    p.skip_ws();
    if (p.pos != text.size()) {
      throw parse_error("trailing characters", p.pos);
    }
    return v;
  }

  static std::optional<ordered_json> try_parse(std::string_view text) {
    try {
      return parse(text);
    } catch (const parse_error &) {
      return std::nullopt;
    }
  }

  static void escape(std::string &out, std::string_view s) {
    out.push_back('"');
    for (unsigned char c : s) {
      switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
      }
    }
    out.push_back('"');
  }

private:
  using storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, array, object>;
  storage v_;

  static void newline(std::string &out, int indent, int depth) {
    if (indent >= 0) {
      out.push_back('\n');
      out.append(static_cast<std::size_t>(indent * depth), ' ');
    }
  }

  void write(std::string &out, int indent, int depth) const {
    switch (type()) {
    case kind::null:
      out += "null";
      break;
    case kind::boolean:
      out += std::get<bool>(v_) ? "true" : "false";
      break;
    case kind::integer:
      out += std::to_string(std::get<std::int64_t>(v_));
      break;
    case kind::number: {
      double d = std::get<double>(v_);
      if (!std::isfinite(d)) {
        out += "null";
        break;
      }
      char buf[32];
      std::snprintf(buf, sizeof buf, "%.17g", d);
      // Prefer the shortest representation that round-trips.
      for (int prec = 1; prec <= 17; ++prec) {
        char tmp[32];
        std::snprintf(tmp, sizeof tmp, "%.*g", prec, d);
        if (std::strtod(tmp, nullptr) == d) {
          std::snprintf(buf, sizeof buf, "%s", tmp);
          break;
        }
      }
      out += buf;
      break;
    }
    case kind::string:
      escape(out, std::get<std::string>(v_));
      break;
    case kind::array: {
      const auto &a = std::get<array>(v_);
      out.push_back('[');
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (i) {
          out.push_back(',');
        }
        newline(out, indent, depth + 1);
        a[i].write(out, indent, depth + 1);
      }
      if (!a.empty()) {
        newline(out, indent, depth);
      }
      out.push_back(']');
      break;
    }
    case kind::object: {
      const auto &o = std::get<object>(v_);
      out.push_back('{');
      for (std::size_t i = 0; i < o.size(); ++i) {
        if (i) {
          out.push_back(',');
        }
        newline(out, indent, depth + 1);
        escape(out, o[i].first);
        out += indent >= 0 ? ": " : ":";
        o[i].second.write(out, indent, depth + 1);
      }
      if (!o.empty()) {
        newline(out, indent, depth);
      }
      out.push_back('}');
      break;
    }
    }
  }

  struct parser {
    std::string_view s;
    std::size_t pos;
    static constexpr int max_depth = 64;

    [[noreturn]] void fail(const char *what) const { throw parse_error(what, pos); }

    void skip_ws() {
      while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) {
        ++pos;
      }
    }

    bool consume(std::string_view lit) {
      if (s.substr(pos, lit.size()) == lit) {
        pos += lit.size();
        return true;
      }
      return false;
    }

    ordered_json value(int depth) {
      if (depth > max_depth) {
        fail("nesting too deep");
      }
      if (pos >= s.size()) {
        fail("unexpected end of input");
      }
      char c = s[pos];
      if (c == '{') {
        return obj(depth);
      }
      if (c == '[') {
        return arr(depth);
      }
      if (c == '"') {
        return ordered_json(str());
      }
      if (c == '-' || (c >= '0' && c <= '9')) {
        return num();
      }
      if (consume("true")) {
        return ordered_json(true);
      }
      if (consume("false")) {
        return ordered_json(false);
      }
      if (consume("null")) {
        return ordered_json();
      }
      fail("unexpected character");
    }

    ordered_json obj(int depth) {
      ++pos; // {
      ordered_json out = ordered_json::make_object();
      skip_ws();
      if (pos < s.size() && s[pos] == '}') {
        ++pos;
        return out;
      }
      for (;;) {
        skip_ws();
        if (pos >= s.size() || s[pos] != '"') {
          fail("expected object key");
        }
        std::string key = str();
        skip_ws();
        if (pos >= s.size() || s[pos] != ':') {
          fail("expected ':'");
        }
        ++pos;
        skip_ws();
        ordered_json v = value(depth + 1);
        if (ordered_json *existing = out.find(key)) {
          *existing = std::move(v); // last duplicate wins
        } else {
          out.as_object().emplace_back(std::move(key), std::move(v));
        }
        skip_ws();
        if (pos < s.size() && s[pos] == ',') {
          ++pos;
          continue;
        }
        if (pos < s.size() && s[pos] == '}') {
          ++pos;
          return out;
        }
        fail("expected ',' or '}'");
      }
    }

    ordered_json arr(int depth) {
      ++pos; // [
      ordered_json out = ordered_json::make_array();
      skip_ws();
      if (pos < s.size() && s[pos] == ']') {
        ++pos;
        return out;
      }
      for (;;) {
        skip_ws();
        out.push_back(value(depth + 1));
        skip_ws();
        if (pos < s.size() && s[pos] == ',') {
          ++pos;
          continue;
        }
        if (pos < s.size() && s[pos] == ']') {
          ++pos;
          return out;
        }
        fail("expected ',' or ']'");
      }
    }

    static void put_utf8(std::string &out, std::uint32_t cp) {
      if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
      } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    }

    std::uint32_t hex4() {
      if (pos + 4 > s.size()) {
        fail("truncated \\u escape");
      }
      std::uint32_t v = 0;
      for (int i = 0; i < 4; ++i) {
        char c = s[pos++];
        v <<= 4;
        if (c >= '0' && c <= '9') {
          v |= static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
          v |= static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
          v |= static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
          fail("bad hex digit");
        }
      }
      return v;
    }

    std::string str() {
      ++pos; // opening quote
      std::string out;
      while (pos < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[pos++]);
        if (c == '"') {
          return out;
        }
        if (c < 0x20) {
          fail("control character in string");
        }
        if (c != '\\') {
          out.push_back(static_cast<char>(c));
          continue;
        }
        if (pos >= s.size()) {
          break;
        }
        char e = s[pos++];
        switch (e) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t cp = hex4();
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (!consume("\\u")) {
              fail("unpaired surrogate");
            }
            std::uint32_t lo = hex4();
            if (lo < 0xDC00 || lo > 0xDFFF) {
              fail("bad low surrogate");
            }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            fail("unpaired surrogate");
          }
          put_utf8(out, cp);
          break;
        }
        default:
          fail("bad escape");
        }
      }
      fail("unterminated string");
    }

    ordered_json num() {
      std::size_t start = pos;
      bool is_float = false;
      if (s[pos] == '-') {
        ++pos;
      }
      if (pos >= s.size() || !(s[pos] >= '0' && s[pos] <= '9')) {
        fail("bad number");
      }
      if (s[pos] == '0') {
        ++pos;
      } else {
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
          ++pos;
        }
      }
      if (pos < s.size() && s[pos] == '.') {
        is_float = true;
        ++pos;
        if (pos >= s.size() || !(s[pos] >= '0' && s[pos] <= '9')) {
          fail("bad fraction");
        }
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
          ++pos;
        }
      }
      if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
        is_float = true;
        ++pos;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
          ++pos;
        }
        if (pos >= s.size() || !(s[pos] >= '0' && s[pos] <= '9')) {
          fail("bad exponent");
        }
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
          ++pos;
        }
      }
      std::string text(s.substr(start, pos - start));
      if (!is_float) {
        errno = 0;
        char *end = nullptr;
        long long v = std::strtoll(text.c_str(), &end, 10);
        if (errno == 0) {
          return ordered_json(static_cast<std::int64_t>(v));
        }
      }
      return ordered_json(std::strtod(text.c_str(), nullptr));
    }
  };
};

} // namespace cx::core::serialization
