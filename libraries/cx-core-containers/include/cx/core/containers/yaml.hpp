// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <cx/core/containers/property.hpp>
#include <cx/core/containers/variant.hpp>
#include <cx/core/serialization/yaml.hpp>

// A variant, a property and a property map as YAML, through serialization::yaml
// - the one form documents on disk (agenticx-ncortex's `ncortex.yaml`) and
// command-line tokens (`--property=<flow mapping>`, arguments.hpp) share.
// Rendering and decoding are each other's exact inverse for every alternative:
// a string that would read back as a bool, number or null is quoted, a double
// keeps a fraction, and an integer comes back as `int` when it fits one (what a
// bare literal selects) and `int64_t` otherwise.
namespace cx::core::containers::yaml {

namespace text = ::cx::core::serialization::yaml;
using error = text::error;

inline text::value to_yaml(const variant &value) {
  return std::visit(
      [](const auto &v) -> text::value {
        using type = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<type, std::map<std::string, variant>>) {
          text::value::mapping_type entries;
          for (const auto &[key, item] : v)
            entries.emplace_back(key, to_yaml(item));
          return text::value::mapping(std::move(entries));
        } else if constexpr (std::is_same_v<type, std::deque<variant>>) {
          text::value::sequence_type items;
          for (const auto &item : v)
            items.push_back(to_yaml(item));
          return text::value::sequence(std::move(items));
        } else if constexpr (std::is_same_v<type, std::nullptr_t>) {
          return text::value::null();
        } else if constexpr (std::is_same_v<type, bool>) {
          return text::value::boolean(v);
        } else if constexpr (std::is_same_v<type, std::uint64_t>) {
          // Past long long there is no integer to write; the nearest double is what a reader would make of it too.
          if (v > static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
            return text::value::floating(static_cast<double>(v));
          return text::value::integer(static_cast<long long>(v));
        } else if constexpr (std::is_integral_v<type>) {
          return text::value::integer(static_cast<long long>(v));
        } else if constexpr (std::is_floating_point_v<type>) {
          return text::value::floating(v);
        } else {
          return text::value::string(v);
        }
      },
      static_cast<const variant::base_type &>(value));
}

// A property record: metadata first, value last, so a saved document reads
// top-down the way it is documented.
inline text::value to_yaml(const property &value) {
  return text::value::mapping({{"name", text::value::string(value.name)},
                               {"option", text::value::string(value.option)},
                               {"description", text::value::string(value.description)},
                               {"value", to_yaml(static_cast<const variant &>(value))}});
}

inline text::value to_yaml(const property_map &values) {
  text::value::mapping_type entries;
  for (const auto &[key, value] : values)
    entries.emplace_back(key, to_yaml(value));
  return text::value::mapping(std::move(entries));
}

inline variant from_yaml(const text::value &node) {
  switch (node.type()) {
  case text::value::kind::null:
    return nullptr;
  case text::value::kind::boolean:
    return node.as_bool();
  case text::value::kind::integer: {
    const long long integer = node.as_int();
    if (integer >= std::numeric_limits<int>::min() && integer <= std::numeric_limits<int>::max())
      return static_cast<int>(integer);
    return static_cast<std::int64_t>(integer);
  }
  case text::value::kind::floating:
    return node.as_double();
  case text::value::kind::string:
    return node.as_string();
  case text::value::kind::sequence: {
    std::deque<variant> values;
    for (const auto &item : node.as_sequence())
      values.push_back(from_yaml(item));
    return values;
  }
  case text::value::kind::mapping: {
    std::map<std::string, variant> values;
    for (const auto &[key, item] : node.as_mapping())
      values.emplace(key, from_yaml(item));
    return values;
  }
  }
  return nullptr;
}

inline property property_from_yaml(const text::value &node) {
  if (!node.is_mapping())
    throw error("expected a property record: a mapping with a value key");
  const auto scalar = [](const text::value &field, const char *what) {
    if (field.is_null())
      return std::string();
    if (!field.is_scalar())
      throw error(std::string("expected a scalar for ") + what);
    return field.to_string();
  };
  property result;
  bool has_value = false;
  for (const auto &[key, field] : node.as_mapping()) {
    if (key == "value") {
      static_cast<variant &>(result) = from_yaml(field);
      has_value = true;
    } else if (key == "name") {
      result.name = scalar(field, "name");
    } else if (key == "option") {
      result.option = scalar(field, "option");
    } else if (key == "description") {
      result.description = scalar(field, "description");
    } else {
      throw error("unknown property record key: " + key);
    }
  }
  if (!has_value)
    throw error("property record has no value");
  return result;
}

inline property_map properties_from_yaml(const text::value &node) {
  if (!node.is_mapping())
    throw error("expected a property map");
  property_map values;
  for (const auto &[key, record] : node.as_mapping())
    values.emplace(key, property_from_yaml(record));
  return values;
}

// Block-style document text, newline-terminated.
template <class type> std::string render(const type &value) { return text::dump(to_yaml(value)); }

// One line, flow style - the form a command-line token carries.
template <class type> std::string flow(const type &value) { return text::flow(to_yaml(value)); }

inline variant decode(std::string_view document) { return from_yaml(text::parse(document)); }
inline property_map decode_properties(std::string_view document) { return properties_from_yaml(text::parse(document)); }

} // namespace cx::core::containers::yaml
