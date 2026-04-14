// =============================================================================
// ui_screens.cpp — P4 UI screens (data-driven from UART state)
// All screen rendering reads from P4State (p4.*) — no direct hardware access.
// =============================================================================

#include "ui_screens.h"
#include "ui_theme.h"
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
static lv_obj_t* hdr_play_label = NULL;
static lv_obj_t* hdr_wifi_label = NULL;
static lv_obj_t* hdr_s3_label = NULL;
static lv_obj_t* hdr_step_dots[16] = {};

// Current active screen index
static int active_screen = 0;

// Track names
static const char* trackNames[] = {
    "BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL",
    "MA", "CY", "HT", "LT", "MC", "MT", "HC", "LC"
};

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

// =============================================================================
// HEADER BAR (top of every screen)
// =============================================================================
void ui_create_header(lv_obj_t* parent) {
    header_bar = lv_obj_create(parent);
    lv_obj_set_size(header_bar, UI_W, 56);
    lv_obj_set_pos(header_bar, 0, 0);
    lv_obj_set_style_bg_color(header_bar, lv_color_hex(0x0A0F16), 0);
    lv_obj_set_style_bg_opa(header_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header_bar, 0, 0);
    lv_obj_set_style_radius(header_bar, 0, 0);
    lv_obj_set_style_pad_all(header_bar, 0, 0);
    lv_obj_clear_flag(header_bar, LV_OBJ_FLAG_SCROLLABLE);

    // BPM
    hdr_bpm_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_bpm_label, "120.0");
    lv_obj_set_style_text_font(hdr_bpm_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hdr_bpm_label, RED808_ACCENT, 0);
    lv_obj_set_pos(hdr_bpm_label, 12, 14);

    // Pattern
    hdr_pattern_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_pattern_label, "P01");
    lv_obj_set_style_text_font(hdr_pattern_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(hdr_pattern_label, RED808_WARNING, 0);
    lv_obj_set_pos(hdr_pattern_label, 120, 18);

    // Play state
    hdr_play_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_play_label, LV_SYMBOL_STOP);
    lv_obj_set_style_text_font(hdr_play_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(hdr_play_label, RED808_TEXT, 0);
    lv_obj_set_pos(hdr_play_label, 180, 16);

    // Step dots (16 dots showing current sequencer position)
    int dotStartX = (UI_W - 16 * 18) / 2;
    for (int i = 0; i < 16; i++) {
        hdr_step_dots[i] = lv_obj_create(header_bar);
        lv_obj_set_size(hdr_step_dots[i], 12, 12);
        lv_obj_set_pos(hdr_step_dots[i], dotStartX + i * 18, 22);
        lv_obj_set_style_radius(hdr_step_dots[i], 6, 0);
        lv_obj_set_style_bg_color(hdr_step_dots[i], RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(hdr_step_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(hdr_step_dots[i], 0, 0);
        lv_obj_clear_flag(hdr_step_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // WiFi indicator
    hdr_wifi_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(hdr_wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hdr_wifi_label, RED808_TEXT_DIM, 0);
    lv_obj_align(hdr_wifi_label, LV_ALIGN_TOP_RIGHT, -44, 18);

    // S3 connection indicator
    hdr_s3_label = lv_label_create(header_bar);
    lv_label_set_text(hdr_s3_label, "S3");
    lv_obj_set_style_text_font(hdr_s3_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hdr_s3_label, RED808_TEXT_DIM, 0);
    lv_obj_align(hdr_s3_label, LV_ALIGN_TOP_RIGHT, -12, 20);
}

void ui_update_header(void) {
    static int prev_bpm = -1, prev_frac = -1, prev_pat = -1, prev_step = -1;
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
        if (hdr_play_label) {
            lv_label_set_text(hdr_play_label, p4.is_playing ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP);
            lv_obj_set_style_text_color(hdr_play_label, p4.is_playing ? RED808_SUCCESS : RED808_TEXT, 0);
        }
    }

    if (p4.current_step != prev_step) {
        if (prev_step >= 0 && prev_step < 16 && hdr_step_dots[prev_step])
            lv_obj_set_style_bg_color(hdr_step_dots[prev_step], RED808_SURFACE, 0);
        prev_step = p4.current_step;
        if (prev_step >= 0 && prev_step < 16 && hdr_step_dots[prev_step])
            lv_obj_set_style_bg_color(hdr_step_dots[prev_step], RED808_ACCENT, 0);
    }

    if (p4.wifi_connected != prev_wifi) {
        prev_wifi = p4.wifi_connected;
        if (hdr_wifi_label) lv_obj_set_style_text_color(hdr_wifi_label,
            p4.wifi_connected ? RED808_SUCCESS : RED808_ERROR, 0);
    }

    if (p4.s3_connected != prev_s3) {
        prev_s3 = p4.s3_connected;
        if (hdr_s3_label) lv_obj_set_style_text_color(hdr_s3_label,
            p4.s3_connected ? RED808_SUCCESS : RED808_ERROR, 0);
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
// LIVE PAD SCREEN — 4×4 pad grid with velocity flash
// =============================================================================
static lv_obj_t* live_pad_btns[16] = {};
static lv_obj_t* live_pad_labels[16] = {};

static void pad_touch_cb(lv_event_t* e) {
    int pad = (int)(intptr_t)lv_event_get_user_data(e);
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_PAD_TAP, (uint8_t)pad);
}

static void create_live_screen(void) {
    scr_live = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_live, RED808_BG, 0);
    lv_obj_clear_flag(scr_live, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_live);

    int gridX = 12, gridY = 64;
    int padW = (UI_W - 24 - 3 * 8) / 4;
    int padH = padW;
    int gap = 8;

    for (int i = 0; i < 16; i++) {
        int col = i % 4, row = i / 4;
        int x = gridX + col * (padW + gap);
        int y = gridY + row * (padH + gap);

        live_pad_btns[i] = lv_btn_create(scr_live);
        lv_obj_set_size(live_pad_btns[i], padW, padH);
        lv_obj_set_pos(live_pad_btns[i], x, y);
        lv_obj_set_style_radius(live_pad_btns[i], 12, 0);
        lv_obj_set_style_bg_color(live_pad_btns[i], lv_color_hex(theme_presets[currentTheme].track_colors[i]), 0);
        lv_obj_set_style_bg_opa(live_pad_btns[i], LV_OPA_60, 0);
        lv_obj_set_style_border_width(live_pad_btns[i], 2, 0);
        lv_obj_set_style_border_color(live_pad_btns[i], lv_color_hex(theme_presets[currentTheme].track_colors[i]), 0);
        lv_obj_add_event_cb(live_pad_btns[i], pad_touch_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        live_pad_labels[i] = lv_label_create(live_pad_btns[i]);
        lv_label_set_text(live_pad_labels[i], trackNames[i]);
        lv_obj_set_style_text_font(live_pad_labels[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(live_pad_labels[i], RED808_TEXT, 0);
        lv_obj_center(live_pad_labels[i]);
    }
}

static void update_live_screen(void) {
    unsigned long now = millis();
    for (int i = 0; i < 16; i++) {
        if (!live_pad_btns[i]) continue;
        bool flashing = (now < p4.pad_flash_until[i]);
        lv_obj_set_style_bg_opa(live_pad_btns[i], flashing ? LV_OPA_COVER : LV_OPA_60, 0);
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
// SEQUENCER SCREEN — step grid (8 tracks visible × 16 steps)
// =============================================================================
static lv_obj_t* seq_step_btns[8][16] = {};
static lv_obj_t* seq_track_labels[8] = {};

static void create_sequencer_screen(void) {
    scr_sequencer = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sequencer, RED808_BG, 0);
    lv_obj_clear_flag(scr_sequencer, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_sequencer);

    int gridX = 60, gridY = 64;
    int stepW = (UI_W - gridX - 12) / 16;
    int stepH = (UI_H - gridY - 80) / 8;
    stepW = min(stepW, stepH);
    stepH = stepW;

    for (int t = 0; t < 8; t++) {
        seq_track_labels[t] = lv_label_create(scr_sequencer);
        lv_label_set_text(seq_track_labels[t], trackNames[t]);
        lv_obj_set_style_text_font(seq_track_labels[t], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(seq_track_labels[t],
            lv_color_hex(theme_presets[currentTheme].track_colors[t]), 0);
        lv_obj_set_pos(seq_track_labels[t], 8, gridY + t * (stepH + 4) + 6);

        for (int s = 0; s < 16; s++) {
            int x = gridX + s * (stepW + 2);
            int y = gridY + t * (stepH + 4);

            seq_step_btns[t][s] = lv_obj_create(scr_sequencer);
            lv_obj_set_size(seq_step_btns[t][s], stepW - 2, stepH - 2);
            lv_obj_set_pos(seq_step_btns[t][s], x, y);
            lv_obj_set_style_radius(seq_step_btns[t][s], 4, 0);
            lv_obj_set_style_bg_color(seq_step_btns[t][s], RED808_SURFACE, 0);
            lv_obj_set_style_bg_opa(seq_step_btns[t][s], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(seq_step_btns[t][s], 1, 0);
            lv_obj_set_style_border_color(seq_step_btns[t][s], RED808_BORDER, 0);
            lv_obj_clear_flag(seq_step_btns[t][s], LV_OBJ_FLAG_SCROLLABLE);
        }
    }
}

static void update_sequencer_screen(void) {
    for (int t = 0; t < 8; t++) {
        lv_color_t trackColor = lv_color_hex(theme_presets[currentTheme].track_colors[t]);
        for (int s = 0; s < 16; s++) {
            if (!seq_step_btns[t][s]) continue;
            bool active = p4.steps[t][s];
            bool isCurrent = p4.is_playing && (p4.current_step == s);
            lv_color_t bg = active ? trackColor : RED808_SURFACE;
            if (isCurrent) bg = lv_color_white();
            lv_obj_set_style_bg_color(seq_step_btns[t][s], bg, 0);
            lv_obj_set_style_bg_opa(seq_step_btns[t][s], active ? LV_OPA_80 : LV_OPA_40, 0);
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
