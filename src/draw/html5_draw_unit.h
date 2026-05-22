/**
 * @file html5_draw_unit.h
 * LVGL 9 draw unit that encodes draw ops into the lvgl-html5-canvas
 * binary protocol and pushes them to connected viewers via ws_server.
 *
 * M0: registers, declares zero supported ops in evaluate() (everything
 * falls through to SW), logs each evaluate call for verification.
 */
#ifndef LHC_HTML5_DRAW_UNIT_H
#define LHC_HTML5_DRAW_UNIT_H

#ifdef __cplusplus
extern "C" {
#endif

/** Register the html5 draw unit with LVGL. Must be called after lv_init(). */
void lhc_html5_draw_unit_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LHC_HTML5_DRAW_UNIT_H */
