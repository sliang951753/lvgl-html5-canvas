/**
 * @file lv_conf.h
 * LVGL 9.5 configuration for lvgl-html5-canvas.
 *
 * Tuned for: headless host (Linux/Termux) + future Allwinner T507.
 * Color depth 32 (ARGB8888), SW renderer kept (HTML5 unit coexists).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR / DRAW
 *====================*/
#define LV_COLOR_DEPTH          32
#define LV_COLOR_16_SWAP        0

#define LV_USE_DRAW_SW          1
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_RGB888   1
#define LV_DRAW_SW_SUPPORT_RGB565   1
#define LV_DRAW_SW_SUPPORT_L8       1
#define LV_DRAW_SW_SUPPORT_A8       1

/*====================
   MEMORY
 *====================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (2 * 1024 * 1024)
#define LV_MEM_POOL_INCLUDE     <stdlib.h>

/*====================
   HAL / TICK
 *====================*/
#define LV_DEF_REFR_PERIOD      33   /* ~30 fps */
#define LV_USE_TICK_CUSTOM      0

/*====================
   LOG
 *====================*/
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_INFO
#define LV_LOG_PRINTF           1

/*====================
   FEATURES
 *====================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0

#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_16

/*====================
   WIDGETS / EXTRA
 *====================*/
#define LV_USE_DEMO_WIDGETS     1
#define LV_USE_DEMO_BENCHMARK   1

/*====================
   OBSERVER / FS / etc.
 *====================*/
#define LV_USE_FS_STDIO         0
#define LV_USE_PNG              0
#define LV_USE_BMP              0

#endif /* LV_CONF_H */
