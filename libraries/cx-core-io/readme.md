# cx-core-io

The reactor: one thread, readiness callbacks, next-tick queue and timers, over poll(2) or epoll(7).

Namespace `cx::core::io`, headers under `cx/core/io/`. Header-only.

## Headers

- `event_loop.hpp` — `watch` / `unwatch`, `defer`, `offload`, `set_timeout`; a `serialized` `async_executor`
- `poller.hpp`, `poller_events.hpp`, `descriptor.hpp`, `io_result.hpp` — backend façades; `impl/posix/` behind them
- `config.hpp` — `CX_CORE_BACKEND_POSIX` / `_STANDALONE` (the `CX_NET_BACKEND_*` spellings still work)
- `coro/sleep.hpp`, `coro/run.hpp` — `co_await sleep(loop, 10ms)`; pump a `coroutine_executor` from a loop

`CX_CORE_IO_BACKEND=poll|epoll` forces the engine at run time.

## Depends on

cx-core-threading.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-io CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-io::cx-core-io)
```

Unit tests are colocated under `include/cx/core/io/testing/`, compiled into `cxcore-tests`, never installed.
