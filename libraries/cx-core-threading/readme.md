# cx-core-threading

Executors and the asynchronous vocabulary built on them.

Namespace `cx::core::threading`, headers under `cx/core/threading/`. Header-only.

## Headers

- `async_executor.hpp`, `task_priority.hpp` — the one scheduling surface (`defer` / `offload`)
- `thread_pool.hpp` — a priority queue of independent jobs on a fixed worker set
- `task.hpp` — a dedicated thread running a repeating loop (pause/resume/stop)
- `parallel_for.hpp` — lock-free fork-join over an index range; the shape compute kernels need
- `future.hpp` — `promise` / `future::then`, continuation dispatched inline or through an executor
- `coro/task.hpp`, `coroutine_executor.hpp` — lazy coroutines, `spawn` / `sync_wait` / `resume_on` / `yield`

`thread_pool` costs a few hundred nanoseconds a job; cut a kernel into chunks with `parallel_for`, not with
submissions (see `benchmarks/`, section F).

## Depends on

Threads (pthreads).

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-threading CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-threading::cx-core-threading)
```

Unit tests are colocated under `include/cx/core/threading/testing/`, compiled into `cxcore-tests`, never installed.
