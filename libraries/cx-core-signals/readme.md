# cx-core-signals

Thread-safe signals: connect slots, emit synchronously in connection order, or hand each slot to an executor.

Namespace `cx::core::signals`, headers under `cx/core/signals/`. Header-only.

## Headers

- `signal.hpp` — `signal<args...>`, `connection`, `scoped_connection`, `emit` / `emit_async(thread_pool&)`
- `async_signal.hpp` — the same over any `async_executor` (`thread_pool`, `event_loop`, `coroutine_executor`)

## Depends on

cx-core-threading.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-signals CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-signals::cx-core-signals)
```

Unit tests are colocated under `include/cx/core/signals/testing/`, compiled into `cxcore-tests`, never installed.
