// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cx/core/testing/test_group.hpp>
#include <cx/core/containers/object.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace cx::core::testing {

struct object_test : public test_group {
  object_test() : test_group("object", {
    {"a missing property is absent", [](test_context &ctx) {
      containers::object obj;
      ctx.check(!obj.has("rate"), "an unset property should be absent");
      ctx.check(!obj.property_get<std::uint64_t>("rate").has_value(), "property_get() on an unset key should return nullopt");
    }},
    {"property_get() does not insert on a missing key", [](test_context &ctx) {
      containers::object obj;
      (void)obj.property_get<std::uint64_t>("rate");
      ctx.check(!obj.has("rate"), "reading a missing property must not create it as a side effect");
    }},
    {"property_set then property_get round-trips the value", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("rate", std::uint64_t{44100});
      ctx.check(obj.has("rate"), "property should be present after property_set()");
      auto v = obj.property_get<std::uint64_t>("rate");
      ctx.check(v.has_value() && *v == 44100, "property_get() should return the value passed to property_set()");
    }},
    {"property_set fires property_changed with the property name", [](test_context &ctx) {
      containers::object obj;
      std::vector<std::string> changed;
      obj.property_changed.connect([&](const std::string &name) { changed.push_back(name); });
      obj.property_set("rate", std::uint64_t{44100});
      obj.property_set("channels", std::uint64_t{2});
      ctx.check(changed == std::vector<std::string>{"rate", "channels"}, "property_changed should fire once per property_set(), in order, naming the set key");
    }},
    {"property_changed is emitted after the mutation is visible, not before", [](test_context &ctx) {
      containers::object obj;
      bool visible_inside_slot = false;
      obj.property_changed.connect([&](const std::string &name) {
        visible_inside_slot = obj.property_get<std::uint64_t>(name).value_or(-1) == 44100;
      });
      obj.property_set("rate", std::uint64_t{44100});
      ctx.check(visible_inside_slot, "a property_changed slot should already see the new value (lock released before emit)");
    }},
    {"property_get_variant() returns the stored value without committing to an alternative", [](test_context &ctx) {
      containers::object obj;
      ctx.check(!obj.property_get_variant("rate").has_value(), "an absent key should return nullopt");

      obj.property_set("rate", std::uint64_t{44100});
      auto v = obj.property_get_variant("rate");
      ctx.require(v.has_value(), "property_get_variant() should find a present key");
      ctx.check(std::holds_alternative<std::uint64_t>(*v), "the returned variant should hold whatever alternative was set");
      ctx.check(std::get<std::uint64_t>(*v) == 44100, "the returned variant should carry the value passed to property_set()");
    }},
    {"a copy has its own independent storage", [](test_context &ctx) {
      containers::object original;
      original.property_set("rate", std::uint64_t{44100});

      containers::object copy = original;
      ctx.check(copy.property_get<std::uint64_t>("rate").value_or(0) == 44100, "the copy should start with the source's data");

      copy.property_set("rate", std::uint64_t{48000});
      ctx.check(original.property_get<std::uint64_t>("rate").value_or(0) == 44100, "mutating the copy must not affect the original");
      ctx.check(copy.property_get<std::uint64_t>("rate").value_or(0) == 48000, "the copy should hold its own new value");
    }},
    {"a copy does not inherit the original's property_changed connections", [](test_context &ctx) {
      containers::object original;
      int original_notifications = 0;
      original.property_changed.connect([&](const std::string &) { ++original_notifications; });

      containers::object copy = original;
      copy.property_set("rate", std::uint64_t{44100});

      ctx.check(original_notifications == 0, "a slot connected to the original should not fire for the copy's mutations");
    }},
    {"move transfers storage out of the source", [](test_context &ctx) {
      containers::object original;
      original.property_set("rate", std::uint64_t{44100});

      containers::object moved = std::move(original);
      ctx.check(moved.property_get<std::uint64_t>("rate").value_or(0) == 44100, "the moved-to object should hold the source's data");
    }},
    {"copy assignment replaces the target's storage", [](test_context &ctx) {
      containers::object a;
      a.property_set("rate", std::uint64_t{44100});
      containers::object b;
      b.property_set("rate", std::uint64_t{8000});
      b.property_set("channels", std::uint64_t{1});

      b = a;
      ctx.check(b.property_get<std::uint64_t>("rate").value_or(0) == 44100, "assignment should overwrite an existing key with the source's value");
      ctx.check(!b.has("channels"), "assignment should replace the whole property set, not merge into it");
    }},
    {"self copy-assignment is a no-op, not data loss", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("rate", std::uint64_t{44100});
      obj = obj;
      ctx.check(obj.property_get<std::uint64_t>("rate").value_or(0) == 44100, "self-assignment must not clear or corrupt the object's data");
    }},
    {"begin()/end() iterate every property exactly once, in key-sorted order", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("channels", std::uint64_t{2});
      obj.property_set("rate", std::uint64_t{44100});
      obj.property_set("format", std::string{"S16LE"});

      std::vector<std::string> seen;
      for (const auto &entry : obj) {
        seen.push_back(entry.first);
      }
      ctx.check(seen == std::vector<std::string>{"channels", "format", "rate"}, "range-for should visit every property, key-sorted (the underlying storage is a map, not insertion-ordered)");
    }},
    {"an empty object iterates zero times", [](test_context &ctx) {
      containers::object obj;
      int count = 0;
      for (const auto &entry : obj) {
        (void)entry;
        ++count;
      }
      ctx.check(count == 0, "iterating an object with no properties should visit nothing");
    }},
    {"the iterator is a snapshot: mutating the object mid-loop does not perturb an in-flight iteration", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("a", std::uint64_t{1});
      obj.property_set("b", std::uint64_t{2});

      int visited = 0;
      for (const auto &entry : obj) {
        (void)entry;
        obj.property_set("extra", std::uint64_t{99}); // must not extend or invalidate this iteration
        ++visited;
      }
      ctx.check(visited == 2, "the loop should see exactly the properties present when begin() was called");
      ctx.check(obj.has("extra"), "the mutation performed from inside the loop body should still have taken effect");
    }},
    {"concurrent property_set() from multiple threads all land", [](test_context &ctx) {
      containers::object obj;
      constexpr int thread_count = 8;
      constexpr int sets_per_thread = 200;

      std::vector<std::thread> threads;
      for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&obj, t] {
          for (int i = 0; i < sets_per_thread; ++i) {
            obj.property_set("key-" + std::to_string(t), static_cast<std::uint64_t>(i));
          }
        });
      }
      for (auto &th : threads) {
        th.join();
      }

      for (int t = 0; t < thread_count; ++t) {
        auto v = obj.property_get<std::uint64_t>("key-" + std::to_string(t));
        ctx.check(v.has_value() && *v == sets_per_thread - 1, "each thread's key should end on its last written value, with no lost or torn writes");
      }
    }},
    {"property_get<uint64_t>() coerces a non-negative int64_t-stored property instead of throwing", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("count", std::int64_t{42});
      auto v = obj.property_get<std::uint64_t>("count");
      ctx.check(v.has_value() && *v == 42, "a non-negative int64_t should coerce to uint64_t");
    }},
    {"property_get<uint64_t>() still throws for a negative int64_t-stored property", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("count", std::int64_t{-1});
      ctx.check_throws<std::bad_variant_access>([&] { (void)obj.property_get<std::uint64_t>("count"); },
                                                  "a negative int64_t cannot represent a uint64_t");
    }},
    {"property_get<int64_t>() coerces an in-range uint64_t-stored property instead of throwing", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("count", std::uint64_t{42});
      auto v = obj.property_get<std::int64_t>("count");
      ctx.check(v.has_value() && *v == 42, "an in-range uint64_t should coerce to int64_t");
    }},
    {"property_get<int64_t>() still throws for a uint64_t-stored property beyond int64_t's range", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("count", std::numeric_limits<std::uint64_t>::max());
      ctx.check_throws<std::bad_variant_access>([&] { (void)obj.property_get<std::int64_t>("count"); },
                                                  "a uint64_t beyond int64_t's range cannot coerce");
    }},
    {"property_get<T>() for a non-integer type still throws on a genuine mismatch", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("name", std::string("hello"));
      ctx.check_throws<std::bad_variant_access>([&] { (void)obj.property_get<bool>("name"); },
                                                  "the int64_t/uint64_t coercion must not widen to unrelated types");
    }},
    {"property_get<double>() coerces an int64_t-stored property instead of throwing", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("freq", std::int64_t{440}); // e.g. "freq=440" via the text grammar - no decimal point
      auto v = obj.property_get<double>("freq");
      ctx.check(v.has_value() && *v == 440.0, "an int64_t-stored property should coerce to double");
    }},
    {"property_get<double>() coerces a uint64_t-stored property instead of throwing", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("freq", std::uint64_t{440});
      auto v = obj.property_get<double>("freq");
      ctx.check(v.has_value() && *v == 440.0, "a uint64_t-stored property should coerce to double");
    }},
    {"property_get<int64_t>() still throws for a double-stored property (no narrowing coercion)", [](test_context &ctx) {
      containers::object obj;
      obj.property_set("level", 1.5);
      ctx.check_throws<std::bad_variant_access>([&] { (void)obj.property_get<std::int64_t>("level"); },
                                                  "double->int64_t is a narrowing direction this coercion does not attempt");
    }},
    {"concurrent iteration alongside concurrent property_set() never crashes or reads torn data", [](test_context &ctx) {
      containers::object obj;
      for (int i = 0; i < 50; ++i) {
        obj.property_set("key-" + std::to_string(i), static_cast<std::uint64_t>(i));
      }

      std::atomic<bool> stop{false};
      std::thread writer([&] {
        int i = 0;
        while (!stop.load()) {
          obj.property_set("key-" + std::to_string(i % 50), static_cast<std::uint64_t>(i));
          ++i;
        }
      });

      for (int pass = 0; pass < 200; ++pass) {
        std::size_t count = 0;
        for (const auto &entry : obj) {
          (void)entry;
          ++count;
        }
        ctx.check(count >= 50, "a concurrent iteration should see at least the properties present at begin()");
      }

      stop.store(true);
      writer.join();
    }},
  }) {}
};

inline static object_test object_test_instance;

} // namespace cx::core::testing
