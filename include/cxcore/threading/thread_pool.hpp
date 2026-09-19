#pragma once

// Pre-split path, kept so existing consumers keep compiling: forwards to
// <cx/core/threading/thread_pool.hpp>.

#include <cx/core/threading/thread_pool.hpp>

namespace cx::core {

using threading::thread_pool;
using threading::async_executor;
using threading::concurrency;
using threading::task_priority;

} // namespace cx::core
