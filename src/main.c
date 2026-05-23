/**
 * @file main.c
 * lvgl-html5-canvas — M1 entrypoint.
 *
 * Wires the html5 draw unit into LVGL's refresh cycle:
 *   REFR_START  → begin frame buffer
 *   REFR_READY  → end + broadcast over WS
 *
 * Demo: a few coloured rectangles + a label, with one rect animated
 * left-right so we can actually see frames updating in the browser.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "src/widgets/line/lv_line.h"

#include "draw/html5_draw_unit.h"
#include "transport/ws_server.h"
#include "assets/demo_sprite.h"

#define DISP_W 800
#define DISP_H 480
#define WS_PORT 9000

static volatile int g_should_exit = 0;
static void on_sigint(int sig) { (void)sig; g_should_exit = 1; }

static uint32_t tick_get_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* dummy flush — SW unit still renders pixels we don't ship.
 * (The html5 unit claims FILL tasks; SW handles the rest.) */
static void dummy_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area; (void)px_map;
    lv_display_flush_ready(disp);
}

/* REFR cycle hooks → begin/end frame on the html5 unit. */
static void on_refr_start(lv_event_t *e)
{
    (void)e;
    static int n = 0; if (++n <= 5 || n % 30 == 0) { fprintf(stderr, "lhc: REFR_START #%d\n", n); fflush(stderr); }
    lhc_html5_draw_unit_begin_frame(DISP_W, DISP_H);
}
static void on_refr_ready(lv_event_t *e)
{
    (void)e;
    static int n = 0; if (++n <= 5 || n % 30 == 0) { fprintf(stderr, "lhc: REFR_READY #%d\n", n); fflush(stderr); }
    lhc_html5_draw_unit_flush_frame();
}

static void line_anim_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_point_precise_t *pts = lv_line_get_points_mutable(obj);
    if (!pts) return;
    pts[1].x = (lv_value_precise_t)v;
    lv_obj_invalidate(obj);
}

/* M3a direct line draw task (p1/p2 path): emitted from a DRAW_MAIN callback
 * to guarantee at least one LV_DRAW_TASK_TYPE_LINE with d->points == NULL. */
static lv_draw_line_dsc_t g_direct_line_dsc;

static void direct_line_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    g_direct_line_dsc.base.layer = layer;
    lv_draw_line(layer, &g_direct_line_dsc);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    signal(SIGINT, on_sigint);

    fprintf(stderr, "lvgl-html5-canvas M1 starting (LVGL %d.%d.%d)\n",
            LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    /* Full-screen single buffer + FULL render mode.
     * The html5 draw unit doesn't ship pixels — it ships commands — so a
     * full-screen repaint each cycle is what we want: every static FILL_RECT
     * ends up in every frame, and newly-connected viewers see the complete
     * picture immediately. Dirty-rect streaming is a separate M5 feature. */
    static uint8_t fb[DISP_W * DISP_H * 4];
    lv_display_t *disp = lv_display_create(DISP_W, DISP_H);
    if (!disp) {
        fprintf(stderr, "lhc: lv_display_create failed\n");
        return 1;
    }
    lv_display_set_buffers(disp, fb, NULL, sizeof(fb),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, dummy_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    lhc_html5_draw_unit_init();

    lhc_ws_server_t *srv = lhc_ws_server_start(WS_PORT);
    if (!srv) {
        fprintf(stderr, "lhc: ws server failed to start\n");
        return 1;
    }
    lhc_html5_draw_unit_attach_ws(srv);

    lv_display_add_event_cb(disp, on_refr_start, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);

    /* ---- Scene selection ---- */
    bool use_benchmark = (argc > 1 && strcmp(argv[1], "--benchmark") == 0);
    if (use_benchmark) {
        fprintf(stderr, "lhc: launching lv_demo_benchmark\n");
        extern void lv_demo_benchmark(void);
        lv_demo_benchmark();
        goto run_loop;
    }

    /* ---- Default demo scene: solid-coloured rects + animated mover ---- */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* a couple of stationary panels (FILL_RECT instances) */
    lv_obj_t *panel_a = lv_obj_create(scr);
    lv_obj_remove_style_all(panel_a);
    lv_obj_set_size(panel_a, 200, 120);
    lv_obj_set_pos(panel_a, 40, 60);
    lv_obj_set_style_bg_color(panel_a, lv_color_hex(0xE53935), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel_a, LV_OPA_COVER, LV_PART_MAIN);
    /* M2: full white border, 4px, with radius matching the panel. */
    lv_obj_set_style_border_color(panel_a, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel_a, 4, LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel_a, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(panel_a, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
    lv_obj_set_style_radius(panel_a, 8, LV_PART_MAIN);

    lv_obj_t *panel_b = lv_obj_create(scr);
    lv_obj_remove_style_all(panel_b);
    lv_obj_set_size(panel_b, 200, 120);
    lv_obj_set_pos(panel_b, 280, 60);
    lv_obj_set_style_bg_color(panel_b, lv_color_hex(0x43A047), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel_b, LV_OPA_COVER, LV_PART_MAIN);
    /* M2: yellow top+bottom border only (partial-side path). */
    lv_obj_set_style_border_color(panel_b, lv_color_hex(0xFFEB3B), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel_b, 6, LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel_b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(panel_b, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    lv_obj_t *panel_c = lv_obj_create(scr);
    lv_obj_remove_style_all(panel_c);
    lv_obj_set_size(panel_c, 200, 120);
    lv_obj_set_pos(panel_c, 520, 60);
    lv_obj_set_style_bg_color(panel_c, lv_color_hex(0x1E88E5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel_c, LV_OPA_COVER, LV_PART_MAIN);
    /* M2: orange thick rounded full border. */
    lv_obj_set_style_border_color(panel_c, lv_color_hex(0xFF9800), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel_c, 8, LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel_c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(panel_c, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
    lv_obj_set_style_radius(panel_c, 24, LV_PART_MAIN);
    /* M2: black drop shadow — blur=20, spread=0, offset=(6,8). */
    lv_obj_set_style_shadow_color(panel_c, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(panel_c, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(panel_c, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(panel_c, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_x(panel_c, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(panel_c, 8, LV_PART_MAIN);

    /* Also throw a soft cyan glow on panel_a so we can see a colored shadow. */
    lv_obj_set_style_shadow_color(panel_a, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(panel_a, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(panel_a, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(panel_a, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_x(panel_a, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(panel_a, 0, LV_PART_MAIN);

    /* animated mover */
    lv_obj_t *mover = lv_obj_create(scr);
    lv_obj_remove_style_all(mover);
    lv_obj_set_size(mover, 60, 60);
    lv_obj_set_pos(mover, 40, 260);
    lv_obj_set_style_bg_color(mover, lv_color_hex(0xFFEB3B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mover, LV_OPA_COVER, LV_PART_MAIN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, mover);
    lv_anim_set_values(&a, 40, DISP_W - 100);
    lv_anim_set_duration(&a, 2500);
    lv_anim_set_reverse_duration(&a, 2500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_start(&a);

    /* M2c: two stationary sprites + one animated. The animated sprite proves
     * IMAGE redraws every frame using the cached blob (no re-upload). */
    lv_obj_t *img_static = lv_image_create(scr);
    lv_image_set_src(img_static, &demo_sprite);
    lv_obj_set_pos(img_static, 360, 80);

    lv_obj_t *img_static2 = lv_image_create(scr);
    lv_image_set_src(img_static2, &demo_sprite);
    lv_obj_set_pos(img_static2, 600, 80);

    lv_obj_t *img_mover = lv_image_create(scr);
    lv_image_set_src(img_mover, &demo_sprite);
    lv_obj_set_pos(img_mover, 60, 340);

    lv_anim_t a2;
    lv_anim_init(&a2);
    lv_anim_set_var(&a2, img_mover);
    lv_anim_set_values(&a2, DISP_W - 100, 60);
    lv_anim_set_duration(&a2, 2500);
    lv_anim_set_reverse_duration(&a2, 2500);
    lv_anim_set_repeat_count(&a2, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a2, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_start(&a2);

    /* M2d: force LVGL LAYER tasks with a semi-transparent parent container.
     * Children are rendered into an offscreen layer then blended back.
     * This makes LV_DRAW_TASK_TYPE_LAYER visible in stats/logs. */
    lv_obj_t *layer_host = lv_obj_create(scr);
    /* Keep <=128x128 so it goes through current html5 layer/image fast path. */
    lv_obj_set_size(layer_host, 120, 100);
    lv_obj_set_pos(layer_host, 620, 320);
    lv_obj_set_style_bg_color(layer_host, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(layer_host, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(layer_host, lv_color_hex(0x9FA8DA), LV_PART_MAIN);
    lv_obj_set_style_border_width(layer_host, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(layer_host, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(layer_host, 12, LV_PART_MAIN);
    lv_obj_set_style_opa_layered(layer_host, 180, LV_PART_MAIN);

    lv_obj_t *lh_rect = lv_obj_create(layer_host);
    lv_obj_remove_style_all(lh_rect);
    lv_obj_set_size(lh_rect, 46, 36);
    lv_obj_set_pos(lh_rect, 8, 8);
    lv_obj_set_style_bg_color(lh_rect, lv_color_hex(0xFF6F61), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lh_rect, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(lh_rect, 10, LV_PART_MAIN);

    lv_obj_t *lh_img = lv_image_create(layer_host);
    lv_image_set_src(lh_img, &demo_sprite);
    lv_obj_set_pos(lh_img, 60, 8);

    lv_obj_t *lh_lbl = lv_label_create(layer_host);
    lv_label_set_text(lh_lbl, "LAYER task demo");
    lv_obj_set_style_text_color(lh_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lh_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);

    /* Animate layer host horizontally so LAYER task is continuously emitted. */
    lv_anim_t a3;
    lv_anim_init(&a3);
    lv_anim_set_var(&a3, layer_host);
    lv_anim_set_values(&a3, 600, 670);
    lv_anim_set_duration(&a3, 2200);
    lv_anim_set_reverse_duration(&a3, 2200);
    lv_anim_set_repeat_count(&a3, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a3, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_start(&a3);

    /* M3a LINE case: a dedicated card with multiple line styles so LINE replay
     * is obvious in demo view (single-segment lines are html5 path). */
    lv_obj_t *line_case = lv_obj_create(scr);
    lv_obj_remove_style_all(line_case);
    lv_obj_set_size(line_case, 250, 130);
    lv_obj_set_pos(line_case, 20, 320);
    lv_obj_set_style_bg_color(line_case, lv_color_hex(0x162033), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line_case, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(line_case, 10, LV_PART_MAIN);
    lv_obj_set_style_border_color(line_case, lv_color_hex(0x5C6BC0), LV_PART_MAIN);
    lv_obj_set_style_border_width(line_case, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(line_case, LV_OPA_70, LV_PART_MAIN);

    lv_obj_t *line_case_title = lv_label_create(line_case);
    lv_label_set_text(line_case_title, "LINE case");
    lv_obj_set_style_text_color(line_case_title, lv_color_hex(0xE3F2FD), 0);
    lv_obj_align(line_case_title, LV_ALIGN_TOP_LEFT, 8, 4);

    static lv_point_precise_t line_case_h[] = { {10, 28}, {230, 28} };
    lv_obj_t *line_h = lv_line_create(line_case);
    lv_line_set_points_mutable(line_h, line_case_h, 2);
    lv_obj_set_size(line_h, 240, 40);
    lv_obj_set_pos(line_h, 4, 12);
    lv_obj_set_style_line_width(line_h, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(line_h, lv_color_hex(0x26C6DA), LV_PART_MAIN);
    lv_obj_set_style_line_opa(line_h, LV_OPA_COVER, LV_PART_MAIN);

    static lv_point_precise_t line_case_v[] = { {24, 0}, {24, 84} };
    lv_obj_t *line_v = lv_line_create(line_case);
    lv_line_set_points_mutable(line_v, line_case_v, 2);
    lv_obj_set_size(line_v, 60, 90);
    lv_obj_set_pos(line_v, 150, 30);
    lv_obj_set_style_line_width(line_v, 4, LV_PART_MAIN);
    lv_obj_set_style_line_color(line_v, lv_color_hex(0xEF5350), LV_PART_MAIN);
    lv_obj_set_style_line_opa(line_v, LV_OPA_COVER, LV_PART_MAIN);

    static lv_point_precise_t line_case_d[] = { {0, 0}, {180, 72} };
    lv_obj_t *line_d = lv_line_create(line_case);
    lv_line_set_points_mutable(line_d, line_case_d, 2);
    lv_obj_set_size(line_d, 190, 80);
    lv_obj_set_pos(line_d, 50, 40);
    lv_obj_set_style_line_width(line_d, 5, LV_PART_MAIN);
    lv_obj_set_style_line_color(line_d, lv_color_hex(0xFFCA28), LV_PART_MAIN);
    lv_obj_set_style_line_opa(line_d, LV_OPA_90, LV_PART_MAIN);

    /* M3b ARC case: dedicated arc widget card to visualize OP_ARC replay. */
    lv_obj_t *arc_case = lv_obj_create(scr);
    lv_obj_remove_style_all(arc_case);
    lv_obj_set_size(arc_case, 260, 140);
    lv_obj_set_pos(arc_case, 280, 315);
    lv_obj_set_style_bg_color(arc_case, lv_color_hex(0x1B2238), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc_case, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(arc_case, 10, LV_PART_MAIN);
    lv_obj_set_style_border_color(arc_case, lv_color_hex(0x7E57C2), LV_PART_MAIN);
    lv_obj_set_style_border_width(arc_case, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(arc_case, LV_OPA_70, LV_PART_MAIN);

    lv_obj_t *arc_title = lv_label_create(arc_case);
    lv_label_set_text(arc_title, "ARC case");
    lv_obj_set_style_text_color(arc_title, lv_color_hex(0xEDE7F6), 0);
    lv_obj_align(arc_title, LV_ALIGN_TOP_LEFT, 8, 4);

    lv_obj_t *arc_obj = lv_arc_create(arc_case);
    lv_obj_set_size(arc_obj, 96, 96);
    lv_obj_align(arc_obj, LV_ALIGN_LEFT_MID, 16, 8);
    lv_arc_set_bg_angles(arc_obj, 30, 330);
    lv_arc_set_range(arc_obj, 0, 100);
    lv_arc_set_value(arc_obj, 20);
    lv_obj_set_style_arc_width(arc_obj, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_obj, lv_color_hex(0x3949AB), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_obj, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_obj, lv_color_hex(0xFF7043), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc_obj, LV_OPA_90, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_obj, NULL, LV_PART_KNOB);

    lv_obj_t *arc_obj2 = lv_arc_create(arc_case);
    lv_obj_set_size(arc_obj2, 72, 72);
    lv_obj_align(arc_obj2, LV_ALIGN_RIGHT_MID, -18, 8);
    lv_arc_set_bg_angles(arc_obj2, 200, 20);
    lv_arc_set_range(arc_obj2, 0, 100);
    lv_arc_set_value(arc_obj2, 65);
    lv_obj_set_style_arc_width(arc_obj2, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_obj2, lv_color_hex(0x455A64), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_obj2, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_obj2, lv_color_hex(0x26C6DA), LV_PART_INDICATOR);
    lv_obj_remove_style(arc_obj2, NULL, LV_PART_KNOB);

    lv_anim_t a5;
    lv_anim_init(&a5);
    lv_anim_set_var(&a5, arc_obj);
    lv_anim_set_values(&a5, 8, 92);
    lv_anim_set_duration(&a5, 1800);
    lv_anim_set_reverse_duration(&a5, 1800);
    lv_anim_set_repeat_count(&a5, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a5, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_start(&a5);

    /* M3a/M3c LINE: horizontal + diagonal + polyline points[] are encoded
     * by html5 draw unit (dash/round-cap variants remain SW fallback). */
    static lv_point_precise_t line_pts_a[] = { {40, 0}, {220, 0} };
    lv_obj_t *line_a = lv_line_create(scr);
    lv_line_set_points_mutable(line_a, line_pts_a, 2);
    lv_obj_set_size(line_a, 240, 12);
    lv_obj_set_pos(line_a, 40, 225);
    lv_obj_set_style_line_width(line_a, 4, LV_PART_MAIN);
    lv_obj_set_style_line_color(line_a, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_line_opa(line_a, LV_OPA_COVER, LV_PART_MAIN);

    static lv_point_precise_t line_pts_b[] = { {0, 0}, {180, 80} };
    lv_obj_t *line_b = lv_line_create(scr);
    lv_line_set_points_mutable(line_b, line_pts_b, 2);
    lv_obj_set_size(line_b, 200, 96);
    lv_obj_set_pos(line_b, 300, 210);
    lv_obj_set_style_line_width(line_b, 6, LV_PART_MAIN);
    lv_obj_set_style_line_color(line_b, lv_color_hex(0xFFD54F), LV_PART_MAIN);
    lv_obj_set_style_line_opa(line_b, LV_OPA_80, LV_PART_MAIN);

    /* M3a direct line draw task (p1/p2 path): should be claimed by html5 LINE encoder. */
    static lv_obj_t *line_direct = NULL;
    line_direct = lv_obj_create(scr);
    lv_obj_remove_style_all(line_direct);
    /* Keep it visible and full-screen so DRAW_MAIN always runs; style-less so
     * it contributes no extra primitives except our explicit lv_draw_line(). */
    lv_obj_set_size(line_direct, DISP_W, DISP_H);
    lv_obj_set_pos(line_direct, 0, 0);

    lv_draw_line_dsc_init(&g_direct_line_dsc);
    g_direct_line_dsc.color = lv_color_hex(0xFF8A65);
    g_direct_line_dsc.opa = LV_OPA_COVER;
    g_direct_line_dsc.width = 5;
    g_direct_line_dsc.round_start = 0;
    g_direct_line_dsc.round_end = 0;
    g_direct_line_dsc.raw_end = 0;
    g_direct_line_dsc.dash_width = 0;
    g_direct_line_dsc.dash_gap = 0;
    g_direct_line_dsc.p1.x = 70;
    g_direct_line_dsc.p1.y = 360;
    g_direct_line_dsc.p2.x = 240;
    g_direct_line_dsc.p2.y = 430;

    lv_obj_add_event_cb(line_direct, direct_line_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    static lv_point_precise_t line_pts_c[] = { {0, 56}, {50, 12}, {105, 50}, {160, 8} };
    lv_obj_t *line_c = lv_line_create(scr);
    lv_line_set_points_mutable(line_c, line_pts_c, 4);
    lv_obj_set_size(line_c, 180, 70);
    lv_obj_set_pos(line_c, 540, 215);
    lv_obj_set_style_line_width(line_c, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(line_c, lv_color_hex(0xB2FF59), LV_PART_MAIN);
    lv_obj_set_style_line_opa(line_c, LV_OPA_COVER, LV_PART_MAIN);

    /* animate one LINE endpoint to prove live LINE task replay */
    lv_anim_t a4;
    lv_anim_init(&a4);
    lv_anim_set_var(&a4, line_b);
    lv_anim_set_values(&a4, 120, 188);
    lv_anim_set_duration(&a4, 1700);
    lv_anim_set_reverse_duration(&a4, 1700);
    lv_anim_set_repeat_count(&a4, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a4, line_anim_cb);
    lv_anim_start(&a4);

    /* label (SW renders glyphs, html5 ignores in M1) */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "lvgl-html5-canvas M3b — +ARC over WS (label still SW)");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);

run_loop:
    (void)0;

    fprintf(stderr, "lhc: entering main loop. Ctrl-C to quit.\n");
    fflush(stderr);
    /* WS server runs on its own thread — main loop never blocks on socket I/O.
     * We only drive LVGL here at ~30 Hz. */
    lv_obj_t *root = lv_screen_active();
    uint32_t last_stat = 0;
    uint32_t last_refr = 0;
    while (!g_should_exit) {
        uint32_t now = tick_get_cb();
        lv_timer_handler();
        if (now - last_refr >= 33) {
            last_refr = now;
            lv_obj_invalidate(root);
            lv_refr_now(disp);
        }
        /* sleep ~5ms — fine-grained enough for 30 Hz, keeps CPU low. */
        struct timespec slp = { 0, 5 * 1000 * 1000 };
        nanosleep(&slp, NULL);

        if (now - last_stat >= 1000) {
            last_stat = now;
            lhc_html5_stats_t s;
            lhc_html5_draw_unit_get_stats(&s);
            fprintf(stderr,
                    "lhc: stats eval=%u disp=%u taken=%u frames=%u fills=%u borders=%u lines=%u arcs=%u shadows=%u images=%u layers=%u blobs=%u blobKB=%u\n",
                    s.evaluate_calls, s.dispatch_calls, s.tasks_taken, s.frames_sent,
                    s.fills_encoded, s.borders_encoded, s.lines_encoded, s.arcs_encoded, s.shadows_encoded,
                    s.images_encoded, s.layers_encoded, s.blobs_uploaded,
                    s.blob_bytes_sent / 1024u);
            fflush(stderr);
        }
    }

    fprintf(stderr, "lhc: shutting down\n");
    lhc_ws_server_stop(srv);
    lv_deinit();
    return 0;
}
