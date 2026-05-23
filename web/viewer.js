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

log('viewer M1 ready');
