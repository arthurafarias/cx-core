#pragma once

#include <functional>
#include <string_view>

#include <cxcore/threading/task_priority.hpp>

namespace cx::core {

enum class concurrency { serialized, concurrent };

class async_executor {
public:
  using task = std::function<void()>;

  virtual ~async_executor() = default;
  virtual void defer(task fn) = 0;
  virtual void offload(task fn, task_priority priority = task_priority::normal) = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual bool running() const = 0;
  virtual std::string_view name() const noexcept = 0;
  virtual concurrency model() const noexcept = 0;

protected:
  async_executor() = default;
  async_executor(const async_executor &) = delete;
  async_executor &operator=(const async_executor &) = delete;
};

} // namespace cx::core
