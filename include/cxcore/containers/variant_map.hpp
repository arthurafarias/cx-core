// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <functional>
#include <optional>
#include <string>

#include <cxcore/containers/variant.hpp>

namespace cx::core {

// Interface for a named collection of key -> variant entries. set() is the
// *only* mutator on purpose: a future observable-decorated implementation
// (see object.hpp) can guarantee "every mutation is notified" only because
// there is no second, raw-setter/reference-returning path that bypasses it.
// Pure storage contract here - no notification of its own.
class variant_map {
public:
  virtual ~variant_map() = default;

  virtual bool has(const std::string &key) const = 0;

  // std::nullopt for an absent key - distinct from a present key holding
  // variant{std::monostate{}}.
  virtual std::optional<variant> get(const std::string &key) const = 0;

  virtual void set(const std::string &key, variant value) = 0;
  virtual bool erase(const std::string &key) = 0;

  virtual void for_each(const std::function<void(const std::string &, const variant &)> &fn) const = 0;
};

} // namespace cx::core
