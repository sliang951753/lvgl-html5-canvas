/**
 * @file encoder.h
 * Tiny little-endian writer for protocol frames.
 *
 * Usage:
 *   lhc_enc_t e;
 *   lhc_enc_init(&e, buf, buf_cap);
 *   lhc_enc_begin_frame(&e, frame_id, w, h);
 *   lhc_enc_fill_rect(&e, x, y, w, h, argb, radius);
 *   ...
 *   lhc_enc_end_frame(&e);
 *   size_t n = lhc_enc_finalize(&e);   // updates cmd_count, returns bytes
 *
 * All multi-byte fields are written little-endian regardless of host.
 */
#ifndef LHC_ENCODER_H
#define LHC_ENCODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;            /* next write offset */
    uint16_t cmd_count;      /* incremented per command */
    bool     overflow;       /* sticky: set on first oversize write */
    bool     frame_open;     /* between begin_frame and end_frame */
} lhc_enc_t;

void lhc_enc_init(lhc_enc_t *e, uint8_t *buf, size_t cap);

/* Frame markers (begin reserves 4-byte frame header). */
void lhc_enc_begin_frame(lhc_enc_t *e, uint16_t frame_id, int16_t w, int16_t h);
void lhc_enc_end_frame(lhc_enc_t *e);

/* Returns total bytes written, or 0 on overflow. */
size_t lhc_enc_finalize(lhc_enc_t *e);

/* Drawing ops. */
void lhc_enc_fill_rect(lhc_enc_t *e, int16_t x, int16_t y, int16_t w, int16_t h,
                       uint32_t argb, uint8_t radius);
void lhc_enc_border(lhc_enc_t *e, int16_t x, int16_t y, int16_t w, int16_t h,
                    uint32_t argb, uint8_t width, uint8_t radius, uint8_t side);

/* Low-level escape hatch (also used by tests). */
bool lhc_enc_cmd(lhc_enc_t *e, uint8_t opcode, uint8_t flags,
                 const void *payload, uint16_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* LHC_ENCODER_H */
