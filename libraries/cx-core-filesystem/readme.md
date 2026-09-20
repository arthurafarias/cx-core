# cx-core-filesystem

Two processes, one file: an exclusive lock and an atomic replace. POSIX only.

Namespace `cx::core::filesystem`, headers under `cx/core/filesystem/`. Header-only.

## Headers

- `file_lock.hpp` — `file_lock` (RAII `flock`, blocking or `try_acquire`), `atomic_replace` (scratch + rename)

`watch` is not provided: no requirement for it has been written.

## Depends on

Nothing.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-filesystem CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-filesystem::cx-core-filesystem)
```

Unit tests are colocated under `include/cx/core/filesystem/testing/`, compiled into `cxcore-tests`, never installed.
