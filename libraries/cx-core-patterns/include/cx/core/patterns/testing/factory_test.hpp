// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/patterns/factory.hpp>
#include <cx/core/testing/test_group.hpp>

#include <memory>
#include <string>

namespace cx::core::testing {

namespace factory_test_detail {
struct shape {
  virtual ~shape() = default;
  virtual std::string name() const = 0;
};
struct circle : shape {
  explicit circle(std::string label) : label(std::move(label)) {}
  std::string name() const override { return "circle:" + label; }
  std::string label;
};
using shape_factory = patterns::factory<shape, std::string, std::string>;
} // namespace factory_test_detail

inline test_group factory_tests{
    "factory",
    {
        {"create() builds through the creator registered under the key", [](test_context &ctx) {
           factory_test_detail::shape_factory f;
           f.register_type("circle", [](std::string label) { return std::make_shared<factory_test_detail::circle>(label); });
           auto made = f.create("circle", "a");
           ctx.require(made != nullptr, "a registered key should create");
           ctx.check(made->name() == "circle:a", "constructor arguments should reach the creator");
         }},
        {"an unknown key creates nothing", [](test_context &ctx) {
           factory_test_detail::shape_factory f;
           ctx.check(f.create("square", "a") == nullptr, "create() should return nullptr for an unregistered key");
           ctx.check(!f.has("square"), "has() should be false for an unregistered key");
         }},
        {"the last registration under a key wins", [](test_context &ctx) {
           factory_test_detail::shape_factory f;
           ctx.check(!f.register_type("circle", [](std::string) { return nullptr; }), "the first registration replaces nothing");
           ctx.check(f.register_type("circle", [](std::string l) { return std::make_shared<factory_test_detail::circle>(l); }),
                     "the second should report that it replaced one");
           ctx.check(f.create("circle", "b") != nullptr, "the replacing creator should be the one used");
         }},
        {"a creator may use the factory it is registered in", [](test_context &ctx) {
           factory_test_detail::shape_factory f;
           f.register_type("circle", [](std::string l) { return std::make_shared<factory_test_detail::circle>(l); });
           f.register_type("alias", [&f](std::string l) { return f.create("circle", l); });
           auto made = f.create("alias", "c");
           ctx.check(made && made->name() == "circle:c", "re-entering create() from a creator should not deadlock");
         }},
        {"registration wires a type in on construction, and keys() lists it", [](test_context &ctx) {
           factory_test_detail::shape_factory f;
           patterns::registration<factory_test_detail::shape_factory> r{
               f, "circle", [](std::string l) { return std::make_shared<factory_test_detail::circle>(l); }};
           ctx.check(f.keys() == std::vector<std::string>{"circle"}, "keys() should list what was registered");
           ctx.check(f.unregister_type("circle") && !f.has("circle"), "unregister_type() should remove it");
         }},
    }};

} // namespace cx::core::testing
