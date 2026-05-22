# lvgl-html5-canvas

Stream LVGL 9.5 vector drawing commands from an embedded board to a browser
HTML5 Canvas viewer over WebSocket.

Designed for low-bandwidth remote UI on resource-constrained Linux boards
(target: Allwinner T507 / Timesys Linux, aarch64, 1 GB RAM), with a
Linux-host development path on x86_64 / Termux.

## Why not VNC?

VNC streams pixels. LVGL UIs are typically vector-heavy (flat fills,
glyphs/icons repeated, small dirty regions) so a command stream over a
custom binary protocol is **~10–50× smaller** for typical UI frames over a
VPN link.

## Architecture (one paragraph)

LVGL 9 exposes a draw-unit extension point. This project registers an
`html5_draw_unit` alongside the SW draw unit. The HTML5 draw unit
encodes each `lv_draw_*` op into a compact little-endian binary command
and queues it into the current frame. On `LV_EVENT_REFR_FINISH` the frame
is flushed to all connected WebSocket viewers. The browser-side
`viewer.js` decodes the command stream and replays it on a Canvas2D
context. Glyphs and images are sent once as bitmap blobs and cached by
`blob_id` (LRU on both ends). Pointer/key events come back upstream as
`lv_indev` reads.

See [`docs/architecture.md`](docs/architecture.md) and
[`PLAN.md`](PLAN.md) for the full design.

## Status

🚧 **M0 — scaffold**. Empty draw unit registers and logs each op. WS
server stub. Build runs on Linux-host. No real drawing yet.

| Milestone | Scope | Tag |
|-----------|-------|-----|
| **M0** | scaffold, build, WS stub, empty draw unit | `v0.1.0-m0` |
| M1 | FILL_RECT + browser viewer + end-to-end frame | `v0.2.0-m1` |
| M2 | BORDER/LINE/GLYPH + bitmap LRU | `v0.3.0-m2` |
| M3 | IMAGE/ARC/CLIP, SW fallback for the rest | `v0.4.0-m3` |
| M4 | Input round-trip (pointer/key → lv_indev) | `v0.5.0-m4` |
| M5 | Dirty-rect / batching / T507 cross build / vs-VNC bench | `v1.0.0` |

## Quickstart (Linux-host)

```bash
cmake --preset linux-host
cmake --build --preset linux-host
./build/linux-host/lvgl_html5_canvas
# then in a browser: open web/index.html, point it at ws://localhost:9000
```

Dependencies (LVGL 9.5, libwebsockets) are pulled via CMake `FetchContent`
on first configure. No git submodules.

## Layout

```
src/
  main.c
  draw/html5_draw_unit.{c,h}     # the draw unit (op encoders)
  proto/{protocol.h,encoder.{c,h},gen_js_protocol.py}
  transport/ws_server.{c,h}       # libwebsockets server
  input/input_inject.{c,h}        # M4
  lv_conf.h
web/
  index.html viewer.js ws.js decoder.js protocol.js  (generated)
tests/   scripts/   docs/   third_party/ (FetchContent fills this)
```

## License

MIT. See [LICENSE](LICENSE).
