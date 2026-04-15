// =============================================================================
// ui_screens.cpp — P4 UI screens (data-driven from UART state)
// All screen rendering reads from P4State (p4.*) — no direct hardware access.
// =============================================================================

#include "ui_screens.h"
#include "ui_theme.h"
#include "../udp_handler.h"
#include "../uart_handler.h"
#include "../include/config.h"
#include <Arduino.h>

// Screen objects
lv_obj_t* scr_boot = NULL;
lv_obj_t* scr_live = NULL;
lv_obj_t* scr_sequencer = NULL;
lv_obj_t* scr_fx = NULL;
lv_obj_t* scr_volumes = NULL;
lv_obj_t* scr_settings = NULL;
lv_obj_t* scr_performance = NULL;

// Header widgets
static lv_obj_t* header_bar = NULL;
static lv_obj_t* hdr_bpm_label = NULL;
static lv_obj_t* hdr_pattern_label = NULL;
static lv_obj_t* hdr_play_btn = NULL;
static lv_obj_t* hdr_play_label = NULL;
static lv_obj_t* hdr_pattern_minus_btn = NULL;
static lv_obj_t* hdr_pattern_plus_btn = NULL;
static lv_obj_t* hdr_wifi_label = NULL;
static lv_obj_t* hdr_s3_label = NULL;
static lv_obj_t* hdr_step_dots[16] = {};

// Current active screen index + history for back navigation
static int active_screen = 0;
static int prev_active_screen = 0;

// Track names
static const char* trackNames[] = {
    "BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL",
    "MA", "CY", "HT", "LT", "MC", "MT", "HC", "LC"
};

static int ui_layout_w(void) {
    return LCD_H_RES;
}

static int ui_layout_h(void) {
    // The current panel path renders 1024x600 without framebuffer rotation.
    // Keep the live layout inside the visible 600px height.
    return (UI_H > LCD_V_RES) ? LCD_V_RES : UI_H;
}

// =============================================================================
// HELPER: Section shell (styled container)
// =============================================================================
lv_obj_t* create_section_shell(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, RED808_BORDER, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_pad_all(obj, 14, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static bool ui_use_udp_transport(void) {
    return p4.wifi_connected || p4.master_connected;
}

static lv_obj_t* create_header_button(lv_obj_t* parent, int x, int y, int w, int h,
                                      const char* text, lv_color_t bg, lv_color_t border) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, border, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label, RED808_TEXT, 0);
    lv_obj_center(label);
    return btn;
}

static void header_play_cb(lv_event_t* e) {
    LV_UNUSED(e);
    bool next_play = !p4.is_playing;
    // Always route play through S3 — S3 is the sequencer master
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_PLAY_TOGGLE, 0);
    p4.is_playing = next_play;
}

static void header_pattern_cb(lv_event_t* e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int next_pattern = p4.current_pattern + delta;
    if (next_pattern < 0) next_pattern = Config::MAX_PATTERNS - 1;
    if (next_pattern >= Config::MAX_PATTERNS) next_pattern = 0;

    p4.current_pattern = next_pattern;
    // Always route through S3 — S3 manages patterns and master sync
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_PATTERN_SEL, (uint8_t)next_pattern);
}

// =============================================================================
// HEADER BAR (top of every screen)
// =============================================================================
void ui_create_header(lv_obj_t* parent) {
    int layout_w = ui_layout_w();
    header_bar = lv_obj_create(parent);
    lv_obj_set_size(header_bar, layout_w - 24, 68);
    lv_obj_set_pos(header_bar, 12, 8);
    lv_obj_set_style_bg_color(header_bar, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(header_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(header_bar, 1, 0);
    lv_obj_set_style_border_color(header_bar, RED808_BORDER, 0);
    lv_obj_set_style_radius(header_bar, 18, 0);
    lv_obj_set_style_pad_all(header_bar, 0, 0);
    lv_obj_clear_flag(header_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Back button (top-left)
    lv_obj_t* back_btn = lv_btn_create(header_bar);
    lv_obj_set_size(back_btn, 48, 46);
    lv_obj_set_pos(back_btn, 8, 10);
    lv_obj_set_style_radius(back_btn, 14, 0);
    lv_obj_set_style_bg_color(back_btn, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back_btn, 2, 0);
    lv_obj_set_style_border_color(back_btn, RED808_BORDER, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        LV_UNUSED(e);
        if (prev_active_screen != active_screen) {
            ui_navigate_to(prev_active_screen);
        }
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(back_lbl, RED808_TEXT, 0);
    lv_obj_center(back_lbl);

    // BPM
    hdr_bpm_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_bpm_label, "120.0");
    lv_obj_set_style_text_font(hdr_bpm_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hdr_bpm_label, RED808_ACCENT, 0);
    lv_obj_set_pos(hdr_bpm_label, 66, 9);

    // Pattern
    hdr_pattern_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_pattern_label, "P01");
    lv_obj_set_style_text_font(hdr_pattern_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr_pattern_label, RED808_WARNING, 0);
    lv_obj_set_pos(hdr_pattern_label, 16, 40);

    hdr_wifi_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_wifi_label, "NET OFF");
    lv_obj_set_style_text_font(hdr_wifi_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_wifi_label, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(hdr_wifi_label, 118, 14);

    hdr_s3_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_s3_label, "AUX OFF");
    lv_obj_set_style_text_font(hdr_s3_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_s3_label, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(hdr_s3_label, 118, 36);

    hdr_pattern_minus_btn = create_header_button(header_bar, layout_w - 286, 10, 48, 46, "-", RED808_SURFACE, RED808_BORDER);
    lv_obj_add_event_cb(hdr_pattern_minus_btn, header_pattern_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);

    hdr_pattern_plus_btn = create_header_button(header_bar, layout_w - 232, 10, 48, 46, "+", RED808_SURFACE, RED808_BORDER);
    lv_obj_add_event_cb(hdr_pattern_plus_btn, header_pattern_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);

    hdr_play_btn = create_header_button(header_bar, layout_w - 176, 10, 150, 46, "PLAY", RED808_ACCENT, RED808_ACCENT2);
    lv_obj_add_event_cb(hdr_play_btn, header_play_cb, LV_EVENT_CLICKED, NULL);
    hdr_play_label = lv_obj_get_child(hdr_play_btn, 0);
}

void ui_update_header(void) {
    static int prev_bpm = -1, prev_frac = -1, prev_pat = -1;
    static bool prev_play = false, prev_wifi = false, prev_s3 = false;

    if (p4.bpm_int != prev_bpm || p4.bpm_frac != prev_frac) {
        prev_bpm = p4.bpm_int;
        prev_frac = p4.bpm_frac;
        if (hdr_bpm_label) lv_label_set_text_fmt(hdr_bpm_label, "%d.%d", p4.bpm_int, p4.bpm_frac);
    }

    if (p4.current_pattern != prev_pat) {
        prev_pat = p4.current_pattern;
        if (hdr_pattern_label) lv_label_set_text_fmt(hdr_pattern_label, "P%02d", p4.current_pattern + 1);
    }

    if (p4.is_playing != prev_play) {
        prev_play = p4.is_playing;
        if (hdr_play_btn && hdr_play_label) {
            lv_label_set_text(hdr_play_label, p4.is_playing ? "PAUSE" : "PLAY");
            lv_obj_set_style_bg_color(hdr_play_btn, p4.is_playing ? RED808_SUCCESS : RED808_ACCENT, 0);
            lv_obj_set_style_border_color(hdr_play_btn, p4.is_playing ? RED808_CYAN : RED808_ACCENT2, 0);
        }
    }

    if (p4.wifi_connected != prev_wifi) {
        prev_wifi = p4.wifi_connected;
        if (hdr_wifi_label) {
            lv_label_set_text(hdr_wifi_label, p4.wifi_connected ? "NET OK" : "NET OFF");
            lv_obj_set_style_text_color(hdr_wifi_label,
                p4.wifi_connected ? RED808_SUCCESS : RED808_ERROR, 0);
        }
    }

    if (p4.s3_connected != prev_s3) {
        prev_s3 = p4.s3_connected;
        if (hdr_s3_label) {
            lv_label_set_text(hdr_s3_label, p4.s3_connected ? "AUX ON" : "AUX OFF");
            lv_obj_set_style_text_color(hdr_s3_label,
                p4.s3_connected ? RED808_INFO : RED808_TEXT_DIM, 0);
        }
    }
}

// =============================================================================
// BOOT SCREEN
// =============================================================================
static void create_boot_screen(void) {
    scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, RED808_BG, 0);
    lv_obj_clear_flag(scr_boot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(scr_boot);
    lv_label_set_text(title, "RED808 P4");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(title, RED808_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t* sub = lv_label_create(scr_boot);
    lv_label_set_text(sub, "Connecting to RED808 Master...");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sub, RED808_TEXT_DIM, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t* spinner = lv_spinner_create(scr_boot, 1000, 60);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 80);
}

// =============================================================================
// LIVE GRID SCREEN — 8×4 full-screen grid (4×4 pads + 4×4 controls)
// =============================================================================
static lv_obj_t* live_pad_btns[16] = {};
static lv_obj_t* live_pad_labels[16] = {};

// Right-side control widgets (dynamic updates)
static lv_obj_t* grid_play_btn = NULL;
static lv_obj_t* grid_play_lbl = NULL;
static lv_obj_t* grid_bpm_lbl = NULL;
static lv_obj_t* grid_pat_lbl = NULL;
static lv_obj_t* grid_step_lbl = NULL;
static lv_obj_t* grid_wifi_lbl = NULL;
static lv_obj_t* grid_s3_lbl = NULL;
static lv_obj_t* grid_step_dot = NULL;

static void pad_touch_cb(lv_event_t* e) {
    int pad = (int)(intptr_t)lv_event_get_user_data(e);
    if (p4.wifi_connected || p4.master_connected) {
        udp_send_trigger((uint8_t)pad, 127);
    } else {
        uart_send_to_s3(MSG_TOUCH_CMD, TCMD_PAD_TAP, (uint8_t)pad);
    }
}

static void grid_nav_cb(lv_event_t* e) {
    int screen_id = (int)(intptr_t)lv_event_get_user_data(e);
    ui_navigate_to(screen_id);
}

static void grid_theme_cb(lv_event_t* e) {
    LV_UNUSED(e);
    int next = ((int)currentTheme + 1) % THEME_COUNT;
    p4.theme = next;
    ui_theme_apply((VisualTheme)next);
}

// Helper: styled control button
static lv_obj_t* create_ctrl_btn(lv_obj_t* parent, int x, int y, int w, int h,
                                  const char* text, lv_color_t border_color,
                                  const lv_font_t* font) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, border_color, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, RED808_TEXT, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return btn;
}

// Helper: info display cell (non-clickable)
static lv_obj_t* create_info_cell(lv_obj_t* parent, int x, int y, int w, int h,
                                   const char* title, const char* value,
                                   lv_color_t value_color, lv_obj_t** value_out) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_bg_color(panel, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, RED808_BORDER, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(panel);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t, RED808_TEXT_DIM, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* v = lv_label_create(panel);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(v, value_color, 0);
    lv_obj_align(v, LV_ALIGN_CENTER, 0, 10);

    if (value_out) *value_out = v;
    return panel;
}

static void create_live_screen(void) {
    scr_live = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_live, RED808_BG, 0);
    lv_obj_clear_flag(scr_live, LV_OBJ_FLAG_SCROLLABLE);

    // 8×4 full-screen grid: 1024×600
    // Left 4 cols = pads, Right 4 cols = controls
    const int M = 8, G = 4, CG = 8, CW = 122, CH = 143;
    #define COL_X(c) ((c) < 4 ? (M + (c)*(CW+G)) : (M + 4*(CW+G) + CG + ((c)-4)*(CW+G)))
    #define ROW_Y(r) (M + (r)*(CH+G))

    // Vertical separator
    lv_obj_t* sep = lv_obj_create(scr_live);
    lv_obj_set_size(sep, 2, LCD_V_RES - 2*M);
    lv_obj_set_pos(sep, M + 4*(CW+G) + CG/2 - 1, M);
    lv_obj_set_style_bg_color(sep, RED808_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 1, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // === LEFT 4×4: Drum Pads (Neon Ring Style) ===
    for (int i = 0; i < 16; i++) {
        int c = i % 4, r = i / 4;
        lv_color_t tc = lv_color_hex(theme_presets[currentTheme].track_colors[i]);

        live_pad_btns[i] = lv_btn_create(scr_live);
        lv_obj_set_size(live_pad_btns[i], CW, CH);
        lv_obj_set_pos(live_pad_btns[i], COL_X(c), ROW_Y(r));
        // Dark interior
        lv_obj_set_style_radius(live_pad_btns[i], 16, 0);
        lv_obj_set_style_bg_color(live_pad_btns[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(live_pad_btns[i], LV_OPA_80, 0);
        // Neon ring border
        lv_obj_set_style_border_width(live_pad_btns[i], 3, 0);
        lv_obj_set_style_border_color(live_pad_btns[i], tc, 0);
        // Outer neon outline
        lv_obj_set_style_outline_width(live_pad_btns[i], 3, 0);
        lv_obj_set_style_outline_color(live_pad_btns[i], tc, 0);
        lv_obj_set_style_outline_opa(live_pad_btns[i], LV_OPA_40, 0);
        lv_obj_set_style_outline_pad(live_pad_btns[i], 2, 0);
        // Neon glow shadow — OFF by default (too expensive with full_refresh)
        lv_obj_set_style_shadow_width(live_pad_btns[i], 0, 0);
        lv_obj_set_style_shadow_color(live_pad_btns[i], tc, 0);
        lv_obj_set_style_shadow_opa(live_pad_btns[i], LV_OPA_0, 0);
        lv_obj_set_style_shadow_spread(live_pad_btns[i], 0, 0);
        lv_obj_add_event_cb(live_pad_btns[i], pad_touch_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);

        live_pad_labels[i] = lv_label_create(live_pad_btns[i]);
        lv_label_set_text(live_pad_labels[i], trackNames[i]);
        lv_obj_set_style_text_font(live_pad_labels[i], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(live_pad_labels[i], tc, 0);
        lv_obj_center(live_pad_labels[i]);
    }

    // === RIGHT 4×4: Controls ===

    // --- Row 0: Transport ---
    // [4,0] PLAY / PAUSE
    grid_play_btn = lv_btn_create(scr_live);
    lv_obj_set_size(grid_play_btn, CW, CH);
    lv_obj_set_pos(grid_play_btn, COL_X(4), ROW_Y(0));
    lv_obj_set_style_radius(grid_play_btn, 14, 0);
    lv_obj_set_style_bg_color(grid_play_btn, RED808_ACCENT, 0);
    lv_obj_set_style_bg_opa(grid_play_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid_play_btn, 3, 0);
    lv_obj_set_style_border_color(grid_play_btn, RED808_ACCENT2, 0);
    lv_obj_set_style_shadow_width(grid_play_btn, 0, 0);
    lv_obj_add_event_cb(grid_play_btn, header_play_cb, LV_EVENT_CLICKED, NULL);
    grid_play_lbl = lv_label_create(grid_play_btn);
    lv_label_set_text(grid_play_lbl, LV_SYMBOL_PLAY "\nPLAY");
    lv_obj_set_style_text_font(grid_play_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(grid_play_lbl, RED808_TEXT, 0);
    lv_obj_set_style_text_align(grid_play_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(grid_play_lbl);

    // [5,0] PAT -
    lv_obj_t* b;
    b = create_ctrl_btn(scr_live, COL_X(5), ROW_Y(0), CW, CH,
                         LV_SYMBOL_LEFT "\nPAT", RED808_WARNING, &lv_font_montserrat_20);
    lv_obj_add_event_cb(b, header_pattern_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);

    // [6,0] PAT +
    b = create_ctrl_btn(scr_live, COL_X(6), ROW_Y(0), CW, CH,
                         "PAT\n" LV_SYMBOL_RIGHT, RED808_WARNING, &lv_font_montserrat_20);
    lv_obj_add_event_cb(b, header_pattern_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);

    // [7,0] BPM display
    create_info_cell(scr_live, COL_X(7), ROW_Y(0), CW, CH,
                     "BPM", "120.0", RED808_ACCENT, &grid_bpm_lbl);

    // --- Row 1: Screen Navigation ---
    static const char* nav_texts[] = {"STEPS", "FX", "MIXER", "PERF"};
    static const int nav_screens[] = {3, 8, 7, 10};
    lv_color_t nav_colors[] = {RED808_CYAN, RED808_INFO, RED808_SUCCESS, RED808_WARNING};
    for (int i = 0; i < 4; i++) {
        b = create_ctrl_btn(scr_live, COL_X(4 + i), ROW_Y(1), CW, CH,
                             nav_texts[i], nav_colors[i], &lv_font_montserrat_20);
        lv_obj_add_event_cb(b, grid_nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)nav_screens[i]);
    }

    // --- Row 2: System ---
    // [4,2] SETTINGS
    b = create_ctrl_btn(scr_live, COL_X(4), ROW_Y(2), CW, CH,
                         LV_SYMBOL_SETTINGS "\nSET", RED808_TEXT_DIM, &lv_font_montserrat_18);
    lv_obj_add_event_cb(b, grid_nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)4);

    // [5,2] THEME
    b = create_ctrl_btn(scr_live, COL_X(5), ROW_Y(2), CW, CH,
                         "THEME\n" LV_SYMBOL_RIGHT, RED808_ACCENT2, &lv_font_montserrat_18);
    lv_obj_add_event_cb(b, grid_theme_cb, LV_EVENT_CLICKED, NULL);

    // [6,2] Network status
    create_info_cell(scr_live, COL_X(6), ROW_Y(2), CW, CH,
                     "NETWORK", "OFF", RED808_ERROR, &grid_wifi_lbl);

    // [7,2] AUX status
    create_info_cell(scr_live, COL_X(7), ROW_Y(2), CW, CH,
                     "AUX", "OFF", RED808_TEXT_DIM, &grid_s3_lbl);

    // --- Row 3: Info Displays ---
    // [4,3] Pattern
    create_info_cell(scr_live, COL_X(4), ROW_Y(3), CW, CH,
                     "PATTERN", "P01", RED808_WARNING, &grid_pat_lbl);

    // [5,3] Step
    create_info_cell(scr_live, COL_X(5), ROW_Y(3), CW, CH,
                     "STEP", "--", RED808_CYAN, &grid_step_lbl);

    // [6,3] Brand
    lv_obj_t* brand = lv_obj_create(scr_live);
    lv_obj_set_size(brand, CW, CH);
    lv_obj_set_pos(brand, COL_X(6), ROW_Y(3));
    lv_obj_set_style_radius(brand, 14, 0);
    lv_obj_set_style_bg_color(brand, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(brand, LV_OPA_60, 0);
    lv_obj_set_style_border_width(brand, 1, 0);
    lv_obj_set_style_border_color(brand, RED808_BORDER, 0);
    lv_obj_clear_flag(brand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* bl = lv_label_create(brand);
    lv_label_set_text(bl, "RED\n808");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(bl, RED808_ACCENT, 0);
    lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(bl);

    // [7,3] Live indicator with pulsing dot
    lv_obj_t* live_ind = lv_obj_create(scr_live);
    lv_obj_set_size(live_ind, CW, CH);
    lv_obj_set_pos(live_ind, COL_X(7), ROW_Y(3));
    lv_obj_set_style_radius(live_ind, 14, 0);
    lv_obj_set_style_bg_color(live_ind, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(live_ind, LV_OPA_80, 0);
    lv_obj_set_style_border_width(live_ind, 2, 0);
    lv_obj_set_style_border_color(live_ind, RED808_SUCCESS, 0);
    lv_obj_clear_flag(live_ind, LV_OBJ_FLAG_SCROLLABLE);
    grid_step_dot = lv_obj_create(live_ind);
    lv_obj_set_size(grid_step_dot, 14, 14);
    lv_obj_align(grid_step_dot, LV_ALIGN_CENTER, 0, -14);
    lv_obj_set_style_radius(grid_step_dot, 7, 0);
    lv_obj_set_style_bg_color(grid_step_dot, RED808_SUCCESS, 0);
    lv_obj_set_style_bg_opa(grid_step_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid_step_dot, 0, 0);
    lv_obj_clear_flag(grid_step_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* ll = lv_label_create(live_ind);
    lv_label_set_text(ll, "LIVE");
    lv_obj_set_style_text_font(ll, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ll, RED808_SUCCESS, 0);
    lv_obj_align(ll, LV_ALIGN_CENTER, 0, 12);

    #undef COL_X
    #undef ROW_Y
}

static void update_live_screen(void) {
    unsigned long now = millis();

    // Pad flash effects — only update on state transition (no continuous animation)
    static bool pad_was_flash[16] = {};
    for (int i = 0; i < 16; i++) {
        if (!live_pad_btns[i]) continue;
        bool flashing = (now < p4.pad_flash_until[i]);
        if (flashing == pad_was_flash[i]) continue;  // no change, skip
        pad_was_flash[i] = flashing;
        lv_color_t tc = lv_color_hex(theme_presets[currentTheme].track_colors[i]);

        if (flashing) {
            // BURST: full color fill + brief glow
            lv_obj_set_style_bg_color(live_pad_btns[i], tc, 0);
            lv_obj_set_style_bg_opa(live_pad_btns[i], LV_OPA_COVER, 0);
            lv_obj_set_style_outline_opa(live_pad_btns[i], LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(live_pad_btns[i], 20, 0);
            lv_obj_set_style_shadow_opa(live_pad_btns[i], LV_OPA_80, 0);
            lv_obj_set_style_shadow_spread(live_pad_btns[i], 4, 0);
        } else {
            // IDLE: dark interior, no shadow (fast render)
            lv_obj_set_style_bg_color(live_pad_btns[i], lv_color_black(), 0);
            lv_obj_set_style_bg_opa(live_pad_btns[i], LV_OPA_80, 0);
            lv_obj_set_style_outline_opa(live_pad_btns[i], LV_OPA_40, 0);
            lv_obj_set_style_shadow_width(live_pad_btns[i], 0, 0);
            lv_obj_set_style_shadow_opa(live_pad_btns[i], LV_OPA_0, 0);
            lv_obj_set_style_shadow_spread(live_pad_btns[i], 0, 0);
        }
    }

    // Play button state
    static bool gp_prev_play = false;
    if (grid_play_btn && grid_play_lbl && p4.is_playing != gp_prev_play) {
        gp_prev_play = p4.is_playing;
        lv_label_set_text(grid_play_lbl, p4.is_playing
            ? LV_SYMBOL_PAUSE "\nPAUSE" : LV_SYMBOL_PLAY "\nPLAY");
        lv_obj_set_style_bg_color(grid_play_btn,
            p4.is_playing ? RED808_SUCCESS : RED808_ACCENT, 0);
        lv_obj_set_style_border_color(grid_play_btn,
            p4.is_playing ? RED808_CYAN : RED808_ACCENT2, 0);
    }

    // BPM
    static int gp_prev_bpm = -1, gp_prev_frac = -1;
    if (grid_bpm_lbl && (p4.bpm_int != gp_prev_bpm || p4.bpm_frac != gp_prev_frac)) {
        gp_prev_bpm = p4.bpm_int;
        gp_prev_frac = p4.bpm_frac;
        lv_label_set_text_fmt(grid_bpm_lbl, "%d.%d", p4.bpm_int, p4.bpm_frac);
    }

    // Pattern
    static int gp_prev_pat = -1;
    if (grid_pat_lbl && p4.current_pattern != gp_prev_pat) {
        gp_prev_pat = p4.current_pattern;
        lv_label_set_text_fmt(grid_pat_lbl, "P%02d", p4.current_pattern + 1);
    }

    // Step
    static int gp_prev_step = -1;
    if (grid_step_lbl && p4.current_step != gp_prev_step) {
        gp_prev_step = p4.current_step;
        lv_label_set_text_fmt(grid_step_lbl, "%02d", p4.current_step + 1);
    }

    // Step dot pulse — only update on change
    static bool prev_dot_state = false;
    if (grid_step_dot) {
        bool pulse = p4.is_playing && ((now / 250) % 2 == 0);
        if (pulse != prev_dot_state) {
            prev_dot_state = pulse;
            lv_obj_set_style_bg_opa(grid_step_dot, pulse ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }

    // WiFi status
    static bool gp_prev_wifi = false;
    if (grid_wifi_lbl && p4.wifi_connected != gp_prev_wifi) {
        gp_prev_wifi = p4.wifi_connected;
        lv_label_set_text(grid_wifi_lbl, p4.wifi_connected ? "OK" : "OFF");
        lv_obj_set_style_text_color(grid_wifi_lbl,
            p4.wifi_connected ? RED808_SUCCESS : RED808_ERROR, 0);
    }

    // S3/AUX status
    static bool gp_prev_s3 = false;
    if (grid_s3_lbl && p4.s3_connected != gp_prev_s3) {
        gp_prev_s3 = p4.s3_connected;
        lv_label_set_text(grid_s3_lbl, p4.s3_connected ? "ON" : "OFF");
        lv_obj_set_style_text_color(grid_s3_lbl,
            p4.s3_connected ? RED808_INFO : RED808_TEXT_DIM, 0);
    }
}

// =============================================================================
// FX LAB SCREEN — 6 arc widgets (3 encoders + 3 pots)
// =============================================================================
static lv_obj_t* fx_arcs[6] = {};
static lv_obj_t* fx_value_labels[6] = {};
static lv_obj_t* fx_name_labels[6] = {};
static lv_obj_t* fx_mute_labels[6] = {};

static void create_fx_screen(void) {
    scr_fx = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_fx, RED808_BG, 0);
    lv_obj_clear_flag(scr_fx, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_fx);

    lv_obj_t* title = lv_label_create(scr_fx);
    lv_label_set_text(title, LV_SYMBOL_AUDIO "  FX LAB");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_set_pos(title, 20, 64);

    static const char* names[] = {"FLANGER", "REVERB", "PHASER", "---", "RESONANCE", "DRIVE"};
    static const char* sources[] = {"DF-1", "DF-2", "DF-3", "P2", "P3", "P4"};
    static const uint32_t colors[] = {0x58A6FF, 0x39D2C0, 0xD29922, 0xB58BFF, 0xFF8F5A, 0x7DD36F};

    int panelW = UI_W - 24;
    int panelH = UI_H - 200;
    int gap = 10, cols = 2, rows = 3;
    int cellW = (panelW - gap) / cols;
    int cellH = (panelH - gap * 2) / rows;

    for (int cell = 0; cell < 6; cell++) {
        int col = cell % 2, row = cell / 2;
        int x = 12 + col * (cellW + gap);
        int y = 100 + row * (cellH + gap);

        lv_obj_t* card = create_section_shell(scr_fx, x, y, cellW, cellH);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x17222B), 0);

        fx_name_labels[cell] = lv_label_create(card);
        lv_label_set_text(fx_name_labels[cell], names[cell]);
        lv_obj_set_style_text_font(fx_name_labels[cell], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(fx_name_labels[cell], lv_color_hex(colors[cell]), 0);
        lv_obj_set_pos(fx_name_labels[cell], 12, 8);

        fx_mute_labels[cell] = lv_label_create(card);
        lv_label_set_text(fx_mute_labels[cell], cell < 3 ? "ON" : "ANLG");
        lv_obj_set_style_text_font(fx_mute_labels[cell], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(fx_mute_labels[cell], RED808_SUCCESS, 0);
        lv_obj_align(fx_mute_labels[cell], LV_ALIGN_TOP_RIGHT, -8, 10);

        int arcSize = min(cellW - 24, cellH - 68);
        arcSize = constrain(arcSize, 84, 180);
        fx_arcs[cell] = lv_arc_create(card);
        lv_obj_set_size(fx_arcs[cell], arcSize, arcSize);
        lv_obj_align(fx_arcs[cell], LV_ALIGN_CENTER, 0, 0);
        lv_arc_set_rotation(fx_arcs[cell], 135);
        lv_arc_set_bg_angles(fx_arcs[cell], 0, 270);
        lv_arc_set_range(fx_arcs[cell], 0, 127);
        lv_arc_set_value(fx_arcs[cell], 0);
        lv_obj_clear_flag(fx_arcs[cell], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(fx_arcs[cell], NULL, LV_PART_KNOB);
        lv_obj_set_style_arc_width(fx_arcs[cell], 10, LV_PART_MAIN);
        lv_obj_set_style_arc_color(fx_arcs[cell], lv_color_hex(0x3A4B58), LV_PART_MAIN);
        lv_obj_set_style_arc_width(fx_arcs[cell], 16, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(fx_arcs[cell], lv_color_hex(colors[cell]), LV_PART_INDICATOR);

        fx_value_labels[cell] = lv_label_create(card);
        lv_label_set_text(fx_value_labels[cell], "000");
        lv_obj_set_style_text_font(fx_value_labels[cell], &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(fx_value_labels[cell], RED808_TEXT, 0);
        lv_obj_align(fx_value_labels[cell], LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* src = lv_label_create(card);
        lv_label_set_text(src, sources[cell]);
        lv_obj_set_style_text_font(src, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(src, RED808_TEXT_DIM, 0);
        lv_obj_align(src, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}

static void update_fx_screen(void) {
    for (int i = 0; i < 3; i++) {
        int val = p4.enc_value[i];
        bool muted = p4.enc_muted[i];
        if (fx_arcs[i]) lv_arc_set_value(fx_arcs[i], muted ? 0 : val);
        if (fx_value_labels[i]) lv_label_set_text_fmt(fx_value_labels[i], "%03d", val);
        if (fx_mute_labels[i]) {
            lv_label_set_text(fx_mute_labels[i], muted ? "MUTE" : "ON");
            lv_obj_set_style_text_color(fx_mute_labels[i], muted ? RED808_ERROR : RED808_SUCCESS, 0);
        }
    }
    // Pots: resonance (cell 4), drive (cell 5). Cell 3 disabled.
    int pot_vals[3] = {0, p4.pot_value[2], p4.pot_value[3]};
    for (int i = 3; i < 6; i++) {
        int idx = i - 3;
        bool muted = (idx > 0) ? p4.pot_muted[idx] : false;
        if (fx_arcs[i]) lv_arc_set_value(fx_arcs[i], muted ? 0 : pot_vals[idx]);
        if (fx_value_labels[i]) lv_label_set_text_fmt(fx_value_labels[i], "%03d", pot_vals[idx]);
        if (fx_mute_labels[i]) {
            lv_label_set_text(fx_mute_labels[i], (i == 3) ? "---" : (muted ? "MUTE" : "ANLG"));
        }
    }
}

// =============================================================================
// SEQUENCER SCREEN — full 16-track × 16-step professional grid
// =============================================================================
static lv_obj_t* seq_step_btns[16][16] = {};
static lv_obj_t* seq_track_labels[16] = {};
static lv_obj_t* seq_mute_btns[16] = {};
static lv_obj_t* seq_solo_btns[16] = {};
static lv_obj_t* seq_mute_labels[16] = {};
static lv_obj_t* seq_solo_labels[16] = {};
static lv_obj_t* seq_playhead[16] = {};  // vertical playhead markers per row

// Layout constants for 1024×600
static const int SEQ_Y_START     = 80;
static const int SEQ_X_LABEL     = 4;
static const int SEQ_LABEL_W     = 26;
static const int SEQ_BTN_W       = 24;
static const int SEQ_BTN_GAP     = 2;
static const int SEQ_X_MUTE      = SEQ_X_LABEL + SEQ_LABEL_W + 2;
static const int SEQ_X_SOLO      = SEQ_X_MUTE + SEQ_BTN_W + SEQ_BTN_GAP;
static const int SEQ_X_GRID      = SEQ_X_SOLO + SEQ_BTN_W + 6;
static const int SEQ_X_END       = 1018;
static const int SEQ_Y_END       = 596;

static void seq_step_cb(lv_event_t* e) {
    int data = (int)(intptr_t)lv_event_get_user_data(e);
    int track = (data >> 8) & 0xFF;
    int step  = data & 0xFF;
    if (track < 16 && step < 16) {
        bool next = !p4.steps[track][step];
        p4.steps[track][step] = next;
        udp_send_set_step(track, step, next);
    }
}

static void seq_mute_cb(lv_event_t* e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    if (track < 16) {
        bool next = !p4.track_muted[track];
        p4.track_muted[track] = next;
        udp_send_mute(track, next);
    }
}

static void seq_solo_cb(lv_event_t* e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    if (track < 16) {
        p4.track_solo[track] = !p4.track_solo[track];
    }
}

static void create_sequencer_screen(void) {
    scr_sequencer = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sequencer, RED808_BG, 0);
    lv_obj_clear_flag(scr_sequencer, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_sequencer);

    int gridW = SEQ_X_END - SEQ_X_GRID;
    int gridH = SEQ_Y_END - SEQ_Y_START;
    int cellW = (gridW - 15 * 1 - 3 * 3) / 16;  // 1px gap + 3px extra every 4 beats
    int cellH = (gridH - 15 * 1) / 16;
    if (cellH > 30) cellH = 30;
    if (cellW > 56) cellW = 56;

    for (int t = 0; t < 16; t++) {
        int rowY = SEQ_Y_START + t * (cellH + 1);
        lv_color_t trkColor = lv_color_hex(theme_presets[currentTheme].track_colors[t]);

        // Track label (BD, SD, CH, etc.)
        seq_track_labels[t] = lv_label_create(scr_sequencer);
        lv_label_set_text(seq_track_labels[t], trackNames[t]);
        lv_obj_set_style_text_font(seq_track_labels[t], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(seq_track_labels[t], trkColor, 0);
        lv_obj_set_pos(seq_track_labels[t], SEQ_X_LABEL, rowY + (cellH - 12) / 2);

        // Mute button
        seq_mute_btns[t] = lv_btn_create(scr_sequencer);
        lv_obj_set_size(seq_mute_btns[t], SEQ_BTN_W, cellH);
        lv_obj_set_pos(seq_mute_btns[t], SEQ_X_MUTE, rowY);
        lv_obj_set_style_radius(seq_mute_btns[t], 4, 0);
        lv_obj_set_style_bg_color(seq_mute_btns[t], RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(seq_mute_btns[t], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(seq_mute_btns[t], 1, 0);
        lv_obj_set_style_border_color(seq_mute_btns[t], RED808_BORDER, 0);
        lv_obj_set_style_shadow_width(seq_mute_btns[t], 0, 0);
        lv_obj_set_style_pad_all(seq_mute_btns[t], 0, 0);
        lv_obj_add_event_cb(seq_mute_btns[t], seq_mute_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t);
        seq_mute_labels[t] = lv_label_create(seq_mute_btns[t]);
        lv_label_set_text(seq_mute_labels[t], "M");
        lv_obj_set_style_text_font(seq_mute_labels[t], &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(seq_mute_labels[t], RED808_TEXT_DIM, 0);
        lv_obj_center(seq_mute_labels[t]);

        // Solo button
        seq_solo_btns[t] = lv_btn_create(scr_sequencer);
        lv_obj_set_size(seq_solo_btns[t], SEQ_BTN_W, cellH);
        lv_obj_set_pos(seq_solo_btns[t], SEQ_X_SOLO, rowY);
        lv_obj_set_style_radius(seq_solo_btns[t], 4, 0);
        lv_obj_set_style_bg_color(seq_solo_btns[t], RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(seq_solo_btns[t], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(seq_solo_btns[t], 1, 0);
        lv_obj_set_style_border_color(seq_solo_btns[t], RED808_BORDER, 0);
        lv_obj_set_style_shadow_width(seq_solo_btns[t], 0, 0);
        lv_obj_set_style_pad_all(seq_solo_btns[t], 0, 0);
        lv_obj_add_event_cb(seq_solo_btns[t], seq_solo_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t);
        seq_solo_labels[t] = lv_label_create(seq_solo_btns[t]);
        lv_label_set_text(seq_solo_labels[t], "S");
        lv_obj_set_style_text_font(seq_solo_labels[t], &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(seq_solo_labels[t], RED808_TEXT_DIM, 0);
        lv_obj_center(seq_solo_labels[t]);

        // Step buttons (16 per track)
        int xOff = SEQ_X_GRID;
        for (int s = 0; s < 16; s++) {
            // Extra gap every 4 steps (beat grouping)
            if (s > 0 && (s % 4) == 0) xOff += 3;

            seq_step_btns[t][s] = lv_obj_create(scr_sequencer);
            lv_obj_set_size(seq_step_btns[t][s], cellW, cellH);
            lv_obj_set_pos(seq_step_btns[t][s], xOff, rowY);
            lv_obj_set_style_radius(seq_step_btns[t][s], 3, 0);
            lv_obj_set_style_bg_color(seq_step_btns[t][s], RED808_SURFACE, 0);
            lv_obj_set_style_bg_opa(seq_step_btns[t][s], LV_OPA_60, 0);
            lv_obj_set_style_border_width(seq_step_btns[t][s], 1, 0);
            lv_obj_set_style_border_color(seq_step_btns[t][s], RED808_BORDER, 0);
            lv_obj_clear_flag(seq_step_btns[t][s], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(seq_step_btns[t][s], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(seq_step_btns[t][s], seq_step_cb, LV_EVENT_CLICKED,
                                (void*)(intptr_t)((t << 8) | s));

            xOff += cellW + 1;
        }
    }
}

static void update_sequencer_screen(void) {
    int step = p4.current_step;
    bool playing = p4.is_playing;

    for (int t = 0; t < 16; t++) {
        lv_color_t trkColor = lv_color_hex(theme_presets[currentTheme].track_colors[t]);
        bool muted = p4.track_muted[t];
        bool soloed = p4.track_solo[t];

        // Update mute button appearance
        if (seq_mute_btns[t]) {
            lv_obj_set_style_bg_color(seq_mute_btns[t], muted ? RED808_ERROR : RED808_SURFACE, 0);
            lv_obj_set_style_bg_opa(seq_mute_btns[t], muted ? LV_OPA_90 : LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(seq_mute_labels[t],
                muted ? lv_color_white() : RED808_TEXT_DIM, 0);
        }
        // Update solo button appearance
        if (seq_solo_btns[t]) {
            lv_obj_set_style_bg_color(seq_solo_btns[t], soloed ? RED808_WARNING : RED808_SURFACE, 0);
            lv_obj_set_style_bg_opa(seq_solo_btns[t], soloed ? LV_OPA_90 : LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(seq_solo_labels[t],
                soloed ? lv_color_black() : RED808_TEXT_DIM, 0);
        }

        // Update step grid
        for (int s = 0; s < 16; s++) {
            if (!seq_step_btns[t][s]) continue;
            bool active = p4.steps[t][s];
            bool isCurrent = playing && (step == s);

            lv_color_t bg;
            lv_opa_t opa;
            lv_color_t border;

            if (isCurrent && active) {
                bg = lv_color_white();
                opa = LV_OPA_COVER;
                border = trkColor;
            } else if (isCurrent) {
                bg = RED808_PANEL;
                opa = LV_OPA_90;
                border = RED808_ACCENT;
            } else if (active) {
                bg = trkColor;
                opa = muted ? LV_OPA_30 : LV_OPA_80;
                border = trkColor;
            } else {
                bg = RED808_SURFACE;
                opa = LV_OPA_40;
                border = RED808_BORDER;
            }

            lv_obj_set_style_bg_color(seq_step_btns[t][s], bg, 0);
            lv_obj_set_style_bg_opa(seq_step_btns[t][s], opa, 0);
            lv_obj_set_style_border_color(seq_step_btns[t][s], border, 0);
        }

        // Update track label (dim if muted)
        if (seq_track_labels[t]) {
            lv_obj_set_style_text_color(seq_track_labels[t],
                muted ? RED808_TEXT_DIM : trkColor, 0);
        }
    }
}

// =============================================================================
// VOLUMES SCREEN — 8 vertical bars
// =============================================================================
static lv_obj_t* vol_bars[8] = {};
static lv_obj_t* vol_labels[8] = {};
static lv_obj_t* vol_mute_dots[8] = {};

static void create_volumes_screen(void) {
    scr_volumes = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_volumes, RED808_BG, 0);
    lv_obj_clear_flag(scr_volumes, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_volumes);

    lv_obj_t* title = lv_label_create(scr_volumes);
    lv_label_set_text(title, LV_SYMBOL_VOLUME_MAX "  TRACK VOLUMES");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_set_pos(title, 20, 64);

    int barW = (UI_W - 120) / 8;
    int barH = UI_H - 200;

    for (int i = 0; i < 8; i++) {
        int x = 60 + i * (barW + 4);

        vol_bars[i] = lv_bar_create(scr_volumes);
        lv_obj_set_size(vol_bars[i], barW - 8, barH);
        lv_obj_set_pos(vol_bars[i], x, 100);
        lv_bar_set_range(vol_bars[i], 0, 100);
        lv_bar_set_value(vol_bars[i], 75, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(vol_bars[i], RED808_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(vol_bars[i],
            lv_color_hex(theme_presets[currentTheme].track_colors[i]), LV_PART_INDICATOR);

        vol_labels[i] = lv_label_create(scr_volumes);
        lv_label_set_text(vol_labels[i], trackNames[i]);
        lv_obj_set_style_text_font(vol_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(vol_labels[i],
            lv_color_hex(theme_presets[currentTheme].track_colors[i]), 0);
        lv_obj_set_pos(vol_labels[i], x + barW / 2 - 10, 100 + barH + 8);

        vol_mute_dots[i] = lv_obj_create(scr_volumes);
        lv_obj_set_size(vol_mute_dots[i], 10, 10);
        lv_obj_set_pos(vol_mute_dots[i], x + barW / 2 - 5, 100 + barH + 28);
        lv_obj_set_style_radius(vol_mute_dots[i], 5, 0);
        lv_obj_set_style_bg_color(vol_mute_dots[i], RED808_SUCCESS, 0);
        lv_obj_set_style_bg_opa(vol_mute_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(vol_mute_dots[i], 0, 0);
        lv_obj_clear_flag(vol_mute_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void update_volumes_screen(void) {
    for (int i = 0; i < 8; i++) {
        if (vol_bars[i]) lv_bar_set_value(vol_bars[i], p4.track_volume[i], LV_ANIM_OFF);
        if (vol_mute_dots[i]) {
            lv_obj_set_style_bg_color(vol_mute_dots[i],
                p4.track_muted[i] ? RED808_ERROR : RED808_SUCCESS, 0);
        }
    }
}

// =============================================================================
// SETTINGS SCREEN (placeholder)
// =============================================================================
static void create_settings_screen(void) {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, RED808_BG, 0);
    lv_obj_clear_flag(scr_settings, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_settings);

    lv_obj_t* title = lv_label_create(scr_settings);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_set_pos(title, 20, 64);

    lv_obj_t* info = lv_label_create(scr_settings);
    lv_label_set_text(info, "RED808 V6 — P4 Visual Beast\nGuition ESP32-P4 JC1060P470C\n1024x600 MIPI-DSI");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(info, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(info, 20, 120);
}

// =============================================================================
// PERFORMANCE SCREEN (placeholder)
// =============================================================================
static void create_performance_screen(void) {
    scr_performance = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_performance, RED808_BG, 0);
    lv_obj_clear_flag(scr_performance, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_performance);

    lv_obj_t* title = lv_label_create(scr_performance);
    lv_label_set_text(title, LV_SYMBOL_AUDIO "  PERFORMANCE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_set_pos(title, 20, 64);
}

// =============================================================================
// PUBLIC API
// =============================================================================
void ui_create_all_screens(void) {
    create_boot_screen();
    create_live_screen();
    create_sequencer_screen();
    create_fx_screen();
    create_volumes_screen();
    create_settings_screen();
    create_performance_screen();

    // Start on boot screen
    lv_scr_load(scr_boot);
    active_screen = 0;
}

void ui_navigate_to(int screen_id) {
    lv_obj_t* targets[] = {
        scr_boot, NULL, scr_live, scr_sequencer, scr_settings,
        NULL, NULL, scr_volumes, scr_fx, NULL, scr_performance
    };
    int count = sizeof(targets) / sizeof(targets[0]);
    if (screen_id >= 0 && screen_id < count && targets[screen_id]) {
        lv_scr_load_anim(targets[screen_id], LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
        prev_active_screen = active_screen;
        active_screen = screen_id;
    }
}

void ui_update_current_screen(void) {
    // Auto-navigate from boot to live when Master or optional S3 connects
    if (active_screen == 0 && (p4.master_connected || p4.s3_connected)) {
        ui_navigate_to(2);  // SCREEN_LIVE
    }

    // Apply theme if changed
    static int prev_theme = -1;
    if (p4.theme != prev_theme) {
        prev_theme = p4.theme;
        ui_theme_apply((VisualTheme)p4.theme);
    }

    // Navigate if S3 sends screen command
    static int prev_screen = -1;
    if (p4.current_screen != prev_screen) {
        prev_screen = p4.current_screen;
        ui_navigate_to(p4.current_screen);
    }

    ui_update_header();

    // Update active screen content
    lv_obj_t* active = lv_scr_act();
    if (active == scr_live) update_live_screen();
    else if (active == scr_sequencer) update_sequencer_screen();
    else if (active == scr_fx) update_fx_screen();
    else if (active == scr_volumes) update_volumes_screen();
}
