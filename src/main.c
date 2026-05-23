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

    /* label (SW renders glyphs, html5 ignores in M1) */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "lvgl-html5-canvas M2d — +LAYER (layered opacity) over WS");
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
                    "lhc: stats eval=%u disp=%u taken=%u frames=%u fills=%u borders=%u shadows=%u images=%u layers=%u blobs=%u blobKB=%u\n",
                    s.evaluate_calls, s.dispatch_calls, s.tasks_taken, s.frames_sent,
                    s.fills_encoded, s.borders_encoded, s.shadows_encoded,
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
