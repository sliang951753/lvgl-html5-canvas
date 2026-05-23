// viewer.js — top-level glue between WS, decoder, and the canvas.
// M1: replay BEGIN_FRAME / END_FRAME / FILL_RECT onto a 2D canvas.

import { connect } from './ws.js';
import { decodeFrame, Decoders } from './decoder.js';
import { Proto } from './protocol.js';

const $ = (sel) => document.querySelector(sel);
const logEl = $('#log');
const statusEl = $('#status');
const canvas = $('#canvas');
const ctx = canvas.getContext('2d');

let frameCount = 0;
let lastFps = 0;
let fpsT0 = performance.now();
let fpsFrames = 0;

function log(msg) {
  const t = new Date().toISOString().substr(11, 12);
  logEl.textContent += `[${t}] ${msg}\n`;
  logEl.scrollTop = logEl.scrollHeight;
  if (logEl.textContent.length > 8000) {
    logEl.textContent = logEl.textContent.slice(-6000);
  }
}

function setStatus(s, color) {
  statusEl.textContent = s;
  statusEl.style.color = color || '#6f6';
}

function argbToCss(argb) {
  const a = ((argb >>> 24) & 0xFF) / 255;
  const r = (argb >>> 16) & 0xFF;
  const g = (argb >>> 8) & 0xFF;
  const b = argb & 0xFF;
  return `rgba(${r},${g},${b},${a})`;
}

// Blob cache: blob_id -> ImageBitmap (preferred, GPU-friendly) or ImageData
// fallback. We always keep an ImageData copy so a redraw works synchronously
// even if the ImageBitmap promise is still pending.
const blobCache = new Map(); // id -> { imageData, bitmap?: ImageBitmap }

function storeBlob({ id, w, h, fmt, bytes }) {
  // fmt 1 = ARGB8888 in canvas-native RGBA order (server pre-converted).
  if (fmt !== 1) { log(`blob ${id}: unsupported fmt=${fmt}`); return; }
  const expect = w * h * 4;
  if (bytes.length !== expect) {
    log(`blob ${id}: size mismatch got=${bytes.length} expect=${expect}`);
    return;
  }
  // Re-upload of an existing blob_id is expected (server refresh for late joiners).
  // blob_id is content-hash based, so same id => same pixels. Keep current cached
  // bitmap/imageData to avoid draw-path toggling (putImageData <-> drawImage)
  // that can appear as periodic flicker.
  if (blobCache.has(id)) return;

  // Copy because the underlying frame buffer is reused per WS message.
  const copy = new Uint8ClampedArray(bytes); // copies via Uint8Array → Clamped
  const imageData = new ImageData(copy, w, h);
  blobCache.set(id, { imageData });
  // Hint to GPU upload path; bitmap may resolve later but fallback path works.
  if (typeof createImageBitmap === 'function') {
    createImageBitmap(imageData).then((bm) => {
      const slot = blobCache.get(id);
      if (slot && slot.imageData === imageData) slot.bitmap = bm;
    }).catch(() => {});
  }
}

function drawBlob(c, id, x, y, w, h) {
  const slot = blobCache.get(id);
  if (!slot) return false;
  if (slot.bitmap) {
    c.drawImage(slot.bitmap, x, y, w, h);
  } else {
    // putImageData ignores transforms / w,h scaling — but we only claim
    // tasks where dst size matches src, so this is fine.
    c.putImageData(slot.imageData, x, y);
  }
  return true;
}

function fillRoundedRect(c, x, y, w, h, r) {
  if (r <= 0) { c.fillRect(x, y, w, h); return; }
  const rr = Math.min(r, Math.floor(w / 2), Math.floor(h / 2));
  c.beginPath();
  c.moveTo(x + rr, y);
  c.lineTo(x + w - rr, y);
  c.quadraticCurveTo(x + w, y, x + w, y + rr);
  c.lineTo(x + w, y + h - rr);
  c.quadraticCurveTo(x + w, y + h, x + w - rr, y + h);
  c.lineTo(x + rr, y + h);
  c.quadraticCurveTo(x, y + h, x, y + h - rr);
  c.lineTo(x, y + rr);
  c.quadraticCurveTo(x, y, x + rr, y);
  c.closePath();
  c.fill();
}

// LVGL border-side bitmap (see src/misc/lv_style.h):
//   BOTTOM=0x01, TOP=0x02, LEFT=0x04, RIGHT=0x08, FULL=0x0F.
const SIDE_BOTTOM = 0x01, SIDE_TOP = 0x02, SIDE_LEFT = 0x04, SIDE_RIGHT = 0x08;
const SIDE_FULL   = 0x0F;

function strokeBorder(c, x, y, w, h, width, radius, side) {
  // LVGL inset-stroke convention: border draws inside the rect.
  // Translate to canvas: shift by width/2 and shrink size by width.
  const lw = Math.max(1, width);
  c.lineWidth = lw;
  c.lineCap = 'butt';
  c.lineJoin = 'miter';

  if (side === SIDE_FULL || side === 0) {
    // Full border — single stroked rounded path.
    const inset = lw / 2;
    const ix = x + inset, iy = y + inset;
    const iw = w - lw,    ih = h - lw;
    if (iw <= 0 || ih <= 0) return;
    const rr = Math.max(0, Math.min(radius - inset, Math.floor(iw / 2), Math.floor(ih / 2)));
    c.beginPath();
    if (rr <= 0) {
      c.rect(ix, iy, iw, ih);
    } else {
      c.moveTo(ix + rr, iy);
      c.lineTo(ix + iw - rr, iy);
      c.quadraticCurveTo(ix + iw, iy, ix + iw, iy + rr);
      c.lineTo(ix + iw, iy + ih - rr);
      c.quadraticCurveTo(ix + iw, iy + ih, ix + iw - rr, iy + ih);
      c.lineTo(ix + rr, iy + ih);
      c.quadraticCurveTo(ix, iy + ih, ix, iy + ih - rr);
      c.lineTo(ix, iy + rr);
      c.quadraticCurveTo(ix, iy, ix + rr, iy);
      c.closePath();
    }
    c.stroke();
    return;
  }

  // Partial sides — draw selected edges as straight lines. Radius is
  // ignored on partial sides (matches LVGL SW behaviour closely enough
  // for M2; corner-rounded partial borders land with full ARC support).
  const half = lw / 2;
  c.beginPath();
  if (side & SIDE_TOP)    { c.moveTo(x, y + half);         c.lineTo(x + w, y + half); }
  if (side & SIDE_BOTTOM) { c.moveTo(x, y + h - half);     c.lineTo(x + w, y + h - half); }
  if (side & SIDE_LEFT)   { c.moveTo(x + half, y);         c.lineTo(x + half, y + h); }
  if (side & SIDE_RIGHT)  { c.moveTo(x + w - half, y);     c.lineTo(x + w - half, y + h); }
  c.stroke();
}

function renderFrame(frame) {
  // Always clear; LVGL re-paints the full screen each refresh for M1.
  // (When dirty-rects land in M5 we'll switch to incremental.)
  let cleared = false;
  for (const cmd of frame.cmds) {
    switch (cmd.opcode) {
      case Proto.OP_BEGIN_FRAME: {
        const d = Decoders[cmd.opcode](cmd.payload);
        if (canvas.width !== d.w || canvas.height !== d.h) {
          canvas.width = d.w; canvas.height = d.h;
        }
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        cleared = true;
        break;
      }
      case Proto.OP_END_FRAME:
        break;
      case Proto.OP_FILL_RECT: {
        const d = Decoders[cmd.opcode](cmd.payload);
        ctx.fillStyle = argbToCss(d.argb);
        fillRoundedRect(ctx, d.x, d.y, d.w, d.h, d.radius);
        break;
      }
      case Proto.OP_BORDER: {
        const d = Decoders[cmd.opcode](cmd.payload);
        ctx.strokeStyle = argbToCss(d.argb);
        strokeBorder(ctx, d.x, d.y, d.w, d.h, d.width, d.radius, d.side);
        break;
      }
      case Proto.OP_BOX_SHADOW: {
        const d = Decoders[cmd.opcode](cmd.payload);
        // LVGL box shadow: paint a rect outside the base rect, expanded by
        // `spread`, offset by (ofs_x, ofs_y), with a Gaussian-ish blur of
        // `blur` px. The bg fill that comes after covers the center, leaving
        // only the halo. We approximate by drawing the shadow rect itself
        // tinted with the shadow color and using canvas shadow* on a 0-offset
        // shadow so the blur extends past the rect edges.
        const sx = d.x - d.spread + d.ofs_x;
        const sy = d.y - d.spread + d.ofs_y;
        const sw = d.w + d.spread * 2;
        const sh = d.h + d.spread * 2;
        if (sw > 0 && sh > 0) {
          const sr = Math.max(0, d.radius + d.spread);
          ctx.save();
          ctx.shadowColor = argbToCss(d.argb);
          ctx.shadowBlur = d.blur;
          ctx.shadowOffsetX = 0;
          ctx.shadowOffsetY = 0;
          ctx.fillStyle = argbToCss(d.argb);
          fillRoundedRect(ctx, sx, sy, sw, sh, sr);
          ctx.restore();
        }
        break;
      }
      case Proto.OP_BLOB_UPLOAD: {
        const d = Decoders[cmd.opcode](cmd.payload);
        storeBlob(d);
        break;
      }
      case Proto.OP_BLOB_EVICT: {
        const d = Decoders[cmd.opcode](cmd.payload);
        blobCache.delete(d.id);
        break;
      }
      case Proto.OP_IMAGE: {
        const d = Decoders[cmd.opcode](cmd.payload);
        if (!drawBlob(ctx, d.blob_id, d.x, d.y, d.w, d.h)) {
          // Blob not in cache yet — viewer joined mid-stream. Stays blank
          // until the server's periodic refresh re-uploads it.
        }
        break;
      }
      default:
        // unknown opcode — silently skip (forward compat)
        break;
    }
  }
  if (!cleared) {
    // safety: if BEGIN_FRAME missing, still clear
    ctx.clearRect(0, 0, canvas.width, canvas.height);
  }
}

function defaultWsUrl() {
  // Same host as the page, fixed port 9000. Works for localhost AND LAN.
  const host = location.hostname || 'localhost';
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${host}:9000/`;
}

let ws = null;
let reconnectTimer = null;
function doConnect() {
  if (ws) { try { ws.close(); } catch (_) {} ws = null; }
  let url = $('#url').value.trim();
  if (!url) { url = defaultWsUrl(); $('#url').value = url; }
  setStatus('connecting…', '#fc6');
  log(`connect ${url}`);
  ws = connect(url, {
    onOpen:    () => { setStatus('connected', '#6f6'); log(`open ${url}`); },
    onClose:   (ev) => {
      const code = ev && ev.code;
      const reason = ev && ev.reason;
      const clean = ev && ev.wasClean;
      setStatus('disconnected — retry in 2s', '#f66');
      log(`close code=${code} reason="${reason||''}" wasClean=${clean}`);
      clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(doConnect, 2000);
    },
    onError:   (e) => {
      setStatus('error', '#f66');
      // The 'error' event itself carries no detail per spec; the real
      // diagnostic comes from the following 'close' event's code/reason.
      log(`error event (see close code below for detail)`);
    },
    onMessage: handleMessage,
  });
}
$('#connect').addEventListener('click', () => {
  clearTimeout(reconnectTimer);
  doConnect();
});
// Auto-connect on load — no button click needed.
window.addEventListener('DOMContentLoaded', () => {
  $('#url').value = defaultWsUrl();
  doConnect();
});

function handleMessage(data) {
  let frame;
  try {
    frame = decodeFrame(data);
  } catch (e) {
    log(`decode err: ${e.message}`);
    return;
  }
  renderFrame(frame);

  frameCount++;
  fpsFrames++;
  const now = performance.now();
  if (now - fpsT0 >= 1000) {
    lastFps = (fpsFrames * 1000 / (now - fpsT0)).toFixed(1);
    fpsT0 = now; fpsFrames = 0;
    setStatus(`connected · ${lastFps} fps · frame #${frame.frameId}`, '#6f6');
  }
  if (frameCount <= 3) {
    log(`frame #${frame.frameId} cmds=${frame.cmds.length} bytes=${data.byteLength}`);
  }
}

log('viewer M2c ready (FILL/BORDER/SHADOW/IMAGE)');
