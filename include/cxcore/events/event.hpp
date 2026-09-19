#pragma once

// Pre-split path, kept so existing consumers keep compiling: forwards to
// <cx/core/events/event.hpp>.

#include <cx/core/events/event.hpp>

#include <cxcore/threading/signal.hpp>

namespace cx::core {

using events::event;

} // namespace cx::core
