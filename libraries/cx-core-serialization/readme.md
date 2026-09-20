# cx-core-serialization

Text encodings shared by everything that writes records or speaks a wire format.

Namespace `cx::core::serialization`, headers under `cx/core/serialization/`. Header-only.

## Headers

- `text_escape.hpp` — `write_json_escaped`, `write_xml_escaped`, `write_csv_field` (stream writers), `json_escaped`
- `json.hpp` — `json::value` document tree, strict RFC 8259 `parse`, compact `dump`, `quote`
- `tags.hpp` — `tags::v`, `tags::kv` and the object/array markers: what an `archive()` function hands an archiver
- `base64.hpp` — `base64_encode` / `base64_decode` (RFC 4648)
- `utf8.hpp` — `valid_utf8`, and an incremental `utf8_validator` for streamed input

## Depends on

Nothing.

## Using it

In-tree it is part of cx-core (`add_subdirectory(third-party/cx-core)`), either alone or through the
`cxcore::cxcore` umbrella; installed:

```cmake
find_package(cx-core-serialization CONFIG REQUIRED)
target_link_libraries(mytarget PRIVATE cx-core-serialization::cx-core-serialization)
```

Unit tests are colocated under `include/cx/core/serialization/testing/`, compiled into `cxcore-tests`, never installed.
