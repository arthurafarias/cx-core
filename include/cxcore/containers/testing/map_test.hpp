// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cxcore/testing/test_group.hpp>
#include <cxcore/containers/map.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace cx::core::testing {

struct map_test : public test_group {
  map_test() : test_group("map", {
    {"a missing key is absent", [](test_context &ctx) {
      cx::core::map m;
      ctx.check(!m.has("rate"), "an unset key should be absent");
      ctx.check(!m.get("rate").has_value(), "get() on an unset key should return nullopt");
    }},
    {"set then get round-trips the value", [](test_context &ctx) {
      cx::core::map m;
      m.set("rate", std::uint64_t{44100});
      ctx.check(m.has("rate"), "key should be present after set()");
      auto v = m.get("rate");
      ctx.check(v.has_value() && std::get<std::uint64_t>(*v) == 44100, "get() should return the value passed to set()");
    }},
    {"set on an existing key overwrites in place, not append", [](test_context &ctx) {
      cx::core::map m;
      m.set("rate", std::uint64_t{44100});
      m.set("rate", std::uint64_t{48000});
      ctx.check(m.size() == 1, "re-setting an existing key should not grow the map");
      ctx.check(std::get<std::uint64_t>(*m.get("rate")) == 48000, "the newer value should win");
    }},
    {"erase removes an entry and reports success", [](test_context &ctx) {
      cx::core::map m;
      m.set("rate", std::uint64_t{44100});
      ctx.check(m.erase("rate"), "erase() on a present key should return true");
      ctx.check(!m.has("rate"), "key should be absent after erase()");
      ctx.check(!m.erase("rate"), "erase() on an absent key should return false");
    }},
    {"for_each visits entries in key-sorted order", [](test_context &ctx) {
      cx::core::map m;
      m.set("rate", std::uint64_t{44100});
      m.set("channels", std::uint64_t{2});
      m.set("format", std::string{"S16LE"});

      std::vector<std::string> seen;
      m.for_each([&seen](const std::string &key, const cx::core::variant &) { seen.push_back(key); });

      std::vector<std::string> expected{"channels", "format", "rate"};
      ctx.check(seen == expected, "for_each() order should be key-sorted, regardless of insertion order");
    }},
    {"re-setting an existing key does not change key-sorted order", [](test_context &ctx) {
      cx::core::map m;
      m.set("channels", std::uint64_t{2});
      m.set("rate", std::uint64_t{44100});
      m.set("channels", std::uint64_t{6}); // overwrite; still sorts before "rate"

      std::vector<std::string> seen;
      m.for_each([&seen](const std::string &key, const cx::core::variant &) { seen.push_back(key); });

      std::vector<std::string> expected{"channels", "rate"};
      ctx.check(seen == expected, "overwriting a key should not change its key-sorted position");
    }},
    {"empty/size reflect the entry count", [](test_context &ctx) {
      cx::core::map m;
      ctx.check(m.empty() && m.size() == 0, "a fresh map should be empty");
      m.set("rate", std::uint64_t{44100});
      ctx.check(!m.empty() && m.size() == 1, "size should reflect one set() call");
    }},
  }) {}
};

inline static map_test map_test_instance;

} // namespace cx::core::testing
