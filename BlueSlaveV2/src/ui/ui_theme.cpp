// =============================================================================
// ui_theme.cpp - RED808 dark theme for LVGL
// =============================================================================
#include "ui_theme.h"

void ui_theme_init() {
    // Apply dark theme
    lv_theme_t* theme = lv_theme_default_init(
        lv_disp_get_default(),
        RED808_ACCENT,    // Primary color
        RED808_INFO,      // Secondary color
        true,             // Dark mode
        &lv_font_montserrat_16
    );
    lv_disp_set_theme(lv_disp_get_default(), theme);

    // Set default background
    lv_obj_set_style_bg_color(lv_scr_act(), RED808_BG, 0);
}
