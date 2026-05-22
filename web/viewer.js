// viewer.js — top-level glue between WS, decoder, and the canvas.
// M0: connects, logs incoming bytes (4-byte heartbeat). No drawing yet.

import { connect } from './ws.js';
import { decodeFrame } from './decoder.js';

const $ = (sel) => document.querySelector(sel);
const logEl = $('#log');
const statusEl = $('#status');
const canvas = $('#canvas');
const ctx = canvas.getContext('2d');

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

let ws = null;
$('#connect').addEventListener('click', () => {
  if (ws) { ws.close(); ws = null; }
  const url = $('#url').value;
  setStatus('connecting…', '#fc6');
  ws = connect(url, {
    onOpen:    () => { setStatus('connected', '#6f6'); log(`open ${url}`); },
    onClose:   () => { setStatus('disconnected', '#f66'); log('close'); },
    onError:   (e) => { setStatus('error', '#f66'); log(`error: ${e.message || e}`); },
    onMessage: (data) => { handleMessage(data); },
  });
});

function handleMessage(data) {
  // M0: heartbeat is 4 ASCII bytes "LHC\0". Detect and skip.
  if (data.byteLength === 4) {
    const v = new Uint8Array(data);
    if (v[0] === 0x4C && v[1] === 0x48 && v[2] === 0x43 && v[3] === 0x00) {
      log(`heartbeat (${data.byteLength}B)`);
      return;
    }
  }
  // M1+: full frame
  try {
    const frame = decodeFrame(data);
    log(`frame #${frame.frameId} cmds=${frame.cmds.length}`);
    // M1 will replay cmds onto ctx here.
  } catch (e) {
    log(`decode err: ${e.message}`);
  }
}

log('viewer M0 ready');
