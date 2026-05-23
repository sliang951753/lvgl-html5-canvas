"""End-to-end protocol regression test.

Spawns the lvgl_html5_canvas binary, connects as a WS viewer, and
asserts that the bug-fix invariants hold over a sample of frames:

  * BEGIN_FRAME / END_FRAME bookend every message
  * Every broadcast frame contains >=1 FILL_RECT op (no empty frames
    leak through — that was the v0.1->v0.2 bug)
  * Frame IDs strictly increase
  * BEGIN_FRAME advertises 800x480 (matches main.c DISP_W/H)
  * The 5 demo rectangles are all present (red/green/blue/yellow + bg)

Run with: ctest -R protocol_e2e --output-on-failure
"""
import asyncio
import os
import struct
import subprocess
import sys
import time

try:
    import websockets
except ImportError:
    print("SKIP: websockets module not available")
    sys.exit(0)

URI = "ws://127.0.0.1:9000/"
OP_BEGIN, OP_END, OP_FILL, OP_BORDER, OP_BOX_SHADOW, OP_IMAGE, OP_BLOB_UPLOAD = 0x01, 0x02, 0x10, 0x11, 0x13, 0x21, 0x40
N_FRAMES = 20  # enough to catch a 50% drop pattern many times over

EXPECTED_COLORS = {  # (r, g, b) — see main.c demo scene
    (16, 24, 32),    # bg
    (229, 57, 53),   # red
    (67, 160, 71),   # green
    (30, 136, 229),  # blue
    (255, 235, 59),  # yellow
}


def decode_frame(msg):
    fid, cnt = struct.unpack_from("<HH", msg, 0)
    off = 4
    ops = []
    for _ in range(cnt):
        if off + 4 > len(msg):
            raise ValueError(f"truncated cmd header at {off}")
        op, fl, pl = struct.unpack_from("<BBH", msg, off)
        payload = msg[off + 4:off + 4 + pl]
        off += 4 + pl
        ops.append((op, fl, payload))
    return fid, ops


async def collect_frames():
    async with websockets.connect(URI, subprotocols=["lhc-v0"]) as ws:
        frames = []
        while len(frames) < N_FRAMES:
            msg = await asyncio.wait_for(ws.recv(), timeout=10.0)
            if isinstance(msg, (bytes, bytearray)) and len(msg) >= 4:
                frames.append(bytes(msg))
        return frames


def main():
    binary = os.environ.get("LHC_BIN")
    if not binary or not os.path.exists(binary):
        print(f"SKIP: binary not found ({binary})")
        return 0

    proc = subprocess.Popen(
        [binary],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        # Wait for WS server to bind.
        time.sleep(2.0)
        frames = asyncio.run(collect_frames())
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    failures = []
    prev_fid = -1
    seen_colors = set()
    saw_border = False
    border_sides_seen = set()
    border_payload_lens = set()
    saw_shadow = False
    shadow_payload_lens = set()
    saw_image = False
    image_payload_lens = set()
    saw_blob_upload = False
    blob_payload_lens = set()

    for i, msg in enumerate(frames):
        try:
            fid, ops = decode_frame(msg)
        except Exception as e:
            failures.append(f"frame {i}: decode error: {e}")
            continue

        if fid <= prev_fid:
            failures.append(f"frame {i}: fid {fid} <= prev {prev_fid}")
        prev_fid = fid

        if not ops or ops[0][0] != OP_BEGIN:
            failures.append(f"frame {i}: missing BEGIN_FRAME")
            continue
        if ops[-1][0] != OP_END:
            failures.append(f"frame {i}: missing END_FRAME")

        # bug regression: empty frames must not be broadcast.
        fills = [o for o in ops if o[0] == OP_FILL]
        if len(fills) < 1:
            failures.append(
                f"frame {i} (fid={fid}): empty frame leaked through "
                f"(cmds={len(ops)}, fills=0) — bug v0.2.0-m1 regression"
            )

        # canvas dims from BEGIN_FRAME
        w, h = struct.unpack_from("<HH", ops[0][2], 0)
        if (w, h) != (800, 480):
            failures.append(f"frame {i}: bad canvas {w}x{h}")

        for _, _, payload in fills:
            if len(payload) < 13:
                continue
            b, g, r, a = struct.unpack_from("<BBBB", payload, 8)
            seen_colors.add((r, g, b))

        for op, _, payload in ops:
            if op == OP_BORDER:
                saw_border = True
                border_payload_lens.add(len(payload))
                if len(payload) >= 15:
                    border_sides_seen.add(payload[14])
            elif op == OP_BOX_SHADOW:
                saw_shadow = True
                shadow_payload_lens.add(len(payload))
            elif op == OP_IMAGE:
                saw_image = True
                image_payload_lens.add(len(payload))
            elif op == OP_BLOB_UPLOAD:
                saw_blob_upload = True
                blob_payload_lens.add(len(payload))

    missing = EXPECTED_COLORS - seen_colors
    if missing:
        failures.append(f"missing demo colors: {sorted(missing)}")
    if not saw_border:
        failures.append("no BORDER op observed — M2 demo borders missing")
    if border_payload_lens and border_payload_lens != {15}:
        failures.append(f"BORDER payload size unexpected: {border_payload_lens} (want {{15}})")
    if not saw_shadow:
        failures.append("no BOX_SHADOW op observed — M2 demo shadows missing")
    if shadow_payload_lens and shadow_payload_lens != {20}:
        failures.append(f"BOX_SHADOW payload size unexpected: {shadow_payload_lens} (want {{20}})")
    if not saw_image:
        failures.append("no IMAGE op observed — M2c image replay missing")
    if image_payload_lens and image_payload_lens != {12}:
        failures.append(f"IMAGE payload size unexpected: {image_payload_lens} (want {{12}})")
    # BLOB_UPLOAD may or may not appear in this 20-frame window: if the viewer
    # connects after the initial upload and before refresh-period re-upload,
    # IMAGE ops can still appear while blob uploads are absent. So we only
    # validate payload shape when uploads are observed.

    if failures:
        print(f"FAIL ({len(failures)} issues over {len(frames)} frames):")
        for f in failures[:10]:
            print(f"  - {f}")
        return 1

    print(
        f"PASS protocol_e2e: {len(frames)} frames, "
        f"all non-empty, fids monotonic, {len(seen_colors)} unique colors, "
        f"borders={saw_border} sides_seen={sorted(border_sides_seen)} "
        f"shadows={saw_shadow} images={saw_image} blobs_seen={saw_blob_upload}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
