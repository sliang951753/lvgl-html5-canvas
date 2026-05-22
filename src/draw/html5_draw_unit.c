/**
 * @file html5_draw_unit.c
 * M0 stub: registers the draw unit, logs every evaluate() call,
 * returns 0 score so SW unit handles everything.
 *
 * In M1 this file gains a real evaluate() that scores LV_DRAW_TASK_TYPE_FILL,
 * a dispatch() that calls lhc_enc_fill_rect, and flush hooks into ws_server.
 */
#include "html5_draw_unit.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
/* lv_draw_unit_t is opaque in the public API since LVGL 9.x; pull in the
 * private header so we can embed it as a base struct in our draw unit. */
#include "src/draw/lv_draw_private.h"

typedef struct {
    lv_draw_unit_t base;
    /* M1+: per-frame encoder + viewer list pointer go here */
    uint32_t evaluate_calls;
    uint32_t dispatch_calls;
} lhc_draw_unit_t;

static int32_t lhc_evaluate_cb(lv_draw_unit_t *draw_unit, lv_draw_task_t *task)
{
    lhc_draw_unit_t *u = (lhc_draw_unit_t *)draw_unit;
    u->evaluate_calls++;
    /* M0: we cannot handle anything; SW unit wins. */
    (void)task;
    return 0;
}

static int32_t lhc_dispatch_cb(lv_draw_unit_t *draw_unit, lv_layer_t *layer)
{
    lhc_draw_unit_t *u = (lhc_draw_unit_t *)draw_unit;
    u->dispatch_calls++;
    (void)layer;
    /* No tasks claimed → nothing to do. */
    return 0;
}

static int32_t lhc_delete_cb(lv_draw_unit_t *draw_unit)
{
    /* nothing dynamic in M0 */
    (void)draw_unit;
    return 0;
}

void lhc_html5_draw_unit_init(void)
{
    lhc_draw_unit_t *u = (lhc_draw_unit_t *)lv_draw_create_unit(sizeof(lhc_draw_unit_t));
    if (!u) {
        LV_LOG_ERROR("lhc: failed to create html5 draw unit");
        return;
    }
    u->base.evaluate_cb = lhc_evaluate_cb;
    u->base.dispatch_cb = lhc_dispatch_cb;
    u->base.delete_cb   = lhc_delete_cb;
    u->evaluate_calls   = 0;
    u->dispatch_calls   = 0;

    LV_LOG_INFO("lhc: html5 draw unit registered (M0 stub, score=0 for all ops)");
}
