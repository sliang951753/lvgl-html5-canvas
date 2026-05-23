# Troubleshooting

## Browser can't connect / page is blank

### Page renders fully black on mobile browser
**Symptom:** Open `http://<host>:8000/` on a phone, page is solid
black, no controls visible.
**Cause:** Missing `<meta name="viewport">` — mobile browsers default
to ~980 px desktop width, shrinking the entire UI to a few pixels in
the top-left corner against the dark background.
**Fix:** Already in `web/index.html` since v0.2.0-m1:
```html
<meta name="viewport" content="width=device-width, initial-scale=1" />
```
If the page is still black after refresh, force-reload to bypass cache
(see "Stale cached viewer" below).

### Stale cached viewer (Edge / mobile browsers)
**Symptom:** Log shows old format like `error: [object Event]` /
`close` (without a code), after we shipped the verbose close handler.
**Cause:** The browser cached `viewer.js` / `index.html` from a
previous session. The `<script type="module">` ESM cache is
particularly sticky on Edge mobile.
**Fix (in order of preference):**
1. Open the page in an **InPrivate / Incognito window** — fastest.
2. Append a cache-buster: `http://127.0.0.1:8000/?v=N` (bump N).
3. Settings → Privacy → Clear browsing data → "Cached images and files".

### WebSocket error then immediate close (`code=1006`)
**Symptom:** Page loads, hitting Connect logs
`close code=1006 reason="" wasClean=false`.
**Cause:** `1006` = abnormal closure, no close frame received. Common
on mobile browsers when:
- Browser-level "protect against tracking" / "secure DNS" blocks
  loopback WebSocket upgrades (Edge mobile is a known offender).
- The page was loaded over `https://` but tried to open `ws://`
  (mixed-content block — silent in some browsers).
**Fix:**
1. Try **Incognito / InPrivate** mode (disables most blockers).
2. Use the LAN IP form: `http://<lan-ip>:8000/` instead of
   `127.0.0.1`. Some mobile browsers special-case loopback.
3. Verify the server side is fine with the Python smoke test:
   ```bash
   python3 -c "
   import asyncio, websockets
   async def t():
       async with websockets.connect('ws://127.0.0.1:9000/',
                                     subprotocols=['lhc-v0']) as w:
           print('got', len(await asyncio.wait_for(w.recv(), 5)), 'bytes')
   asyncio.run(t())
   "
   ```
   If this prints `got 101 bytes`, the server is healthy and the
   problem is browser-side.

### `error: [object Event]` in the log
The DOM `error` event on a WebSocket carries no detail by design — the
spec hides it to avoid leaking origin info. Always check the
**immediately-following `close` event's code**; that's where the real
diagnostic lives. Our viewer logs both since v0.2.0-m1.

## Server-side issues

### LINE capability appears missing / `lines=0` in stats
**Symptom:** After M3a rollout, line widgets are visible but runtime stats
show `lines=0`.

**Cause:** Current LINE fast path only claims simple single-segment lines:
- `points == NULL` and `point_cnt == 0`
- no dash (`dash_width == 0`, `dash_gap == 0`)
- no round caps (`round_start == round_end == 0`)

Polyline/dashed/rounded variants intentionally fall back to SW for now.

**Fix:**
1. Validate with a 2-point line object (single segment) first.
2. Confirm `lines=` counter rises in `~/lhc.log`.
3. Keep polyline/dashed lines in demo as explicit SW fallback coverage.

### ARC capability appears missing / `arcs=0` in stats
**Symptom:** After M3b rollout, arc widgets are visible via SW fallback but runtime stats
show `arcs=0`.

**Cause:** Current ARC fast path only claims simple color-only arc tasks:
- `img_src == NULL`
- `opa > 0`, `width > 0`, `radius > 0`

Image-source arcs and unsupported variants intentionally fall back to SW.

**Fix:**
1. Validate with standard `lv_arc` widgets using color styles first.
2. Confirm `arcs=` counter rises in `~/lhc.log`.
3. Keep advanced arc variants as explicit SW fallback coverage until claimed.

### LAYER capability appears ineffective / `layers=0` in stats
**Symptom:** After M2d, visual output seems unchanged and runtime stats
show `layers=0` while other counters move.

**Cause:** Current M2d implementation reuses IMAGE/blob fast path for
`LV_DRAW_TASK_TYPE_LAYER` and keeps the same source-dimension gate
(`LHC_IMG_MAX_DIM`, default 128). If the layered offscreen buffer exceeds
that cap, the task falls back to SW and no layer op is encoded.

**Fix:**
1. Keep the validation layered object within cap (current demo uses 120×100).
2. Watch per-second stats in `~/lhc.log`: `layers=` must increase.
3. If larger layers are required, raise the cap deliberately and re-check
   frame size / blob traffic pressure before enabling by default.

### Frames flicker between content and pure black
**Symptom (pre-v0.2.0-m1):** Even frame IDs show 5 fills, odd ones
show 0 fills.
**Cause:** LVGL's internal refresh timer fires a no-op cycle ~30 ms
after each forced `lv_refr_now()`, producing
`BEGIN_FRAME + END_FRAME` with no content. The viewer cleared its
canvas on each BEGIN_FRAME, flashing black.
**Fix:** `html5_draw_unit.c::flush_frame()` drops frames with
`fills_this_frame == 0`. Counter exposed in `lhc_html5_stats_t.empty_frames_skipped`.
Pinned by `tests/test_protocol_e2e.py`.

### LVGL main loop drops to ~1 Hz
**Symptom (M1 development bug):** `frames_sent` increments once a
second instead of 30 times.
**Cause:** `lws_service()` was being called from the LVGL main
thread; libwebsockets 4.x's scheduler blocks the call ~1 s waiting on
internal timers.
**Fix:** WS server now runs `lws_service()` on its own pthread; main
loop only does `lv_timer_handler` + `lv_refr_now` + `nanosleep(5 ms)`.
See `docs/architecture.md` §Threads.
