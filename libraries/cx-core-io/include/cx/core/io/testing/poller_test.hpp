// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <cx/core/io/poller.hpp>
#include <cx/core/testing/test_group.hpp>

namespace cx::core::testing {

namespace poller = cx::core::io::poller;
using cx::core::io::io_event;

/// @brief A write on one end of a socketpair must drive the registered
/// callback with a `readable` result, and remove() must stop it.
inline void poller_readiness_case(test_context &ctx, poller::state &p) {
  int sv[2];
  ctx.require(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

  std::atomic<int> hits{0};
  io_event last = io_event::none;
  poller::add(p, sv[0], io_event::readable, [&](io_event revents) {
    last = revents;
    hits.fetch_add(1);
  });

  char b = 'x';
  ctx.check(::write(sv[1], &b, 1) == 1, "write to the pair");

  std::size_t dispatched = poller::dispatch(p, std::chrono::milliseconds(500));
  ctx.check_equal(dispatched, std::size_t{1}, "one fd dispatched");
  ctx.check_equal(hits.load(), 1, "callback ran once");
  ctx.check(cx::core::io::any(last & io_event::readable), "result carries readable");

  ctx.check(::read(sv[0], &b, 1) == 1, "drain the byte");
  poller::remove(p, sv[0]);
  ctx.check(::write(sv[1], &b, 1) == 1, "second write");
  poller::dispatch(p, std::chrono::milliseconds(50));
  ctx.check_equal(hits.load(), 1, "no callback after remove()");

  ::close(sv[0]);
  ::close(sv[1]);
}

/// @brief interrupt() must break a blocking dispatch() on another thread.
inline void poller_interrupt_case(test_context &ctx, poller::state &p) {
  std::atomic<bool> returned{false};
  std::thread thread([&] {
    poller::dispatch(p, std::chrono::milliseconds(-1)); // block indefinitely
    returned.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ctx.check(!returned.load(), "dispatch is still blocked before interrupt()");
  poller::interrupt(p);

  for (int i = 0; i < 100 && !returned.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ctx.check(returned.load(), "interrupt() released the blocked dispatch()");
  thread.join();
}

struct poller_test : public test_group {
  poller_test()
      : test_group(
            "core::poller",
            {
                {"io_event flags combine and test with the bitwise operators",
                 [](test_context &ctx) {
                   io_event both = io_event::readable | io_event::writable;
                   ctx.check(cx::core::io::any(both & io_event::readable), "readable is set");
                   ctx.check(cx::core::io::any(both & io_event::writable), "writable is set");
                   ctx.check(!cx::core::io::any(both & io_event::error), "error is not set");
                   both &= ~io_event::writable;
                   ctx.check(!cx::core::io::any(both & io_event::writable), "writable cleared");
                   ctx.check(cx::core::io::any(both & io_event::readable), "readable still set");
                 }},
                {"the default poller delivers readiness and honours remove()",
                 [](test_context &ctx) {
                   poller::state p;
                   poller_readiness_case(ctx, p);
                 }},
                {"the default poller's interrupt() unblocks dispatch()",
                 [](test_context &ctx) {
                   poller::state p;
                   poller_interrupt_case(ctx, p);
                 }},
#if CX_CORE_BACKEND_POSIX
                {"the posix poll engine delivers readiness",
                 [](test_context &ctx) {
                   poller::state p{poller::backend::engine::poll};
                   ctx.check_equal(std::string(poller::backend_name(p)), std::string("poll"), "engine name");
                   poller_readiness_case(ctx, p);
                 }},
                {"the posix poll engine's interrupt() unblocks dispatch()",
                 [](test_context &ctx) {
                   poller::state p{poller::backend::engine::poll};
                   poller_interrupt_case(ctx, p);
                 }},
#if defined(__linux__)
                {"the posix epoll engine delivers readiness",
                 [](test_context &ctx) {
                   poller::state p{poller::backend::engine::epoll};
                   ctx.check_equal(std::string(poller::backend_name(p)), std::string("epoll"), "engine name");
                   poller_readiness_case(ctx, p);
                 }},
                {"the posix epoll engine's interrupt() unblocks dispatch()",
                 [](test_context &ctx) {
                   poller::state p{poller::backend::engine::epoll};
                   poller_interrupt_case(ctx, p);
                 }},
#endif
#endif
            }) {}
};

inline static poller_test poller_test_instance;

} // namespace cx::core::testing
