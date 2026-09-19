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
/// @brief cx::core::threading::future / cx::core::threading::promise - a
/// minimal single-consumer completion token for the protocol layers.
///
/// This is not `std::future`: there is no blocking `get()` and no shared
/// state across a thread boundary by default. A `promise<T>` is fulfilled
/// once with `set_value()`; the matching `future<T>` takes exactly one
/// continuation via `then()`, invoked with the value. If the value is
/// already present when `then()` is called, the continuation runs straight
/// away; otherwise it runs from `set_value()`.
///
/// Producers that resolve from another thread (an offloaded hook) hand the
/// promise an cx::core::threading::async_executor at construction: the
/// continuation is then dispatched through `executor->defer()` so it always
/// runs in the executor's context (the loop thread), never on the resolver's.
///
/// Used for MQTT publish/subscribe acknowledgements (SRS-008 §5.3, §5.4) and
/// the broker's blocking hook points (§6.4).

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <cx/core/threading/async_executor.hpp>

namespace cx::core::threading {

template <typename value_type> class promise;
template <typename value_type> class future;

namespace detail {

/// @brief Shared state behind one promise/future pair.
template <typename value_type> struct future_state {
  std::mutex mutex;
  std::optional<value_type> value;
  std::function<void(value_type)> continuation;
  std::shared_ptr<async_executor> executor;
  bool delivered = false;

  /// @brief Fire the continuation once both halves are present.
  void deliver() {
    std::function<void(value_type)> cont;
    value_type snapshot;
    {
      std::unique_lock lock(mutex);
      if (delivered || !value || !continuation) {
        return;
      }
      delivered = true;
      cont = std::move(continuation);
      continuation = nullptr;
      snapshot = *value;
    }
    if (executor) {
      executor->defer([cont = std::move(cont), snapshot = std::move(snapshot)]() mutable { cont(std::move(snapshot)); });
    } else {
      cont(std::move(snapshot));
    }
  }
};

/// @brief `void` specialisation: completion carries no value.
template <> struct future_state<void> {
  std::mutex mutex;
  bool ready = false;
  std::function<void()> continuation;
  std::shared_ptr<async_executor> executor;
  bool delivered = false;

  void deliver() {
    std::function<void()> cont;
    {
      std::unique_lock lock(mutex);
      if (delivered || !ready || !continuation) {
        return;
      }
      delivered = true;
      cont = std::move(continuation);
      continuation = nullptr;
    }
    if (executor) {
      executor->defer(std::move(cont));
    } else {
      cont();
    }
  }
};

} // namespace detail

/// @ingroup core
/// @brief The read side of a completion token. Takes exactly one continuation.
template <typename value_type> class future {
public:
  future() = default;

  /// @brief Whether this future is bound to a promise.
  bool valid() const { return state_ != nullptr; }

  /// @brief Whether the value has already been produced.
  bool is_ready() const {
    if (!state_) {
      return false;
    }
    std::unique_lock lock(state_->mutex);
    return state_->value.has_value();
  }

  /// @brief Register the single continuation. Runs now if the value is
  /// already present, otherwise when the promise is fulfilled.
  /// @param fn Invoked once with the produced value.
  void then(std::function<void(value_type)> fn) {
    if (!state_) {
      return;
    }
    {
      std::unique_lock lock(state_->mutex);
      state_->continuation = std::move(fn);
    }
    state_->deliver();
  }

private:
  friend class promise<value_type>;
  explicit future(std::shared_ptr<detail::future_state<value_type>> state) : state_(std::move(state)) {}
  std::shared_ptr<detail::future_state<value_type>> state_;
};

/// @ingroup core
/// @brief `void` future: `then()` takes a nullary continuation.
template <> class future<void> {
public:
  future() = default;
  bool valid() const { return state_ != nullptr; }
  bool is_ready() const {
    if (!state_) {
      return false;
    }
    std::unique_lock lock(state_->mutex);
    return state_->ready;
  }
  void then(std::function<void()> fn) {
    if (!state_) {
      return;
    }
    {
      std::unique_lock lock(state_->mutex);
      state_->continuation = std::move(fn);
    }
    state_->deliver();
  }

private:
  friend class promise<void>;
  explicit future(std::shared_ptr<detail::future_state<void>> state) : state_(std::move(state)) {}
  std::shared_ptr<detail::future_state<void>> state_;
};

/// @ingroup core
/// @brief The write side of a completion token. Fulfilled once.
template <typename value_type> class promise {
public:
  /// @brief Construct with an inline continuation dispatch.
  promise() : state_(std::make_shared<detail::future_state<value_type>>()) {}

  /// @brief Construct so the continuation is dispatched through @p executor
  /// (use when `set_value()` may be called from another thread).
  explicit promise(std::shared_ptr<async_executor> executor)
      : state_(std::make_shared<detail::future_state<value_type>>()) {
    state_->executor = std::move(executor);
  }

  /// @brief The matching future. Call once.
  future<value_type> get_future() { return future<value_type>(state_); }

  /// @brief Produce the value. The first call wins; later calls are ignored.
  void set_value(value_type v) {
    {
      std::unique_lock lock(state_->mutex);
      if (state_->value) {
        return;
      }
      state_->value = std::move(v);
    }
    state_->deliver();
  }

  /// @brief Whether set_value() has been called.
  bool fulfilled() const {
    std::unique_lock lock(state_->mutex);
    return state_->value.has_value();
  }

private:
  std::shared_ptr<detail::future_state<value_type>> state_;
};

/// @ingroup core
/// @brief `void` promise: `set_value()` takes no argument.
template <> class promise<void> {
public:
  promise() : state_(std::make_shared<detail::future_state<void>>()) {}
  explicit promise(std::shared_ptr<async_executor> executor) : state_(std::make_shared<detail::future_state<void>>()) {
    state_->executor = std::move(executor);
  }
  future<void> get_future() { return future<void>(state_); }
  void set_value() {
    {
      std::unique_lock lock(state_->mutex);
      if (state_->ready) {
        return;
      }
      state_->ready = true;
    }
    state_->deliver();
  }
  bool fulfilled() const {
    std::unique_lock lock(state_->mutex);
    return state_->ready;
  }

private:
  std::shared_ptr<detail::future_state<void>> state_;
};

/// @ingroup core
/// @brief A future that is already complete with @p v.
template <typename value_type> future<value_type> make_ready_future(value_type v) {
  promise<value_type> p;
  p.set_value(std::move(v));
  return p.get_future();
}

/// @ingroup core
/// @brief A `void` future that is already complete.
inline future<void> make_ready_future() {
  promise<void> p;
  p.set_value();
  return p.get_future();
}

} // namespace cx::core::threading
