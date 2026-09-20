# cx-core-testing

The self-hosted test facility every cx project uses: self-registering test groups, no external framework.

Namespace `cx::core::testing`, headers under `cx/core/testing/`. Header-only.

## Headers

- `test_group.hpp`, `test_case.hpp`, `test_context.hpp` — declare a group; `check`/`require`/`check_throws`
- `registry.hpp`, `run_all.hpp` — `return cx::core::testing::run_all(argc, argv);` is a whole test `main`

## Depends on

Nothing.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-testing CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-testing::cx-core-testing)
```

Unit tests are colocated under `include/cx/core/testing/testing/`, compiled into `cxcore-tests`, never installed.
