# cx-core-process

Child processes. POSIX only.

Namespace `cx::core::process`, headers under `cx/core/process/`. Header-only.

## Headers

- `process.hpp` — `run` (inherit stdio, return the exit status), `run_captured` (stdout and stderr drained
  together with poll, so a child filling either pipe cannot deadlock it), `which`, `exit_status`

`sandbox` is not provided: the isolation mechanism (namespaces, seccomp) is undecided.

## Depends on

Nothing.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-process CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-process::cx-core-process)
```

Unit tests are colocated under `include/cx/core/process/testing/`, compiled into `cxcore-tests`, never installed.
