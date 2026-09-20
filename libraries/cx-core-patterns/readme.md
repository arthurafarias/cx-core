# cx-core-patterns

Reusable structural patterns.

Namespace `cx::core::patterns`, headers under `cx/core/patterns/`. Header-only.

## Headers

- `factory.hpp` — `factory<product, key, args...>`: register creators under a key, create by key;
  `registration` wires a type in from a namespace-scope static

## Depends on

Nothing.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-patterns CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-patterns::cx-core-patterns)
```

Unit tests are colocated under `include/cx/core/patterns/testing/`, compiled into `cxcore-tests`, never installed.
