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
void lhc_enc_line(lhc_enc_t *e, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                  uint32_t argb, uint8_t width);
void lhc_enc_line_ex(lhc_enc_t *e, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                     uint32_t argb, uint8_t width,
                     uint8_t dash_width, uint8_t dash_gap, uint8_t cap_bits);
void lhc_enc_arc(lhc_enc_t *e, int16_t cx, int16_t cy, uint16_t radius,
                 int16_t start_angle, int16_t end_angle,
                 uint32_t argb, uint8_t width);
void lhc_enc_box_shadow(lhc_enc_t *e, int16_t x, int16_t y, int16_t w, int16_t h,
                        uint32_t argb, uint8_t radius, uint8_t blur,
                        int8_t spread, int16_t ofs_x, int16_t ofs_y,
                        uint8_t bg_cover);
void lhc_enc_image(lhc_enc_t *e, int16_t x, int16_t y, int16_t w, int16_t h,
                   uint32_t blob_id);
/* BLOB_UPLOAD: data points to (w*h) pixels in canvas RGBA order for ARGB8888.
 * Returns false on overflow (frame buffer full); caller can choose to skip
 * the upload AND the IMAGE op that would reference it. */
bool lhc_enc_blob_upload(lhc_enc_t *e, uint32_t blob_id, uint16_t w, uint16_t h,
                         uint8_t fmt, const uint8_t *data, size_t data_len);
void lhc_enc_blob_evict(lhc_enc_t *e, uint32_t blob_id);
/* Low-level escape hatch (also used by tests). */
bool lhc_enc_cmd(lhc_enc_t *e, uint8_t opcode, uint8_t flags,
                 const void *payload, uint16_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* LHC_ENCODER_H */
