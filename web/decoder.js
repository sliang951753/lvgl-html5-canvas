// decoder.js — parses one binary frame into { frameId, cmds: [...] }.
// Mirrors src/proto/encoder.c. Forward-compatible: unknown opcodes are
// skipped via their payload_len.

import { Proto } from './protocol.js';

export function decodeFrame(arrayBuffer) {
  const view = new DataView(arrayBuffer);
  if (arrayBuffer.byteLength < Proto.FRAME_HDR_SIZE) {
    throw new Error(`frame too short: ${arrayBuffer.byteLength}B`);
  }
  const frameId  = view.getUint16(0, true);
  const cmdCount = view.getUint16(2, true);
  let off = Proto.FRAME_HDR_SIZE;
  const cmds = [];
  for (let i = 0; i < cmdCount; i++) {
    if (off + Proto.CMD_HDR_SIZE > arrayBuffer.byteLength) {
      throw new Error(`cmd ${i} hdr OOB at offset ${off}`);
    }
    const opcode     = view.getUint8(off);
    const flags      = view.getUint8(off + 1);
    const payloadLen = view.getUint16(off + 2, true);
    const payloadOff = off + Proto.CMD_HDR_SIZE;
    if (payloadOff + payloadLen > arrayBuffer.byteLength) {
      throw new Error(`cmd ${i} payload OOB at offset ${payloadOff}`);
    }
    cmds.push({
      opcode, flags, payloadLen,
      payload: new DataView(arrayBuffer, payloadOff, payloadLen),
    });
    off = payloadOff + payloadLen;
  }
  return { frameId, cmds };
}

// Per-op decoders (small bodies; expand in M1+).
export const Decoders = {
  [Proto.OP_BEGIN_FRAME]: (p) => ({ w: p.getInt16(0, true), h: p.getInt16(2, true) }),
  [Proto.OP_END_FRAME]:   () => ({}),
  [Proto.OP_FILL_RECT]:   (p) => ({
    x: p.getInt16(0, true),  y: p.getInt16(2, true),
    w: p.getInt16(4, true),  h: p.getInt16(6, true),
    argb: p.getUint32(8, true) >>> 0,
    radius: p.getUint8(12),
  }),
  [Proto.OP_BORDER]:      (p) => ({
    x: p.getInt16(0, true),  y: p.getInt16(2, true),
    w: p.getInt16(4, true),  h: p.getInt16(6, true),
    argb: p.getUint32(8, true) >>> 0,
    width:  p.getUint8(12),
    radius: p.getUint8(13),
    side:   p.getUint8(14),
  }),
  [Proto.OP_LINE]:        (p) => ({
    x1: p.getInt16(0, true), y1: p.getInt16(2, true),
    x2: p.getInt16(4, true), y2: p.getInt16(6, true),
    argb: p.getUint32(8, true) >>> 0,
    width: p.getUint8(12),
  }),
  [Proto.OP_LINE_EX]:     (p) => ({
    x1: p.getInt16(0, true), y1: p.getInt16(2, true),
    x2: p.getInt16(4, true), y2: p.getInt16(6, true),
    argb: p.getUint32(8, true) >>> 0,
    width: p.getUint8(12),
    dash_width: p.getUint8(13),
    dash_gap: p.getUint8(14),
    cap_bits: p.getUint8(15),
  }),
  [Proto.OP_ARC]:         (p) => ({
    cx: p.getInt16(0, true), cy: p.getInt16(2, true),
    r: p.getUint16(4, true),
    a0: p.getInt16(6, true), a1: p.getInt16(8, true),
    argb: p.getUint32(10, true) >>> 0,
    width: p.getUint8(14),
  }),
  [Proto.OP_BOX_SHADOW]:  (p) => ({
    x: p.getInt16(0, true),  y: p.getInt16(2, true),
    w: p.getInt16(4, true),  h: p.getInt16(6, true),
    argb: p.getUint32(8, true) >>> 0,
    radius: p.getUint8(12),
    blur:   p.getUint8(13),
    spread: p.getInt8(14),
    ofs_x:  p.getInt16(15, true),
    ofs_y:  p.getInt16(17, true),
    bg_cover: p.getUint8(19),
  }),
  [Proto.OP_IMAGE]: (p) => ({
    x: p.getInt16(0, true),  y: p.getInt16(2, true),
    w: p.getInt16(4, true),  h: p.getInt16(6, true),
    blob_id: p.getUint32(8, true) >>> 0,
  }),
  [Proto.OP_BLOB_UPLOAD]: (p) => {
    // header(9) = u32 id; u16 w; u16 h; u8 fmt; then raw RGBA bytes
    const id  = p.getUint32(0, true) >>> 0;
    const w   = p.getUint16(4, true);
    const h   = p.getUint16(6, true);
    const fmt = p.getUint8(8);
    const dataLen = p.byteLength - 9;
    // Subarray of the original buffer; viewer copies into ImageData.
    const bytes = new Uint8Array(p.buffer, p.byteOffset + 9, dataLen);
    return { id, w, h, fmt, bytes };
  },
  [Proto.OP_BLOB_EVICT]: (p) => ({ id: p.getUint32(0, true) >>> 0 }),
};
