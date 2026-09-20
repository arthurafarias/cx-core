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
#include <istream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <cx/core/containers/yaml.hpp>

// A property map on a command line: complete argv tokens, no shell in between.
// A map renders as repeatable `--property=<YAML flow mapping>` tokens, each
// carrying one complete record (key, metadata and value); reading accepts
// those tokens back plus human-facing `--<key>=<value>` / `--<option>=<value>`
// overrides, typed by the property they address. quote_argument() adds POSIX
// single quoting so a rendered line can be pasted into a shell; argv tokens
// themselves are never quoted.
namespace cx::core::containers::arguments {

inline std::string quote_argument(const std::string &argument) {
  std::string out = "'";
  for (char c : argument)
    out += c == '\'' ? "'\\''" : std::string(1, c);
  return out + "'";
}

// Splits a stream into argv tokens the way a POSIX shell would (single
// quotes, double quotes, backslash escapes, whitespace separation).
inline std::vector<std::string> read(std::istream &stream) {
  std::vector<std::string> tokens;
  std::string token;
  char quote = 0;
  bool started = false;
  for (int ch; (ch = stream.get()) != std::char_traits<char>::eof();) {
    const char c = static_cast<char>(ch);
    if (c == '\\' && quote != '\'') {
      const int next = stream.get();
      if (next == std::char_traits<char>::eof())
        throw std::runtime_error("incomplete CLI escape");
      token += static_cast<char>(next);
      started = true;
    } else if (quote) {
      if (c == quote)
        quote = 0;
      else
        token += c;
    } else if (c == '\'' || c == '"') {
      quote = c;
      started = true;
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      if (started)
        tokens.push_back(std::move(token));
      token.clear();
      started = false;
    } else {
      token += c;
      started = true;
    }
  }
  if (quote)
    throw std::runtime_error("unterminated CLI quote");
  if (started)
    tokens.push_back(std::move(token));
  return tokens;
}

inline std::vector<std::string> from(const property_map &values) {
  std::vector<std::string> tokens;
  tokens.reserve(values.size());
  for (const auto &[key, value] : values)
    tokens.push_back("--property=" + yaml::flow(property_map{{key, value}}));
  return tokens;
}

namespace detail {
template <typename number> bool parse_number(const std::string &text, number &out) {
  const auto [end, failure] = std::from_chars(text.data(), text.data() + text.size(), out);
  return failure == std::errc{} && end == text.data() + text.size();
}

// Parses `text` as the alternative `target` currently holds, so an
// override never changes a property's type: a string property takes the
// raw text (no YAML interpretation), numbers and booleans must parse,
// and a map/sequence must be a YAML flow collection of the same shape.
// A null property accepts any YAML value.
inline variant parse_typed(const variant &target, const std::string &key, const std::string &text) {
  if (target.is<std::string>())
    return text;
  if (target.is<bool>()) {
    if (text == "true")
      return true;
    if (text == "false")
      return false;
    throw std::runtime_error("expected true or false for --" + key);
  }
  if (target.is<int>() || target.is<std::int64_t>() || target.is<std::uint64_t>()) {
    const variant parsed = yaml::decode(text);
    if (target.is<int>() && parsed.is<int>())
      return parsed;
    if (target.is<std::int64_t>() && (parsed.is<int>() || parsed.is<std::int64_t>()))
      return parsed.is<int>() ? static_cast<std::int64_t>(parsed.as<int>()) : parsed.as<std::int64_t>();
    std::uint64_t natural{};
    if (target.is<std::uint64_t>() && parse_number(text, natural))
      return natural;
    throw std::runtime_error("expected an integer for --" + key);
  }
  if (target.is<double>()) {
    const variant parsed = yaml::decode(text);
    if (parsed.is<double>())
      return parsed;
    if (parsed.is<int>())
      return static_cast<double>(parsed.as<int>());
    if (parsed.is<std::int64_t>())
      return static_cast<double>(parsed.as<std::int64_t>());
    throw std::runtime_error("expected a number for --" + key);
  }
  variant value = yaml::decode(text);
  if (!target.is<std::nullptr_t>() && value.index() != target.index())
    throw std::runtime_error("wrong type for CLI property: " + key);
  return value;
}

inline property_map::iterator find_property(property_map &values, const std::string &key) {
  if (auto exact = values.find(key); exact != values.end())
    return exact;
  auto found = values.end();
  for (auto it = values.begin(); it != values.end(); ++it) {
    if (it->second.option.empty() || it->second.option != key)
      continue;
    if (found != values.end())
      throw std::runtime_error("ambiguous CLI option: --" + key);
    found = it;
  }
  return found;
}
} // namespace detail

// Applies a batch atomically: any malformed token leaves `values` as it was.
inline void apply(property_map &values, const std::vector<std::string> &tokens) {
  auto updated = values;
  for (const auto &token : tokens) {
    if (!token.starts_with("--"))
      throw std::runtime_error("expected --name=value, got: " + token);
    const auto separator = token.find('=');
    if (separator == std::string::npos || separator == 2)
      throw std::runtime_error("expected --name=value, got: " + token);
    const auto key = token.substr(2, separator - 2);
    const auto text = token.substr(separator + 1);
    if (key == "property") {
      for (auto &[name, value] : yaml::decode_properties(text))
        updated.insert_or_assign(name, std::move(value));
      continue;
    }
    auto entry = detail::find_property(updated, key);
    if (entry == updated.end()) {
      property fresh{yaml::decode(text)};
      fresh.name = key;
      updated.emplace(key, std::move(fresh));
    } else {
      static_cast<variant &>(entry->second) = detail::parse_typed(entry->second, key, text);
    }
  }
  values = std::move(updated);
}

} // namespace cx::core::containers::arguments
