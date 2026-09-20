// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <cx/core/containers/property.hpp>
#include <cx/core/containers/variant_array.hpp>
#include <cx/core/testing/test_group.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <type_traits>

namespace cx::core::testing {

namespace detail {
// An archiver that only counts what it is handed, and of which type.
struct counting_archiver {
  int variants = 0, properties = 0, maps = 0, arrays = 0;
  template <typename type> counting_archiver &operator%(serialization::tags::v<type> node) {
    using plain = std::remove_const_t<type>;
    variants += std::is_same_v<plain, containers::variant>;
    properties += std::is_same_v<plain, containers::property>;
    maps += std::is_same_v<plain, containers::property_map>;
    arrays += std::is_same_v<plain, containers::variant_array>;
    (void)node;
    return *this;
  }
};
} // namespace detail

inline test_group property_tests{
    "property",
    {
        {"a bare int is stored as int, null is an alternative, and neither moved the 64-bit ones", [](test_context &ctx) {
           using containers::variant;
           variant plain = 8;
           ctx.check(plain.is<int>() && plain.as<int>() == 8, "an int argument selects the int alternative");
           variant nothing = nullptr;
           ctx.check(nothing.is<std::nullptr_t>(), "nullptr selects the null alternative");
           ctx.check(variant{std::int64_t{-1}}.index() == 1 && variant{std::uint64_t{1}}.index() == 2,
                     "the original alternatives keep their indices");
           ctx.check(variant{}.is<bool>(), "a default variant is still bool false");
           variant text = "hello";
           ctx.check(text.is<std::string>(), "a string literal is a string, not a bool");
         }},
        {"as/is/operator[]/explicit conversion read a variant without std::get", [](test_context &ctx) {
           using containers::variant;
           variant document = std::map<std::string, variant>{};
           document["rank"] = 8;
           document["name"] = "tuned";
           const variant &view = document;
           ctx.check(view["rank"].as<int>() == 8 && static_cast<std::string>(view["name"]) == "tuned", "members read back");
           bool threw = false;
           try {
             (void)view["rank"]["nested"];
           } catch (const std::bad_variant_access &) {
             threw = true;
           }
           ctx.check(threw, "indexing a non-map throws rather than converting");
         }},
        {"visit_all offers every alternative in order and stops at the first taker", [](test_context &ctx) {
           int offered = 0;
           const bool taken = containers::variant::visit_all([&]<typename type>() {
             ++offered;
             return std::is_same_v<type, std::string>;
           });
           ctx.check(taken && offered == 5, "string is the fifth alternative, and the search stops there");
         }},
        {"a property is its value plus name, option and description", [](test_context &ctx) {
           using containers::property;
           property rank{8, "lora_rank", "lora-rank", "Rank of the LoRA update."};
           ctx.check(rank.as<int>() == 8 && rank.option == "lora-rank", "aggregate initialization fills value then metadata");
           property other = rank;
           ctx.check(other == rank, "equal value and metadata compare equal");
           other.description = "changed";
           ctx.check(!(other == rank), "metadata takes part in equality");
           static_cast<containers::variant &>(other) = 16;
           ctx.check(other.as<int>() == 16 && other.name == "lora_rank", "assigning the value leaves the metadata");
         }},
        {"archive() hands an archiver one tags::v node per value", [](test_context &ctx) {
           detail::counting_archiver ar;
           containers::variant value = 1;
           const containers::property single{2, "n", "n", ""};
           containers::property_map map{{"a.b", single}};
           containers::variant_array array{value};
           archive(ar, value);
           archive(ar, single);
           archive(ar, map);
           archive(ar, array);
           ctx.check(ar.variants == 1 && ar.properties == 1 && ar.maps == 1 && ar.arrays == 1,
                     "each archive() is found by ADL and passes its own type through");
         }},
    }};

} // namespace cx::core::testing
