// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

/// @file
/// @ingroup core
/// @brief cx::core::io::coro::sleep - an awaitable `setTimeout`.
///
/// `co_await coro::sleep(loop, 100ms)` suspends the coroutine and resumes it
/// on its home executor after the delay. Timers are a
/// cx::core::io::event_loop capability, so a loop is required.

#include <chrono>
#include <coroutine>
#include <memory>
#include <utility>

#include <cx/core/threading/coro/task.hpp>
#include <cx/core/io/event_loop.hpp>

namespace cx::core::io::coro {

namespace detail {

struct sleep_awaiter {
  std::shared_ptr<event_loop> loop;
  std::chrono::milliseconds delay;

  bool await_ready() const noexcept { return delay <= std::chrono::milliseconds::zero(); }

  template <typename promise_type>
  void await_suspend(std::coroutine_handle<promise_type> h) {
    std::shared_ptr<threading::async_executor> home = h.promise().executor_;
    if (!home) {
      home = loop;
    }
    loop->set_timeout(delay, [home, h] { home->defer([h] { h.resume(); }); });
  }

  void await_resume() const noexcept {}
};

} // namespace detail

/// @ingroup core
/// @brief Awaitable that resumes the coroutine after @p delay, timed on
/// @p loop and resumed on the coroutine's home executor.
inline detail::sleep_awaiter sleep(std::shared_ptr<event_loop> loop,
                                   std::chrono::milliseconds delay) {
  return detail::sleep_awaiter{std::move(loop), delay};
}

} // namespace cx::core::io::coro
