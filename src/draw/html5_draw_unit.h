/**
 * @file html5_draw_unit.h
 * LVGL 9 draw unit that encodes draw ops into the lvgl-html5-canvas
 * binary protocol and pushes them to connected viewers via ws_server.
 *
 * M1: FILL_RECT (solid colour, no gradient).
 * M2: + BORDER (solid colour, any side mask, with radius).
 *     + BOX_SHADOW (solid color, radius/blur/spread/offset).
 * M3a: + LINE (single-segment, non-dashed, no round caps).
 * M3b: + ARC (solid-color stroke arcs; no image-source arcs).
 * Begin/End frame is driven from main.c via display REFR_START / REFR_READY
 * events.
 */
#ifndef LHC_HTML5_DRAW_UNIT_H
#define LHC_HTML5_DRAW_UNIT_H

#include <stddef.h>
#include <stdint.h>

#include "../transport/ws_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register the html5 draw unit with LVGL. Must be called after lv_init(). */
void lhc_html5_draw_unit_init(void);

/** Attach a ws server to receive flushed frames (call once after start). */
void lhc_html5_draw_unit_attach_ws(lhc_ws_server_t *srv);

/** Begin a new frame buffer. Idempotent across nested calls. */
void lhc_html5_draw_unit_begin_frame(int16_t w, int16_t h);

/** Finalize current frame and broadcast it to all viewers. Returns frame
 *  byte size, or 0 if the frame was empty / skipped. */
size_t lhc_html5_draw_unit_flush_frame(void);

/** Stats for debugging / tests. */
typedef struct {
    uint32_t evaluate_calls;
    uint32_t dispatch_calls;
    uint32_t tasks_taken;
    uint32_t frames_sent;
    uint32_t empty_frames_skipped;
    uint32_t fills_encoded;
    uint32_t borders_encoded;
    uint32_t shadows_encoded;
    uint32_t images_encoded;
    uint32_t lines_encoded;
    uint32_t arcs_encoded;
    uint32_t layers_encoded;
    uint32_t blobs_uploaded;
    uint32_t blob_bytes_sent;
} lhc_html5_stats_t;
void lhc_html5_draw_unit_get_stats(lhc_html5_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LHC_HTML5_DRAW_UNIT_H */
