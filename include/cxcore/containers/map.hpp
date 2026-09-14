// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <map>
#include <utility>

#include <cxcore/containers/variant_map.hpp>

namespace cx::core {

// Concrete variant_map backed directly by std::map<std::string, variant>:
// iteration (for_each(), begin()/end()) visits entries in key-sorted order,
// not insertion order - there is no side index or list threading entries
// together, just the map itself. has()/get()/set()/erase() are the usual
// O(log n) std::map operations.
class map : public variant_map {
public:
  using entry_type = std::pair<const std::string, variant>;

  bool has(const std::string &key) const override { return entries_.contains(key); }

  std::optional<variant> get(const std::string &key) const override {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void set(const std::string &key, variant value) override { entries_.insert_or_assign(key, std::move(value)); }

  bool erase(const std::string &key) override { return entries_.erase(key) > 0; }

  void for_each(const std::function<void(const std::string &, const variant &)> &fn) const override {
    for (const auto &[key, value] : entries_) {
      fn(key, value);
    }
  }

  bool empty() const { return entries_.empty(); }
  std::size_t size() const { return entries_.size(); }

private:
  std::map<std::string, variant> entries_;
};

} // namespace cx::core
