// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

#include <cx/core/serialization/tags.hpp>

namespace cx::core::containers {

// A closed-set typed value: the base storage unit for variant_map entries
// (see variant_map.hpp) and observable object properties (see object.hpp).
// No std::any/type-erasure escape hatch - matches structure.hpp's existing
// "no dynamic GValue-equivalent type system yet (deferred)" stance. Widen
// the base's scalar alternatives if/when a new field type is actually
// needed, rather than adding an open-ended fallback now.
//
// Closed set is bool/int64_t/uint64_t/double_t/string plus the two
// self-referential aggregate alternatives below. Any argument not
// implicitly, non-narrowingly convertible to one of the alternatives
// simply fails to compile at the call site, same as passing an unrelated
// type to a bare std::variant of these alternatives would.
//
// int64_t was added alongside uint64_t (SRS-001 OPEN-6, resolved: widen
// rather than scope negative values out) so that structure::field_value's
// signed 64-bit integer alternative has a lossless home here - a
// structure/caps field holding a negative value, or an element property
// that needs a signed sentinel (e.g. fake_src's num-buffers, where -1 means
// "unbounded"), now round-trips exactly instead of wrapping into a huge
// unsigned value or being rejected outright. Callers must still pass an
// exact-width, exact-signedness integer literal (std::int64_t{...} /
// std::uint64_t{...}), never a bare int/long: with two integral
// alternatives of equal conversion rank now present, an implicitly-widened
// argument is ambiguous between them (the same reason a bare string
// literal already had to be reasoned about via P0608 below) - this was
// already the project's convention before this alternative was added, not
// a new restriction it introduces.
//
// Publicly inherits std::variant instead of wrapping it, so the whole
// standard variant interface applies directly to this type: std::get,
// std::get_if, std::holds_alternative, std::visit, and operator== all work
// on a `variant` exactly as they would on a bare std::variant, found via
// template argument deduction through the (unique, public) base class and
// via ADL considering base classes - no bespoke get<T>()/holds<T>()
// re-implementation needed. `using base::base;` inherits std::variant's
// constructors as-is, including its converting constructor, which already
// excludes binding to another `variant` (so copy/move still go through the
// implicit special members, not the converting constructor) and already
// carries the C++20 fix (P0608) that makes a `const char*` prefer the
// std::string alternative over bool - without it, `variant v = "hello";`
// would silently pick the pre-P0608 boolean-conversion candidate instead
// of constructing a string.
//
// std::deque<variant> (an ordered list of variants) and
// std::map<std::string, variant> (a string-keyed, key-sorted collection of
// variants) make variant self-referential, with no pointer/indirection
// wrapper needed: std::deque tolerates an incomplete element type at the
// point it's instantiated (a guarantee deque/list/forward_list/vector
// specifically have - std::map is not guaranteed this by the standard),
// and in practice libstdc++'s and libc++'s std::map do not require the
// mapped type to be complete merely to be named as a variant alternative
// here either, since sizeof(map<K, V>) does not depend on sizeof(V).
// Verified to compile on GCC 16 and Clang 22 in C++23.
//
// `int` and std::nullptr_t came with agenticx-ncortex's variant, which this type
// absorbed: a workspace document (YAML, argv tokens) has plain integers and an
// explicit null, and its readers ask for `int`. They sit *after* the original
// seven so no existing index moves. With `int` present a bare int argument is
// an exact match and is stored as `int`; a reader that wants it wider goes
// through object::property_get, which widens a stored int to int64_t, uint64_t
// or double exactly as it already did between the two 64-bit alternatives.
struct variant;

using variant_types = std::tuple<bool, std::int64_t, std::uint64_t, std::double_t, std::string, std::deque<variant>,
                                 std::map<std::string, variant>, int, std::nullptr_t>;

namespace detail {
template <typename> struct variant_from_tuple;
template <typename... types> struct variant_from_tuple<std::tuple<types...>> {
  using type = std::variant<types...>;
};
} // namespace detail

struct variant : public detail::variant_from_tuple<variant_types>::type {
public:
  using types = variant_types;
  using base = detail::variant_from_tuple<variant_types>::type;
  using base_type = base;
  using base::base;
  using base::operator=;

  // Calls fn.template operator()<alternative>() for each alternative in turn
  // until one returns true: how a reader tries every type a token could be.
  template <typename visitor> static constexpr bool visit_all(visitor &&fn) {
    return [&]<std::size_t... indices>(std::index_sequence<indices...>) {
      return (fn.template operator()<std::tuple_element_t<indices, types>>() || ...);
    }(std::make_index_sequence<std::tuple_size_v<types>>{});
  }

  template <typename type> type &as() { return std::get<type>(*this); }
  template <typename type> const type &as() const { return std::get<type>(*this); }
  template <typename type> bool is() const { return std::holds_alternative<type>(*this); }

  // Map access - only meaningful while this variant holds a map; any other
  // alternative throws std::bad_variant_access, the same way as() does,
  // rather than silently converting.
  variant &operator[](const std::string &key) { return as<std::map<std::string, variant>>()[key]; }
  const variant &operator[](const std::string &key) const { return as<std::map<std::string, variant>>().at(key); }

  // Explicit so a variant never silently decays inside an expression
  // (operator<<, comparisons against literals): static_cast<T>(v) is the
  // spelling, as<T>() the reference-returning form.
  template <typename T> explicit operator T() const { return as<T>(); }
};

// Every archive() describes a value as one serialization::tags::v node; each
// archiver (YAML, argv, ...) implements operator% for the node types it
// understands, so an archiver holds no state beyond the stream it reads or writes.
template <typename archiver> void archive(archiver &ar, variant &value) { ar % serialization::tags::v{value}; }
template <typename archiver> void archive(archiver &ar, const variant &value) { ar % serialization::tags::v{value}; }

} // namespace cx::core::containers
