/**
 * @file protocol.h
 * Single source of truth for the lvgl-html5-canvas wire protocol.
 *
 * Parsed by src/proto/gen_js_protocol.py to emit web/protocol.js.
 *
 * Wire format: little-endian. See docs/protocol.md.
 */
#ifndef LHC_PROTOCOL_H
#define LHC_PROTOCOL_H

#include <stdint.h>

/* ---- protocol version ---- */
#define LHC_PROTO_VERSION       0

/* ---- frame header (4 bytes) ---- */
/* layout: u16 frame_id; u16 cmd_count; */
#define LHC_FRAME_HDR_SIZE      4

/* ---- command header (4 bytes) ---- */
/* layout: u8 opcode; u8 flags; u16 payload_len; */
#define LHC_CMD_HDR_SIZE        4

/* ---- flag bits ---- */
#define LHC_FLAG_HAS_ALPHA_HINT (1u << 0)

/* ---- opcodes ---- */
/* @op category: control */
#define LHC_OP_BEGIN_FRAME      0x01    /* payload: i16 w, i16 h            (4) */
#define LHC_OP_END_FRAME        0x02    /* payload: (none)                  (0) */

/* @op category: 2D primitives */
#define LHC_OP_FILL_RECT        0x10    /* i16 x,y,w,h; u32 argb; u8 radius (13) */
#define LHC_OP_BORDER           0x11    /* i16 x,y,w,h; u32 argb; u8 w; u8 r; u8 side (15) */
#define LHC_OP_LINE             0x12    /* i16 x1,y1,x2,y2; u32 argb; u8 w  (13) */
#define LHC_OP_BOX_SHADOW       0x13    /* i16 x,y,w,h; u32 argb; u8 radius; u8 blur; i8 spread; i16 ofs_x,ofs_y; u8 bg_cover (20) */

/* @op category: glyph / image */
#define LHC_OP_GLYPH            0x20    /* i16 x,y; u32 blob_id; u32 argb   (12) */
#define LHC_OP_IMAGE            0x21    /* i16 x,y,w,h; u32 blob_id         (12) */
#define LHC_OP_ARC              0x22    /* i16 cx,cy; u16 r; i16 a0,a1; u32 argb; u8 w (15) */

/* @op category: clip */
#define LHC_OP_SET_CLIP         0x30    /* i16 x,y,w,h                      (8) */
#define LHC_OP_CLEAR_CLIP       0x31    /* (none)                           (0) */

/* @op category: blob mgmt */
#define LHC_OP_BLOB_UPLOAD      0x40    /* u32 id; u16 w,h; u8 fmt; bytes[] (variable) */
#define LHC_OP_BLOB_EVICT       0x41    /* u32 id                           (4) */

/* @op category: upstream */
#define LHC_OP_INPUT_POINTER    0xF0    /* u8 state; i16 x,y                (5) */
#define LHC_OP_INPUT_KEY        0xF1    /* u8 state; u16 key                (3) */

/* ---- blob formats ---- */
#define LHC_BLOB_FMT_A8         0
#define LHC_BLOB_FMT_ARGB8888   1
#define LHC_BLOB_FMT_RGB565     2

/* ---- pointer/key state ---- */
#define LHC_INPUT_STATE_PRESSED  1
#define LHC_INPUT_STATE_RELEASED 0

#endif /* LHC_PROTOCOL_H */
