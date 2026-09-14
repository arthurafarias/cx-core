// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include <cxcore/threading/thread_pool.hpp>

namespace cx::core {

// GObject/GStreamer-style signal: connect zero or more slots, emit invokes
// them all. No separate "sender" template parameter - a caller that wants
// GObject's "emitter passed as first arg" convention just includes
// `*this`/`this` as the first element of args_types, e.g.
//   signal<element&, state, state> state_changed;
//   state_changed(*this, old_state, new_state);
//
// emit() (the default, via operator()) is synchronous and ordered: it
// snapshots the connected slots under the lock, releases the lock, then
// invokes them in connection order on the calling thread. This is required
// for deterministic pad buffer-push and bus-message ordering, and matches
// real GObject signal semantics (g_signal_emit is synchronous). It is also
// what makes it safe for a slot to disconnect itself, or to connect/emit
// again, from within its own invocation - it is never iterating the live
// sink list, and never holds the lock while calling out.
//
// emit_async() is a separate, explicit opt-in for fire-and-forget dispatch
// through a thread_pool. It is unsound for reference-typed args (a deferred
// call may run after the referent is gone), so it is disabled at
// instantiation for any such signal.
template <typename... args_types> class signal {
private:
  struct control_block {
    std::mutex mutex;
    std::list<std::function<void(args_types...)>> sinks;
  };

public:
  using slot_type = std::function<void(args_types...)>;

  signal() : control_(std::make_shared<control_block>()) {}

  signal(const signal &) = delete;
  signal &operator=(const signal &) = delete;
  signal(signal &&) = delete;
  signal &operator=(signal &&) = delete;

  ~signal() { disconnect_all(); }

  // A live handle to one connected slot. Safe to disconnect() even after
  // the owning signal has been destroyed - the weak_ptr to the signal's
  // control block simply makes that a no-op instead of a use-after-free.
  class connection {
  public:
    connection() = default;

    void disconnect() {
      if (auto control = control_.lock(); control && valid_) {
        std::unique_lock lock(control->mutex);
        control->sinks.erase(it_);
      }
      valid_ = false;
    }

    bool connected() const { return valid_ && !control_.expired(); }

  private:
    friend class signal;

    connection(std::weak_ptr<control_block> control, typename std::list<slot_type>::iterator it)
        : control_(std::move(control)), it_(it), valid_(true) {}

    std::weak_ptr<control_block> control_;
    typename std::list<slot_type>::iterator it_{};
    bool valid_ = false;
  };

  // Move-only RAII wrapper: disconnects automatically when it goes out of
  // scope, so callers wiring up dynamic pad/bus connections don't need to
  // remember manual disconnect() discipline.
  class scoped_connection {
  public:
    scoped_connection() = default;
    explicit scoped_connection(connection c) : connection_(std::move(c)) {}

    scoped_connection(const scoped_connection &) = delete;
    scoped_connection &operator=(const scoped_connection &) = delete;

    scoped_connection(scoped_connection &&other) noexcept : connection_(std::move(other.connection_)) {
      other.connection_ = connection{};
    }

    scoped_connection &operator=(scoped_connection &&other) noexcept {
      if (this != &other) {
        connection_.disconnect();
        connection_ = std::move(other.connection_);
        other.connection_ = connection{};
      }
      return *this;
    }

    ~scoped_connection() { connection_.disconnect(); }

    void disconnect() { connection_.disconnect(); }
    bool connected() const { return connection_.connected(); }

  private:
    connection connection_;
  };

  connection connect(slot_type slot) {
    std::unique_lock lock(control_->mutex);
    auto it = control_->sinks.insert(control_->sinks.end(), std::move(slot));
    return connection(control_, it);
  }

  void disconnect_all() {
    std::unique_lock lock(control_->mutex);
    control_->sinks.clear();
  }

  void operator()(args_types... args) const { emit(args...); }

  void emit(args_types... args) const {
    for (const auto &slot : snapshot_sinks()) {
      slot(args...);
    }
  }

  void emit_async(thread_pool &pool, args_types... args) const {
    emit_async(pool, task_priority::normal, args...);
  }

  void emit_async(thread_pool &pool, task_priority priority, args_types... args) const {
    static_assert((!std::is_reference_v<args_types> && ...),
                  "signal::emit_async cannot safely capture reference-typed arguments "
                  "(the referent may not outlive the deferred call) - use emit() instead");
    for (const auto &slot : snapshot_sinks()) {
      pool.submit([slot, args...]() { slot(args...); }, priority);
    }
  }

  signal &operator+=(slot_type slot) {
    connect(std::move(slot));
    return *this;
  }

  std::size_t slot_count() const {
    std::unique_lock lock(control_->mutex);
    return control_->sinks.size();
  }

  std::vector<slot_type> connected_slots() const { return snapshot_sinks(); }

private:
  std::vector<slot_type> snapshot_sinks() const {
    std::unique_lock lock(control_->mutex);
    return std::vector<slot_type>(control_->sinks.begin(), control_->sinks.end());
  }

  std::shared_ptr<control_block> control_;
};

} // namespace cx::core
