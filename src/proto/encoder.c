#include "encoder.h"

#include <string.h>

static inline void wr_u8(lhc_enc_t *e, uint8_t v)
{
    if (e->pos + 1 > e->cap) { e->overflow = true; return; }
    e->buf[e->pos++] = v;
}
static inline void wr_u16(lhc_enc_t *e, uint16_t v)
{
    if (e->pos + 2 > e->cap) { e->overflow = true; return; }
    e->buf[e->pos++] = (uint8_t)(v & 0xFF);
    e->buf[e->pos++] = (uint8_t)((v >> 8) & 0xFF);
}
static inline void wr_i16(lhc_enc_t *e, int16_t v) { wr_u16(e, (uint16_t)v); }
static inline void wr_u32(lhc_enc_t *e, uint32_t v)
{
    if (e->pos + 4 > e->cap) { e->overflow = true; return; }
    e->buf[e->pos++] = (uint8_t)(v & 0xFF);
    e->buf[e->pos++] = (uint8_t)((v >> 8) & 0xFF);
    e->buf[e->pos++] = (uint8_t)((v >> 16) & 0xFF);
    e->buf[e->pos++] = (uint8_t)((v >> 24) & 0xFF);
}

void lhc_enc_init(lhc_enc_t *e, uint8_t *buf, size_t cap)
{
    e->buf = buf;
    e->cap = cap;
    e->pos = 0;
    e->cmd_count = 0;
    e->overflow = false;
    e->frame_open = false;
}

bool lhc_enc_cmd(lhc_enc_t *e, uint8_t opcode, uint8_t flags,
                 const void *payload, uint16_t payload_len)
{
    if (e->overflow) return false;
    if (e->pos + LHC_CMD_HDR_SIZE + payload_len > e->cap) {
        e->overflow = true;
        return false;
    }
    wr_u8(e, opcode);
    wr_u8(e, flags);
    wr_u16(e, payload_len);
    if (payload_len) {
        memcpy(e->buf + e->pos, payload, payload_len);
        e->pos += payload_len;
    }
    e->cmd_count++;
    return !e->overflow;
}

void lhc_enc_begin_frame(lhc_enc_t *e, uint16_t frame_id, int16_t w, int16_t h)
{
    /* Reserve 4-byte frame header; we'll patch cmd_count on finalize. */
    if (e->cap < LHC_FRAME_HDR_SIZE) { e->overflow = true; return; }
    e->buf[0] = (uint8_t)(frame_id & 0xFF);
    e->buf[1] = (uint8_t)((frame_id >> 8) & 0xFF);
    /* cmd_count placeholder, patched in finalize */
    e->buf[2] = 0;
    e->buf[3] = 0;
    e->pos = LHC_FRAME_HDR_SIZE;
    e->cmd_count = 0;
    e->frame_open = true;

    uint8_t p[4];
    p[0] = (uint8_t)(w & 0xFF); p[1] = (uint8_t)((w >> 8) & 0xFF);
    p[2] = (uint8_t)(h & 0xFF); p[3] = (uint8_t)((h >> 8) & 0xFF);
    lhc_enc_cmd(e, LHC_OP_BEGIN_FRAME, 0, p, 4);
}

void lhc_enc_end_frame(lhc_enc_t *e)
{
    lhc_enc_cmd(e, LHC_OP_END_FRAME, 0, NULL, 0);
    e->frame_open = false;
}

size_t lhc_enc_finalize(lhc_enc_t *e)
{
    if (e->overflow) return 0;
    /* Patch cmd_count at offset 2..3 (little-endian). */
    e->buf[2] = (uint8_t)(e->cmd_count & 0xFF);
    e->buf[3] = (uint8_t)((e->cmd_count >> 8) & 0xFF);
    return e->pos;
}

void lhc_enc_fill_rect(lhc_enc_t *e, int16_t x, int16_t y, int16_t w, int16_t h,
                       uint32_t argb, uint8_t radius)
{
    uint8_t p[13];
    p[0] = (uint8_t)(x & 0xFF);   p[1] = (uint8_t)((x >> 8) & 0xFF);
    p[2] = (uint8_t)(y & 0xFF);   p[3] = (uint8_t)((y >> 8) & 0xFF);
    p[4] = (uint8_t)(w & 0xFF);   p[5] = (uint8_t)((w >> 8) & 0xFF);
    p[6] = (uint8_t)(h & 0xFF);   p[7] = (uint8_t)((h >> 8) & 0xFF);
    p[8]  = (uint8_t)(argb & 0xFF);
    p[9]  = (uint8_t)((argb >> 8) & 0xFF);
    p[10] = (uint8_t)((argb >> 16) & 0xFF);
    p[11] = (uint8_t)((argb >> 24) & 0xFF);
    p[12] = radius;
    uint8_t flags = ((argb >> 24) != 0xFF) ? LHC_FLAG_HAS_ALPHA_HINT : 0;
    lhc_enc_cmd(e, LHC_OP_FILL_RECT, flags, p, sizeof(p));
}

void lhc_enc_border(lhc_enc_t *e, int16_t x, int16_t y, int16_t w, int16_t h,
                    uint32_t argb, uint8_t width, uint8_t radius, uint8_t side)
{
    uint8_t p[15];
    p[0] = (uint8_t)(x & 0xFF);   p[1] = (uint8_t)((x >> 8) & 0xFF);
    p[2] = (uint8_t)(y & 0xFF);   p[3] = (uint8_t)((y >> 8) & 0xFF);
    p[4] = (uint8_t)(w & 0xFF);   p[5] = (uint8_t)((w >> 8) & 0xFF);
    p[6] = (uint8_t)(h & 0xFF);   p[7] = (uint8_t)((h >> 8) & 0xFF);
    p[8]  = (uint8_t)(argb & 0xFF);
    p[9]  = (uint8_t)((argb >> 8) & 0xFF);
    p[10] = (uint8_t)((argb >> 16) & 0xFF);
    p[11] = (uint8_t)((argb >> 24) & 0xFF);
    p[12] = width;
    p[13] = radius;
    p[14] = side;
    uint8_t flags = ((argb >> 24) != 0xFF) ? LHC_FLAG_HAS_ALPHA_HINT : 0;
    lhc_enc_cmd(e, LHC_OP_BORDER, flags, p, sizeof(p));
}

void lhc_enc_line(lhc_enc_t *e, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                  uint32_t argb, uint8_t width)
{
    uint8_t p[13];
    p[0] = (uint8_t)(x1 & 0xFF);   p[1] = (uint8_t)((x1 >> 8) & 0xFF);
    p[2] = (uint8_t)(y1 & 0xFF);   p[3] = (uint8_t)((y1 >> 8) & 0xFF);
    p[4] = (uint8_t)(x2 & 0xFF);   p[5] = (uint8_t)((x2 >> 8) & 0xFF);
    p[6] = (uint8_t)(y2 & 0xFF);   p[7] = (uint8_t)((y2 >> 8) & 0xFF);
    p[8]  = (uint8_t)(argb & 0xFF);
    p[9]  = (uint8_t)((argb >> 8) & 0xFF);
    p[10] = (uint8_t)((argb >> 16) & 0xFF);
    p[11] = (uint8_t)((argb >> 24) & 0xFF);
    p[12] = width;
    uint8_t flags = ((argb >> 24) != 0xFF) ? LHC_FLAG_HAS_ALPHA_HINT : 0;
    lhc_enc_cmd(e, LHC_OP_LINE, flags, p, sizeof(p));
}

void lhc_enc_arc(lhc_enc_t *e, int16_t cx, int16_t cy, uint16_t radius,
                 int16_t start_angle, int16_t end_angle,
                 uint32_t argb, uint8_t width)
{
    uint8_t p[15];
    p[0] = (uint8_t)(cx & 0xFF);           p[1] = (uint8_t)((cx >> 8) & 0xFF);
    p[2] = (uint8_t)(cy & 0xFF);           p[3] = (uint8_t)((cy >> 8) & 0xFF);
    p[4] = (uint8_t)(radius & 0xFF);       p[5] = (uint8_t)((radius >> 8) & 0xFF);
    p[6] = (uint8_t)(start_angle & 0xFF);  p[7] = (uint8_t)((start_angle >> 8) & 0xFF);
    p[8] = (uint8_t)(end_angle & 0xFF);    p[9] = (uint8_t)((end_angle >> 8) & 0xFF);
    p[10] = (uint8_t)(argb & 0xFF);
    p[11] = (uint8_t)((argb >> 8) & 0xFF);
    p[12] = (uint8_t)((argb >> 16) & 0xFF);
    p[13] = (uint8_t)((argb >> 24) & 0xFF);
    p[14] = width;
    uint8_t flags = ((argb >> 24) != 0xFF) ? LHC_FLAG_HAS_ALPHA_HINT : 0;
    lhc_enc_cmd(e, LHC_OP_ARC, flags, p, sizeof(p));
}

void lhc_enc_box_shadow(lhc_enc_t *e, int16_t x, int16_t y, int16_t w, int16_t h,
                        uint32_t argb, uint8_t radius, uint8_t blur,
                        int8_t spread, int16_t ofs_x, int16_t ofs_y, uint8_t bg_cover)
{
    uint8_t p[20];
    p[0] = (uint8_t)(x & 0xFF);   p[1] = (uint8_t)((x >> 8) & 0xFF);
    p[2] = (uint8_t)(y & 0xFF);   p[3] = (uint8_t)((y >> 8) & 0xFF);
    p[4] = (uint8_t)(w & 0xFF);   p[5] = (uint8_t)((w >> 8) & 0xFF);
    p[6] = (uint8_t)(h & 0xFF);   p[7] = (uint8_t)((h >> 8) & 0xFF);
    p[8]  = (uint8_t)(argb & 0xFF);
    p[9]  = (uint8_t)((argb >> 8) & 0xFF);
    p[10] = (uint8_t)((argb >> 16) & 0xFF);
    p[11] = (uint8_t)((argb >> 24) & 0xFF);
    p[12] = radius;
    p[13] = blur;
    p[14] = (uint8_t)spread;
    p[15] = (uint8_t)(ofs_x & 0xFF); p[16] = (uint8_t)((ofs_x >> 8) & 0xFF);
    p[17] = (uint8_t)(ofs_y & 0xFF); p[18] = (uint8_t)((ofs_y >> 8) & 0xFF);
    p[19] = bg_cover;
    /* shadow is almost always semi-transparent -> hint always set */
    lhc_enc_cmd(e, LHC_OP_BOX_SHADOW, LHC_FLAG_HAS_ALPHA_HINT, p, sizeof(p));
}

void lhc_enc_image(lhc_enc_t *e, int16_t x, int16_t y, int16_t w, int16_t h,
                   uint32_t blob_id)
{
    uint8_t p[12];
    p[0]  = (uint8_t)(x & 0xFF);  p[1]  = (uint8_t)((x >> 8) & 0xFF);
    p[2]  = (uint8_t)(y & 0xFF);  p[3]  = (uint8_t)((y >> 8) & 0xFF);
    p[4]  = (uint8_t)(w & 0xFF);  p[5]  = (uint8_t)((w >> 8) & 0xFF);
    p[6]  = (uint8_t)(h & 0xFF);  p[7]  = (uint8_t)((h >> 8) & 0xFF);
    p[8]  = (uint8_t)(blob_id & 0xFF);
    p[9]  = (uint8_t)((blob_id >> 8) & 0xFF);
    p[10] = (uint8_t)((blob_id >> 16) & 0xFF);
    p[11] = (uint8_t)((blob_id >> 24) & 0xFF);
    /* Images with alpha are the common case for icons/sprites. */
    lhc_enc_cmd(e, LHC_OP_IMAGE, LHC_FLAG_HAS_ALPHA_HINT, p, sizeof(p));
}

bool lhc_enc_blob_upload(lhc_enc_t *e, uint32_t blob_id, uint16_t w, uint16_t h,
                         uint8_t fmt, const uint8_t *data, size_t data_len)
{
    if (e->overflow) return false;
    /* payload = 9-byte header + raw bytes; payload_len is u16 -> max 65535 */
    const size_t hdr_bytes = 9;
    if (data_len + hdr_bytes > 0xFFFF) { return false; }
    uint16_t payload_len = (uint16_t)(hdr_bytes + data_len);
    if (e->pos + LHC_CMD_HDR_SIZE + payload_len > e->cap) {
        e->overflow = true;
        return false;
    }
    /* command header */
    wr_u8(e, LHC_OP_BLOB_UPLOAD);
    wr_u8(e, 0);
    wr_u16(e, payload_len);
    /* blob header */
    wr_u32(e, blob_id);
    wr_u16(e, w);
    wr_u16(e, h);
    wr_u8(e, fmt);
    if (data_len) {
        memcpy(e->buf + e->pos, data, data_len);
        e->pos += data_len;
    }
    e->cmd_count++;
    return !e->overflow;
}

void lhc_enc_blob_evict(lhc_enc_t *e, uint32_t blob_id)
{
    uint8_t p[4];
    p[0] = (uint8_t)(blob_id & 0xFF);
    p[1] = (uint8_t)((blob_id >> 8) & 0xFF);
    p[2] = (uint8_t)((blob_id >> 16) & 0xFF);
    p[3] = (uint8_t)((blob_id >> 24) & 0xFF);
    lhc_enc_cmd(e, LHC_OP_BLOB_EVICT, 0, p, sizeof(p));
}
