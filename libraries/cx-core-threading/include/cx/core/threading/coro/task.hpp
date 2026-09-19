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
/// @brief cx::core::threading::coro::task - a lazy C++20 coroutine type, and
/// the vocabulary that drives it (spawn / sync_wait / resume_on).
///
/// `task<T>` is lazy (nothing runs until it is awaited or spawned), moves its
/// coroutine frame, and reports its result or exception through
/// `co_await` / `sync_wait`. Awaiting one uses symmetric transfer, so a chain
/// of `co_await`ing tasks does not grow the stack.
///
/// Every task has a *home* cx::core::threading::async_executor - set by
/// spawn() / sync_wait(), inherited by children, changed by
/// `co_await resume_on(other)`. Awaiters that complete from another thread
/// (socket readiness, offloaded work) resume the coroutine back on its home
/// executor, so a task stays on one thread between explicit hops.

#include <coroutine>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <cx/core/threading/async_executor.hpp>

namespace cx::core::threading::coro {

template <typename T> class task;

namespace detail {

/// @brief Non-templated promise state shared by `task<T>` and `task<void>`.
struct promise_base {
  std::coroutine_handle<> continuation_{};
  std::shared_ptr<async_executor> executor_{};
  std::exception_ptr error_{};

  std::suspend_always initial_suspend() noexcept { return {}; }
  void unhandled_exception() noexcept { error_ = std::current_exception(); }

  /// @brief On completion, symmetric-transfer to whoever is awaiting us.
  struct final_awaiter {
    bool await_ready() const noexcept { return false; }
    template <typename promise_type>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> self) noexcept {
      std::coroutine_handle<> cont = self.promise().continuation_;
      return cont ? cont : std::noop_coroutine();
    }
    void await_resume() const noexcept {}
  };
  final_awaiter final_suspend() noexcept { return {}; }
};

template <typename T> struct promise final : promise_base {
  std::optional<T> value_;
  task<T> get_return_object();
  template <typename U = T> void return_value(U &&value) {
    value_.emplace(std::forward<U>(value));
  }
  T &&result() {
    if (error_) {
      std::rethrow_exception(error_);
    }
    return std::move(*value_);
  }
};

template <> struct promise<void> final : promise_base {
  task<void> get_return_object();
  void return_void() noexcept {}
  void result() {
    if (error_) {
      std::rethrow_exception(error_);
    }
  }
};

/// @brief Awaiter for `co_await task<T>`: starts the task and hooks the
/// awaiting coroutine as its continuation.
template <typename T> struct task_awaiter {
  std::coroutine_handle<promise<T>> coro;
  bool await_ready() const noexcept { return !coro || coro.done(); }
  template <typename caller_promise>
  std::coroutine_handle<> await_suspend(std::coroutine_handle<caller_promise> caller) noexcept {
    coro.promise().continuation_ = caller;
    if constexpr (requires { caller.promise().executor_; }) {
      if (!coro.promise().executor_) {
        coro.promise().executor_ = caller.promise().executor_;
      }
    }
    return coro;
  }
  decltype(auto) await_resume() { return coro.promise().result(); }
};

/// @brief Awaiter for resume_on(): rebinds the home executor and reschedules.
struct resume_on_awaiter {
  std::shared_ptr<async_executor> exec;
  bool await_ready() const noexcept { return false; }
  template <typename promise_type>
  void await_suspend(std::coroutine_handle<promise_type> h) const {
    if constexpr (requires { h.promise().executor_; }) {
      h.promise().executor_ = exec;
    }
    auto e = exec;
    e->defer([h] { h.resume(); });
  }
  void await_resume() const noexcept {}
};

/// @brief Awaiter for yield(): reschedule on the current home executor.
struct yield_awaiter {
  bool await_ready() const noexcept { return false; }
  template <typename promise_type>
  void await_suspend(std::coroutine_handle<promise_type> h) const {
    h.promise().executor_->defer([h] { h.resume(); });
  }
  void await_resume() const noexcept {}
};

/// @brief The fire-and-forget wrapper spawn() schedules. `suspend_always` at
/// entry (so spawn controls the first resume), `suspend_never` at exit (so it
/// destroys its own frame).
struct detached_task {
  struct promise_type {
    detached_task get_return_object() {
      return detached_task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {
      try {
        std::rethrow_exception(std::current_exception());
      } catch (const std::exception &e) {
        std::cerr << "cx::core::threading::coro::spawn: task threw: " << e.what() << '\n';
      } catch (...) {
        std::cerr << "cx::core::threading::coro::spawn: task threw a non-exception value\n";
      }
    }
  };
  std::coroutine_handle<promise_type> handle;
};

} // namespace detail

/// @ingroup core
/// @brief A lazy coroutine returning @p T (default `void`). Move-only; the
/// frame is destroyed with the last owner.
template <typename T = void> class task {
public:
  using promise_type = detail::promise<T>;

  task(task &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  task &operator=(task &&other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }
  task(const task &) = delete;
  task &operator=(const task &) = delete;
  ~task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  /// @brief Await the task: starts it (symmetric transfer) and, on
  /// completion, resumes the awaiting coroutine with the value or a rethrown
  /// exception.
  detail::task_awaiter<T> operator co_await() && noexcept {
    return detail::task_awaiter<T>{handle_};
  }

  /// @brief Whether the task has run to completion.
  bool done() const noexcept { return handle_ && handle_.done(); }

  /// @brief The underlying coroutine handle (non-owning view).
  std::coroutine_handle<promise_type> handle() const noexcept { return handle_; }

  /// @brief Relinquish ownership of the frame to the caller (used by spawn()).
  std::coroutine_handle<promise_type> release() noexcept {
    return std::exchange(handle_, {});
  }

private:
  friend struct detail::promise<T>;
  explicit task(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}
  std::coroutine_handle<promise_type> handle_{};
};

namespace detail {
template <typename T> inline task<T> promise<T>::get_return_object() {
  return task<T>{std::coroutine_handle<promise<T>>::from_promise(*this)};
}
inline task<void> promise<void>::get_return_object() {
  return task<void>{std::coroutine_handle<promise<void>>::from_promise(*this)};
}

template <typename T> inline detached_task run_detached(task<T> body) {
  co_await std::move(body);
}
} // namespace detail

/// @ingroup core
/// @brief Start @p body on @p exec, fire-and-forget. The task's home executor
/// is @p exec; an escaping exception is caught and logged to `stderr`.
template <typename T>
void spawn(std::shared_ptr<async_executor> exec, task<T> body) {
  body.handle().promise().executor_ = exec;
  detail::detached_task d = detail::run_detached(std::move(body));
  std::coroutine_handle<detail::detached_task::promise_type> h = d.handle;
  exec->defer([h] { h.resume(); });
}

/// @ingroup core
/// @brief Awaitable that moves the rest of the current coroutine onto @p exec
/// (and makes it the new home executor).
inline detail::resume_on_awaiter resume_on(std::shared_ptr<async_executor> exec) {
  return detail::resume_on_awaiter{std::move(exec)};
}

/// @ingroup core
/// @brief Awaitable cooperative yield: reschedule on the current home
/// executor, letting other queued work run first.
inline detail::yield_awaiter yield() { return {}; }

namespace detail {
template <typename T>
detached_task sync_runner(task<T> body, std::optional<T> *out, std::exception_ptr *err,
                          std::shared_ptr<async_executor> stop_target) {
  try {
    out->emplace(co_await std::move(body));
  } catch (...) {
    *err = std::current_exception();
  }
  stop_target->stop();
}

inline detached_task sync_runner_void(task<void> body, std::exception_ptr *err,
                                      std::shared_ptr<async_executor> stop_target) {
  try {
    co_await std::move(body);
  } catch (...) {
    *err = std::current_exception();
  }
  stop_target->stop();
}
} // namespace detail

/// @ingroup core
/// @brief Run @p body to completion on @p exec, blocking the calling thread,
/// and return its value (or rethrow its exception).
///
/// @p exec must expose a blocking `run()` that returns once `stop()` is
/// called and a `schedule(std::coroutine_handle<>)` -
/// cx::core::threading::coroutine_executor does. Use it for compute-only
/// tasks and tests; a task that suspends on socket readiness needs its
/// executor pumped by the owning cx::core::io::event_loop instead.
template <typename executor_type, typename T>
T sync_wait(std::shared_ptr<executor_type> exec, task<T> body) {
  body.handle().promise().executor_ = exec;
  std::exception_ptr err;
  exec->start();
  if constexpr (std::is_void_v<T>) {
    detail::detached_task d = detail::sync_runner_void(std::move(body), &err, exec);
    exec->schedule(d.handle);
    exec->run();
    if (err) {
      std::rethrow_exception(err);
    }
  } else {
    std::optional<T> out;
    detail::detached_task d = detail::sync_runner(std::move(body), &out, &err, exec);
    exec->schedule(d.handle);
    exec->run();
    if (err) {
      std::rethrow_exception(err);
    }
    return std::move(*out);
  }
}

} // namespace cx::core::threading::coro
