#pragma once

// Pre-split path, kept so existing consumers keep compiling: forwards to
// <cx/core/signals/signal.hpp>.

#include <cx/core/signals/signal.hpp>

#include <cxcore/threading/thread_pool.hpp>

namespace cx::core {

using signals::signal;

} // namespace cx::core
