#pragma once

// Pre-split path, kept so existing consumers keep compiling: forwards to
// <cx/core/containers/map.hpp>.

#include <cx/core/containers/map.hpp>

namespace cx::core {

using containers::map;
using containers::variant_map;
using containers::variant;

} // namespace cx::core
