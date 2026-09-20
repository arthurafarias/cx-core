// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <map>
#include <string>

#include <cx/core/containers/variant.hpp>

namespace cx::core::containers {

// A value travelling together with the presentation metadata every serializer
// carries along - the human-facing `name`, the command-line `option` alias
// (`--<option>=<value>`), and a `description` used for help text.
// Aggregate-initializable, so a declaration reads
// `property{value, "name", "option", "description"}`, with everything after
// the value optional. Not object.hpp's observable properties: those are named
// slots on a live object, this is a record in a document.
struct property : public variant {
  std::string name;
  std::string option;
  std::string description;

  friend bool operator==(const property &, const property &) = default;
};

template <class Archiver> void archive(Archiver &ar, property &value) { ar % serialization::tags::v{value}; }
template <class Archiver> void archive(Archiver &ar, const property &value) { ar % serialization::tags::v{value}; }

// Dotted keys (`training.lora_rank`) to properties. std::map keeps every
// rendering (a YAML document, an argv) key-sorted and therefore stable.
using property_map = std::map<std::string, property>;

template <class Archiver> void archive(Archiver &ar, property_map &value) { ar % serialization::tags::v{value}; }
template <class Archiver> void archive(Archiver &ar, const property_map &value) { ar % serialization::tags::v{value}; }

} // namespace cx::core::containers
