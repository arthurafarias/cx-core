# cx-core-log

`journal`: levelled, `std::format`-checked logging with source location, dispatched through a signal.

Namespace `cx::core::log`, headers under `cx/core/log/`. Header-only.

## Headers

- `journal.hpp` — `journal::info/warn/debug/error`, `level_set`, `set_serializer`, `emit_async`
- `journal_entry.hpp`, `journal_stream.hpp`
- `bunyan.hpp` — `bunyan::info() << "text" << bunyan::field("key", value)`: one Bunyan JSON line per record on
  stderr, minimum level from `CX_LOG_LEVEL` or `configuration()`; what agenticx-ncortex's commands log with
- `journal_serializer.hpp` — plain, JSON, XML and CSV serializers, selectable per `ostream`

One journal per process: every library logging through it shares its level and sink.

## Depends on

cx-core-signals, cx-core-serialization.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-log CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-log::cx-core-log)
```

Unit tests are colocated under `include/cx/core/log/testing/`, compiled into `cxcore-tests`, never installed.
