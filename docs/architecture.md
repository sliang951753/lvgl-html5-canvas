# Architecture

```
   ┌──────────────────────────── lvgl_html5_canvas (host) ────────────────────────────┐
   │                                                                                  │
   │   ┌────────────┐    invalidate +     ┌─────────────────┐  evaluate/dispatch      │
   │   │ demo scene │───lv_refr_now()────▶│   LVGL core     │──────────────────┐      │
   │   └────────────┘     (30 Hz)         │ (single thread) │                  │      │
   │                                      └───────┬─────────┘                  ▼      │
   │                                              │              ┌──────────────────┐ │
   │                                  REFR_START  │              │ html5_draw_unit  │ │
   │                                  REFR_READY  ▼              │ (id=50, score=80)│ │
   │                                      ┌────────────┐         │  - claims FILL/  │ │
   │                                      │ begin_frame│         │    BORDER/LINE/  │ │
   │                                      │ flush_frame│◀────────┤    SHADOW/IMAGE/ │ │
   │                                      │            │         │    LAYER         │ │
   │                                      └─────┬──────┘         └────────┬─────────┘ │
   │                                            │ broadcast(buf,n)        │           │
   │                                            ▼                         │           │
   │                          ┌──────────────────────────────────┐        │           │
   │                          │   ws_server (libwebsockets)      │        │           │
   │                          │   service loop on its OWN pthread│        │           │
   │                          │   wake via lws_cancel_service()  │        │           │
   │                          │   per-viewer 16-slot ring buffer │        │           │
   │                          └─────────────────┬────────────────┘        │           │
   │                                            │ ws binary frame         │           │
   └────────────────────────────────────────────┼─────────────────────────┼───────────┘
                                                ▼                         ▼
                                       ┌─────────────────┐         (SW unit handles
                                       │ browser viewer  │          everything we
                                       │ web/decoder.js  │          don't claim →
                                       │ web/viewer.js   │          pixels into
                                       │ Canvas2D fillRect│          dummy fb)
                                       └─────────────────┘
```

## Threads

| Thread          | Owns                                  | Blocking? |
|-----------------|---------------------------------------|-----------|
| main            | LVGL core, draw units, demo scene     | no — `nanosleep(5 ms)` between iterations |
| ws_service      | `lws_service()` loop, viewer I/O      | yes — `epoll_wait` up to 50 ms; woken instantly by `lws_cancel_service()` |

The split was forced by an early M1 bug: `lws_service()` was running
**inside** the LVGL main loop and the libwebsockets 4.x scheduler kept
it blocked ~1 s per call (waiting on internal timers), starving LVGL
down to ~1 Hz. Moving it to its own pthread restored 30 Hz.

Hand-off between the threads:
- **main → ws**: `lhc_ws_server_broadcast()` copies the frame into the
  ring buffer behind a small mutex, then calls
  `lws_cancel_service()` to wake the service thread for a WRITEABLE
  drain.
- **ws → main**: none in M1. No input events yet.

## Draw unit lifecycle (per refresh cycle)

1. LVGL emits `LV_EVENT_REFR_START` → main.c calls
   `lhc_html5_draw_unit_begin_frame(w, h)`, resetting the encoder and
   `fills_this_frame = 0`.
2. LVGL builds the task list and asks every draw unit to `evaluate()`.
   The html5 unit returns `1` for a constrained subset of task types
   (currently FILL / BORDER / LINE / ARC / BOX_SHADOW / IMAGE / LAYER) with
   `score = 80` (beats SW's 100), claiming them.
3. LVGL repeatedly calls `dispatch()` on each unit. We encode each
   claimed task as protocol ops (`FILL_RECT`, `BORDER`, `LINE`, `ARC`,
   `BOX_SHADOW`, `IMAGE`, plus blob management for image/layer sources)
   and mark it `FINISHED`. The SW unit continues to render anything we
   into the dummy framebuffer (which the dummy flush_cb then discards).
4. LVGL emits `LV_EVENT_REFR_READY` → `flush_frame()`:
   - If `fills_this_frame == 0`, the frame is **dropped silently**
     (no broadcast, `empty_frames_skipped++`). See `docs/protocol.md` §3.
   - Otherwise, append END_FRAME, finalize header, `ws_server.broadcast()`.

## Why score = 80?

LVGL's SW unit declares `score = 100` on every task it can handle.
Lower score wins. We pick 80 so future units (e.g. an OpenGL-accelerated
one) can pick a lower number and beat us for the same op, without us
having to rewire the protocol.

## Runtime observability (M2d)

`main.c` prints one compact stats line per second:

- `eval`, `disp`, `taken`, `frames`
- per-op encoded counters: `fills`, `borders`, `lines`, `arcs`, `shadows`, `images`, `layers`
- blob traffic: `blobs`, `blobKB`

Use these counters to confirm capability deltas quickly without pixel
inspection. Example: after enabling the layered-opacity demo widget,
`layers` should increase continuously (non-zero slope).

## File map

| Path                          | Role                                   |
|-------------------------------|----------------------------------------|
| `src/main.c`                  | App entry, LVGL setup, demo scene, 30 Hz invalidate loop |
| `src/proto/protocol.h`        | Wire-format constants (single source of truth) |
| `src/proto/encoder.{h,c}`     | Pure LE byte writer; no LVGL deps      |
| `src/draw/html5_draw_unit.{h,c}` | LVGL draw unit; evaluate/dispatch + frame lifecycle |
| `src/transport/ws_server.{h,c}`  | libwebsockets server + service thread + ring buffer |
| `scripts/gen_js_protocol.py`  | Generates `web/protocol.js` from `protocol.h` |
| `web/decoder.js`              | (generated) opcode constants            |
| `web/viewer.js`               | Canvas2D replayer (`fillRect`/border/shadow/image + blob cache) |
| `tests/test_encoder.c`        | Byte-level encoder unit tests (fill/border/shadow/image/blob) |
| `tests/test_protocol_e2e.py`  | Spawns binary, validates frame structure + M2 op presence |
