// =============================================================================
// BlueSlaveP4 - lv_conf.h
// LVGL 9.x for Guition ESP32-P4 JC1060P470C (1024x600 MIPI-DSI)
// =============================================================================
#ifndef LV_CONF_H
#define LV_CONF_H

#if !defined(__ASSEMBLY__)
#include <stdint.h>
#endif

// =============================================================================
// COLOR
// =============================================================================
#define LV_COLOR_DEPTH 16

// =============================================================================
// STDLIB / MEMORY
// =============================================================================
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_STDINT_INCLUDE   <stdint.h>
#define LV_STDDEF_INCLUDE   <stddef.h>
#define LV_STDBOOL_INCLUDE  <stdbool.h>
#define LV_INTTYPES_INCLUDE <inttypes.h>
#define LV_LIMITS_INCLUDE   <limits.h>
#define LV_STDARG_INCLUDE   <stdarg.h>

// =============================================================================
// DISPLAY / HAL
// =============================================================================
#define LV_DPI_DEF         130
#define LV_DEF_REFR_PERIOD 16   // 60fps target; SW renderer on 1024x600 needs >8ms
#define LV_USE_OS          LV_OS_NONE

// =============================================================================
// RENDERING
// =============================================================================
#define LV_DRAW_BUF_STRIDE_ALIGN 64
#define LV_DRAW_BUF_ALIGN        64
#define LV_USE_DRAW_SW           1
#define LV_DRAW_SW_SUPPORT_RGB565    1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8  1
#define LV_DRAW_SW_SUPPORT_RGB888    1
#define LV_DRAW_SW_SUPPORT_XRGB8888  1
#define LV_DRAW_SW_SUPPORT_ARGB8888  1
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 1
#define LV_DRAW_SW_SUPPORT_L8        1
#define LV_DRAW_SW_SUPPORT_AL88      1
#define LV_DRAW_SW_SUPPORT_A8        1
#define LV_DRAW_SW_SUPPORT_I1        1
#define LV_DRAW_SW_I1_LUM_THRESHOLD  127
#define LV_DRAW_SW_DRAW_UNIT_CNT     1
#define LV_USE_DRAW_ARM2D_SYNC       0
#define LV_USE_NATIVE_HELIUM_ASM     0
#define LV_DRAW_SW_COMPLEX           1
#define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 0
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE

// ESP32-P4 PPA backend is present in LVGL 9.5, but the Arduino core currently
// ships a fixed sdkconfig where CONFIG_LV_DRAW_BUF_ALIGN != L2 cache line size.
// Enabling it fails compilation inside LVGL's PPA backend. Keep zero-copy DPI
// framebuffers as the stable hardware path until the framework config is rebuilt.
#define LV_USE_PPA      0
#define LV_USE_PPA_IMG  0
#define LV_PPA_BURST_LENGTH 128

// =============================================================================
// FONTS
// =============================================================================
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// =============================================================================
// TEXT
// =============================================================================
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

// =============================================================================
// WIDGETS
// =============================================================================
#define LV_WIDGETS_HAS_DEFAULT_VALUE 1
#define LV_USE_ANIMIMG      0
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CANVAS       0
#define LV_USE_CALENDAR     0
#define LV_USE_CHART        0
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMAGE        1
#define LV_USE_IMAGEBUTTON  0
#define LV_USE_KEYBOARD     0
#define LV_USE_LABEL        1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT  1
#define LV_USE_LED          1
#define LV_USE_LINE         1
#define LV_USE_LIST         1
#define LV_USE_MENU         0
#define LV_USE_MSGBOX       1
#define LV_USE_ROLLER       0
#define LV_USE_SCALE        0
#define LV_USE_SLIDER       1
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      1
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     0
#define LV_USE_TABLE        1
#define LV_USE_TABVIEW      0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0

// Compatibility aliases for the current v8-style UI code.
#define LV_USE_BTN       LV_USE_BUTTON
#define LV_USE_BTNMATRIX LV_USE_BUTTONMATRIX
#define LV_USE_IMG       LV_USE_IMAGE
#define LV_USE_IMGBTN    LV_USE_IMAGEBUTTON

// =============================================================================
// LAYOUT
// =============================================================================
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

// =============================================================================
// THEME / DEBUG
// =============================================================================
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#define LV_USE_THEME_SIMPLE 1
#define LV_USE_THEME_MONO   0

#define LV_USE_LOG           0
#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE  0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ    0
#define LV_USE_REFR_DEBUG    0
#define LV_USE_LAYER_DEBUG   0
#define LV_USE_PARALLEL_DRAW_DEBUG 0
#define LV_USE_SYSMON        0
#define LV_USE_PROFILER      0

// =============================================================================
// LIBRARIES / DRIVERS / DEMOS
// =============================================================================
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_BMP    0
#define LV_USE_TJPGD  0
#define LV_USE_GIF    0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_THORVG_EXTERNAL 0

#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY   0
#define LV_USE_GRIDNAV  0
#define LV_USE_FRAGMENT 0
#define LV_USE_OBSERVER 0

#define LV_USE_SDL       0
#define LV_USE_X11       0
#define LV_USE_WAYLAND   0
#define LV_USE_LINUX_FBDEV 0
#define LV_USE_TFT_ESPI  0
#define LV_USE_EVDEV     0
#define LV_USE_ST7735    0
#define LV_USE_ST7789    0
#define LV_USE_ST7796    0
#define LV_USE_ILI9341   0
#define LV_USE_WINDOWS   0
#define LV_USE_OPENGLES  0

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_RENDER 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif // LV_CONF_H
