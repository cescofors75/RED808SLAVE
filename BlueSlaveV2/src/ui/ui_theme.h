// =============================================================================
// ui_theme.h - RED808 theme system with visual presets
// =============================================================================
#pragma once

#include "lvgl.h"

// =============================================================================
// THEME PRESETS
// =============================================================================
enum VisualTheme : uint8_t {
    THEME_RED808 = 0,    // Default red
    THEME_OCEAN,         // Blue
    THEME_NEON,          // Green neon
    THEME_SUNSET,        // Orange/purple
    THEME_RAINBOW,       // Rainbow multicolor
    THEME_GREYSCALE,     // Monochrome greyscale
    THEME_COUNT
};

struct ThemeColors {
    uint32_t bg;
    uint32_t panel;
    uint32_t surface;
    uint32_t border;
    uint32_t text;
    uint32_t text_dim;
    uint32_t accent;
    uint32_t accent2;
    uint32_t success;
    uint32_t warning;
    uint32_t error;
    uint32_t info;
    uint32_t cyan;
    uint32_t led_uniform;      // Single LED color for M5/ByteButton (0 = use per-track)
    uint32_t nav_colors[7];    // ByteButton nav colors
    uint32_t pad_colors[8];    // ByteButton live pad colors
    uint8_t  encoder_rgb[3];   // M5 encoder uniform color (when led_uniform != 0)
    uint32_t track_colors[16]; // Per-track UI colors (pads, sequencer, volumes)
    const char* name;
};

extern const ThemeColors theme_presets[THEME_COUNT];
extern VisualTheme currentTheme;

// Runtime accessors — use these instead of RED808_* macros for themed elements
lv_color_t theme_bg();
lv_color_t theme_panel();
lv_color_t theme_surface();
lv_color_t theme_border();
lv_color_t theme_text();
lv_color_t theme_text_dim();
lv_color_t theme_accent();
lv_color_t theme_accent2();
lv_color_t theme_success();
lv_color_t theme_warning();
lv_color_t theme_error();
lv_color_t theme_info();
lv_color_t theme_cyan();

// Apply theme
void ui_theme_init();
void ui_theme_apply(VisualTheme theme);

// LED color helpers for main.cpp
uint32_t theme_nav_color(int index);
uint32_t theme_pad_color(int pad);
void     theme_encoder_color(int track, uint8_t out_rgb[3]);

// Dynamic macros — resolve to current theme at runtime
#define RED808_BG          theme_bg()
#define RED808_PANEL       theme_panel()
#define RED808_SURFACE     theme_surface()
#define RED808_BORDER      theme_border()
#define RED808_TEXT        theme_text()
#define RED808_TEXT_DIM    theme_text_dim()
#define RED808_ACCENT      theme_accent()
#define RED808_ACCENT2     theme_accent2()
#define RED808_SUCCESS     theme_success()
#define RED808_WARNING     theme_warning()
#define RED808_ERROR       theme_error()
#define RED808_INFO        theme_info()
#define RED808_CYAN        theme_cyan()

// Instrument/track colors — updated on theme change
extern lv_color_t inst_colors[16];
