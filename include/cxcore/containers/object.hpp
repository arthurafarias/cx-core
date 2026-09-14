// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include "cxcore/containers/map.hpp"
#include "cxcore/threading/signal.hpp"

#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cx::core {

// An observable, named bag of variant properties: property_set()/
// property_get() around a map, plus a property_changed signal fired on
// every property_set() (see threading/signal.hpp for the
// GObject-`notify::`-style semantics that follows from).
//
// Storage is a concrete map, not a variant_map member: variant_map (see
// variant_map.hpp) is a pure interface (all-virtual, no data), so it
// cannot be held by value - a `variant_map container_;` member would not
// even compile. Every object owns one concrete, independent map.
//
// Copy/move give the copy its own storage and its own signal, not a
// shared one: cx::core::signal is itself explicitly non-copyable/
// non-movable (see signal.hpp), on the reasoning that a signal's
// connections describe who is watching *this specific instance* - a copy
// starts with no observers of its own, it does not inherit the original's.
// Consistently, the copy also gets its own mutex and its own ordered_map
// (a deep copy of the data, not a shared_ptr to the original's) - unlike
// the previous revision of this file, which shared one mutex via
// shared_ptr across copies (implying shared identity) while separately
// giving each copy an independent, uncopied container (implying
// independent identity). Those two choices contradicted each other; this
// revision picks one coherent model - object is an ordinary value type,
// every instance (including copies) is independent.
class object {
public:
  using entry_type = map::entry_type;

  cx::core::signal<std::string> property_changed;

  object() = default;

  object(const object &other) {
    std::unique_lock lock(other.mutex_);
    container_ = other.container_;
  }

  object(object &&other) noexcept {
    std::unique_lock lock(other.mutex_);
    container_ = std::move(other.container_);
  }

  object &operator=(const object &other) {
    if (this == &other) {
      return *this;
    }
    std::scoped_lock lock(mutex_, other.mutex_);
    container_ = other.container_;
    return *this;
  }

  object &operator=(object &&other) noexcept {
    if (this == &other) {
      return *this;
    }
    std::scoped_lock lock(mutex_, other.mutex_);
    container_ = std::move(other.container_);
    return *this;
  }

  // For callers that need several property_get()/property_set() calls to
  // appear atomic to other threads. Reentrant on the calling thread (the
  // mutex is recursive), so calling property_set()/property_get() while
  // already holding this lock - e.g. from inside a property_changed slot
  // triggered by a call made under the lock - does not deadlock.
  std::unique_lock<std::recursive_mutex> acquire_lock() const { return std::unique_lock<std::recursive_mutex>(mutex_); }

  bool has(const std::string &name) const {
    std::unique_lock lock(mutex_);
    return container_.has(name);
  }

  template <typename ValueType> void property_set(const std::string &name, const ValueType &value) {
    {
      std::unique_lock lock(mutex_);
      container_.set(name, variant(value));
    }
    // Emitted after the lock is released (see signal.hpp), so a
    // slot that calls property_set()/property_get() again on this same
    // object cannot deadlock against the mutation it is reacting to.
    property_changed.emit(name);
  }

  // std::nullopt when name is absent. Throws std::bad_variant_access (the
  // same as a bare std::get<ValueType>) when name is present but holds a
  // different alternative - with deliberate exceptions, all in the same
  // direction (widen an integer literal to a wider/differently-signed
  // numeric type the property actually needs, never narrow one back
  // down): requesting int64_t/uint64_t against a property holding the
  // *other* signedness coerces instead of throwing, provided the stored
  // value actually fits (non-negative for int64_t->uint64_t; within
  // int64_t's range for uint64_t->int64_t); requesting double against a
  // property holding int64_t or uint64_t coerces unconditionally (widening
  // an integer to double is exact for any value this codebase's
  // properties actually carry). Found necessary in practice, not designed
  // speculatively: SRS-003's text-grammar parser has exactly one integer
  // literal type (int64_t, chosen so fake_src's own int64_t-typed
  // "num-buffers" round-trips - see pipeline_parser.hpp's own comment),
  // so any element property independently typed uint64_t (queue's
  // "max-size-buffers", fake_src's own "interval-ms") or double
  // (audio_test_src's "freq", set via a plain integer-looking literal
  // like "freq=440" with no decimal point) throws the moment a
  // text-grammar pipeline sets it, crashing the whole process rather than
  // erroring gracefully. Every other type combination (bool vs string,
  // double vs a property that expects an *integer*, ...) is unaffected
  // and still throws exactly as before - this narrows to the specific
  // collisions an integer-literal-only grammar produces against
  // independently-reasonable property type choices, not a general
  // type-coercion layer.
  template <typename ValueType> std::optional<ValueType> property_get(const std::string &name) const {
    auto value = property_get_variant(name);
    if (!value) {
      return std::nullopt;
    }
    if constexpr (std::is_same_v<ValueType, std::uint64_t>) {
      if (const auto *signed_v = std::get_if<std::int64_t>(&*value)) {
        if (*signed_v >= 0) {
          return static_cast<std::uint64_t>(*signed_v);
        }
      }
    } else if constexpr (std::is_same_v<ValueType, std::int64_t>) {
      if (const auto *unsigned_v = std::get_if<std::uint64_t>(&*value)) {
        if (*unsigned_v <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
          return static_cast<std::int64_t>(*unsigned_v);
        }
      }
    } else if constexpr (std::is_same_v<ValueType, double>) {
      if (const auto *signed_v = std::get_if<std::int64_t>(&*value)) {
        return static_cast<double>(*signed_v);
      }
      if (const auto *unsigned_v = std::get_if<std::uint64_t>(&*value)) {
        return static_cast<double>(*unsigned_v);
      }
    }
    return std::get<ValueType>(*value);
  }

  // Same absent-key contract as property_get(), but returns the stored
  // variant itself rather than unwrapping one specific alternative via
  // std::get<ValueType> - the accessor a caller reaches for when it wants
  // "whatever this property currently holds" without committing to an
  // alternative ahead of time (e.g. structure::get(), §5.4, whose field
  // values were never scalar-typed to begin with - the original
  // structure::field_value was itself a small variant).
  std::optional<variant> property_get_variant(const std::string &name) const {
    std::unique_lock lock(mutex_);
    return container_.get(name);
  }

  // A thread-safe forward iterator: begin() takes the lock exactly once,
  // copies every current entry into a snapshot, and releases the lock
  // before returning - so `for (auto entry : obj)` never holds object's
  // mutex for the duration of the loop body. This is deliberate, not just
  // a convenience: holding the lock across the loop body would let a
  // property_set() call made from inside that body reenter safely (the
  // mutex is recursive), but it would also block *every other thread's*
  // property_set()/property_get() for the entire iteration, and a
  // reference into a live, concurrently-mutable ordered_map would dangle
  // the moment another thread's erase()/set() ran between iterations if
  // the lock were instead dropped and reacquired per-step. Snapshotting
  // once under a single lock acquisition sidesteps both problems, at the
  // cost of iterating a point-in-time copy rather than the live map -
  // concurrent mutations after begin() is called are simply not visible
  // to that iteration, matching what a caller taking any single consistent
  // snapshot of shared, mutex-guarded state should expect.
  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = entry_type;
    using difference_type = std::ptrdiff_t;
    using pointer = const entry_type *;
    using reference = const entry_type &;

    reference operator*() const { return (*snapshot_)[index_]; }
    pointer operator->() const { return &(*snapshot_)[index_]; }

    iterator &operator++() {
      ++index_;
      return *this;
    }

    iterator operator++(int) {
      iterator previous = *this;
      ++index_;
      return previous;
    }

    bool operator==(const iterator &other) const {
      bool this_is_end = !snapshot_ || index_ >= snapshot_->size();
      bool other_is_end = !other.snapshot_ || other.index_ >= other.snapshot_->size();
      if (this_is_end || other_is_end) {
        return this_is_end == other_is_end;
      }
      return snapshot_ == other.snapshot_ && index_ == other.index_;
    }

  private:
    friend class object;

    iterator() = default;
    iterator(std::shared_ptr<const std::vector<entry_type>> snapshot, std::size_t index)
        : snapshot_(std::move(snapshot)), index_(index) {}

    std::shared_ptr<const std::vector<entry_type>> snapshot_;
    std::size_t index_ = 0;
  };

  iterator begin() const {
    auto snapshot = std::make_shared<std::vector<entry_type>>();
    {
      std::unique_lock lock(mutex_);
      snapshot->reserve(container_.size());
      container_.for_each([&](const std::string &key, const variant &value) { snapshot->emplace_back(key, value); });
    }
    return iterator(std::move(snapshot), 0);
  }

  iterator end() const { return iterator(); }

private:
  mutable std::recursive_mutex mutex_;
  map container_;
};

} // namespace cx::core
