# Protocol v0

See `PLAN.md` for the opcode table. This doc covers framing, endianness,
and edge cases.

## Endianness

**Little-endian** everywhere on the wire. Both encoder.c and
decoder.js assume this. Modern x86 and aarch64 are LE.

## Frame layout

```
+----------+----------+--------+--------+ ... +--------+
| frame_id | cmd_count|  cmd 0 |  cmd 1 | ... |  cmd N |
|   u16    |   u16    | <hdr+p>| <hdr+p>|     | <hdr+p>|
+----------+----------+--------+--------+ ... +--------+
```

Frame header is 4 bytes. `cmd_count` does NOT include implicit
BEGIN_FRAME / END_FRAME (those are explicit ops in the list).

## Command layout

```
+--------+-------+-------------+----------------+
| opcode | flags | payload_len |    payload     |
|   u8   |  u8   |     u16     |    bytes...    |
+--------+-------+-------------+----------------+
```

`flags`:
- bit 0: `HAS_ALPHA_HINT` — payload color uses non-trivial alpha
- bit 1..7: reserved (must be 0)

`payload_len` is the byte count of `payload` only (not including the
4-byte cmd header). Allows skip-on-unknown forward compatibility.

## WS framing

Each LVGL refresh = one WS *binary* message containing one full frame.
No fragmentation. Browser must read each binary message as a whole frame.

## Blob lifecycle

```
board emit BLOB_UPLOAD(id, w, h, fmt, bytes)  -- once per cache miss
board emit GLYPH(x, y, blob_id, argb)         -- N times per frame
board emit BLOB_EVICT(id)                     -- on LRU eviction (M5)
```

Browser must hold blobs across frames. If a GLYPH/IMAGE references an
unknown blob_id, the browser logs a warning and skips the draw (do not
crash).

## Upstream (browser → board)

Same framing. `frame_id` field is **reused as a sequence number** for
the upstream direction (independent from downstream frame_id).

## Compatibility

- Unknown opcodes: skip via `payload_len`.
- Unknown flag bits: log + ignore.
- Version negotiation deferred to v1.

## CRC / integrity

None. WS is over TCP; we trust the transport. If we move to UDP or QUIC
later, add a 16-bit checksum field after `cmd_count`.
