# cx-core-containers

A closed-set `variant`, string-keyed maps of them, and `object`: observable properties over a map.

Namespace `cx::core::containers`, headers under `cx/core/containers/`. Header-only.

## Headers

- `variant.hpp`, `variant_map.hpp`, `map.hpp`
- `object.hpp` — `property_set` / `property_get<T>`, `property_changed` signal

## Depends on

cx-core-signals.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-containers CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-containers::cx-core-containers)
```

Unit tests are colocated under `include/cx/core/containers/testing/`, compiled into `cxcore-tests`, never installed.
