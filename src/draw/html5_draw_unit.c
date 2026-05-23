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
 * Frame buffer is a single static 64 KiB scratch — fine for M1 demos.
 * Overflows are dropped (logged once per frame) so the SW unit still
 * renders the picture correctly even if we lose ops.
 */
#include "html5_draw_unit.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/lv_draw_rect.h"
#include "src/misc/lv_grad.h"
#include "src/misc/lv_style.h"

#include "../proto/encoder.h"
#include "../transport/ws_server.h"

#define LHC_DRAW_UNIT_ID_HTML5  50
#define LHC_FRAME_BUF_SIZE      (64 * 1024)

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
    LV_LOG_INFO("lhc: html5 draw unit registered (M1: FILL_RECT, score=80)");
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
