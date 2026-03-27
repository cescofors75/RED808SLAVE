// =============================================================================
// ui_theme.h - RED808 dark theme colors for LVGL
// =============================================================================
#pragma once

#include "lvgl.h"

// RED808 Color Palette
#define RED808_BG          lv_color_hex(0x0D1117)   // Deep background
#define RED808_PANEL       lv_color_hex(0x161B22)   // Panel background
#define RED808_SURFACE     lv_color_hex(0x21262D)   // Surface elements
#define RED808_BORDER      lv_color_hex(0x30363D)   // Borders
#define RED808_TEXT        lv_color_hex(0xE6EDF3)   // Primary text
#define RED808_TEXT_DIM    lv_color_hex(0x8B949E)   // Dim text
#define RED808_ACCENT      lv_color_hex(0xFF4444)   // RED808 red accent
#define RED808_ACCENT2     lv_color_hex(0xFF6B6B)   // Red lighter
#define RED808_SUCCESS     lv_color_hex(0x3FB950)   // Green
#define RED808_WARNING     lv_color_hex(0xD29922)   // Yellow/amber
#define RED808_ERROR       lv_color_hex(0xF85149)   // Error red
#define RED808_INFO        lv_color_hex(0x58A6FF)   // Blue info
#define RED808_CYAN        lv_color_hex(0x39D2C0)   // Cyan

// Instrument colors
static const lv_color_t inst_colors[] = {
    lv_color_hex(0xFF4444), // KICK - Red
    lv_color_hex(0xFF8C00), // SNARE - Orange
    lv_color_hex(0xFFD700), // CL-HAT - Yellow
    lv_color_hex(0x00CED1), // OP-HAT - Cyan
    lv_color_hex(0xFF00FF), // CLAP - Magenta
    lv_color_hex(0x00FF00), // TOM-LO - Green
    lv_color_hex(0x00FA9A), // TOM-HI - MedSpringGreen
    lv_color_hex(0x6495ED), // CYMBAL - CornflowerBlue
    lv_color_hex(0xDA70D6), // PERC-1 - Orchid
    lv_color_hex(0xFFA07A), // PERC-2 - LightSalmon
    lv_color_hex(0xADFF2F), // SHAKER - GreenYellow
    lv_color_hex(0xFFE4B5), // COWBELL - Moccasin
    lv_color_hex(0x87CEEB), // RIDE - SkyBlue
    lv_color_hex(0xDDA0DD), // CONGA - Plum
    lv_color_hex(0xF0E68C), // BONGO - Khaki
    lv_color_hex(0xB0C4DE), // EXTRA - LightSteelBlue
};

void ui_theme_init();  // Apply RED808 dark theme
