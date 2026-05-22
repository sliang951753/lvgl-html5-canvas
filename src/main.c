/**
 * @file main.c
 * lvgl-html5-canvas — M0 entrypoint.
 *
 * Spins up LVGL with a dummy display buffer (no framebuffer needed —
 * the html5 draw unit will own pixel delivery in M1+), registers the
 * html5 draw unit, starts the WS server, creates a hello-world label, and
 * services both LVGL ticks and WS events in a single thread.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"

#include "draw/html5_draw_unit.h"
#include "transport/ws_server.h"

#define DISP_W 800
#define DISP_H 480
#define WS_PORT 9000

static volatile int g_should_exit = 0;
static void on_sigint(int sig) { (void)sig; g_should_exit = 1; }

/* tick source */
static uint32_t tick_get_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* dummy flush — SW unit writes here, M1 will replace with a no-op or
 * a viewer-shadow buffer. */
static void dummy_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area; (void)px_map;
    lv_display_flush_ready(disp);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    signal(SIGINT, on_sigint);

    fprintf(stderr, "lvgl-html5-canvas M0 starting (LVGL %d.%d.%d)\n",
            LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    static uint8_t buf1[DISP_W * 40 * 4];
    static uint8_t buf2[DISP_W * 40 * 4];

    lv_display_t *disp = lv_display_create(DISP_W, DISP_H);
    if (!disp) {
        fprintf(stderr, "lhc: lv_display_create failed\n");
        return 1;
    }
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, dummy_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    lhc_html5_draw_unit_init();

    lhc_ws_server_t *srv = lhc_ws_server_start(WS_PORT);
    if (!srv) {
        fprintf(stderr, "lhc: ws server failed to start\n");
        return 1;
    }

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "lvgl-html5-canvas M0: hello");
    lv_obj_center(label);

    fprintf(stderr, "lhc: entering main loop. Ctrl-C to quit.\n");
    while (!g_should_exit) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms > 50) sleep_ms = 50;
        lhc_ws_server_service(srv, (int)sleep_ms);
    }

    fprintf(stderr, "lhc: shutting down\n");
    lhc_ws_server_stop(srv);
    lv_deinit();
    return 0;
}
