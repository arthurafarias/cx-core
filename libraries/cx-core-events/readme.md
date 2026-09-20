# cx-core-events

Node-style events over a signal: `+=`, `once`, `off_all`, `emit`.

Namespace `cx::core::events`, headers under `cx/core/events/`. Header-only.

## Headers

- `event.hpp` — `event<args...>`

## Depends on

cx-core-signals.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-events CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-events::cx-core-events)
```

Unit tests are colocated under `include/cx/core/events/testing/`, compiled into `cxcore-tests`, never installed.
