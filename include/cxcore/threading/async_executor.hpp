#pragma once

// Pre-split path, kept so existing consumers keep compiling: forwards to
// <cx/core/threading/async_executor.hpp>.

#include <cx/core/threading/async_executor.hpp>

namespace cx::core {

using threading::async_executor;
using threading::concurrency;

} // namespace cx::core
