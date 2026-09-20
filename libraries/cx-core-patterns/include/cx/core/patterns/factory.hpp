// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace cx::core::patterns {

// A registry of named ways to make a product_type: register a creator under a
// key, create by key later - the plug-in seam behind "an element type named in
// a pipeline description", "a serializer chosen in a config file". Instances
// are independent; a process-wide one is a static local at the use site.
//
// create() copies the creator out under the lock and calls it unlocked, so a
// creator may itself register or create without deadlocking.
template <typename product_type, typename key_type, typename... argument_types> class factory {
public:
  using creator_function = std::function<std::shared_ptr<product_type>(argument_types...)>;

  // The last registration under a key wins; returns whether it replaced one.
  bool register_type(key_type key, creator_function creator) {
    std::unique_lock lock(mutex_);
    return !creators_.insert_or_assign(std::move(key), std::move(creator)).second;
  }

  bool unregister_type(const key_type &key) {
    std::unique_lock lock(mutex_);
    return creators_.erase(key) > 0;
  }

  bool has(const key_type &key) const {
    std::unique_lock lock(mutex_);
    return creators_.contains(key);
  }

  std::optional<creator_function> find(const key_type &key) const {
    std::unique_lock lock(mutex_);
    auto it = creators_.find(key);
    if (it == creators_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  // nullptr for an unknown key, or whatever the creator itself returned.
  std::shared_ptr<product_type> create(const key_type &key, argument_types... arguments) const {
    auto creator = find(key);
    if (!creator) {
      return nullptr;
    }
    return (*creator)(std::forward<argument_types>(arguments)...);
  }

  std::vector<key_type> keys() const {
    std::unique_lock lock(mutex_);
    std::vector<key_type> result;
    result.reserve(creators_.size());
    for (const auto &[key, creator] : creators_) {
      result.push_back(key);
    }
    return result;
  }

private:
  mutable std::mutex mutex_;
  std::map<key_type, creator_function> creators_;
};

// Registers on construction, so a namespace-scope `static registration r{...}`
// wires a type in before main() - the self-registering plug-in idiom.
template <typename factory_type> class registration {
public:
  template <typename key_type, typename creator_type>
  registration(factory_type &target, key_type &&key, creator_type &&creator) {
    target.register_type(std::forward<key_type>(key), std::forward<creator_type>(creator));
  }
};

} // namespace cx::core::patterns
