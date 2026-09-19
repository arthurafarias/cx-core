#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include <cx/core/threading/async_executor.hpp>
#include <cx/core/signals/signal.hpp>

namespace cx::core::signals {

template <typename... args_types> class async_signal {
public:
  using slot_type = typename signal<args_types...>::slot_type;
  using connection = typename signal<args_types...>::connection;
  using scoped_connection = typename signal<args_types...>::scoped_connection;

  connection connect(slot_type slot) { return signal_.connect(std::move(slot)); }
  async_signal &operator+=(slot_type slot) { signal_.connect(std::move(slot)); return *this; }
  void disconnect_all() { signal_.disconnect_all(); }
  std::size_t slot_count() const { return signal_.slot_count(); }
  void emit(args_types... args) const { signal_.emit(args...); }
  void operator()(args_types... args) const { emit(args...); }

  void emit_async(std::shared_ptr<threading::async_executor> exec, args_types... args) const {
    static_assert((!std::is_reference_v<args_types> && ...),
                  "async_signal cannot defer reference arguments");
    for (auto &slot : signal_.connected_slots())
      exec->defer([slot, args...] { slot(args...); });
  }

  void emit_async(std::shared_ptr<threading::async_executor> exec, threading::task_priority priority,
                  args_types... args) const {
    static_assert((!std::is_reference_v<args_types> && ...),
                  "async_signal cannot defer reference arguments");
    for (auto &slot : signal_.connected_slots())
      exec->offload([slot, args...] { slot(args...); }, priority);
  }

  signal<args_types...> &sync() { return signal_; }
  const signal<args_types...> &sync() const { return signal_; }

private:
  signal<args_types...> signal_;
};

} // namespace cx::core::signals
