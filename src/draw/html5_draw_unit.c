/**
 * @file html5_draw_unit.c
 * M1: real FILL_RECT handling.
 *  - evaluate(): claim LV_DRAW_TASK_TYPE_FILL when it's a solid colour
 *    (no gradient, no skew). Score 80 (beats SW's 100).
 *  - dispatch(): pull tasks tagged for us, encode FILL_RECT into the
 *    current frame buffer, mark FINISHED.
 *  - begin_frame / flush_frame are driven from main.c on REFR_START /
 *    REFR_READY display events, then the buffer is broadcast over WS.
 *
 * M3a extension: claim and encode simple LV_DRAW_TASK_TYPE_LINE tasks
 * (single segment only; no polyline/dash/round caps yet) as OP_LINE (0x12).
 *
 * Frame buffer is a single static 64 KiB scratch — fine for M1 demos.
 * Overflows are dropped (logged once per frame) so the SW unit still
 * renders the picture correctly even if we lose ops.
 */
#include "html5_draw_unit.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "lvgl.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/lv_draw_image.h"
#include "src/draw/lv_draw_line.h"
#include "src/draw/lv_draw_rect.h"
#include "src/draw/lv_image_dsc.h"
#include "src/misc/lv_grad.h"
#include "src/misc/lv_style.h"

#include "../proto/encoder.h"
#include "../transport/ws_server.h"

#define LHC_DRAW_UNIT_ID_HTML5  50
/* Frame buffer must hold the largest BLOB_UPLOAD payload too. We cap blobs
 * at 128x128 ARGB (64 KiB pixels + 13 B header) so 256 KiB leaves room for
 * multiple uploads + the regular draw ops on the same frame. */
#define LHC_FRAME_BUF_SIZE      (256 * 1024)
/* Max image we'll claim, both dimensions inclusive. */
#define LHC_IMG_MAX_DIM         128
/* Re-broadcast every cached blob every N frames so a viewer that reconnects
 * mid-stream eventually receives the bitmap it needs. */
#define LHC_BLOB_REFRESH_PERIOD 60
/* LRU slots — every distinct image source the demo uses. Keep small. */
#define LHC_BLOB_CACHE_SLOTS    16

typedef struct {
    lv_draw_unit_t base;
} lhc_draw_unit_t;

static lhc_draw_unit_t   *g_unit = NULL;
static lhc_ws_server_t   *g_ws = NULL;
static lhc_enc_t          g_enc;
static uint8_t            g_buf[LHC_FRAME_BUF_SIZE];
static uint16_t           g_frame_id = 0;
static bool               g_frame_open = false;
static bool               g_overflow_logged_this_frame = false;
static lhc_html5_stats_t  g_stats;
static uint32_t           g_ops_this_frame = 0;

/* ---- blob cache (per-server, simple LRU by last_used) ---- */
typedef struct {
    bool       in_use;
    const void *src_ptr;     /* lv_image_dsc_t* — used as identity key */
    uint32_t   blob_id;      /* FNV-1a 32 of (cf,w,h,data bytes) */
    uint16_t   w, h;
    uint8_t    cf;
    uint32_t   last_used_frame;
    uint16_t   last_uploaded_frame;
    bool       uploaded;
} lhc_blob_slot_t;

static lhc_blob_slot_t g_blobs[LHC_BLOB_CACHE_SLOTS];

static uint32_t fnv1a32(const uint8_t *data, size_t n, uint32_t seed)
{
    uint32_t h = seed;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

/* ---- helpers ---- */

static uint32_t color_to_argb(lv_color_t c, lv_opa_t opa)
{
    /* lv_color_t in LVGL 9 = { blue, green, red } (see lv_color.h) */
    uint32_t a = opa;
    return (a << 24) | ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | (uint32_t)c.blue;
}

static bool fill_is_simple(const lv_draw_fill_dsc_t *d)
{
    /* Solid colour fills only — no gradients, no transparent ops. */
    if (!d) return false;
    if (d->grad.dir != LV_GRAD_DIR_NONE) return false;
    if (d->opa == 0) return false;
    return true;
}

static bool border_is_simple(const lv_draw_border_dsc_t *d)
{
    /* M2: any solid-colour border with positive width is fine.
     * Side bitmap is forwarded verbatim — viewer decides which edges
     * to stroke. NONE / INTERNAL we skip (SW handles or it's a no-op). */
    if (!d) return false;
    if (d->opa == 0) return false;
    if (d->width <= 0) return false;
    if (d->side == LV_BORDER_SIDE_NONE) return false;
    return true;
}

static bool box_shadow_is_simple(const lv_draw_box_shadow_dsc_t *d)
{
    /* M2: any visible box shadow. We forward width(=blur)/spread/offset
     * verbatim — viewer maps to ctx.shadow* on a 1x1 invisible rect or
     * stroked path. Skip zero-opa / zero-blur AND zero-spread AND zero-offset
     * (truly invisible) but accept anything else. */
    if (!d) return false;
    if (d->opa == 0) return false;
    if (d->width == 0 && d->spread == 0 && d->ofs_x == 0 && d->ofs_y == 0) return false;
    return true;
}

static bool line_is_simple(const lv_draw_line_dsc_t *d)
{
    if (!d) return false;
    if (d->opa == 0) return false;
    if (d->width <= 0) return false;
    if (d->dash_width != 0 || d->dash_gap != 0) return false;

    /* M3a: accept both direct (p1/p2) and polyline-backed line tasks.
     * We'll encode a single segment from p1/p2; when LVGL stores points,
     * SW may still iterate additional segments separately. */
    return true;
}

/* M2c: variable-form image, only the easy cases.
 *  - no rotation / no scale != 256 / no skew / no tile / no mask / no recolor
 *  - source must be a constant lv_image_dsc_t* (in-binary, not file)
 *  - color format ARGB8888 or XRGB8888 (no palette, no compression)
 *  - dimensions ≤ LHC_IMG_MAX_DIM each (so one BLOB_UPLOAD fits in u16 len)
 * Anything else falls through to SW. */
static bool image_is_simple(const lv_draw_image_dsc_t *d)
{
    if (!d) return false;
    if (d->opa == 0) return false;
    if (d->rotation != 0) return false;
    if (d->scale_x != LV_SCALE_NONE || d->scale_y != LV_SCALE_NONE) return false;
    if (d->skew_x != 0 || d->skew_y != 0) return false;
    if (d->tile) return false;
    if (d->bitmap_mask_src) return false;
    if (d->recolor_opa != 0) return false;
    if (d->clip_radius != 0) return false;
    if (!d->src) return false;
    if (lv_image_src_get_type(d->src) != LV_IMAGE_SRC_VARIABLE) return false;
    const lv_image_dsc_t *img = (const lv_image_dsc_t *)d->src;
    if (img->header.magic != LV_IMAGE_HEADER_MAGIC) return false;
    if (img->header.flags & LV_IMAGE_FLAGS_COMPRESSED) return false;
    if (img->header.cf != LV_COLOR_FORMAT_ARGB8888 &&
        img->header.cf != LV_COLOR_FORMAT_XRGB8888) return false;
    if (img->header.w == 0 || img->header.h == 0) return false;
    if (img->header.w > LHC_IMG_MAX_DIM || img->header.h > LHC_IMG_MAX_DIM) return false;
    if (!img->data || img->data_size == 0) return false;
    return true;
}

/* LAYER task uses lv_draw_image_dsc_t too, but d->src is lv_layer_t*.
 * We only claim simple, non-transformed layer blends whose backing draw_buf
 * is ARGB8888/XRGB8888 and fits our M2 image size cap. */
static bool layer_is_simple(const lv_draw_image_dsc_t *d)
{
    if (!d) return false;
    if (d->opa == 0) return false;
    if (d->rotation != 0) return false;
    if (d->scale_x != LV_SCALE_NONE || d->scale_y != LV_SCALE_NONE) return false;
    if (d->skew_x != 0 || d->skew_y != 0) return false;
    if (d->tile) return false;
    if (d->bitmap_mask_src) return false;
    if (d->recolor_opa != 0) return false;
    if (d->clip_radius != 0) return false;
    if (!d->src) return false;

    const lv_layer_t *layer = (const lv_layer_t *)d->src;
    const lv_draw_buf_t *buf = layer->draw_buf;
    if (!buf || !buf->data) return false;
    if (buf->header.magic != LV_IMAGE_HEADER_MAGIC) return false;
    if (buf->header.cf != LV_COLOR_FORMAT_ARGB8888 &&
        buf->header.cf != LV_COLOR_FORMAT_XRGB8888) return false;
    if (buf->header.w == 0 || buf->header.h == 0) return false;
    if (buf->header.w > LHC_IMG_MAX_DIM || buf->header.h > LHC_IMG_MAX_DIM) return false;
    if (buf->data_size == 0) return false;
    return true;
}

/* Find slot for this src; allocate via LRU if absent. Returns NULL on
 * out-of-cache (caller falls back to SW). Computes blob_id on insert. */
static lhc_blob_slot_t *blob_cache_get_or_insert(const lv_image_dsc_t *img)
{
    /* hit */
    for (int i = 0; i < LHC_BLOB_CACHE_SLOTS; ++i) {
        if (g_blobs[i].in_use && g_blobs[i].src_ptr == img) {
            g_blobs[i].last_used_frame = g_frame_id;
            return &g_blobs[i];
        }
    }
    /* miss: find empty slot or LRU victim */
    int victim = -1;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < LHC_BLOB_CACHE_SLOTS; ++i) {
        if (!g_blobs[i].in_use) { victim = i; break; }
        if (g_blobs[i].last_used_frame < oldest) {
            oldest = g_blobs[i].last_used_frame;
            victim = i;
        }
    }
    if (victim < 0) return NULL;
    lhc_blob_slot_t *s = &g_blobs[victim];
    /* If we are evicting, tell the viewer to drop the old blob. */
    if (s->in_use && s->uploaded && g_frame_open) {
        lhc_enc_blob_evict(&g_enc, s->blob_id);
    }
    /* Seed with cf/w/h then hash the pixel data. */
    uint32_t seed = 0x811C9DC5u;
    uint8_t hdr_bytes[5] = {
        (uint8_t)img->header.cf,
        (uint8_t)(img->header.w & 0xFF), (uint8_t)((img->header.w >> 8) & 0xFF),
        (uint8_t)(img->header.h & 0xFF), (uint8_t)((img->header.h >> 8) & 0xFF),
    };
    seed = fnv1a32(hdr_bytes, sizeof(hdr_bytes), seed);
    s->in_use   = true;
    s->src_ptr  = img;
    s->w        = (uint16_t)img->header.w;
    s->h        = (uint16_t)img->header.h;
    s->cf       = (uint8_t)img->header.cf;
    s->blob_id  = fnv1a32(img->data, img->data_size, seed);
    s->last_used_frame = g_frame_id;
    s->last_uploaded_frame = 0;
    s->uploaded = false;
    return s;
}

/* Convert LVGL ARGB8888 bytes (B,G,R,A per pixel) to canvas-native RGBA
 * (R,G,B,A) in-place into 'dst'. For XRGB8888 we force A=255. */
static void blit_to_rgba(uint8_t *dst, const uint8_t *src, uint16_t w, uint16_t h,
                         uint8_t cf, uint32_t stride_bytes)
{
    const bool has_alpha = (cf == LV_COLOR_FORMAT_ARGB8888);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *row = src + (size_t)y * stride_bytes;
        uint8_t *out = dst + (size_t)y * w * 4u;
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t b = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t r = row[x * 4 + 2];
            uint8_t a = has_alpha ? row[x * 4 + 3] : 0xFF;
            out[x * 4 + 0] = r;
            out[x * 4 + 1] = g;
            out[x * 4 + 2] = b;
            out[x * 4 + 3] = a;
        }
    }
}

/* Upload the blob if not yet uploaded this session, or if the refresh period
 * has elapsed (for late-arriving viewers). Returns true if the blob is
 * available on the viewer side by end of this frame, false on failure. */
static bool blob_ensure_uploaded(lhc_blob_slot_t *s, const lv_image_dsc_t *img)
{
    /* Already uploaded recently? */
    if (s->uploaded) {
        uint16_t age = (uint16_t)(g_frame_id - s->last_uploaded_frame);
        if (age < LHC_BLOB_REFRESH_PERIOD) return true;
    }
    /* Need to (re)upload. */
    uint32_t stride = img->header.stride ? img->header.stride : (uint32_t)img->header.w * 4u;
    size_t pixel_bytes = (size_t)img->header.w * img->header.h * 4u;
    /* Scratch on stack — capped by LHC_IMG_MAX_DIM^2*4 = 64 KiB. */
    static uint8_t pixbuf[LHC_IMG_MAX_DIM * LHC_IMG_MAX_DIM * 4];
    if (pixel_bytes > sizeof(pixbuf)) return false;
    blit_to_rgba(pixbuf, img->data, s->w, s->h, s->cf, stride);
    bool ok = lhc_enc_blob_upload(&g_enc, s->blob_id, s->w, s->h,
                                  LHC_BLOB_FMT_ARGB8888, pixbuf, pixel_bytes);
    if (!ok) return false;
    s->uploaded = true;
    s->last_uploaded_frame = g_frame_id;
    g_stats.blobs_uploaded++;
    g_stats.blob_bytes_sent += (uint32_t)pixel_bytes;
    return true;
}

/* ---- draw-unit callbacks ---- */

static int32_t lhc_evaluate_cb(lv_draw_unit_t *du, lv_draw_task_t *task)
{
    (void)du;
    g_stats.evaluate_calls++;

    if (task->type == LV_DRAW_TASK_TYPE_FILL) {
        const lv_draw_fill_dsc_t *d = (const lv_draw_fill_dsc_t *)task->draw_dsc;
        if (!fill_is_simple(d)) return 0;
    } else if (task->type == LV_DRAW_TASK_TYPE_BORDER) {
        const lv_draw_border_dsc_t *d = (const lv_draw_border_dsc_t *)task->draw_dsc;
        if (!border_is_simple(d)) return 0;
    } else if (task->type == LV_DRAW_TASK_TYPE_BOX_SHADOW) {
        const lv_draw_box_shadow_dsc_t *d = (const lv_draw_box_shadow_dsc_t *)task->draw_dsc;
        if (!box_shadow_is_simple(d)) return 0;
    } else if (task->type == LV_DRAW_TASK_TYPE_LINE) {
        const lv_draw_line_dsc_t *d = (const lv_draw_line_dsc_t *)task->draw_dsc;
        if (!line_is_simple(d)) return 0;
    } else if (task->type == LV_DRAW_TASK_TYPE_IMAGE) {
        const lv_draw_image_dsc_t *d = (const lv_draw_image_dsc_t *)task->draw_dsc;
        if (!image_is_simple(d)) return 0;
    } else if (task->type == LV_DRAW_TASK_TYPE_LAYER) {
        const lv_draw_image_dsc_t *d = (const lv_draw_image_dsc_t *)task->draw_dsc;
        if (!layer_is_simple(d)) return 0;
    } else {
        return 0;
    }

    /* Beat SW (score 100). 80 = 20% faster claim. */
    if (task->preference_score > 80) {
        task->preference_score = 80;
        task->preferred_draw_unit_id = LHC_DRAW_UNIT_ID_HTML5;
    }
    return 1;
}

static int32_t lhc_dispatch_cb(lv_draw_unit_t *du, lv_layer_t *layer)
{
    (void)du;
    g_stats.dispatch_calls++;

    lv_draw_task_t *t = lv_draw_get_next_available_task(layer, NULL, LHC_DRAW_UNIT_ID_HTML5);
    if (!t) return LV_DRAW_UNIT_IDLE;

    /* take it */
    t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;

    if (g_frame_open) {
        int16_t x = (int16_t)t->area.x1;
        int16_t y = (int16_t)t->area.y1;
        int16_t w = (int16_t)(t->area.x2 - t->area.x1 + 1);
        int16_t h = (int16_t)(t->area.y2 - t->area.y1 + 1);

        if (t->type == LV_DRAW_TASK_TYPE_FILL) {
            const lv_draw_fill_dsc_t *d = (const lv_draw_fill_dsc_t *)t->draw_dsc;
            uint32_t opa = (uint32_t)d->opa * (uint32_t)t->opa / 255u;
            uint32_t argb = color_to_argb(d->color, (lv_opa_t)opa);
            uint8_t  radius = (d->radius < 0) ? 0 :
                              (d->radius > 255 ? 255 : (uint8_t)d->radius);
            lhc_enc_fill_rect(&g_enc, x, y, w, h, argb, radius);
            g_ops_this_frame++;
            g_stats.fills_encoded++;
        } else if (t->type == LV_DRAW_TASK_TYPE_BORDER) {
            const lv_draw_border_dsc_t *d = (const lv_draw_border_dsc_t *)t->draw_dsc;
            uint32_t opa = (uint32_t)d->opa * (uint32_t)t->opa / 255u;
            uint32_t argb = color_to_argb(d->color, (lv_opa_t)opa);
            int32_t bw = d->width; if (bw < 1) bw = 1; if (bw > 255) bw = 255;
            uint8_t  radius = (d->radius < 0) ? 0 :
                              (d->radius > 255 ? 255 : (uint8_t)d->radius);
            uint8_t  side   = (uint8_t)(d->side & 0xFF);
            lhc_enc_border(&g_enc, x, y, w, h, argb, (uint8_t)bw, radius, side);
            g_ops_this_frame++;
            g_stats.borders_encoded++;
        } else if (t->type == LV_DRAW_TASK_TYPE_BOX_SHADOW) {
            const lv_draw_box_shadow_dsc_t *d = (const lv_draw_box_shadow_dsc_t *)t->draw_dsc;
            uint32_t opa = (uint32_t)d->opa * (uint32_t)t->opa / 255u;
            uint32_t argb = color_to_argb(d->color, (lv_opa_t)opa);
            uint8_t  radius = (d->radius < 0) ? 0 :
                              (d->radius > 255 ? 255 : (uint8_t)d->radius);
            int32_t bw = d->width; if (bw < 0) bw = 0; if (bw > 255) bw = 255;
            int32_t sp = d->spread; if (sp < -128) sp = -128; if (sp > 127) sp = 127;
            int32_t ox = d->ofs_x; if (ox < -32768) ox = -32768; if (ox > 32767) ox = 32767;
            int32_t oy = d->ofs_y; if (oy < -32768) oy = -32768; if (oy > 32767) oy = 32767;
            lhc_enc_box_shadow(&g_enc, x, y, w, h, argb, radius, (uint8_t)bw,
                               (int8_t)sp, (int16_t)ox, (int16_t)oy,
                               (uint8_t)(d->bg_cover ? 1 : 0));
            g_ops_this_frame++;
            g_stats.shadows_encoded++;
        } else if (t->type == LV_DRAW_TASK_TYPE_LINE) {
            const lv_draw_line_dsc_t *d = (const lv_draw_line_dsc_t *)t->draw_dsc;
            uint32_t opa = (uint32_t)d->opa * (uint32_t)t->opa / 255u;
            uint32_t argb = color_to_argb(d->color, (lv_opa_t)opa);
            int32_t lw = d->width; if (lw < 1) lw = 1; if (lw > 255) lw = 255;
            int16_t x1 = (int16_t)d->p1.x;
            int16_t y1 = (int16_t)d->p1.y;
            int16_t x2 = (int16_t)d->p2.x;
            int16_t y2 = (int16_t)d->p2.y;
            lhc_enc_line(&g_enc, x1, y1, x2, y2, argb, (uint8_t)lw);
            g_ops_this_frame++;
            g_stats.lines_encoded++;
        } else if (t->type == LV_DRAW_TASK_TYPE_IMAGE ||
                   t->type == LV_DRAW_TASK_TYPE_LAYER) {
            const lv_draw_image_dsc_t *d = (const lv_draw_image_dsc_t *)t->draw_dsc;
            const lv_image_dsc_t *img = NULL;
            if (t->type == LV_DRAW_TASK_TYPE_IMAGE) {
                img = (const lv_image_dsc_t *)d->src;
            } else {
                const lv_layer_t *ly = (const lv_layer_t *)d->src;
                img = (const lv_image_dsc_t *)ly->draw_buf;
            }
            lhc_blob_slot_t *slot = blob_cache_get_or_insert(img);
            /* Use the original image_area coords (LVGL clips coords to the
             * dirty rectangle which would chop our blit). For LAYER tasks
             * LVGL also uses lv_draw_image_dsc_t and image_area carries the
             * original blending area. */
            int16_t ix = (int16_t)d->image_area.x1;
            int16_t iy = (int16_t)d->image_area.y1;
            int16_t iw = (int16_t)(d->image_area.x2 - d->image_area.x1 + 1);
            int16_t ih = (int16_t)(d->image_area.y2 - d->image_area.y1 + 1);
            if (slot && blob_ensure_uploaded(slot, img)) {
                lhc_enc_image(&g_enc, ix, iy, iw, ih, slot->blob_id);
                g_ops_this_frame++;
                if (t->type == LV_DRAW_TASK_TYPE_IMAGE) g_stats.images_encoded++;
                else g_stats.layers_encoded++;
            }
        }

        if (g_enc.overflow && !g_overflow_logged_this_frame) {
            g_overflow_logged_this_frame = true;
            fprintf(stderr, "lhc: frame buffer overflow — dropping ops\n");
        }
    }

    t->state = LV_DRAW_TASK_STATE_FINISHED;
    g_stats.tasks_taken++;

    /* Ask LVGL to dispatch again so we can pick the next task. */
    lv_draw_dispatch_request();
    return 1;
}

static int32_t lhc_delete_cb(lv_draw_unit_t *du)
{
    (void)du;
    return 0;
}

/* ---- public API ---- */

void lhc_html5_draw_unit_init(void)
{
    g_unit = (lhc_draw_unit_t *)lv_draw_create_unit(sizeof(lhc_draw_unit_t));
    if (!g_unit) {
        LV_LOG_ERROR("lhc: failed to create html5 draw unit");
        return;
    }
    g_unit->base.name        = "html5";
    g_unit->base.evaluate_cb = lhc_evaluate_cb;
    g_unit->base.dispatch_cb = lhc_dispatch_cb;
    g_unit->base.delete_cb   = lhc_delete_cb;
    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_blobs, 0, sizeof(g_blobs));
    LV_LOG_INFO("lhc: html5 draw unit registered (M3a: +LINE over M2d FILL/BORDER/SHADOW/IMAGE/LAYER, score=80)");
}

void lhc_html5_draw_unit_attach_ws(lhc_ws_server_t *srv)
{
    g_ws = srv;
}

void lhc_html5_draw_unit_begin_frame(int16_t w, int16_t h)
{
    lhc_enc_init(&g_enc, g_buf, sizeof(g_buf));
    lhc_enc_begin_frame(&g_enc, g_frame_id, w, h);
    g_frame_open = true;
    g_overflow_logged_this_frame = false;
    g_ops_this_frame = 0;
}

size_t lhc_html5_draw_unit_flush_frame(void)
{
    if (!g_frame_open) return 0;
    g_frame_open = false;
    /* Skip empty frames — LVGL's internal refresh timer fires a cycle every
     * ~30ms even when nothing is dirty. Broadcasting BEGIN+END with zero
     * draw ops would clear the viewer's canvas to black between real
     * frames, causing visible flicker. Drop them silently. */
    if (g_ops_this_frame == 0) {
        g_stats.empty_frames_skipped++;
        return 0;
    }
    lhc_enc_end_frame(&g_enc);
    size_t n = lhc_enc_finalize(&g_enc);
    if (n == 0) return 0;
    if (g_ws) {
        lhc_ws_server_broadcast(g_ws, g_buf, n);
    }
    g_stats.frames_sent++;
    g_frame_id++;
    return n;
}

void lhc_html5_draw_unit_get_stats(lhc_html5_stats_t *out)
{
    if (out) *out = g_stats;
}
