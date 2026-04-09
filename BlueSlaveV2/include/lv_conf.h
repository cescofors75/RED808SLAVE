// =============================================================================
// BlueSlaveV2 - lv_conf.h
// LVGL 8.4.x Configuration for Waveshare ESP32-S3-Touch-LCD-7B (1024x600)
// =============================================================================
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// =============================================================================
// COLOR
// =============================================================================
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0

// =============================================================================
// MEMORY
// =============================================================================
#define LV_MEM_CUSTOM      1
#if LV_MEM_CUSTOM
    #define LV_MEM_CUSTOM_INCLUDE   <esp_heap_caps.h>
    #define LV_MEM_CUSTOM_ALLOC(size)     heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
    #define LV_MEM_CUSTOM_FREE            heap_caps_free
    #define LV_MEM_CUSTOM_REALLOC(p,size) heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM)
#endif
#define LV_MEMCPY_MEMSET_STD  1

// =============================================================================
// DISPLAY
// =============================================================================
// Max dimensions: 1024 covers both orientations (1024x600 landscape, 600x1024 portrait).
// Actual resolution is set in disp_drv (lvgl_port.cpp).
#define LV_HOR_RES_MAX     1024
#define LV_VER_RES_MAX     1024
#define LV_DPI_DEF         130

// =============================================================================
// PERFORMANCE
// =============================================================================
#define LV_ATTRIBUTE_FAST_MEM   IRAM_ATTR
#define LV_DISP_DEF_REFR_PERIOD 10   // 10ms — Waveshare recommended; bounce buffers protect PSRAM bus
#define LV_INDEV_DEF_READ_PERIOD 10   // 100Hz touch — responsive without I2C contention
#define LV_USE_PERF_MONITOR   0
#define LV_USE_MEM_MONITOR    0

// =============================================================================
// TICK
// =============================================================================
#define LV_TICK_CUSTOM     1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE  <Arduino.h>
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR  (millis())
#endif

// =============================================================================
// LOGGING
// =============================================================================
#define LV_USE_LOG         0
#if LV_USE_LOG
    #define LV_LOG_LEVEL   LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF  1
#endif

// =============================================================================
// FONTS
// =============================================================================
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_32   1
#define LV_FONT_MONTSERRAT_36   1
#define LV_FONT_MONTSERRAT_40   1
#define LV_FONT_MONTSERRAT_48   1

// Default font
#define LV_FONT_DEFAULT    &lv_font_montserrat_16

// Symbols
#define LV_USE_FONT_COMPRESSED  0
#define LV_FONT_SUBPX_BGR      0
#define LV_FONT_FMT_TXT_LARGE  0

// =============================================================================
// WIDGETS (Core)
// =============================================================================
#define LV_USE_ARC         1
#define LV_USE_BAR         1
#define LV_USE_BTN         1
#define LV_USE_BTNMATRIX   1
#define LV_USE_CANVAS      0
#define LV_USE_CHECKBOX    1
#define LV_USE_DROPDOWN    1
#define LV_USE_IMG         1
#define LV_USE_LABEL       1
#define LV_USE_LINE        1
#define LV_USE_ROLLER      1
#define LV_USE_SLIDER      1
#define LV_USE_SWITCH      1
#define LV_USE_TABLE       1
#define LV_USE_TEXTAREA    1

// =============================================================================
// WIDGETS (Extra)
// =============================================================================
#define LV_USE_ANIMIMG     0
#define LV_USE_CALENDAR    0
#define LV_USE_CHART       1
#define LV_USE_COLORWHEEL  0
#define LV_USE_IMGBTN      0
#define LV_USE_KEYBOARD    1
#define LV_USE_LED         1
#define LV_USE_LIST        1
#define LV_USE_MENU        1
#define LV_USE_METER       1
#define LV_USE_MSGBOX      1
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     1
#define LV_USE_SPINNER     1
#define LV_USE_TABVIEW     1
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0

// =============================================================================
// THEMES
// =============================================================================
#define LV_USE_THEME_DEFAULT   1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK       1
    #define LV_THEME_DEFAULT_GROW       0
    #define LV_THEME_DEFAULT_TRANSITION_TIME  80
#endif
#define LV_USE_THEME_BASIC     1
#define LV_USE_THEME_MONO      0

// =============================================================================
// LAYOUTS
// =============================================================================
#define LV_USE_FLEX        1
#define LV_USE_GRID        1

// =============================================================================
// DRAW
// =============================================================================
#define LV_USE_DRAW_MASKS  1

// =============================================================================
// GPU / VG_LITE
// =============================================================================
#define LV_USE_GPU_SDL     0
#define LV_USE_GPU_ARM2D   0

// =============================================================================
// OTHER
// =============================================================================
#define LV_USE_SNAPSHOT    0
#define LV_BUILD_EXAMPLES  0

#endif // LV_CONF_H
