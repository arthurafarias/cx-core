#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <cx/core/signals/signal.hpp>

namespace cx::core::events {

template <typename... args_types> class event {
public:
  using listener = std::function<void(args_types...)>;
  using subscription = typename signals::signal<args_types...>::connection;

  event &operator+=(listener fn) {
    signal_.connect(std::move(fn));
    return *this;
  }

  subscription once(listener fn) {
    auto slot = std::make_shared<subscription>();
    *slot = signal_.connect([slot, fn = std::move(fn)](args_types... args) {
      slot->disconnect();
      fn(args...);
    });
    return *slot;
  }

  void off_all() { signal_.disconnect_all(); }
  std::size_t listener_count() const { return signal_.slot_count(); }
  void emit(args_types... args) const { signal_.emit(args...); }

private:
  signals::signal<args_types...> signal_;
};

} // namespace cx::core::events
