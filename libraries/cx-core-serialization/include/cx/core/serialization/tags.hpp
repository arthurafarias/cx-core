// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

// The vocabulary an archive() function and an archiver meet on. archive(ar, x)
// hands the archiver nodes - `ar % tags::v{x}` for a value, tags::kv for a
// named member, the object/array markers for shape - and the archiver decides
// what each means in its format. Brought in from agenticx-ncortex, whose YAML
// and argv archivers are written against it.

#include <string>

namespace cx::core::serialization::tags {

template <typename type> struct v {
  type &value;
  explicit v(type &value) : value(value) {}
};

template <typename type> struct kv {
  std::string name;
  type &value;
  explicit kv(const std::string &name, type &value) : name(name), value(value) {}
};

namespace object {
struct start {};
struct separator {};
struct end {};
} // namespace object

namespace array {
struct start {};
struct end {};
} // namespace array

} // namespace cx::core::serialization::tags
