/**
 * LVGL 8.4 configuration for Waveshare ESP32-S3-Touch-LCD-7
 * 1024x600 RGB565 display
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        0

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM           1
#if LV_MEM_CUSTOM == 1
  #define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
  #define LV_MEM_CUSTOM_ALLOC     malloc
  #define LV_MEM_CUSTOM_FREE      free
  #define LV_MEM_CUSTOM_REALLOC   realloc
#endif

#define LV_MEMCPY_MEMSET_STD    1

/*====================
   HAL SETTINGS
 *====================*/
#define LV_TICK_CUSTOM          1
#if LV_TICK_CUSTOM == 1
  #define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_DPI_DEF              130

/*====================
   FEATURE CONFIGURATION
 *====================*/
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_LOG              0

/*====================
   FONT USAGE
 *====================*/
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   1
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK  0
#define LV_FONT_UNSCII_8       0
#define LV_FONT_UNSCII_16      0

#define LV_FONT_DEFAULT         &lv_font_montserrat_16
#define LV_FONT_FMT_TXT_LARGE  0
#define LV_USE_FONT_COMPRESSED  0
#define LV_USE_FONT_SUBPX      0

/*====================
   WIDGETS
 *====================*/
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BTN              1
#define LV_USE_BTNMATRIX        1
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMG              1
#define LV_USE_LABEL            1
#define LV_USE_LINE             1
#define LV_USE_ROLLER           1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_TEXTAREA         0
#define LV_USE_TABLE            1

/*====================
   EXTRA WIDGETS
 *====================*/
#define LV_USE_ANIMIMG          0
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_COLORWHEEL       0
#define LV_USE_IMGBTN           0
#define LV_USE_KEYBOARD         0
#define LV_USE_LED              1
#define LV_USE_LIST             1
#define LV_USE_MENU             0
#define LV_USE_METER            0
#define LV_USE_MSGBOX           1
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          1
#define LV_USE_TABVIEW          1
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/*====================
   LAYOUTS
 *====================*/
#define LV_USE_FLEX             1
#define LV_USE_GRID             1

/*====================
   THEMES
 *====================*/
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1

/*====================
   DRAW
 *====================*/
#define LV_DRAW_COMPLEX         1
#define LV_SHADOW_CACHE_SIZE    0
#define LV_IMG_CACHE_DEF_SIZE   0

/*====================
   GPU
 *====================*/
#define LV_USE_GPU_STM32_DMA2D  0
#define LV_USE_GPU_NXP_PXP      0
#define LV_USE_GPU_NXP_VG_LITE  0
#define LV_USE_GPU_SDL          0

/*====================
   DEMOS (disabled)
 *====================*/
#define LV_USE_DEMO_WIDGETS     0
#define LV_USE_DEMO_BENCHMARK   0
#define LV_USE_DEMO_STRESS      0
#define LV_USE_DEMO_MUSIC       0

#endif /* LV_CONF_H */
