// =============================================================================
// ui_screens.cpp - Screen implementations with LVGL
// RED808 V6 BlueSlaveV2 - 1024x600 touch interface
// =============================================================================
#include "ui_screens.h"
#include "ui_theme.h"
#include "../../include/system_state.h"
#include "../../include/config.h"
#include <Esp.h>

// Screen objects
lv_obj_t* scr_menu = NULL;
lv_obj_t* scr_live = NULL;
lv_obj_t* scr_sequencer = NULL;
lv_obj_t* scr_volumes = NULL;
lv_obj_t* scr_filters = NULL;
lv_obj_t* scr_settings = NULL;
lv_obj_t* scr_diagnostics = NULL;
lv_obj_t* scr_patterns = NULL;

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
static constexpr int LIVE_PAD_AREA_TOP = 110;

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
        case 4: nav_to(SCREEN_SETTINGS, scr_settings); break;
        case 5: nav_to(SCREEN_DIAGNOSTICS, scr_diagnostics); break;
    }
}

void ui_create_menu_screen() {
    scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_menu, RED808_BG, 0);

    ui_create_header(scr_menu);

    lv_obj_t* hero = create_section_shell(scr_menu, 24, 80, 976, 92);

    lv_obj_t* title = lv_label_create(hero);
    lv_label_set_text(title, "RED808 SURFACE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* subtitle = lv_label_create(hero);
    lv_label_set_text(subtitle, "Control en vivo, secuenciacion y mezcla en una home compacta y clara.");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, RED808_TEXT_DIM, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, 46);

    lv_obj_t* status_row = lv_obj_create(scr_menu);
    lv_obj_set_pos(status_row, 24, 186);
    lv_obj_set_size(status_row, 976, 56);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_style_pad_all(status_row, 0, 0);
    lv_obj_set_style_pad_gap(status_row, 18, 0);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* state_card = create_section_shell(status_row, 0, 0, 302, 56);
    lv_obj_t* state_title = lv_label_create(state_card);
    lv_label_set_text(state_title, "MASTER");
    lv_obj_set_style_text_font(state_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(state_title, RED808_TEXT_DIM, 0);
    lv_obj_align(state_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_master = lv_label_create(state_card);
    lv_label_set_text(lbl_master, masterConnected ? "ONLINE" : "OFFLINE");
    lv_obj_set_style_text_font(lbl_master, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_master, masterConnected ? RED808_SUCCESS : RED808_ERROR, 0);
    lv_obj_align(lbl_master, LV_ALIGN_BOTTOM_LEFT, 0, 4);

    lv_obj_t* wifi_card = create_section_shell(status_row, 0, 0, 302, 56);
    lv_obj_t* wifi_title = lv_label_create(wifi_card);
    lv_label_set_text(wifi_title, "NETWORK");
    lv_obj_set_style_text_font(wifi_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wifi_title, RED808_TEXT_DIM, 0);
    lv_obj_align(wifi_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_menu_state = lv_label_create(wifi_card);
    lv_label_set_text(lbl_menu_state, wifiConnected ? "WiFi enlazado" : "WiFi desconectado");
    lv_obj_set_style_text_font(lbl_menu_state, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_menu_state, wifiConnected ? RED808_INFO : RED808_TEXT_DIM, 0);
    lv_obj_align(lbl_menu_state, LV_ALIGN_BOTTOM_LEFT, 0, 4);

    lv_obj_t* transport_card = create_section_shell(status_row, 0, 0, 302, 56);
    lv_obj_t* transport_title = lv_label_create(transport_card);
    lv_label_set_text(transport_title, "TRANSPORT");
    lv_obj_set_style_text_font(transport_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(transport_title, RED808_TEXT_DIM, 0);
    lv_obj_align(transport_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_menu_transport = lv_label_create(transport_card);
    lv_label_set_text_fmt(lbl_menu_transport, "%s  |  PTN %02d  |  %d BPM",
                          isPlaying ? "PLAY" : "PAUSE", currentPattern + 1, currentBPM);
    lv_obj_set_style_text_font(lbl_menu_transport, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_menu_transport, isPlaying ? RED808_SUCCESS : RED808_WARNING, 0);
    lv_obj_align(lbl_menu_transport, LV_ALIGN_BOTTOM_LEFT, 0, 4);

    // 3x2 grid of menu buttons
    static const char* menu_names[] = {
        LV_SYMBOL_AUDIO "  LIVE PADS",
        LV_SYMBOL_LIST "  SEQUENCER",
        LV_SYMBOL_VOLUME_MAX "  VOLUMES",
        LV_SYMBOL_SETTINGS "  FILTERS FX",
        LV_SYMBOL_DRIVE "  SETTINGS",
        LV_SYMBOL_EYE_OPEN "  DIAGNOSTICS"
    };
    const lv_color_t menu_colors[] = {
        RED808_ACCENT, RED808_INFO, RED808_SUCCESS,
        RED808_WARNING, RED808_CYAN, RED808_TEXT_DIM
    };

    int x_start = 28, y_start = 256;
    int btn_w = 300, btn_h = 152, gap = 24;

    for (int i = 0; i < 6; i++) {
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

    lv_obj_t* title_shell = create_section_shell(scr_live, 24, 82, 976, 72);
    lv_obj_t* title = lv_label_create(title_shell);
    lv_label_set_text(title, "LIVE PADS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, -12);

    lv_obj_t* subtitle = lv_label_create(title_shell);
    lv_label_set_text(subtitle, "Multitouch directo, repeticion sostenida y acceso inmediato por ByteButton.");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, RED808_TEXT_DIM, 0);
    lv_obj_align(subtitle, LV_ALIGN_LEFT_MID, 0, 16);

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
        apply_stable_button_style(pad, inst_colors[i], RED808_TEXT);
        lv_obj_set_style_radius(pad, 18, 0);
        lv_obj_set_style_border_width(pad, 2, 0);
        lv_obj_set_style_border_color(pad, RED808_BORDER, 0);
        lv_obj_set_style_pad_all(pad, 14, 0);
        // Sin gradiente: color sólido plano, sin decoloración al blanco

        lv_obj_t* lbl = lv_label_create(pad);
        lv_label_set_text(lbl, trackNames[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, RED808_BG, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 8);
        live_pad_names[i] = lbl;

        lv_obj_t* desc = lv_label_create(pad);
        lv_label_set_text(desc, instrumentNames[i]);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(desc, RED808_BG, 0);
        lv_obj_set_style_text_opa(desc, LV_OPA_80, 0);
        lv_obj_align(desc, LV_ALIGN_BOTTOM_LEFT, 0, -4);
        live_pad_desc[i] = desc;

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

        lv_obj_set_style_border_color(live_pads[pad], active ? lv_color_white() : RED808_BORDER, 0);
        lv_obj_set_style_border_width(live_pads[pad], active ? 4 : 2, 0);
        lv_obj_set_style_bg_opa(live_pads[pad], active ? LV_OPA_COVER : LV_OPA_90, 0);
        if (live_pad_names[pad]) {
            lv_obj_set_style_text_color(live_pad_names[pad], active ? lv_color_white() : RED808_BG, 0);
        }
        if (live_pad_desc[pad]) {
            lv_obj_set_style_text_color(live_pad_desc[pad], active ? lv_color_white() : RED808_BG, 0);
            lv_obj_set_style_text_opa(live_pad_desc[pad], active ? LV_OPA_COVER : LV_OPA_80, 0);
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
            lv_obj_set_style_bg_opa(seq_column_highlights[active_column], LV_OPA_20, 0);
            lv_obj_set_style_border_opa(seq_column_highlights[active_column], LV_OPA_80, 0);
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
                            active ? lv_color_white() : RED808_PANEL, 0);
                        lv_obj_set_style_border_width(seq_grid[t][s], 2, 0);
                        lv_obj_set_style_border_color(seq_grid[t][s], RED808_WARNING, 0);
                    } else {
                        lv_obj_set_style_bg_color(seq_grid[t][s],
                            active ? inst_colors[t] : RED808_SURFACE, 0);
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

    lv_obj_t* title = lv_label_create(scr_volumes);
    lv_label_set_text(title, "TRACK VOLUMES");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    int x_start = 20;
    int slider_w = 50;
    int gap = (1024 - 2 * x_start - 16 * slider_w) / 15;

    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        int x = x_start + i * (slider_w + gap);

        // Track name
        lv_obj_t* name = lv_label_create(scr_volumes);
        lv_label_set_text(name, trackNames[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, inst_colors[i], 0);
        lv_obj_set_pos(name, x + 8, 80);
        vol_name_labels[i] = name;

        // Vertical slider
        lv_obj_t* slider = lv_slider_create(scr_volumes);
        lv_obj_set_size(slider, 20, 400);
        lv_obj_set_pos(slider, x + 15, 105);
        lv_slider_set_range(slider, 0, Config::MAX_VOLUME);
        lv_slider_set_value(slider, trackVolumes[i], LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, RED808_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, inst_colors[i], LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, RED808_TEXT, LV_PART_KNOB);
        lv_obj_add_event_cb(slider, vol_slider_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        vol_sliders[i] = slider;

        // Value label
        lv_obj_t* val = lv_label_create(scr_volumes);
        lv_label_set_text_fmt(val, "%d", trackVolumes[i]);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(val, RED808_TEXT_DIM, 0);
        lv_obj_set_pos(val, x + 10, 515);
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
    if (lbl_master) {
        lv_label_set_text(lbl_master, masterConnected ? "ONLINE" : "OFFLINE");
        lv_obj_set_style_text_color(lbl_master, masterConnected ? RED808_SUCCESS : RED808_ERROR, 0);
    }
    if (lbl_menu_state) {
        lv_label_set_text(lbl_menu_state, wifiConnected ? "WiFi enlazado" : "WiFi desconectado");
        lv_obj_set_style_text_color(lbl_menu_state, wifiConnected ? RED808_INFO : RED808_TEXT_DIM, 0);
    }
    if (lbl_menu_transport) {
        lv_label_set_text_fmt(lbl_menu_transport, "%s  |  PTN %02d  |  %d BPM",
                              isPlaying ? "PLAY" : "PAUSE",
                              currentPattern + 1, currentBPM);
        lv_obj_set_style_text_color(lbl_menu_transport, isPlaying ? RED808_SUCCESS : RED808_WARNING, 0);
    }
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

    static const uint32_t btn_colors[THEME_COUNT] = {0xFF4444, 0x4A9EFF, 0x39FF14, 0xFF6B35, 0xFF00AA};
    for (int i = 0; i < THEME_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(scr_settings);
        lv_obj_set_size(btn, 160, 55);
        lv_obj_set_pos(btn, 40 + i * 190, 420);
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
// PATTERNS SCREEN
// ============================================================================
void ui_create_patterns_screen() {
    scr_patterns = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_patterns, RED808_BG, 0);
    ui_create_header(scr_patterns);

    lv_obj_t* title = lv_label_create(scr_patterns);
    lv_label_set_text(title, "PATTERNS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    // 4x4 grid of pattern buttons
    int btn_size = 200;
    int gap = 16;
    int x_start = (1024 - 4 * btn_size - 3 * gap) / 2;
    int y_start = 100;

    for (int i = 0; i < Config::MAX_PATTERNS; i++) {
        int col = i % 4;
        int row = i / 4;

        lv_obj_t* btn = lv_btn_create(scr_patterns);
        lv_obj_set_size(btn, btn_size, (600 - y_start - 20) / 4 - gap);
        lv_obj_set_pos(btn, x_start + col * (btn_size + gap), y_start + row * ((600 - y_start - 20) / 4));
        lv_obj_set_style_bg_color(btn, (i == currentPattern) ? RED808_ACCENT : RED808_SURFACE, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_color(btn, RED808_BORDER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "PTN\n%d", i + 1);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, RED808_TEXT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}
