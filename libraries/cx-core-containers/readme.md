# cx-core-containers

A closed-set `variant`, string-keyed maps of them, and `object`: observable properties over a map.

Namespace `cx::core::containers`, headers under `cx/core/containers/`. Header-only.

## Headers

- `variant.hpp` — the closed-set value: `bool`, `int64_t`, `uint64_t`, `double`, `string`, list, map, plus `int` and
  null (agenticx-ncortex's document alternatives); `as<T>()`, `is<T>()`, `operator[]`, `visit_all`, `archive()`
- `variant_map.hpp`, `map.hpp`, `variant_array.hpp`
- `property.hpp` — `property` (a variant with `name`, `option`, `description`) and `property_map`: a document of named,
  described values, as agenticx-ncortex's `ncortex.yaml` and command line are
- `object.hpp` — `property_set` / `property_get<T>`, `property_changed` signal
- `buffer.hpp` — `buffer` (an owning `std::vector<std::byte>`), `make_buffer`, `to_string`, `append`: explicit byte/text conversions

## Depends on

cx-core-signals, cx-core-serialization (the archive tags).

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-containers CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-containers::cx-core-containers)
```

Unit tests are colocated under `include/cx/core/containers/testing/`, compiled into `cxcore-tests`, never installed.
