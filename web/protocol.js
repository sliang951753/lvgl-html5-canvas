// AUTO-GENERATED from src/proto/protocol.h. Do not edit by hand.
// Regenerate with: python src/proto/gen_js_protocol.py --header src/proto/protocol.h --out web/protocol.js

export const Proto = Object.freeze({
  PROTO_VERSION: 0,
  FRAME_HDR_SIZE: 4,
  CMD_HDR_SIZE: 4,
  FLAG_HAS_ALPHA_HINT: 1 << 0,
  /** payload: i16 w, i16 h            (4) */
  OP_BEGIN_FRAME: 0x01,
  /** payload: (none)                  (0) */
  OP_END_FRAME: 0x02,
  /** i16 x,y,w,h; u32 argb; u8 radius (13) */
  OP_FILL_RECT: 0x10,
  /** i16 x,y,w,h; u32 argb; u8 w; u8 r; u8 side (15) */
  OP_BORDER: 0x11,
  /** i16 x1,y1,x2,y2; u32 argb; u8 w  (13) */
  OP_LINE: 0x12,
  /** i16 x,y,w,h; u32 argb; u8 radius; u8 blur; i8 spread; i16 ofs_x,ofs_y; u8 bg_cover (20) */
  OP_BOX_SHADOW: 0x13,
  /** i16 x1,y1,x2,y2; u32 argb; u8 w; u8 dash_w; u8 dash_gap; u8 cap_bits (16) */
  OP_LINE_EX: 0x14,
  /** i16 x,y; u32 blob_id; u32 argb   (12) */
  OP_GLYPH: 0x20,
  /** i16 x,y,w,h; u32 blob_id         (12) */
  OP_IMAGE: 0x21,
  /** i16 cx,cy; u16 r; i16 a0,a1; u32 argb; u8 w (15) */
  OP_ARC: 0x22,
  /** i16 x,y,w,h                      (8) */
  OP_SET_CLIP: 0x30,
  /** (none)                           (0) */
  OP_CLEAR_CLIP: 0x31,
  /** u32 id; u16 w,h; u8 fmt; bytes[] (variable) */
  OP_BLOB_UPLOAD: 0x40,
  /** u32 id                           (4) */
  OP_BLOB_EVICT: 0x41,
  /** u8 state; i16 x,y                (5) */
  OP_INPUT_POINTER: 0xF0,
  /** u8 state; u16 key                (3) */
  OP_INPUT_KEY: 0xF1,
  BLOB_FMT_A8: 0,
  BLOB_FMT_ARGB8888: 1,
  BLOB_FMT_RGB565: 2,
  INPUT_STATE_PRESSED: 1,
  INPUT_STATE_RELEASED: 0,
});
