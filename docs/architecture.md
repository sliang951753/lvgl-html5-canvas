# Architecture

## Why a draw unit?

LVGL 9 splits rendering into:

```
widget code  ->  lv_draw_* (high-level ops)  ->  draw_dispatch  ->  draw_unit
```

A draw unit's `evaluate(task)` returns a *score* indicating willingness
to handle a task. The dispatcher picks the highest scorer. By default
the SW unit returns a baseline score for everything. **A custom unit
that returns a higher score on supported ops and 0 on unsupported ones
gets exactly the ops it wants — the SW unit handles the rest.**

This is how we ship M1 with only `FILL_RECT` and still render an entire
LVGL demo correctly: borders, text, images all go through SW silently
(and the browser sees nothing for those ops — the demo is correct but
incomplete on the wire until M2/M3).

Wait — that's not right for a *remote* viewer. SW renders to a local
framebuffer the browser will never see. So either:

**A.** the SW unit also writes to a hidden buffer we *can* push (slow,
defeats the point), or
**B.** we accept that anything not in our op set shows as a blank box
on the browser side until we add it.

We pick **B**. M0/M1 only ship FILL_RECT, so the browser will show a
ton of blanks. Each milestone fills in more.

## Frame lifecycle

```
LVGL refresh tick fires
  for each invalidated area:
    draw_dispatch enumerates tasks
      for each task: html5_unit.evaluate(task)
        if score > 0:
          html5_unit.dispatch(task)
            -> encoder writes [opcode][flags][len][payload] into
               the current frame buffer
        else: SW handles, browser sees nothing
  on LV_EVENT_REFR_FINISH:
    ws_server.broadcast(frame_buf)
    reset frame buf, frame_id++
```

`lws_write` is called *once per frame per viewer*, with the whole
encoded frame. This minimises TCP packets over VPN.

## Threading

LVGL is single-threaded. libwebsockets owns its own service thread.
We **bridge** with a lock-free ring buffer per viewer for outgoing
frames, and a separate ring for inbound input events.

```
LVGL thread:                  lws thread:
  encode into local buf         service connections
  on flush: push to ring  -->   drain ring, lws_write
                                read input bytes
                                push to input ring   -->  lv_indev read_cb
```

## Glyph cache (preview, M2)

Key: `hash32(font_ptr, codepoint, size_8.8_fixed, weight_u8)`.

Board side: open-addressing hash table, ~256 entries. Miss = render
glyph via LVGL's existing font engine into a temp A8 buffer, emit
`BLOB_UPLOAD`, then emit `GLYPH`. Hit = just emit `GLYPH`.

Browser side: `Map<blob_id, ImageBitmap>`. LRU eviction at ~4 MB.
A glyph is drawn via `ctx.drawImage(bitmap, x, y)` after recoloring
through an offscreen tinted copy (cached per (blob_id, argb) pair).

## File map

See `README.md` for layout. The non-obvious bits:

- `proto/gen_js_protocol.py` is invoked by a CMake `custom_command` that
  fires whenever `protocol.h` changes. It greps `#define OP_*` and
  `payload size` annotations and emits `web/protocol.js`.
- `web/protocol.js` is checked in (so the browser side works without
  Python on the user's machine) but is **regenerated** in CI; CI fails
  if the regenerated file diffs from the committed one.
