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
};
