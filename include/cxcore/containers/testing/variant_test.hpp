// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cxcore/testing/test_group.hpp>
#include <cxcore/containers/variant.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <variant>

namespace cx::core::testing {

struct variant_test : public test_group {
  variant_test() : test_group("variant", {
    {"a default-constructed variant holds its first alternative (bool, false)", [](test_context &ctx) {
      cx::core::variant v;
      ctx.check(std::holds_alternative<bool>(v), "the first listed alternative should be the default-held one");
      ctx.check(std::get<bool>(v) == false, "a default-constructed bool alternative should be false");
    }},
    {"holds the alternative it was constructed from", [](test_context &ctx) {
      cx::core::variant v = std::uint64_t{42};
      ctx.check(std::holds_alternative<std::uint64_t>(v), "should hold uint64_t");
      ctx.check(!std::holds_alternative<std::double_t>(v), "should not hold double_t");
      ctx.check(std::get<std::uint64_t>(v) == 42, "std::get<uint64_t>() should return the stored value");
    }},
    {"int64_t and uint64_t are distinct alternatives (OPEN-6)", [](test_context &ctx) {
      cx::core::variant negative = std::int64_t{-1};
      ctx.check(std::holds_alternative<std::int64_t>(negative), "an int64_t argument should select the int64_t alternative");
      ctx.check(std::get<std::int64_t>(negative) == -1, "a negative value should round-trip exactly, not wrap");

      cx::core::variant unsigned_value = std::uint64_t{42};
      ctx.check(std::holds_alternative<std::uint64_t>(unsigned_value), "a uint64_t argument should select the uint64_t alternative");
      ctx.check(!std::holds_alternative<std::int64_t>(unsigned_value), "a uint64_t-held variant should not also report int64_t");
    }},
    {"a string literal is stored as std::string, not bool", [](test_context &ctx) {
      cx::core::variant v = "hello";
      ctx.check(std::holds_alternative<std::string>(v), "string literal should select the std::string alternative, not bool (P0608)");
      ctx.check(std::get<std::string>(v) == "hello", "std::get<std::string>() should return the stored value");
    }},
    {"get_if returns nullptr for the wrong alternative", [](test_context &ctx) {
      cx::core::variant v = true;
      ctx.check(std::get_if<bool>(&v) != nullptr, "get_if<bool>() should succeed");
      ctx.check(std::get_if<std::uint64_t>(&v) == nullptr, "get_if<uint64_t>() should fail on a bool-holding variant");
    }},
    {"visit dispatches to the held alternative", [](test_context &ctx) {
      cx::core::variant v = std::uint64_t{9};
      auto description = std::visit([](auto &&held) -> std::string {
        using held_type = std::decay_t<decltype(held)>;
        if constexpr (std::is_same_v<held_type, std::uint64_t>) {
          return "uint64";
        } else {
          return "other";
        }
      }, v);
      ctx.check(description == "uint64", "std::visit should dispatch based on the alternative actually held");
    }},
    {"equal values compare equal", [](test_context &ctx) {
      cx::core::variant a = std::uint64_t{7};
      cx::core::variant b = std::uint64_t{7};
      ctx.check(a == b, "same alternative and value should compare equal");
    }},
    {"different values compare unequal", [](test_context &ctx) {
      cx::core::variant a = std::uint64_t{7};
      cx::core::variant b = std::uint64_t{8};
      ctx.check(a != b, "same alternative, different value should compare unequal");
    }},
    {"different alternatives compare unequal", [](test_context &ctx) {
      cx::core::variant a = std::uint64_t{0};
      cx::core::variant b = false;
      ctx.check(a != b, "different alternatives should compare unequal even if superficially similar");
    }},
    {"a variant can hold a deque of variants (array-like)", [](test_context &ctx) {
      cx::core::variant v = std::deque<cx::core::variant>{cx::core::variant{std::uint64_t{1}}, cx::core::variant{std::uint64_t{2}}};
      ctx.check(std::holds_alternative<std::deque<cx::core::variant>>(v), "should hold the deque<variant> alternative");
      const auto &arr = std::get<std::deque<cx::core::variant>>(v);
      ctx.check(arr.size() == 2, "the deque should hold both elements");
      ctx.check(std::get<std::uint64_t>(arr[0]) == 1, "the first element should round-trip");
    }},
    {"a variant can hold a string-keyed map of variants (map-like)", [](test_context &ctx) {
      std::map<std::string, cx::core::variant> entries;
      entries.emplace("rate", cx::core::variant{std::uint64_t{44100}});
      cx::core::variant v = entries;
      ctx.check(std::holds_alternative<std::map<std::string, cx::core::variant>>(v), "should hold the map<string, variant> alternative");
      const auto &held = std::get<std::map<std::string, cx::core::variant>>(v);
      ctx.check(held.size() == 1, "the map should hold one entry");
      ctx.check(std::get<std::uint64_t>(held.at("rate")) == 44100, "the entry's value should round-trip under its key");
    }},
    {"a variant can nest a deque of maps", [](test_context &ctx) {
      std::map<std::string, cx::core::variant> inner;
      inner.emplace("x", cx::core::variant{std::uint64_t{1}});
      std::deque<cx::core::variant> outer{cx::core::variant{inner}};

      cx::core::variant v = outer;
      const auto &outer_deque = std::get<std::deque<cx::core::variant>>(v);
      ctx.check(std::holds_alternative<std::map<std::string, cx::core::variant>>(outer_deque[0]), "the nested element should still be a map-holding variant");
      const auto &nested_map = std::get<std::map<std::string, cx::core::variant>>(outer_deque[0]);
      ctx.check(std::get<std::uint64_t>(nested_map.at("x")) == 1, "the nested value should round-trip");
    }},
    {"deque/map-holding variants compare by deep value equality", [](test_context &ctx) {
      cx::core::variant a = std::deque<cx::core::variant>{cx::core::variant{std::uint64_t{1}}};
      cx::core::variant b = std::deque<cx::core::variant>{cx::core::variant{std::uint64_t{1}}};
      cx::core::variant c = std::deque<cx::core::variant>{cx::core::variant{std::uint64_t{2}}};
      ctx.check(a == b, "deques with equal elements should compare equal");
      ctx.check(a != c, "deques with different elements should compare unequal");
    }},
  }) {}
};

inline static variant_test variant_test_instance;

} // namespace cx::core::testing
