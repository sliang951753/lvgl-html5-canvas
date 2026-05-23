# PLAN — lvgl-html5-canvas

Living roadmap. Each milestone is a runnable demo + git tag.

## Goal

Stream LVGL 9.5 vector drawing commands to a browser HTML5 Canvas viewer
over WebSocket, low-bandwidth, with input round-trip. Targets:

- **Dev**: Linux-host (Termux aarch64 / x86_64 Linux), built with CMake.
- **Prod**: Allwinner T507, Timesys Linux, aarch64, 1 GB RAM.

## Architecture (overview)

```
+--------------------+        +-------------+        +---------------+
|       BOARD        |        |   NETWORK   |        |    BROWSER    |
| LVGL 9.5           |        |             |        |               |
| widgets ---> draw_dispatch -> html5_draw_unit       Canvas2D 2D ctx |
|                    |   |    | (encode op)   ws://  | viewer.js     |
|                    |   +--> | sw_draw_unit (fallback for unhandled) |
|                    |        |               -----> decoder.js      |
|        lv_indev <-------- input_inject <----  ws   <- pointer/key  |
+--------------------+        +-------------+        +---------------+
```

Key idea: `html5_draw_unit` declares in `evaluate()` only the ops it can
encode. Anything else falls through to SW. So M1 can ship with only
`FILL_RECT` supported and everything else still draws correctly (just
heavier on the wire).

## Modules

| Module | Purpose |
|---|---|
| `src/draw/html5_draw_unit.{c,h}` | Implements the LVGL 9 draw unit API. One encoder per supported op. |
| `src/proto/protocol.h` | **Single source of truth** for opcodes, payload layouts, frame header. |
| `src/proto/encoder.{c,h}` | Tiny LE writer (no malloc per-cmd; one frame buffer per viewer). |
| `src/proto/gen_js_protocol.py` | Parses `protocol.h`, emits `web/protocol.js`. Run via CMake custom target. |
| `src/transport/ws_server.{c,h}` | libwebsockets server, viewer list, broadcast frame. |
| `src/input/input_inject.{c,h}` | M4. Buffers upstream events; lv_indev `read_cb` drains. |
| `web/viewer.js` | Connects WS, decodes frames, replays on canvas. |
| `web/decoder.js` | One handler per opcode (mirrors C encoders). |
| `web/protocol.js` | **Generated.** Opcode constants + payload sizes. |

## Protocol v0 (binary, little-endian)

Frame: `[u16 frame_id][u16 cmd_count][cmd]*`
Cmd:   `[u8 opcode][u8 flags][u16 payload_len][payload]`

| Opcode | Name | Payload | Milestone |
|--------|------|---------|-----------|
| 0x01 | BEGIN_FRAME | `i16 w, i16 h` | M1 |
| 0x02 | END_FRAME   | — | M1 |
| 0x10 | FILL_RECT   | `i16 x,y,w,h; u32 argb; u8 radius` | M1 |
| 0x11 | BORDER      | `i16 x,y,w,h; u32 argb; u8 width; u8 radius` | M2 |
| 0x12 | LINE        | `i16 x1,y1,x2,y2; u32 argb; u8 width` | M2 |
| 0x20 | GLYPH       | `i16 x,y; u32 blob_id; u32 argb` | M2 |
| 0x21 | IMAGE       | `i16 x,y,w,h; u32 blob_id` | M3 |
| 0x22 | ARC         | `i16 cx,cy; u16 r; i16 a0,a1; u32 argb; u8 width` | M3 |
| 0x30 | SET_CLIP    | `i16 x,y,w,h` | M3 |
| 0x31 | CLEAR_CLIP  | — | M3 |
| 0x40 | BLOB_UPLOAD | `u32 blob_id; u16 w,h; u8 fmt; bytes[w*h*bpp]` | M2/M3 |
| 0x41 | BLOB_EVICT  | `u32 blob_id` | M5 |
| 0xF0 | INPUT_POINTER (↑) | `u8 state; i16 x,y` | M4 |
| 0xF1 | INPUT_KEY (↑)     | `u8 state; u16 key` | M4 |

Blob fmt: `0=A8`, `1=ARGB8888`, `2=RGB565`.

Glyph cache key = `hash(font_ptr, codepoint, size, weight)` → 32-bit `blob_id`.
LRU on both ends; capacity ~4 MB browser, ~256 KB board (configurable).

## Milestones

### M0 — scaffold (this commit)
- repo + CMake + FetchContent (LVGL 9.5, libwebsockets)
- empty `html5_draw_unit` registers, logs each op, declares **nothing**
  in `evaluate()` so SW handles everything
- WS server stub: listens on :9000, broadcasts heartbeat
- web/ minimal: connect, log incoming bytes
- linux-host builds, `lvgl_html5_canvas` runs `lv_demo_widgets`
- **tag**: `v0.1.0-m0`

### M1 — first pixels ✅
- protocol: BEGIN_FRAME / END_FRAME / FILL_RECT
- frame buffer per viewer; flush in `LV_EVENT_REFR_READY`
- decoder: clear → fill_rect loop (with rounded-rect support)
- viewer.js: canvas sized from BEGIN_FRAME, auto-connect on load,
  auto-reconnect on close, mobile viewport
- WS server moved to its own pthread (libwebsockets 4.x scheduler
  was blocking the LVGL main loop)
- empty-frame drop fixes viewer flicker (LVGL fires no-op refresh
  cycles between our forced repaints)
- demo: 4 coloured rounded rectangles on dark BG
- tests: `test_encoder` (6 cases) + `protocol_e2e` (20-frame
  regression pin)
- docs: `docs/protocol.md`, `docs/architecture.md`,
  `docs/troubleshooting.md`
- **tag**: `v0.2.0-m1` ✅

### M2 — text + lines + borders (in progress)
- [x] BORDER (0x11): solid colour, full/partial sides, radius, alpha hint;
      demo panels carry full + top/bottom + thick-rounded variants;
      `test_encoder::border_payload` + e2e BORDER-presence assertion
- [x] BOX_SHADOW (0x13): solid colour, radius/blur/spread/offset, bg_cover;
      encoder + viewer replay + tests
- [x] IMAGE (0x21) + BLOB_UPLOAD (0x40): blob cache/LRU + periodic refresh,
      ARGB8888/XRGB8888 path, demo sprites, encoder/e2e coverage
- [x] LAYER (LV_DRAW_TASK_TYPE_LAYER): replay as IMAGE over layer draw_buf
      for simple non-transformed blends
- [x] LINE (0x12): single-segment non-dashed/no-round-cap fast path
      end-to-end (encoder/draw-unit/viewer/demo/tests)
- [ ] LINE advanced shapes (polyline iterator / dash / round caps)
- [ ] GLYPH (0x20) + BLOB_UPLOAD (0x40) — glyph cache (board hash,
      browser Map+LRU); demo text labels
- **tag**: `v0.3.0-m2` (deferred until LINE + GLYPH land)

### M3 — images + arcs + clipping
- IMAGE / ARC / SET_CLIP / CLEAR_CLIP
- LVGL nested clip stack mapped to ctx.save/restore
- demo: gauge + image button
- **tag**: `v0.4.0-m3`

### M4 — input
- INPUT_POINTER / INPUT_KEY upstream
- `input_inject` ring buffer; lv_indev read_cb
- demo: full lv_demo_widgets interactive
- **tag**: `v0.5.0-m4`

### M5 — production polish
- dirty rects (LVGL invalidated_area → SET_CLIP per frame)
- BLOB_EVICT on LRU eviction
- per-frame compression (zstd, if size > threshold)
- T507 cross-compile preset + sysroot doc
- bench harness: capture .bin frames, replay; compare bytes/frame vs VNC TightVNC trace
- **tag**: `v1.0.0`

## Time budget

~10 working days, ~2 per milestone. M0 is ~0.5 day, M5 is ~2 days.

## Open questions

- T507 sysroot path / toolchain file location → defer to M5
- TLS (wss)? deferred — VPN is the trust boundary
- Multi-viewer state sync (initial frame replay) — deferred to M5 if needed
