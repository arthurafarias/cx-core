// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

// Compile-time lock on the pre-split surface: only <cxcore/...> paths and the
// flat cx::core names, exactly as consumers pinned before the split use them.
// Each must still name the very type that now lives in cx::core::<namespace>.

#include <cxcore/containers/map.hpp>
#include <cxcore/containers/object.hpp>
#include <cxcore/containers/variant.hpp>
#include <cxcore/containers/variant_map.hpp>
#include <cxcore/events/event.hpp>
#include <cxcore/testing/run_all.hpp>
#include <cxcore/threading/async_executor.hpp>
#include <cxcore/threading/async_signal.hpp>
#include <cxcore/threading/signal.hpp>
#include <cxcore/threading/task.hpp>
#include <cxcore/threading/task_priority.hpp>
#include <cxcore/threading/thread_pool.hpp>

#include <type_traits>

static_assert(std::is_same_v<cx::core::thread_pool, cx::core::threading::thread_pool>);
static_assert(std::is_same_v<cx::core::task, cx::core::threading::task>);
static_assert(std::is_same_v<cx::core::task_priority, cx::core::threading::task_priority>);
static_assert(std::is_same_v<cx::core::async_executor, cx::core::threading::async_executor>);
static_assert(std::is_same_v<cx::core::concurrency, cx::core::threading::concurrency>);
static_assert(std::is_same_v<cx::core::signal<int>, cx::core::signals::signal<int>>);
static_assert(std::is_same_v<cx::core::async_signal<int>, cx::core::signals::async_signal<int>>);
static_assert(std::is_same_v<cx::core::event<int>, cx::core::events::event<int>>);
static_assert(std::is_same_v<cx::core::variant, cx::core::containers::variant>);
static_assert(std::is_same_v<cx::core::variant_map, cx::core::containers::variant_map>);
static_assert(std::is_same_v<cx::core::map, cx::core::containers::map>);
static_assert(std::is_same_v<cx::core::object, cx::core::containers::object>);
