#pragma once

// Pre-split path, kept so existing consumers keep compiling: forwards to
// <cx/core/signals/async_signal.hpp>.

#include <cx/core/signals/async_signal.hpp>

#include <cxcore/threading/async_executor.hpp>
#include <cxcore/threading/signal.hpp>

namespace cx::core {

using signals::async_signal;

} // namespace cx::core
