# Nerva v0.1 binary snapshot format

Magic: `Nervav001` (8 bytes)  
Endian: little-endian (`0x0102` marker)  
Header: 128 bytes (`NervaFileHeader`)

## Layout

```text
[header 128B][nodes][edges][memory_blocks][schemas][string_table]
```

String table:

```text
uint32 name_count
repeat name_count times:
  uint16 len
  uint8 bytes[len]
```

Payload CRC32 (IEEE) covers bytes from `payload_offset` (start of nodes) through EOF.

Header CRC32 covers the full 128-byte header with `header_crc32` zeroed during computation.
Both CRCs are validated on load before any engine state is modified.

On save, the header is written twice: a placeholder first, then a final rewrite with offsets,
`file_size`, `payload_crc32`, and `header_crc32` after the payload is complete.

Load validates magic/version, `file_size`, header CRC, sequential offset layout (counts must
match section sizes and sum to `file_size`), caps, payload CRC, then parses into a staging
buffer. The live engine is updated only after the full payload parses cleanly.

Runtime state (events, traces, mutation queue, fire log) is not persisted.
Load clears runtime buffers and rebuilds adjacency.
