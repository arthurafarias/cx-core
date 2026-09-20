# cx-core

The infrastructure shared by cx-flow, cx-networking, cx-algebra and agenticx-ncortex, split by functionality.
Every `libraries/cx-core-<namespace>` is its own header-only CMake target, in namespace `cx::core::<namespace>`,
with headers under `cx/core/<namespace>/`. Link one, or all of them through `cxcore::cxcore`.

| library | what it is |
|---|---|
| [cx-core-testing](libraries/cx-core-testing) | self-hosted test facility |
| [cx-core-threading](libraries/cx-core-threading) | executors, thread_pool, task, parallel_for, future, coroutines |
| [cx-core-signals](libraries/cx-core-signals) | signal, async_signal |
| [cx-core-events](libraries/cx-core-events) | event |
| [cx-core-containers](libraries/cx-core-containers) | variant, map, object |
| [cx-core-io](libraries/cx-core-io) | event_loop reactor, poller, descriptor |
| [cx-core-serialization](libraries/cx-core-serialization) | text escapers, base64, utf8 |
| [cx-core-log](libraries/cx-core-log) | journal and its serializers |
| [cx-core-patterns](libraries/cx-core-patterns) | factory |
| [cx-core-filesystem](libraries/cx-core-filesystem) | file_lock, atomic_replace |
| [cx-core-process](libraries/cx-core-process) | run, run_captured, which |

`include/cxcore/` holds the pre-split paths. They only forward to `cx/core/<namespace>/` and re-export the flat
`cx::core` names, so code written before the split keeps compiling; `tests/compat.cpp` locks that surface.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build && ctest --test-dir build
```

## Benchmarks

```bash
cmake -S . -B build -DCXCORE_BUILD_BENCHMARKS=ON && cmake --build build --target cx-core-benchmark-dispatch
./build/cx-core-benchmark-dispatch [repetitions=9] [scale=1.0]
```

Every dispatch mechanism next to its STL-only equivalent: synchronous events, fire-and-forget calls, calls
returning a value, asynchronous signal dispatch, a coroutine suspended on a signal, and fork-join over all cores.
