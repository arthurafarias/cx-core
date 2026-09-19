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
/// @brief POSIX backend for cx::core::io::poller (SRS-019 §4).
///
/// One file, both readiness engines. `state` picks `epoll(7)` on Linux and
/// `poll(2)` everywhere else at construction; `CX_CORE_IO_BACKEND=poll`
/// / `=epoll` in the environment (or the older `CX_NETWORKING_IO_BACKEND`) forces one (an unknown or unavailable value
/// is ignored and the automatic choice stands). The engine is an internal
/// detail - `event_loop` and the façade never name it. This whole
/// selection only exists here, under `CX_CORE_BACKEND_POSIX`.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/epoll.h>
#endif

#include <cx/core/io/poller_events.hpp>

namespace cx::core::io::poller::impl::posix {

/// @brief Which readiness syscall family `state` is driving.
enum class engine {
  poll,  ///< `poll(2)` - the portable O(n) default.
  epoll, ///< `epoll(7)` - Linux only, O(ready).
};

/// @brief The callback fired for a ready descriptor.
using callback = std::function<void(io_event)>;

/// @brief One registered watch: its interest mask and callback.
struct entry {
  io_event interest = io_event::none;
  callback cb;
};

namespace detail {

inline short to_poll(io_event e) {
  short out = 0;
  if (any(e & io_event::readable)) {
    out |= POLLIN;
  }
  if (any(e & io_event::writable)) {
    out |= POLLOUT;
  }
  return out;
}

inline io_event from_poll(short revents) {
  io_event out = io_event::none;
  if (revents & POLLIN) {
    out |= io_event::readable;
  }
  if (revents & POLLOUT) {
    out |= io_event::writable;
  }
  if (revents & (POLLERR | POLLNVAL)) {
    out |= io_event::error;
  }
  if (revents & POLLHUP) {
    out |= io_event::hangup;
  }
  return out;
}

/// @brief The engine the environment forces, if any and if usable.
inline std::optional<engine> forced_engine() {
  const char *want = std::getenv("CX_CORE_IO_BACKEND");
  if (want == nullptr) {
    want = std::getenv("CX_NETWORKING_IO_BACKEND"); // the pre-cx-core name
  }
  if (want == nullptr) {
    return std::nullopt;
  }
  std::string_view v(want);
  if (v == "poll") {
    return engine::poll;
  }
#if defined(__linux__)
  if (v == "epoll") {
    return engine::epoll;
  }
#endif
  return std::nullopt;
}

/// @brief The engine chosen when nothing forces one: `epoll` on Linux.
inline engine automatic_engine() {
#if defined(__linux__)
  return engine::epoll;
#else
  return engine::poll;
#endif
}

#if defined(__linux__)
inline std::uint32_t to_epoll(io_event e) {
  std::uint32_t out = 0;
  if (any(e & io_event::readable)) {
    out |= EPOLLIN;
  }
  if (any(e & io_event::writable)) {
    out |= EPOLLOUT;
  }
  return out;
}

inline io_event from_epoll(std::uint32_t revents) {
  io_event out = io_event::none;
  if (revents & EPOLLIN) {
    out |= io_event::readable;
  }
  if (revents & EPOLLOUT) {
    out |= io_event::writable;
  }
  if (revents & EPOLLERR) {
    out |= io_event::error;
  }
  if (revents & (EPOLLHUP | EPOLLRDHUP)) {
    out |= io_event::hangup;
  }
  return out;
}
#endif

} // namespace detail

/// @ingroup core
/// @brief The poll set plus its internal cross-thread wake channel. Owns OS
/// resources and a mutex - non-copyable and non-movable; `event_loop` holds
/// it as a direct data member.
class state {
public:
  /// @brief Build the poll set, its wake `eventfd`, and (for `epoll`) the
  /// `epoll` instance. @p forced overrides the automatic / environment
  /// choice - used by the tests to exercise each engine.
  explicit state(std::optional<engine> forced = std::nullopt) {
    engine_ = forced ? *forced : detail::forced_engine().value_or(detail::automatic_engine());
    wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#if defined(__linux__)
    if (engine_ == engine::epoll) {
      epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
      epoll_event ev{};
      ev.events = EPOLLIN;
      ev.data.fd = wake_fd_;
      ::epoll_ctl(epfd_, EPOLL_CTL_ADD, wake_fd_, &ev);
    }
#endif
  }

  ~state() {
    if (wake_fd_ >= 0) {
      ::close(wake_fd_);
    }
#if defined(__linux__)
    if (epfd_ >= 0) {
      ::close(epfd_);
    }
#endif
  }

  state(const state &) = delete;
  state &operator=(const state &) = delete;
  state(state &&) = delete;
  state &operator=(state &&) = delete;

  /// @brief `"poll"` / `"epoll"`.
  std::string_view engine_name() const noexcept {
    return engine_ == engine::epoll ? "epoll" : "poll";
  }

  void add(int fd, io_event interest, callback cb) {
    {
      std::unique_lock lock(mutex_);
      bool existed = entries_.contains(fd);
      entries_[fd] = entry{interest, std::move(cb)};
#if defined(__linux__)
      if (engine_ == engine::epoll) {
        epoll_event ev{};
        ev.events = detail::to_epoll(interest);
        ev.data.fd = fd;
        ::epoll_ctl(epfd_, existed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, fd, &ev);
      }
#else
      (void)existed;
#endif
    }
    interrupt();
  }

  void modify(int fd, io_event interest) {
    {
      std::unique_lock lock(mutex_);
      auto it = entries_.find(fd);
      if (it == entries_.end()) {
        return;
      }
      it->second.interest = interest;
#if defined(__linux__)
      if (engine_ == engine::epoll) {
        epoll_event ev{};
        ev.events = detail::to_epoll(interest);
        ev.data.fd = fd;
        ::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
      }
#endif
    }
    interrupt();
  }

  void remove(int fd) {
    {
      std::unique_lock lock(mutex_);
      if (entries_.erase(fd) == 0) {
        return;
      }
#if defined(__linux__)
      if (engine_ == engine::epoll) {
        ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
      }
#endif
    }
    interrupt();
  }

  std::size_t dispatch(std::chrono::milliseconds timeout) {
#if defined(__linux__)
    if (engine_ == engine::epoll) {
      return dispatch_epoll(timeout);
    }
#endif
    return dispatch_poll(timeout);
  }

  void interrupt() {
    std::uint64_t one = 1;
    [[maybe_unused]] auto written = ::write(wake_fd_, &one, sizeof(one));
  }

private:
  std::size_t dispatch_poll(std::chrono::milliseconds timeout) {
    std::vector<pollfd> fds;
    {
      std::unique_lock lock(mutex_);
      fds.reserve(entries_.size() + 1);
      fds.push_back(pollfd{wake_fd_, POLLIN, 0});
      for (const auto &[fd, e] : entries_) {
        fds.push_back(pollfd{fd, detail::to_poll(e.interest), 0});
      }
    }

    int ms = timeout.count() < 0 ? -1 : static_cast<int>(timeout.count());
    int n = ::poll(fds.data(), fds.size(), ms);
    if (n <= 0) {
      return 0; // timeout, EINTR, or transient - just re-enter
    }

    if (fds[0].revents & POLLIN) {
      drain_wake();
    }

    std::size_t dispatched = 0;
    for (std::size_t i = 1; i < fds.size(); ++i) {
      if (fds[i].revents == 0) {
        continue;
      }
      callback cb;
      {
        std::unique_lock lock(mutex_);
        auto it = entries_.find(fds[i].fd);
        if (it == entries_.end()) {
          continue; // removed by an earlier callback this cycle
        }
        cb = it->second.cb;
      }
      cb(detail::from_poll(fds[i].revents));
      ++dispatched;
    }
    return dispatched;
  }

#if defined(__linux__)
  std::size_t dispatch_epoll(std::chrono::milliseconds timeout) {
    constexpr int kMaxEvents = 128;
    epoll_event events[kMaxEvents];
    int ms = timeout.count() < 0 ? -1 : static_cast<int>(timeout.count());
    int n = ::epoll_wait(epfd_, events, kMaxEvents, ms);
    if (n <= 0) {
      return 0; // timeout or EINTR
    }

    std::size_t dispatched = 0;
    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      if (fd == wake_fd_) {
        drain_wake();
        continue;
      }
      callback cb;
      {
        std::unique_lock lock(mutex_);
        auto it = entries_.find(fd);
        if (it == entries_.end()) {
          continue; // removed by an earlier callback this cycle
        }
        cb = it->second.cb;
      }
      cb(detail::from_epoll(events[i].events));
      ++dispatched;
    }
    return dispatched;
  }
#endif

  void drain_wake() {
    std::uint64_t sink = 0;
    while (::read(wake_fd_, &sink, sizeof(sink)) > 0) {
    }
  }

  mutable std::mutex mutex_;
  std::unordered_map<int, entry> entries_;
  engine engine_ = engine::poll;
  int wake_fd_ = -1;
  int epfd_ = -1;
};

// --- free-function surface the façade forwards to -----------------------

inline void add(state &s, int fd, io_event interest, callback cb) { s.add(fd, interest, std::move(cb)); }
inline void modify(state &s, int fd, io_event interest) { s.modify(fd, interest); }
inline void remove(state &s, int fd) { s.remove(fd); }
inline std::size_t dispatch(state &s, std::chrono::milliseconds timeout) { return s.dispatch(timeout); }
inline void interrupt(state &s) { s.interrupt(); }
inline std::string_view backend_name(const state &s) { return s.engine_name(); }

} // namespace cx::core::io::poller::impl::posix
