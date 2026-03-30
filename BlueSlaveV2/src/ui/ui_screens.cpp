// =============================================================================
// ui_screens.cpp - Screen implementations with LVGL
// RED808 V6 BlueSlaveV2 - 1024x600 touch interface
// =============================================================================
#include "ui_screens.h"
#include "ui_theme.h"
#include "../../include/system_state.h"
#include "../../include/config.h"
#include <Esp.h>
#include <ArduinoJson.h>

extern void sendUDPCommand(JsonDocument& doc);

// Screen objects
lv_obj_t* scr_menu = NULL;
lv_obj_t* scr_live = NULL;
lv_obj_t* scr_sequencer = NULL;
lv_obj_t* scr_volumes = NULL;
lv_obj_t* scr_filters = NULL;
lv_obj_t* scr_settings = NULL;
lv_obj_t* scr_diagnostics = NULL;
lv_obj_t* scr_patterns = NULL;
lv_obj_t* scr_spectrum = NULL;
lv_obj_t* scr_performance = NULL;
lv_obj_t* scr_samples = NULL;

// Header widgets (updated live)
static constexpr uint8_t HEADER_SLOT_COUNT = SCREEN_ENCODER_TEST + 1;
static lv_obj_t* lbl_seq_volume[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_pad_volume[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_bpm[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_pattern[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_play[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_wifi[HEADER_SLOT_COUNT] = {};

// Menu status card labels
static lv_obj_t* lbl_master = NULL;
static lv_obj_t* lbl_menu_state = NULL;
static lv_obj_t* lbl_menu_transport = NULL;

// Sequencer grid buttons
static lv_obj_t* seq_grid[Config::MAX_TRACKS][Config::MAX_STEPS];
static lv_obj_t* seq_track_labels[Config::MAX_TRACKS];
static lv_obj_t* lbl_step_indicator = NULL;
static lv_obj_t* seq_column_highlights[Config::MAX_STEPS];

// Volume sliders
static lv_obj_t* vol_sliders[Config::MAX_TRACKS];
static lv_obj_t* vol_labels[Config::MAX_TRACKS];
static lv_obj_t* vol_name_labels[Config::MAX_TRACKS];

// Filter UI
static lv_obj_t* filter_arcs[3];       // Delay, Flanger, Compressor
static lv_obj_t* filter_labels[3];
static lv_obj_t* filter_value_labels[3];
static lv_obj_t* filter_target_label = NULL;

// Live pads
static lv_obj_t* live_pads[Config::MAX_SAMPLES];
static lv_obj_t* live_pad_names[Config::MAX_SAMPLES];
static lv_obj_t* live_pad_desc[Config::MAX_SAMPLES];
static lv_coord_t live_pad_x[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_y[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_w[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_h[Config::MAX_SAMPLES] = {};

static constexpr int LIVE_PAD_COLS = 4;
static constexpr int LIVE_PAD_SIZE = 220;
static constexpr int LIVE_PAD_GAP = 12;
static constexpr int LIVE_PAD_AREA_TOP = 70;

static uint32_t last_nav_ms = 0;

static Screen screen_from_parent(lv_obj_t* parent) {
    if (parent == scr_menu) return SCREEN_MENU;
    if (parent == scr_live) return SCREEN_LIVE;
    if (parent == scr_sequencer) return SCREEN_SEQUENCER;
    if (parent == scr_settings) return SCREEN_SETTINGS;
    if (parent == scr_diagnostics) return SCREEN_DIAGNOSTICS;
    if (parent == scr_patterns) return SCREEN_PATTERNS;
    if (parent == scr_volumes) return SCREEN_VOLUMES;
    if (parent == scr_filters) return SCREEN_FILTERS;
    if (parent == scr_spectrum) return SCREEN_SPECTRUM;
    if (parent == scr_performance) return SCREEN_PERFORMANCE;
    if (parent == scr_samples) return SCREEN_SAMPLES;
    return SCREEN_BOOT;
}

static const char* get_play_state_text(bool playing) {
    return playing ? LV_SYMBOL_PLAY " PLAY" : LV_SYMBOL_PAUSE " PAUSE";
}

static lv_obj_t* create_info_chip(lv_obj_t* parent, const lv_color_t bg_color, lv_obj_t** out_label) {
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_set_width(chip, LV_SIZE_CONTENT);
    lv_obj_set_height(chip, 26);
    lv_obj_set_style_radius(chip, 13, 0);
    lv_obj_set_style_bg_color(chip, bg_color, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_30, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, bg_color, 0);
    lv_obj_set_style_pad_hor(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 2, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, RED808_TEXT, 0);
    lv_obj_center(label);
    if (out_label) {
        *out_label = label;
    }
    return chip;
}

static lv_obj_t* create_section_shell(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* shell = lv_obj_create(parent);
    lv_obj_set_pos(shell, x, y);
    lv_obj_set_size(shell, w, h);
    lv_obj_set_style_bg_color(shell, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_70, 0);
    lv_obj_set_style_border_width(shell, 1, 0);
    lv_obj_set_style_border_color(shell, RED808_BORDER, 0);
    lv_obj_set_style_radius(shell, 18, 0);
    lv_obj_set_style_pad_all(shell, 14, 0);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_SCROLLABLE);
    return shell;
}

static void apply_stable_button_style(lv_obj_t* obj, lv_color_t base_color, lv_color_t border_color) {
    if (!obj) return;

    lv_obj_set_style_bg_color(obj, base_color, 0);
    lv_obj_set_style_bg_color(obj, base_color, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, base_color, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(obj, border_color, 0);
    lv_obj_set_style_border_color(obj, border_color, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, border_color, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_transform_width(obj, 0, 0);
    lv_obj_set_style_transform_width(obj, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(obj, 0, 0);
    lv_obj_set_style_transform_height(obj, 0, LV_STATE_PRESSED);
    lv_obj_set_style_translate_x(obj, 0, 0);
    lv_obj_set_style_translate_x(obj, 0, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(obj, 0, 0);
    lv_obj_set_style_translate_y(obj, 0, LV_STATE_PRESSED);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
}

static void nav_to(Screen screen, lv_obj_t* scr) {
    if (!scr) return;
    if (currentScreen == screen || lv_scr_act() == scr) return;

    uint32_t now = lv_tick_get();
    if ((uint32_t)(now - last_nav_ms) < 180) return;
    last_nav_ms = now;

    // Guard BEFORE screen change: prevent race with Core 0 touch handler
    if (screen == SCREEN_LIVE) {
        extern unsigned long liveScreenEnteredMs;
        liveScreenEnteredMs = millis();
        memset(livePadPressed, 0, sizeof(bool) * Config::MAX_SAMPLES);
        pendingLivePadTriggerMask = 0;
    }

    currentScreen = screen;
    lv_scr_load(scr);
}

// ============================================================================
// HEADER BAR (shared across screens)
// ============================================================================
static void back_btn_cb(lv_event_t* e) {
    (void)e;
    nav_to(SCREEN_MENU, scr_menu);
}

void ui_create_header(lv_obj_t* parent) {
    Screen screen = screen_from_parent(parent);
    uint8_t slot = static_cast<uint8_t>(screen);

    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_size(header, 1000, 58);
    lv_obj_set_pos(header, 12, 10);
    lv_obj_set_style_bg_color(header, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_80, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, RED808_BORDER, 0);
    lv_obj_set_style_radius(header, 18, 0);
    lv_obj_set_style_pad_left(header, 10, 0);
    lv_obj_set_style_pad_right(header, 10, 0);
    lv_obj_set_style_pad_top(header, 6, 0);
    lv_obj_set_style_pad_bottom(header, 6, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Back button (shown on all screens except menu)
    if (parent != scr_menu) {
        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 94, 40);
        apply_stable_button_style(btn_back, RED808_SURFACE, RED808_ACCENT);
        lv_obj_set_style_radius(btn_back, 12, 0);
        lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_t* bl = lv_label_create(btn_back);
        lv_label_set_text(bl, LV_SYMBOL_LEFT " BACK");
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(bl, RED808_TEXT, 0);
        lv_obj_center(bl);
    }

    // Logo
    lv_obj_t* logo = lv_label_create(header);
    lv_label_set_text(logo, LV_SYMBOL_AUDIO " RED808 V6");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(logo, RED808_ACCENT, 0);

    lv_obj_t* status_row = lv_obj_create(header);
    lv_obj_set_height(status_row, 40);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_style_pad_all(status_row, 0, 0);
    lv_obj_set_style_pad_gap(status_row, 6, 0);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(status_row, 1);

    create_info_chip(status_row, RED808_ACCENT, &lbl_seq_volume[slot]);
    lv_label_set_text_fmt(lbl_seq_volume[slot], "SEQ %d", sequencerVolume);

    create_info_chip(status_row, RED808_ACCENT2, &lbl_pad_volume[slot]);
    lv_label_set_text_fmt(lbl_pad_volume[slot], "PAD %d", livePadsVolume);

    create_info_chip(status_row, RED808_WARNING, &lbl_bpm[slot]);
    lv_label_set_text_fmt(lbl_bpm[slot], "BPM %d", currentBPM);

    create_info_chip(status_row, RED808_INFO, &lbl_pattern[slot]);
    lv_label_set_text_fmt(lbl_pattern[slot], "PTN %d", currentPattern + 1);

    create_info_chip(status_row, isPlaying ? RED808_SUCCESS : RED808_ERROR, &lbl_play[slot]);
    lv_label_set_text(lbl_play[slot], get_play_state_text(isPlaying));

    create_info_chip(status_row, wifiConnected ? RED808_SUCCESS : RED808_TEXT_DIM, &lbl_wifi[slot]);
    lv_label_set_text(lbl_wifi[slot], wifiConnected ? LV_SYMBOL_WIFI " LINK" : LV_SYMBOL_CLOSE " OFF");
}

void ui_update_header() {
    static int prev_bpm = -1;
    static int prev_pattern = -1;
    static int prev_playing = -1;
    static int prev_wifi = -1;

    // Only update the active screen's header slot — avoids dirtying hidden screens
    uint8_t slot = static_cast<uint8_t>(currentScreen);
    if (slot >= HEADER_SLOT_COUNT) return;

    if (currentBPM != prev_bpm) {
        prev_bpm = currentBPM;
        if (lbl_bpm[slot]) {
            lv_label_set_text_fmt(lbl_bpm[slot], "BPM: %d", currentBPM);
        }
    }
    if (currentPattern != prev_pattern) {
        prev_pattern = currentPattern;
        if (lbl_pattern[slot]) {
            lv_label_set_text_fmt(lbl_pattern[slot], "PTN: %d", currentPattern + 1);
        }
    }
    if ((int)isPlaying != prev_playing) {
        prev_playing = (int)isPlaying;
        if (lbl_play[slot]) {
            lv_label_set_text(lbl_play[slot], get_play_state_text(isPlaying));
            lv_obj_set_style_text_color(lbl_play[slot], RED808_TEXT, 0);
        }
    }
    if ((int)wifiConnected != prev_wifi) {
        prev_wifi = (int)wifiConnected;
        if (lbl_wifi[slot]) {
            lv_label_set_text(lbl_wifi[slot], wifiConnected ? LV_SYMBOL_WIFI " LINK" : LV_SYMBOL_CLOSE " OFF");
            lv_obj_set_style_text_color(lbl_wifi[slot], RED808_TEXT, 0);
        }
    }
    static int prev_seq_volume = -1;
    static int prev_pad_volume = -1;
    if (sequencerVolume != prev_seq_volume) {
        prev_seq_volume = sequencerVolume;
        if (lbl_seq_volume[slot]) {
            lv_label_set_text_fmt(lbl_seq_volume[slot], "SEQ %d", sequencerVolume);
        }
    }
    if (livePadsVolume != prev_pad_volume) {
        prev_pad_volume = livePadsVolume;
        if (lbl_pad_volume[slot]) {
            lv_label_set_text_fmt(lbl_pad_volume[slot], "PAD %d", livePadsVolume);
        }
    }
}

// ============================================================================
// MENU SCREEN - 6 big touch buttons
// ============================================================================
static void menu_btn_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0: nav_to(SCREEN_LIVE, scr_live); break;
        case 1: nav_to(SCREEN_SEQUENCER, scr_sequencer); break;
        case 2: nav_to(SCREEN_VOLUMES, scr_volumes); break;
        case 3: nav_to(SCREEN_FILTERS, scr_filters); break;
        case 4: nav_to(SCREEN_PATTERNS, scr_patterns); break;
        case 5: nav_to(SCREEN_SPECTRUM, scr_spectrum); break;
        case 6: nav_to(SCREEN_PERFORMANCE, scr_performance); break;
        case 7: nav_to(SCREEN_SETTINGS, scr_settings); break;
        case 8: nav_to(SCREEN_DIAGNOSTICS, scr_diagnostics); break;
    }
}

void ui_create_menu_screen() {
    scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_menu, RED808_BG, 0);

    ui_create_header(scr_menu);

    // 3x3 grid of menu buttons
    static const char* menu_names[] = {
        LV_SYMBOL_AUDIO "  LIVE PADS",
        LV_SYMBOL_LIST "  SEQUENCER",
        LV_SYMBOL_VOLUME_MAX "  VOLUMES",
        LV_SYMBOL_SETTINGS "  FILTERS FX",
        LV_SYMBOL_DRIVE "  PATTERNS",
        LV_SYMBOL_SHUFFLE "  SPECTRUM",
        LV_SYMBOL_CHARGE "  PERFORMANCE",
        LV_SYMBOL_HOME "  SETTINGS",
        LV_SYMBOL_EYE_OPEN "  DIAGNOSTICS"
    };
    const lv_color_t menu_colors[] = {
        RED808_ACCENT, RED808_INFO, RED808_SUCCESS,
        RED808_WARNING, RED808_CYAN, lv_color_hex(0xFF6B35),
        lv_color_hex(0xFF00AA), lv_color_hex(0x888888), RED808_TEXT_DIM
    };

    int x_start = 28, y_start = 80;
    int btn_w = 300, btn_h = 152, gap = 24;

    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int row = i / 3;

        lv_obj_t* btn = lv_btn_create(scr_menu);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x_start + col * (btn_w + gap), y_start + row * (btn_h + gap));
        apply_stable_button_style(btn, RED808_SURFACE, menu_colors[i]);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, 18, 0);
        lv_obj_set_style_pad_left(btn, 18, 0);
        lv_obj_set_style_pad_right(btn, 18, 0);
        lv_obj_set_style_pad_top(btn, 16, 0);
        lv_obj_set_style_pad_bottom(btn, 16, 0);
        lv_obj_add_event_cb(btn, menu_btn_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menu_names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, RED808_TEXT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

// ============================================================================
// LIVE PADS SCREEN - 4x4 grid of pads
// ============================================================================
int ui_live_pad_hit_test(int x, int y) {
    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (x < live_pad_x[pad] || y < live_pad_y[pad]) continue;
        if (x >= (live_pad_x[pad] + live_pad_w[pad])) continue;
        if (y >= (live_pad_y[pad] + live_pad_h[pad])) continue;
        return pad;
    }
    return -1;
}

void ui_create_live_screen() {
    scr_live = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_live, RED808_BG, 0);
    ui_create_header(scr_live);

    int x_start = (1024 - LIVE_PAD_COLS * LIVE_PAD_SIZE - 3 * LIVE_PAD_GAP) / 2;
    int row_h = (600 - LIVE_PAD_AREA_TOP - 18) / 4;

    for (int i = 0; i < 16; i++) {
        int col = i % 4;
        int row = i / 4;
        lv_coord_t x = x_start + col * (LIVE_PAD_SIZE + LIVE_PAD_GAP);
        lv_coord_t y = LIVE_PAD_AREA_TOP + row * row_h;
        lv_coord_t h = row_h - LIVE_PAD_GAP;

        lv_obj_t* pad = lv_obj_create(scr_live);
        lv_obj_set_size(pad, LIVE_PAD_SIZE, h);
        lv_obj_set_pos(pad, x, y);
        lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);

        // Dark card base with colored left accent bar
        lv_obj_set_style_bg_color(pad, RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pad, 12, 0);
        lv_obj_set_style_border_width(pad, 0, 0);
        lv_obj_set_style_shadow_color(pad, lv_color_black(), 0);
        lv_obj_set_style_shadow_width(pad, 20, 0);
        lv_obj_set_style_shadow_opa(pad, LV_OPA_40, 0);
        lv_obj_set_style_shadow_ofs_y(pad, 4, 0);
        lv_obj_set_style_pad_all(pad, 0, 0);

        // Colored accent bar on left edge
        lv_obj_t* accent = lv_obj_create(pad);
        lv_obj_set_size(accent, 6, h - 16);
        lv_obj_set_pos(accent, 8, 8);
        lv_obj_set_style_bg_color(accent, inst_colors[i], 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(accent, 3, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

        // Track name — large, left-aligned after accent
        lv_obj_t* lbl = lv_label_create(pad);
        lv_label_set_text(lbl, trackNames[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl, inst_colors[i], 0);
        lv_obj_set_pos(lbl, 22, 10);
        live_pad_names[i] = lbl;

        // Instrument description — smaller, below name
        lv_obj_t* desc = lv_label_create(pad);
        lv_label_set_text(desc, instrumentNames[i]);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(desc, RED808_TEXT_DIM, 0);
        lv_obj_set_pos(desc, 22, h - 28);
        live_pad_desc[i] = desc;

        // Bottom colored glow line
        lv_obj_t* glow = lv_obj_create(pad);
        lv_obj_set_size(glow, LIVE_PAD_SIZE - 24, 3);
        lv_obj_set_pos(glow, 12, h - 8);
        lv_obj_set_style_bg_color(glow, inst_colors[i], 0);
        lv_obj_set_style_bg_opa(glow, LV_OPA_40, 0);
        lv_obj_set_style_radius(glow, 1, 0);
        lv_obj_set_style_border_width(glow, 0, 0);
        lv_obj_clear_flag(glow, LV_OBJ_FLAG_SCROLLABLE);

        live_pads[i] = pad;
        live_pad_x[i] = x;
        live_pad_y[i] = y;
        live_pad_w[i] = LIVE_PAD_SIZE;
        live_pad_h[i] = h;
    }
}

void ui_update_live_pads() {
    static uint32_t prev_state_mask = 0xFFFFFFFFUL;
    uint32_t state_mask = 0;

    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        bool active = livePadPressed[pad];
        if (pad < BYTEBUTTON_BUTTONS && byteButtonLivePressed[pad]) {
            active = true;
        }
        if (active) {
            state_mask |= (1UL << pad);
        }
    }

    if (state_mask == prev_state_mask) {
        return;
    }

    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        bool active = (state_mask & (1UL << pad)) != 0;
        bool prev_active = (prev_state_mask & (1UL << pad)) != 0;
        if (active == prev_active || !live_pads[pad]) {
            continue;
        }

        lv_obj_set_style_border_color(live_pads[pad], active ? inst_colors[pad] : lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(live_pads[pad], active ? 2 : 0, 0);
        lv_obj_set_style_bg_color(live_pads[pad], active ? inst_colors[pad] : RED808_SURFACE, 0);
        lv_obj_set_style_shadow_width(live_pads[pad], active ? 30 : 20, 0);
        lv_obj_set_style_shadow_color(live_pads[pad], active ? inst_colors[pad] : lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(live_pads[pad], active ? LV_OPA_60 : LV_OPA_40, 0);
        if (live_pad_names[pad]) {
            lv_obj_set_style_text_color(live_pad_names[pad], active ? lv_color_white() : inst_colors[pad], 0);
        }
        if (live_pad_desc[pad]) {
            lv_obj_set_style_text_color(live_pad_desc[pad], active ? lv_color_white() : RED808_TEXT_DIM, 0);
        }
    }

    prev_state_mask = state_mask;
}

// ============================================================================
// SEQUENCER SCREEN - 16x16 step grid
// ============================================================================
static void seq_step_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int track = idx / Config::MAX_STEPS;
    int step = idx % Config::MAX_STEPS;
    patterns[currentPattern].steps[track][step] = !patterns[currentPattern].steps[track][step];
    bool active = patterns[currentPattern].steps[track][step];
    lv_obj_set_style_bg_color(lv_event_get_target(e),
        active ? inst_colors[track] : RED808_SURFACE, 0);
}

void ui_create_sequencer_screen() {
    scr_sequencer = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sequencer, RED808_BG, 0);
    ui_create_header(scr_sequencer);

    lv_obj_t* shell = create_section_shell(scr_sequencer, 16, 78, 992, 506);

    int grid_x = 80, grid_y = 55;
    int cell_w = 56, cell_h = 30, gap = 2;

    // Track labels
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        lv_obj_t* lbl = lv_label_create(scr_sequencer);
        lv_label_set_text(lbl, trackNames[t]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, inst_colors[t], 0);
        lv_obj_set_pos(lbl, 8, grid_y + t * (cell_h + gap) + 6);
        seq_track_labels[t] = lbl;
    }

    // Step grid
    for (int s = 0; s < Config::MAX_STEPS; s++) {
        lv_obj_t* column = lv_obj_create(scr_sequencer);
        lv_obj_set_size(column, cell_w, Config::MAX_TRACKS * (cell_h + gap) - gap);
        lv_obj_set_pos(column, grid_x + s * (cell_w + gap), grid_y);
        lv_obj_set_style_bg_color(column, RED808_WARNING, 0);
        lv_obj_set_style_bg_opa(column, LV_OPA_0, 0);
        lv_obj_set_style_border_width(column, 1, 0);
        lv_obj_set_style_border_color(column, RED808_WARNING, 0);
        lv_obj_set_style_border_opa(column, LV_OPA_0, 0);
        lv_obj_set_style_radius(column, 3, 0);
        lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(column, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_move_background(column);
        seq_column_highlights[s] = column;
    }

    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            lv_obj_t* cell = lv_btn_create(scr_sequencer);
            lv_obj_set_size(cell, cell_w, cell_h);
            lv_obj_set_pos(cell, grid_x + s * (cell_w + gap), grid_y + t * (cell_h + gap));
            bool active = patterns[currentPattern].steps[t][s];
            apply_stable_button_style(cell, active ? inst_colors[t] : RED808_SURFACE, RED808_BORDER);
            lv_obj_set_style_radius(cell, 3, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            int idx = t * Config::MAX_STEPS + s;
            lv_obj_add_event_cb(cell, seq_step_cb, LV_EVENT_PRESSED, (void*)(intptr_t)idx);
            seq_grid[t][s] = cell;
        }
    }

    // Step indicator
    lbl_step_indicator = lv_label_create(scr_sequencer);
    lv_label_set_text(lbl_step_indicator, "Step: --");
    lv_obj_set_style_text_font(lbl_step_indicator, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_step_indicator, RED808_WARNING, 0);
    lv_obj_set_pos(lbl_step_indicator, 8, 580);
}

void ui_update_sequencer() {
    if (!scr_sequencer) return;

    static int prev_step = -1;
    static int prev_column = -2;
    static uint8_t prev_grid_state[Config::MAX_TRACKS][Config::MAX_STEPS];

    // Update step highlight label only when step changes
    if (lbl_step_indicator && currentStep != prev_step) {
        lv_label_set_text_fmt(lbl_step_indicator, "Step: %02d / %02d", currentStep + 1, Config::MAX_STEPS);
    }

    int active_column = isPlaying ? currentStep : -1;
    if (active_column != prev_column) {
        if (prev_column >= 0 && prev_column < Config::MAX_STEPS && seq_column_highlights[prev_column]) {
            lv_obj_set_style_bg_opa(seq_column_highlights[prev_column], LV_OPA_0, 0);
            lv_obj_set_style_border_opa(seq_column_highlights[prev_column], LV_OPA_0, 0);
        }
        if (active_column >= 0 && active_column < Config::MAX_STEPS && seq_column_highlights[active_column]) {
            lv_obj_set_style_bg_opa(seq_column_highlights[active_column], LV_OPA_50, 0);
            lv_obj_set_style_border_opa(seq_column_highlights[active_column], LV_OPA_COVER, 0);
        }
        prev_column = active_column;
    }

    // Update grid state - only changed cells
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            if (seq_grid[t][s]) {
                bool active = patterns[currentPattern].steps[t][s];
                bool isCurrentStep = (s == currentStep && isPlaying);
                uint8_t state = (isCurrentStep ? 2 : 0) | (active ? 1 : 0);

                if (state != prev_grid_state[t][s]) {
                    prev_grid_state[t][s] = state;
                    if (isCurrentStep) {
                        lv_obj_set_style_bg_color(seq_grid[t][s],
                            active ? lv_color_white() : lv_color_hex(0x3A3A00), 0);
                        lv_obj_set_style_bg_opa(seq_grid[t][s], LV_OPA_COVER, 0);
                        lv_obj_set_style_border_width(seq_grid[t][s], 2, 0);
                        lv_obj_set_style_border_color(seq_grid[t][s], RED808_WARNING, 0);
                    } else {
                        lv_obj_set_style_bg_color(seq_grid[t][s],
                            active ? inst_colors[t] : RED808_SURFACE, 0);
                        lv_obj_set_style_bg_opa(seq_grid[t][s], LV_OPA_COVER, 0);
                        lv_obj_set_style_border_width(seq_grid[t][s], 0, 0);
                        lv_obj_set_style_border_color(seq_grid[t][s], RED808_BORDER, 0);
                    }
                }
            }
        }
    }
    prev_step = currentStep;
}

// ============================================================================
// VOLUMES SCREEN - 16 sliders
// ============================================================================
static void vol_slider_cb(lv_event_t* e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* slider = lv_event_get_target(e);
    trackVolumes[track] = lv_slider_get_value(slider);
    if (vol_labels[track]) {
        lv_label_set_text_fmt(vol_labels[track], "%d", trackVolumes[track]);
    }
}

static void ui_apply_volume_track_style(int track) {
    if (track < 0 || track >= Config::MAX_TRACKS) return;

    lv_color_t indicator = trackMuted[track] ? RED808_ERROR : inst_colors[track];
    lv_color_t text_color = trackMuted[track] ? RED808_ERROR : inst_colors[track];
    lv_opa_t slider_opa = trackMuted[track] ? LV_OPA_40 : LV_OPA_COVER;
    lv_opa_t knob_opa = trackMuted[track] ? LV_OPA_50 : LV_OPA_COVER;

    if (vol_sliders[track]) {
        lv_obj_set_style_bg_opa(vol_sliders[track], slider_opa, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(vol_sliders[track], knob_opa, LV_PART_KNOB);
        lv_obj_set_style_bg_color(vol_sliders[track], indicator, LV_PART_INDICATOR);
    }
    if (vol_name_labels[track]) {
        lv_obj_set_style_text_color(vol_name_labels[track], text_color, 0);
    }
    if (vol_labels[track]) {
        lv_obj_set_style_text_color(vol_labels[track], trackMuted[track] ? RED808_ERROR : RED808_TEXT_DIM, 0);
    }
}

void ui_create_volumes_screen() {
    scr_volumes = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_volumes, RED808_BG, 0);
    ui_create_header(scr_volumes);

    // Fader channel strips — professional mixer style
    int strip_w = 56;
    int gap = (1024 - 16 * strip_w) / 17;  // equal margins
    int slider_h = 380;
    int y_name = 58;
    int y_slider = 82;
    int y_value = y_slider + slider_h + 8;

    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        int x = gap + i * (strip_w + gap);
        int cx = x + strip_w / 2;

        // Track strip background (subtle channel strip)
        lv_obj_t* strip = lv_obj_create(scr_volumes);
        lv_obj_set_size(strip, strip_w, 530);
        lv_obj_set_pos(strip, x, 52);
        lv_obj_set_style_bg_color(strip, RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(strip, LV_OPA_30, 0);
        lv_obj_set_style_radius(strip, 6, 0);
        lv_obj_set_style_border_width(strip, 0, 0);
        lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

        // Track name (top, colored)
        lv_obj_t* name = lv_label_create(scr_volumes);
        lv_label_set_text(name, trackNames[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, inst_colors[i], 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(name, x, y_name);
        lv_obj_set_width(name, strip_w);
        vol_name_labels[i] = name;

        // Slider — thin fader (10px wide, tall)
        lv_obj_t* slider = lv_slider_create(scr_volumes);
        lv_obj_set_size(slider, 10, slider_h);
        lv_obj_set_pos(slider, cx - 5, y_slider);
        lv_slider_set_range(slider, 0, Config::MAX_VOLUME);
        lv_slider_set_value(slider, trackVolumes[i], LV_ANIM_OFF);

        // Track (background rail) — dark thin line
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 5, LV_PART_MAIN);

        // Indicator (filled portion) — track color with glow
        lv_obj_set_style_bg_color(slider, inst_colors[i], LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, 5, LV_PART_INDICATOR);

        // Knob — small white circle
        lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_shadow_color(slider, inst_colors[i], LV_PART_KNOB);
        lv_obj_set_style_shadow_width(slider, 12, LV_PART_KNOB);
        lv_obj_set_style_shadow_opa(slider, LV_OPA_60, LV_PART_KNOB);
        lv_obj_set_style_border_color(slider, inst_colors[i], LV_PART_KNOB);
        lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);

        lv_obj_add_event_cb(slider, vol_slider_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        vol_sliders[i] = slider;

        // Color indicator bar at bottom of strip
        lv_obj_t* color_bar = lv_obj_create(scr_volumes);
        lv_obj_set_size(color_bar, strip_w - 8, 4);
        lv_obj_set_pos(color_bar, x + 4, y_slider + slider_h + 2);
        lv_obj_set_style_bg_color(color_bar, inst_colors[i], 0);
        lv_obj_set_style_bg_opa(color_bar, LV_OPA_80, 0);
        lv_obj_set_style_radius(color_bar, 2, 0);
        lv_obj_set_style_border_width(color_bar, 0, 0);
        lv_obj_clear_flag(color_bar, LV_OBJ_FLAG_SCROLLABLE);

        // Value label (bottom, large)
        lv_obj_t* val = lv_label_create(scr_volumes);
        lv_label_set_text_fmt(val, "%d", trackVolumes[i]);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val, RED808_TEXT, 0);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(val, x, y_value + 4);
        lv_obj_set_width(val, strip_w);
        vol_labels[i] = val;

        ui_apply_volume_track_style(i);
    }
}

void ui_update_volumes() {
    static int prev_vol[Config::MAX_TRACKS];
    static bool prev_mute[Config::MAX_TRACKS];
    static bool initialized = false;
    static uint32_t last_slider_refresh = 0;

    if (!initialized) {
        for (int i = 0; i < Config::MAX_TRACKS; i++) {
            prev_vol[i] = -1;
            prev_mute[i] = !trackMuted[i];
        }
        initialized = true;
    }

    bool allow_slider_refresh = (lv_tick_get() - last_slider_refresh) >= 40;
    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        bool vol_changed = trackVolumes[i] != prev_vol[i];
        bool mute_changed = trackMuted[i] != prev_mute[i];

        if (mute_changed) {
            prev_mute[i] = trackMuted[i];
            ui_apply_volume_track_style(i);
        }

        if (vol_changed && allow_slider_refresh) {
            prev_vol[i] = trackVolumes[i];
            if (vol_sliders[i]) {
                lv_slider_set_value(vol_sliders[i], trackVolumes[i], LV_ANIM_OFF);
            }
            if (vol_labels[i]) {
                lv_label_set_text_fmt(vol_labels[i], "%d", trackVolumes[i]);
            }
        }
    }
    if (allow_slider_refresh) {
        last_slider_refresh = lv_tick_get();
    }
}

void ui_update_menu_status() {
    // Menu has no status cards — header shows all info
}

// ============================================================================
// FILTERS FX SCREEN - 3 arc gauges for Delay/Flanger/Compressor
// ============================================================================
void ui_create_filters_screen() {
    scr_filters = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_filters, RED808_BG, 0);
    ui_create_header(scr_filters);

    lv_obj_t* shell = create_section_shell(scr_filters, 18, 78, 988, 500);

    lv_obj_t* title = lv_label_create(scr_filters);
    lv_label_set_text(title, "FILTERS FX");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    // Target indicator
    filter_target_label = lv_label_create(scr_filters);
    lv_label_set_text(filter_target_label, "TARGET: MASTER");
    lv_obj_set_style_text_font(filter_target_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(filter_target_label, RED808_WARNING, 0);
    lv_obj_set_pos(filter_target_label, 20, 52);

    static const char* fx_names[] = {"DELAY", "FLANGER", "COMPRESSOR"};
    static const lv_color_t fx_colors[] = {
        lv_color_hex(0x58A6FF), // Blue
        lv_color_hex(0x39D2C0), // Cyan
        lv_color_hex(0xD29922), // Amber
    };

    int arc_size = 250;
    int gap = 30;
    int x_start = (1024 - 3 * arc_size - 2 * gap) / 2;

    for (int i = 0; i < 3; i++) {
        int x = x_start + i * (arc_size + gap) + arc_size / 2;
        int y = 280;

        // Arc gauge
        lv_obj_t* arc = lv_arc_create(scr_filters);
        lv_obj_set_size(arc, arc_size, arc_size);
        lv_obj_align(arc, LV_ALIGN_TOP_MID, x - 512, y - arc_size / 2);
        lv_arc_set_range(arc, 0, 127);
        lv_arc_set_value(arc, 0);
        lv_arc_set_bg_angles(arc, 135, 45);
        lv_obj_set_style_arc_color(arc, RED808_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, fx_colors[i], LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, 18, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 18, LV_PART_INDICATOR);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE); // Read-only, DFRobot controls
        filter_arcs[i] = arc;

        // FX name
        lv_obj_t* name = lv_label_create(scr_filters);
        lv_label_set_text(name, fx_names[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(name, fx_colors[i], 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, x - 512, y + arc_size / 2 + 8);
        filter_labels[i] = name;

        // Value label (inside arc)
        lv_obj_t* val = lv_label_create(arc);
        lv_label_set_text(val, "0%");
        lv_obj_set_style_text_font(val, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(val, RED808_TEXT, 0);
        lv_obj_center(val);
        filter_value_labels[i] = val;
    }

    // Footer - control info
    lv_obj_t* info = lv_label_create(scr_filters);
    lv_label_set_text(info, "ROTARY: Amount  |  ROTARY BTN: Cycle FX  |  ENCODER: Track select  |  Touch: ON/OFF");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, RED808_TEXT_DIM, 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void ui_update_filters() {
    static uint8_t prev_amounts[3] = {0xFF, 0xFF, 0xFF};
    static int prev_selectedFX = -1;
    static int prev_target = -999;
    TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
    uint8_t amounts[] = { f.delayAmount, f.flangerAmount, f.compAmount };

    for (int i = 0; i < 3; i++) {
        bool amountChanged = (amounts[i] != prev_amounts[i]);
        bool selChanged = (filterSelectedFX != prev_selectedFX);
        if (filter_arcs[i] && (amountChanged || selChanged)) {
            lv_arc_set_value(filter_arcs[i], amounts[i]);
            bool isSelected = (i == filterSelectedFX);
            lv_obj_set_style_arc_width(filter_arcs[i], isSelected ? 24 : 18, LV_PART_INDICATOR);
        }
        if (filter_value_labels[i] && amountChanged) {
            int pct = amounts[i] * 100 / 127;
            lv_label_set_text_fmt(filter_value_labels[i], "%d%%", pct);
        }
        prev_amounts[i] = amounts[i];
    }
    if (filter_target_label && filterSelectedTrack != prev_target) {
        prev_target = filterSelectedTrack;
        if (filterSelectedTrack == -1) {
            lv_label_set_text(filter_target_label, "TARGET: MASTER");
        } else {
            lv_label_set_text_fmt(filter_target_label, "TARGET: %s", trackNames[filterSelectedTrack]);
        }
    }
    prev_selectedFX = filterSelectedFX;
}

// ============================================================================
// SETTINGS SCREEN
// ============================================================================
void ui_create_settings_screen() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, RED808_BG, 0);
    ui_create_header(scr_settings);

    lv_obj_t* title = lv_label_create(scr_settings);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

    // Kit selector
    lv_obj_t* kit_lbl = lv_label_create(scr_settings);
    lv_label_set_text(kit_lbl, "DRUM KIT:");
    lv_obj_set_style_text_font(kit_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(kit_lbl, RED808_TEXT, 0);
    lv_obj_set_pos(kit_lbl, 100, 160);

    lv_obj_t* dd = lv_dropdown_create(scr_settings);
    lv_dropdown_set_options(dd, "808 CLASSIC\n808 BRIGHT\n808 DRY");
    lv_obj_set_size(dd, 300, 50);
    lv_obj_set_pos(dd, 300, 150);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_18, 0);

    // WiFi info
    lv_obj_t* wifi_info = lv_label_create(scr_settings);
    lv_label_set_text_fmt(wifi_info, "WiFi SSID: %s\nUDP Port: %d",
                          WiFiConfig::SSID, WiFiConfig::UDP_PORT);
    lv_obj_set_style_text_font(wifi_info, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(wifi_info, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(wifi_info, 100, 260);

    // ── Theme selector ──
    lv_obj_t* theme_lbl = lv_label_create(scr_settings);
    lv_label_set_text(theme_lbl, "VISUAL THEME:");
    lv_obj_set_style_text_font(theme_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(theme_lbl, RED808_TEXT, 0);
    lv_obj_set_pos(theme_lbl, 100, 370);

    static const uint32_t btn_colors[THEME_COUNT] = {0xFF4444, 0x4A9EFF, 0x39FF14, 0xFF6B35, 0xFF00AA, 0x999999};
    for (int i = 0; i < THEME_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(scr_settings);
        lv_obj_set_size(btn, 148, 55);
        lv_obj_set_pos(btn, 20 + i * 166, 420);
        lv_obj_set_style_bg_color(btn, lv_color_hex(btn_colors[i]), 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
        lv_obj_set_style_shadow_width(btn, 8, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(btn_colors[i]), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, theme_presets[i].name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            ui_theme_apply((VisualTheme)idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

// ============================================================================
// DIAGNOSTICS SCREEN
// ============================================================================
#define DIAG_ROWS 11
static lv_obj_t* diag_labels[DIAG_ROWS];
static lv_obj_t* diag_values[DIAG_ROWS];

void ui_create_diagnostics_screen() {
    scr_diagnostics = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_diagnostics, RED808_BG, 0);
    ui_create_header(scr_diagnostics);

    lv_obj_t* title = lv_label_create(scr_diagnostics);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN " DIAGNOSTICS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    static const char* row_names[] = {
        "WiFi",  "UDP",  "Touch GT911",  "LCD",  "I2C Hub",
        "M5 Encoder #1", "M5 Encoder #2", "DFRobot #1", "DFRobot #2", "M5 ByteButton", "Memory"
    };

    int y = 100;
    for (int i = 0; i < DIAG_ROWS; i++) {
        diag_labels[i] = lv_label_create(scr_diagnostics);
        lv_label_set_text(diag_labels[i], row_names[i]);
        lv_obj_set_style_text_font(diag_labels[i], &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(diag_labels[i], RED808_TEXT, 0);
        lv_obj_set_pos(diag_labels[i], 60, y);

        diag_values[i] = lv_label_create(scr_diagnostics);
        lv_label_set_text(diag_values[i], "---");
        lv_obj_set_style_text_font(diag_values[i], &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(diag_values[i], RED808_TEXT_DIM, 0);
        lv_obj_set_pos(diag_values[i], 320, y);

        y += 46;
    }
}

void ui_update_diagnostics() {
    if (!scr_diagnostics) return;

    static bool prev_vals[DIAG_ROWS + 1] = {};
    static uint32_t prev_heap = 0;
    static uint32_t last_heap_update_ms = 0;

    struct { bool val; const char* ok; const char* fail; } rows[] = {
        { diagInfo.wifiOk,       "Connected",  "Disconnected" },
        { diagInfo.udpConnected, "Active",     "Inactive" },
        { diagInfo.touchOk,      "OK (0x5D)",  "NOT FOUND" },
        { diagInfo.lcdOk,        "OK 1024x600","ERROR" },
        { diagInfo.i2cHubOk,     "PCA9548A OK","NOT FOUND" },
        { diagInfo.m5encoder1Ok, "OK",         "Not detected" },
        { diagInfo.m5encoder2Ok, "OK",         "Not detected" },
        { diagInfo.dfrobot1Ok,   "OK",         "Not detected" },
        { diagInfo.dfrobot2Ok,   "OK",         "Not detected" },
        { diagInfo.byteButtonOk, "OK",         "Not detected" },
    };

    for (int i = 0; i < 10; i++) {
        if (rows[i].val != prev_vals[i]) {
            prev_vals[i] = rows[i].val;
            lv_label_set_text(diag_values[i], rows[i].val ? rows[i].ok : rows[i].fail);
            lv_obj_set_style_text_color(diag_values[i], rows[i].val ? RED808_SUCCESS : RED808_ERROR, 0);
        }
    }

    // Actualizar heap solo cada 2s para no generar dirty areas continuas (getFreeHeap() cambia cada 16ms)
    uint32_t now_ms = lv_tick_get();
    if ((now_ms - last_heap_update_ms) >= 2000) {
        last_heap_update_ms = now_ms;
        uint32_t heap = ESP.getFreeHeap() / 1024;
        uint32_t psram = ESP.getFreePsram() / 1024;
        if (heap != prev_heap) {
            prev_heap = heap;
            lv_label_set_text_fmt(diag_values[10], "Heap: %luK  PSRAM: %luK", heap, psram);
            lv_obj_set_style_text_color(diag_values[10], RED808_INFO, 0);
        }
    }
}

// ============================================================================
// PATTERNS SCREEN - 6 patterns, click to select
// ============================================================================
static constexpr int VISIBLE_PATTERNS = 6;
static lv_obj_t* pattern_btns[VISIBLE_PATTERNS] = {};
static lv_obj_t* pattern_labels[VISIBLE_PATTERNS] = {};

static void pattern_select_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= VISIBLE_PATTERNS) return;
    currentPattern = idx;

    // Send selectPattern to master
    JsonDocument doc;
    doc["cmd"] = "selectPattern";
    doc["index"] = idx;
    sendUDPCommand(doc);

    // Update button visuals immediately
    for (int i = 0; i < VISIBLE_PATTERNS; i++) {
        if (!pattern_btns[i]) continue;
        bool active = (i == currentPattern);
        lv_obj_set_style_bg_color(pattern_btns[i], active ? RED808_ACCENT : RED808_SURFACE, 0);
        lv_obj_set_style_border_color(pattern_btns[i], active ? lv_color_white() : RED808_BORDER, 0);
        lv_obj_set_style_border_width(pattern_btns[i], active ? 3 : 1, 0);
        if (pattern_labels[i]) {
            lv_obj_set_style_text_color(pattern_labels[i], active ? lv_color_white() : RED808_TEXT, 0);
        }
    }
}

void ui_create_patterns_screen() {
    scr_patterns = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_patterns, RED808_BG, 0);
    ui_create_header(scr_patterns);

    // 2 rows x 3 cols of large pattern buttons
    int cols = 3, rows = 2;
    int btn_w = 280, btn_h = 210;
    int gap = 24;
    int x_start = (1024 - cols * btn_w - (cols - 1) * gap) / 2;
    int y_start = 80;

    for (int i = 0; i < VISIBLE_PATTERNS; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = x_start + col * (btn_w + gap);
        int y = y_start + row * (btn_h + gap);

        bool active = (i == currentPattern);

        lv_obj_t* btn = lv_btn_create(scr_patterns);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x, y);
        apply_stable_button_style(btn, active ? RED808_ACCENT : RED808_SURFACE, active ? lv_color_white() : RED808_BORDER);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_border_width(btn, active ? 3 : 1, 0);
        lv_obj_add_event_cb(btn, pattern_select_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);
        pattern_btns[i] = btn;

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "PATTERN\n%d", i + 1);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl, active ? lv_color_white() : RED808_TEXT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
        pattern_labels[i] = lbl;
    }
}

void ui_update_patterns() {
    for (int i = 0; i < VISIBLE_PATTERNS; i++) {
        if (!pattern_btns[i]) continue;
        bool active = (i == currentPattern);
        lv_obj_set_style_bg_color(pattern_btns[i], active ? RED808_ACCENT : RED808_SURFACE, 0);
        lv_obj_set_style_border_color(pattern_btns[i], active ? lv_color_white() : RED808_BORDER, 0);
        lv_obj_set_style_border_width(pattern_btns[i], active ? 3 : 1, 0);
        if (pattern_labels[i]) {
            lv_obj_set_style_text_color(pattern_labels[i], active ? lv_color_white() : RED808_TEXT, 0);
        }
    }
}

// ============================================================================
// SPECTRUM SCREEN - Animated spectrum analyzer bars
// ============================================================================
static lv_obj_t* spectrum_bars[Config::MAX_TRACKS] = {};
static lv_obj_t* spectrum_peak[Config::MAX_TRACKS] = {};
static int spectrum_heights[Config::MAX_TRACKS] = {};
static int spectrum_peaks[Config::MAX_TRACKS] = {};
static uint32_t spectrum_last_update = 0;

void ui_create_spectrum_screen() {
    scr_spectrum = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_spectrum, RED808_BG, 0);
    ui_create_header(scr_spectrum);

    int bar_w = 48;
    int gap = (1024 - 16 * bar_w) / 17;
    int bar_max_h = 480;
    int y_bottom = 570;

    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        int x = gap + t * (bar_w + gap);

        // Bar (grows upward from bottom)
        lv_obj_t* bar = lv_obj_create(scr_spectrum);
        lv_obj_set_size(bar, bar_w, 20);
        lv_obj_set_pos(bar, x, y_bottom - 20);
        lv_obj_set_style_bg_color(bar, inst_colors[t], 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        spectrum_bars[t] = bar;

        // Peak indicator (thin line)
        lv_obj_t* peak = lv_obj_create(scr_spectrum);
        lv_obj_set_size(peak, bar_w, 3);
        lv_obj_set_pos(peak, x, y_bottom - 24);
        lv_obj_set_style_bg_color(peak, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(peak, LV_OPA_90, 0);
        lv_obj_set_style_radius(peak, 1, 0);
        lv_obj_set_style_border_width(peak, 0, 0);
        lv_obj_clear_flag(peak, LV_OBJ_FLAG_SCROLLABLE);
        spectrum_peak[t] = peak;

        // Track name at bottom
        lv_obj_t* name = lv_label_create(scr_spectrum);
        lv_label_set_text(name, trackNames[t]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, inst_colors[t], 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(name, x, y_bottom + 4);
        lv_obj_set_width(name, bar_w);

        spectrum_heights[t] = 20;
        spectrum_peaks[t] = 24;
    }
}

void ui_update_spectrum() {
    int bar_max_h = 480;
    int y_bottom = 570;

    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        // Height based on track volume (proportional)
        int target_h = (trackVolumes[t] * bar_max_h) / Config::MAX_VOLUME;
        if (target_h < 4) target_h = 4;

        // Add some variation based on step for animation effect
        if (isPlaying) {
            int variation = ((currentStep + t * 3) % 7) * (target_h / 14);
            target_h = constrain(target_h + variation - target_h / 7, 4, bar_max_h);
        }

        // Smooth: rise fast, fall slow
        if (target_h > spectrum_heights[t]) {
            spectrum_heights[t] = target_h;  // instant rise
        } else {
            spectrum_heights[t] = spectrum_heights[t] - (spectrum_heights[t] - target_h) / 4 - 1;
            if (spectrum_heights[t] < 4) spectrum_heights[t] = 4;
        }

        int h = spectrum_heights[t];
        if (spectrum_bars[t]) {
            lv_obj_set_size(spectrum_bars[t], 48, h);
            lv_obj_set_pos(spectrum_bars[t], lv_obj_get_x(spectrum_bars[t]), y_bottom - h);
        }

        // Peak: falls slowly
        if (h > spectrum_peaks[t]) spectrum_peaks[t] = h + 4;
        else spectrum_peaks[t] = spectrum_peaks[t] > 5 ? spectrum_peaks[t] - 1 : 5;

        if (spectrum_peak[t]) {
            lv_obj_set_pos(spectrum_peak[t], lv_obj_get_x(spectrum_peak[t]), y_bottom - spectrum_peaks[t]);
        }
    }
}

// ============================================================================
// PERFORMANCE SCREEN - Mute groups + transport + pattern chain
// ============================================================================
static lv_obj_t* perf_mute_btns[Config::MAX_TRACKS] = {};
static lv_obj_t* perf_mute_labels[Config::MAX_TRACKS] = {};

static void perf_mute_cb(lv_event_t* e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    if (track < 0 || track >= Config::MAX_TRACKS) return;
    trackMuted[track] = !trackMuted[track];

    // Send mute command to master
    JsonDocument doc;
    doc["cmd"] = "mute";
    doc["track"] = track;
    doc["value"] = trackMuted[track];
    sendUDPCommand(doc);
}

void ui_create_performance_screen() {
    scr_performance = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_performance, RED808_BG, 0);
    ui_create_header(scr_performance);

    lv_obj_t* title = lv_label_create(scr_performance);
    lv_label_set_text(title, "PERFORMANCE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    // Mute grid: 4x4 buttons for 16 tracks
    lv_obj_t* mute_title = lv_label_create(scr_performance);
    lv_label_set_text(mute_title, "MUTE / UNMUTE TRACKS");
    lv_obj_set_style_text_font(mute_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mute_title, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(mute_title, 28, 82);

    int btn_w = 220, btn_h = 105, gap = 16;
    int x_start = (1024 - 4 * btn_w - 3 * gap) / 2;
    int y_start = 108;

    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        int col = i % 4;
        int row = i / 4;

        lv_obj_t* btn = lv_btn_create(scr_performance);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x_start + col * (btn_w + gap), y_start + row * (btn_h + gap));
        apply_stable_button_style(btn, trackMuted[i] ? RED808_SURFACE : inst_colors[i], RED808_BORDER);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_add_event_cb(btn, perf_mute_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);
        perf_mute_btns[i] = btn;

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%s\n%s", trackNames[i], trackMuted[i] ? "MUTED" : "ON");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, trackMuted[i] ? RED808_TEXT_DIM : RED808_BG, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
        perf_mute_labels[i] = lbl;
    }
}

static void ui_update_performance() {
    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        if (perf_mute_btns[i]) {
            lv_obj_set_style_bg_color(perf_mute_btns[i],
                trackMuted[i] ? RED808_SURFACE : inst_colors[i], 0);
        }
        if (perf_mute_labels[i]) {
            lv_label_set_text_fmt(perf_mute_labels[i], "%s\n%s",
                trackNames[i], trackMuted[i] ? "MUTED" : "ON");
            lv_obj_set_style_text_color(perf_mute_labels[i],
                trackMuted[i] ? RED808_TEXT_DIM : RED808_BG, 0);
        }
    }
}

// ============================================================================
// SAMPLES SCREEN - Sample browser per pad
// ============================================================================
static lv_obj_t* sample_pad_btns[Config::MAX_SAMPLES] = {};
static int selectedSamplePad = 0;

static const char* sampleFamilies[] = {
    "BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL",
    "MA", "CY", "HT", "LT", "MC", "MT", "HC", "LC"
};

static void sample_pad_select_cb(lv_event_t* e) {
    int pad = (int)(intptr_t)lv_event_get_user_data(e);
    if (pad < 0 || pad >= Config::MAX_SAMPLES) return;
    selectedSamplePad = pad;

    // Update visual selection
    for (int i = 0; i < Config::MAX_SAMPLES; i++) {
        if (sample_pad_btns[i]) {
            lv_obj_set_style_border_color(sample_pad_btns[i],
                (i == pad) ? lv_color_white() : RED808_BORDER, 0);
            lv_obj_set_style_border_width(sample_pad_btns[i],
                (i == pad) ? 3 : 1, 0);
        }
    }
}

void ui_create_samples_screen() {
    scr_samples = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_samples, RED808_BG, 0);
    ui_create_header(scr_samples);

    lv_obj_t* title = lv_label_create(scr_samples);
    lv_label_set_text(title, "SAMPLES");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t* hint = lv_label_create(scr_samples);
    lv_label_set_text(hint, "Selecciona pad, luego toca el encoder para cargar sample");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, RED808_TEXT_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 78);

    // 4x4 pad grid
    int btn_w = 220, btn_h = 105, gap = 16;
    int x_start = (1024 - 4 * btn_w - 3 * gap) / 2;
    int y_start = 108;

    for (int i = 0; i < Config::MAX_SAMPLES; i++) {
        int col = i % 4;
        int row = i / 4;

        lv_obj_t* btn = lv_btn_create(scr_samples);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x_start + col * (btn_w + gap), y_start + row * (btn_h + gap));
        apply_stable_button_style(btn, inst_colors[i], RED808_BORDER);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, (i == 0) ? 3 : 1, 0);
        lv_obj_set_style_border_color(btn, (i == 0) ? lv_color_white() : RED808_BORDER, 0);
        lv_obj_add_event_cb(btn, sample_pad_select_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);
        sample_pad_btns[i] = btn;

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%s\n%s", trackNames[i], sampleFamilies[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, RED808_BG, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}
