// =============================================================================
// ui_screens.cpp - Screen implementations with LVGL
// RED808 V6 BlueSlaveV2 - 1024x600 touch interface
// =============================================================================
#include "ui_screens.h"
#include "ui_theme.h"
#include "../../include/system_state.h"
#include "../../include/config.h"
#include "../drivers/io_extension.h"
#include <Esp.h>
#include <ArduinoJson.h>
#include <math.h>

extern void sendUDPCommand(JsonDocument& doc);
extern void sendUDPCommand(const char* cmd);

// Screen objects
lv_obj_t* scr_menu = NULL;
lv_obj_t* scr_live = NULL;
lv_obj_t* scr_sequencer = NULL;
lv_obj_t* scr_volumes = NULL;
lv_obj_t* scr_filters = NULL;
lv_obj_t* scr_settings = NULL;
lv_obj_t* scr_diagnostics = NULL;
lv_obj_t* scr_patterns = NULL;
lv_obj_t* scr_sdcard = NULL;
lv_obj_t* scr_performance = NULL;
lv_obj_t* scr_samples = NULL;
lv_obj_t* scr_boot = NULL;
lv_obj_t* scr_seq_circle = NULL;

// Header widgets (updated live)
static constexpr uint8_t HEADER_SLOT_COUNT = SCREEN_ENCODER_TEST + 1;
static lv_obj_t* lbl_seq_volume[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_pad_volume[HEADER_SLOT_COUNT] = {};
static lv_obj_t* chip_seq_volume[HEADER_SLOT_COUNT] = {};
static lv_obj_t* chip_pad_volume[HEADER_SLOT_COUNT] = {};
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
static lv_obj_t* seq_track_panels[Config::MAX_TRACKS]; // container: label + S + M
static lv_obj_t* seq_solo_btns[Config::MAX_TRACKS];
static lv_obj_t* seq_mute_btns[Config::MAX_TRACKS];
static lv_obj_t* lbl_step_indicator = NULL;
static lv_obj_t* seq_column_highlights[Config::MAX_STEPS];
static lv_obj_t* seq_step_leds[Config::MAX_STEPS];  // LED strip below grid
static int seq_page = 0;              // 0 = tracks 0-7, 1 = tracks 8-15
static constexpr int SEQ_TRACKS_PER_PAGE = 8;
static lv_obj_t* seq_page_btn = NULL;
static lv_obj_t* seq_page_lbl = NULL;

// Volume sliders
static lv_obj_t* vol_sliders[Config::MAX_TRACKS];
static lv_obj_t* vol_labels[Config::MAX_TRACKS];
static lv_obj_t* vol_name_labels[Config::MAX_TRACKS];

// Filter UI
static lv_obj_t* filter_arcs[3];       // Delay, Flanger, Compressor
static lv_obj_t* filter_labels[3];
static lv_obj_t* filter_value_labels[3];
static lv_obj_t* filter_target_label = NULL;
static lv_obj_t* filter_preset_label = NULL;  // rotary FX preset indicator

// Live pads
static lv_obj_t* live_pads[Config::MAX_SAMPLES];
static lv_obj_t* live_pad_names[Config::MAX_SAMPLES];
static lv_obj_t* live_pad_desc[Config::MAX_SAMPLES];
static lv_coord_t live_pad_x[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_y[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_w[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_h[Config::MAX_SAMPLES] = {};

static constexpr int LIVE_PAD_COLS = 4;
static constexpr int LIVE_PAD_SIZE = 200;
static constexpr int LIVE_PAD_GAP = 12;
static constexpr int LIVE_PAD_AREA_TOP = 70;

// Ratchet controls (repeat count 1-16)
static lv_obj_t* live_ratchet_label = NULL;

// LivePad sequencer sync
static bool livePadSyncMode = false;
static lv_obj_t* live_sync_btn = NULL;
static lv_obj_t* live_sync_lbl = NULL;

static uint32_t last_nav_ms = 0;

// ============================================================================
// BOOT SCREEN (TRON / 80s terminal intro)
// ============================================================================
static constexpr int kBootLineCount = 16;
static lv_obj_t* boot_text_lines[kBootLineCount] = {};
static lv_obj_t* boot_cursor_lbl = NULL;
static lv_obj_t* boot_status_lbl = NULL;
static lv_timer_t* boot_timer = NULL;
static int boot_state = 0;

static const char* const kBootLines[] = {
    "[KERN]  FW V3.8.1 BUILD " __DATE__ " " __TIME__ "...... OK",
    "[CPU ]  ESP32-S3 DUAL CORE 240MHz Xtensa LX7..... OK",
    "[MEM ]  FLASH 16MB QIO  PSRAM 8MB OPI  N16R8..... OK",
    "[DISP]  WAVESHARE 7\" LCD 1024x600 RGB888.......... OK",
    "[GFX ]  LVGL v8.4.0 DIRECT MODE  2x FB PSRAM..... OK",
    "[TCH ]  GOODIX GT911 MULTI-TOUCH 5 POINTS......... OK",
    "[I2C ]  BUS 400kHz  PCA9548A 8-CH MUX............. OK",
    "[ENC1]  M5STACK ROTATE8 x2  RGB LED............... OK",
    "[ENC2]  DFROBOT VISUAL ROTARY x2  I2C............. OK",
    "[ENC3]  DFROBOT ANALOG ROTARY  GPIO6 ADC.......... OK",
    "[BTN ]  BYTEBUTTON 8x1 MATRIX  I2C 0x27.......... OK",
    "[SDIO]  MMC 1-BIT  CLK:12 CMD:11 D0:13........... OK",
    "[WLAN]  802.11b/g/n STA  AP RED808  CH AUTO....... OK",
    "[UDP ]  PORT 8888  MASTER 192.168.4.1............. OK",
    "[SEQ ]  16 TRACKS x 16 STEPS  64 PATTERNS........ OK",
    "\n> BLU808 SLAVE CONTROLLER V6 ............... READY",
};

// ============================================================================
// CIRCULAR SEQUENCER
// ============================================================================
static lv_obj_t* circ_step_btns[Config::MAX_STEPS] = {};
static lv_obj_t* circ_playhead = NULL;
static lv_obj_t* circ_track_name_lbl = NULL;
static lv_obj_t* circ_track_btns[Config::MAX_TRACKS] = {};
static lv_obj_t* lbl_circ_info = NULL;
static int circ_step_cx[Config::MAX_STEPS];
static int circ_step_cy[Config::MAX_STEPS];
static constexpr int CIRC_CX = 295;
static constexpr int CIRC_CY = 315;
static constexpr float CIRC_R = 218.0f;
static constexpr float CIRC_INNER_R = 152.0f;
static constexpr int CIRC_PAD = 44;
static constexpr int CIRC_HEAD_SIZE = 18;

// ============================================================================
// LVGL anim helper callbacks (captureless, safe as C function pointers)
// ============================================================================
static void anim_opa_cb(void* obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}
static void anim_y_cb(void* obj, int32_t v) {
    lv_obj_set_y((lv_obj_t*)obj, v);
}

static Screen screen_from_parent(lv_obj_t* parent) {
    if (parent == scr_menu) return SCREEN_MENU;
    if (parent == scr_live) return SCREEN_LIVE;
    if (parent == scr_sequencer) return SCREEN_SEQUENCER;
    if (parent == scr_settings) return SCREEN_SETTINGS;
    if (parent == scr_diagnostics) return SCREEN_DIAGNOSTICS;
    if (parent == scr_patterns) return SCREEN_PATTERNS;
    if (parent == scr_volumes) return SCREEN_VOLUMES;
    if (parent == scr_filters) return SCREEN_FILTERS;
    if (parent == scr_sdcard) return SCREEN_SDCARD;
    if (parent == scr_performance) return SCREEN_PERFORMANCE;
    if (parent == scr_samples) return SCREEN_SAMPLES;
    if (parent == scr_seq_circle) return SCREEN_SEQ_CIRCLE;
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
        // Sync ByteButton edge-detection state: assume all buttons "were pressed"
        // so no phantom edge fires on first handleByteButton() after entering LIVE.
        prevByteButtonState = 0xFF;
        memset(byteButtonLivePressed, 0, sizeof(bool) * BYTEBUTTON_BUTTONS);
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
    lv_label_set_text(logo, LV_SYMBOL_AUDIO " Blu808Slave");
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

    chip_seq_volume[slot] = create_info_chip(status_row, RED808_ACCENT, &lbl_seq_volume[slot]);
    lv_label_set_text_fmt(lbl_seq_volume[slot], "SEQ %d", sequencerVolume);

    chip_pad_volume[slot] = create_info_chip(status_row, RED808_ACCENT2, &lbl_pad_volume[slot]);
    lv_label_set_text_fmt(lbl_pad_volume[slot], "PAD %d", livePadsVolume);

    // Highlight active volume mode chip
    bool seqActive = (volumeMode == VOL_SEQUENCER);
    lv_obj_set_style_bg_opa(chip_seq_volume[slot], seqActive ? LV_OPA_80 : LV_OPA_20, 0);
    lv_obj_set_style_border_width(chip_seq_volume[slot], seqActive ? 2 : 1, 0);
    lv_obj_set_style_bg_opa(chip_pad_volume[slot], seqActive ? LV_OPA_20 : LV_OPA_80, 0);
    lv_obj_set_style_border_width(chip_pad_volume[slot], seqActive ? 1 : 2, 0);

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
    static uint8_t prev_slot = 255;

    // Only update the active screen's header slot — avoids dirtying hidden screens
    uint8_t slot = static_cast<uint8_t>(currentScreen);
    if (slot >= HEADER_SLOT_COUNT) return;

    // Force full refresh when switching screens (different slot)
    bool slotChanged = (slot != prev_slot);
    if (slotChanged) {
        prev_slot = slot;
        prev_bpm = -1;
        prev_pattern = -1;
        prev_playing = -1;
        prev_wifi = -1;
    }

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
    static int prev_vol_mode = -1;
    if (slotChanged) {
        prev_seq_volume = -1;
        prev_pad_volume = -1;
        prev_vol_mode = -1;
    }
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
    if ((int)volumeMode != prev_vol_mode) {
        prev_vol_mode = (int)volumeMode;
        bool seqActive = (volumeMode == VOL_SEQUENCER);
        if (chip_seq_volume[slot]) {
            lv_obj_set_style_bg_opa(chip_seq_volume[slot], seqActive ? LV_OPA_80 : LV_OPA_20, 0);
            lv_obj_set_style_border_width(chip_seq_volume[slot], seqActive ? 2 : 1, 0);
        }
        if (chip_pad_volume[slot]) {
            lv_obj_set_style_bg_opa(chip_pad_volume[slot], seqActive ? LV_OPA_20 : LV_OPA_80, 0);
            lv_obj_set_style_border_width(chip_pad_volume[slot], seqActive ? 1 : 2, 0);
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
        case 5: nav_to(SCREEN_SDCARD, scr_sdcard); break;
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
        LV_SYMBOL_DRIVE "  SD BROWSER",
        LV_SYMBOL_SETTINGS "  HW ASSIGN",
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
// LIVE PADS SCREEN - 4x4 grid of pads + ratchet buttons
// ============================================================================

static void ratchet_minus_cb(lv_event_t* e) {
    (void)e;
    if (livePadRepeatCount > 1) {
        livePadRepeatCount = livePadRepeatCount - 1;
        if (live_ratchet_label) {
            char buf[8]; snprintf(buf, sizeof(buf), "%dx", (int)livePadRepeatCount);
            lv_label_set_text(live_ratchet_label, buf);
        }
    }
}

static void ratchet_plus_cb(lv_event_t* e) {
    (void)e;
    if (livePadRepeatCount < 16) {
        livePadRepeatCount = livePadRepeatCount + 1;
        if (live_ratchet_label) {
            char buf[8]; snprintf(buf, sizeof(buf), "%dx", (int)livePadRepeatCount);
            lv_label_set_text(live_ratchet_label, buf);
        }
    }
}

int ui_live_pad_hit_test(int x, int y) {
    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (x < live_pad_x[pad] || y < live_pad_y[pad]) continue;
        if (x >= (live_pad_x[pad] + live_pad_w[pad])) continue;
        if (y >= (live_pad_y[pad] + live_pad_h[pad])) continue;
        return pad;
    }
    return -1;
}

static void live_sync_cb(lv_event_t* e) {
    livePadSyncMode = !livePadSyncMode;
    if (live_sync_btn) {
        lv_obj_set_style_bg_color(live_sync_btn,
            livePadSyncMode ? lv_color_hex(0xFFD700) : RED808_SURFACE, 0);
    }
    if (live_sync_lbl) {
        lv_obj_set_style_text_color(live_sync_lbl,
            livePadSyncMode ? RED808_BG : RED808_TEXT_DIM, 0);
    }
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

    // SYNC button — centered horizontally, between header and pad grid
    live_sync_btn = lv_obj_create(scr_live);
    lv_obj_set_size(live_sync_btn, 120, 34);
    lv_obj_set_pos(live_sync_btn, (1024 - 120) / 2, LIVE_PAD_AREA_TOP - 42);
    lv_obj_clear_flag(live_sync_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(live_sync_btn, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(live_sync_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(live_sync_btn, 8, 0);
    lv_obj_set_style_border_color(live_sync_btn, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_border_width(live_sync_btn, 1, 0);
    lv_obj_add_event_cb(live_sync_btn, live_sync_cb, LV_EVENT_PRESSED, NULL);

    live_sync_lbl = lv_label_create(live_sync_btn);
    lv_label_set_text(live_sync_lbl, LV_SYMBOL_REFRESH " SYNC");
    lv_obj_set_style_text_font(live_sync_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(live_sync_lbl, RED808_TEXT_DIM, 0);
    lv_obj_center(live_sync_lbl);

    // Ratchet controls — right side of pad grid: [ - ] Nx [ + ]
    {
        int grid_right = (1024 - LIVE_PAD_COLS * LIVE_PAD_SIZE - 3 * LIVE_PAD_GAP) / 2
                         + LIVE_PAD_COLS * LIVE_PAD_SIZE + 3 * LIVE_PAD_GAP;
        int ctrl_x = grid_right + 4;
        int ctrl_w = 1024 - ctrl_x - 8;  // fill remaining width
        int btn_h = 80;
        int label_h = 50;
        int total_h = btn_h + label_h + btn_h + 12*2;  // 2 gaps of 12
        int y_start = LIVE_PAD_AREA_TOP + (600 - LIVE_PAD_AREA_TOP - total_h) / 2;

        // Title
        lv_obj_t* title = lv_label_create(scr_live);
        lv_label_set_text(title, "RPT");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, RED808_TEXT_DIM, 0);
        lv_obj_set_pos(title, ctrl_x + ctrl_w / 2 - 10, y_start - 18);

        // [ + ] button (top = increase)
        lv_obj_t* btn_plus = lv_btn_create(scr_live);
        lv_obj_set_size(btn_plus, ctrl_w, btn_h);
        lv_obj_set_pos(btn_plus, ctrl_x, y_start);
        lv_obj_set_style_bg_color(btn_plus, RED808_SURFACE, 0);
        lv_obj_set_style_bg_color(btn_plus, RED808_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn_plus, 10, 0);
        lv_obj_add_event_cb(btn_plus, ratchet_plus_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_plus = lv_label_create(btn_plus);
        lv_label_set_text(lbl_plus, "+");
        lv_obj_set_style_text_font(lbl_plus, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl_plus, RED808_ACCENT, 0);
        lv_obj_center(lbl_plus);

        // Value label (middle)
        live_ratchet_label = lv_label_create(scr_live);
        char buf[8]; snprintf(buf, sizeof(buf), "%dx", livePadRepeatCount);
        lv_label_set_text(live_ratchet_label, buf);
        lv_obj_set_style_text_font(live_ratchet_label, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(live_ratchet_label, RED808_ACCENT, 0);
        lv_obj_set_pos(live_ratchet_label, ctrl_x + ctrl_w / 2 - 16, y_start + btn_h + 12 + 8);

        // [ - ] button (bottom = decrease)
        lv_obj_t* btn_minus = lv_btn_create(scr_live);
        lv_obj_set_size(btn_minus, ctrl_w, btn_h);
        lv_obj_set_pos(btn_minus, ctrl_x, y_start + btn_h + 12 + label_h + 12);
        lv_obj_set_style_bg_color(btn_minus, RED808_SURFACE, 0);
        lv_obj_set_style_bg_color(btn_minus, RED808_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn_minus, 10, 0);
        lv_obj_add_event_cb(btn_minus, ratchet_minus_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_minus = lv_label_create(btn_minus);
        lv_label_set_text(lbl_minus, "-");
        lv_obj_set_style_text_font(lbl_minus, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl_minus, RED808_ACCENT, 0);
        lv_obj_center(lbl_minus);
    }
}

static bool livePadsNeedFullRedraw = false;

void ui_live_pads_invalidate() {
    livePadsNeedFullRedraw = true;
}

void ui_update_live_pads() {
    static uint32_t prev_state_mask = 0;
    static int prev_sync_step = -1;

    // Force full redraw after theme change
    bool forceAll = livePadsNeedFullRedraw;
    if (livePadsNeedFullRedraw) livePadsNeedFullRedraw = false;

    // Detect sequencer step change while synced — only flag, don't force all
    if (livePadSyncMode && isPlaying) {
        if ((int)currentStep != prev_sync_step) {
            prev_sync_step = (int)currentStep;
            // Don't force all — let diff logic handle it
        }
    } else {
        prev_sync_step = -1;
    }

    uint32_t state_mask = 0;

    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        bool active = livePadPressed[pad];
        if (livePadSyncMode && isPlaying) {
            if (patterns[currentPattern].steps[pad][currentStep]) {
                active = true;
            }
        }
        if (active) {
            state_mask |= (1UL << pad);
        }
    }

    if (!forceAll && state_mask == prev_state_mask) {
        return;
    }

    // Only restyle pads that actually changed (or all if forceAll)
    uint32_t changed = forceAll ? 0xFFFF : (state_mask ^ prev_state_mask);

    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (!(changed & (1UL << pad))) continue;
        if (!live_pads[pad]) continue;
        bool active = (state_mask & (1UL << pad)) != 0;

        if (active) {
            // === NEON CORONA ON ===
            lv_obj_set_style_bg_color(live_pads[pad], inst_colors[pad], 0);
            lv_obj_set_style_bg_opa(live_pads[pad], LV_OPA_80, 0);
            lv_obj_set_style_border_color(live_pads[pad], inst_colors[pad], 0);
            lv_obj_set_style_border_width(live_pads[pad], 4, 0);
            lv_obj_set_style_border_opa(live_pads[pad], LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_color(live_pads[pad], inst_colors[pad], 0);
            lv_obj_set_style_shadow_width(live_pads[pad], 20, 0);
            lv_obj_set_style_shadow_spread(live_pads[pad], 4, 0);
            lv_obj_set_style_shadow_opa(live_pads[pad], LV_OPA_80, 0);
            if (live_pad_names[pad])
                lv_obj_set_style_text_color(live_pad_names[pad], lv_color_white(), 0);
            if (live_pad_desc[pad])
                lv_obj_set_style_text_color(live_pad_desc[pad], lv_color_white(), 0);
        } else {
            // === NEON CORONA OFF ===
            lv_obj_set_style_bg_color(live_pads[pad], RED808_SURFACE, 0);
            lv_obj_set_style_bg_opa(live_pads[pad], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(live_pads[pad], 0, 0);
            lv_obj_set_style_border_opa(live_pads[pad], LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_color(live_pads[pad], lv_color_black(), 0);
            lv_obj_set_style_shadow_width(live_pads[pad], 8, 0);
            lv_obj_set_style_shadow_spread(live_pads[pad], 0, 0);
            lv_obj_set_style_shadow_opa(live_pads[pad], LV_OPA_30, 0);
            if (live_pad_names[pad])
                lv_obj_set_style_text_color(live_pad_names[pad], inst_colors[pad], 0);
            if (live_pad_desc[pad])
                lv_obj_set_style_text_color(live_pad_desc[pad], RED808_TEXT_DIM, 0);
        }
    }

    prev_state_mask = state_mask;
}

// ============================================================================
// SEQUENCER SCREEN - 8-track paged grid (2 pages of 8 tracks)
// ============================================================================
static void seq_step_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int track = idx / Config::MAX_STEPS;
    int step = idx % Config::MAX_STEPS;
    patterns[currentPattern].steps[track][step] = !patterns[currentPattern].steps[track][step];
    bool active = patterns[currentPattern].steps[track][step];
    lv_obj_set_style_bg_color(lv_event_get_target(e),
        active ? inst_colors[track] : RED808_SURFACE, 0);
    // Send step update to master (same format as old controller)
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setStep\",\"track\":%d,\"step\":%d,\"active\":%s}",
        track, step, active ? "true" : "false");
    sendUDPCommand(buf);
}

// Is track effectively silent? (muted directly, or muted by another track's solo)
static bool isTrackEffectivelyMuted(int track) {
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        if (trackSolo[t]) return (track != t); // solo active: only solo'd track is audible
    }
    return trackMuted[track];
}

static void seq_update_solo_mute_visuals(int track) {
    bool effMuted = isTrackEffectivelyMuted(track);

    // Solo button
    if (seq_solo_btns[track]) {
        lv_obj_set_style_bg_color(seq_solo_btns[track],
            trackSolo[track] ? lv_color_hex(0xFFD700) : RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(seq_solo_btns[track], LV_OPA_COVER, 0);
        lv_obj_t* lbl = lv_obj_get_child(seq_solo_btns[track], 0);
        if (lbl) lv_obj_set_style_text_color(lbl,
            trackSolo[track] ? RED808_BG : RED808_TEXT_DIM, 0);
    }
    // Mute button
    if (seq_mute_btns[track]) {
        lv_obj_set_style_bg_color(seq_mute_btns[track],
            trackMuted[track] ? lv_color_hex(0xFF3030) : RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(seq_mute_btns[track], LV_OPA_COVER, 0);
        lv_obj_t* lbl = lv_obj_get_child(seq_mute_btns[track], 0);
        if (lbl) lv_obj_set_style_text_color(lbl,
            trackMuted[track] ? lv_color_white() : RED808_TEXT_DIM, 0);
    }
    // Track panel: solo'd = gold border, muted = dim + red border, normal = default
    if (seq_track_panels[track]) {
        if (trackSolo[track]) {
            lv_obj_set_style_bg_opa(seq_track_panels[track], LV_OPA_70, 0);
            lv_obj_set_style_border_width(seq_track_panels[track], 2, 0);
            lv_obj_set_style_border_color(seq_track_panels[track], lv_color_hex(0xFFD700), 0);
        } else if (effMuted) {
            lv_obj_set_style_bg_opa(seq_track_panels[track], LV_OPA_20, 0);
            lv_obj_set_style_border_width(seq_track_panels[track], 1, 0);
            lv_obj_set_style_border_color(seq_track_panels[track], lv_color_hex(0xFF3030), 0);
        } else {
            lv_obj_set_style_bg_opa(seq_track_panels[track], LV_OPA_40, 0);
            lv_obj_set_style_border_width(seq_track_panels[track], 0, 0);
        }
    }
    // Track label dim when muted
    if (seq_track_labels[track]) {
        lv_obj_set_style_text_opa(seq_track_labels[track],
            effMuted ? LV_OPA_40 : LV_OPA_COVER, 0);
    }
    // Grid cells: dim when effectively muted
    for (int s = 0; s < Config::MAX_STEPS; s++) {
        if (seq_grid[track][s]) {
            lv_obj_set_style_bg_opa(seq_grid[track][s],
                effMuted ? LV_OPA_30 : LV_OPA_COVER, 0);
        }
    }
}

static void seq_solo_cb(lv_event_t* e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);

    if (trackSolo[track]) {
        // Un-solo: clear and restore original mute states
        trackSolo[track] = false;
        for (int t = 0; t < Config::MAX_TRACKS; t++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"mute\",\"track\":%d,\"value\":%s}",
                t, trackMuted[t] ? "true" : "false");
            sendUDPCommand(buf);
            seq_update_solo_mute_visuals(t);
        }
    } else {
        // Solo this track EXCLUSIVELY — clear all others
        for (int t = 0; t < Config::MAX_TRACKS; t++) trackSolo[t] = false;
        trackSolo[track] = true;
        for (int t = 0; t < Config::MAX_TRACKS; t++) {
            bool shouldMute = (t != track);
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"mute\",\"track\":%d,\"value\":%s}",
                t, shouldMute ? "true" : "false");
            sendUDPCommand(buf);
            seq_update_solo_mute_visuals(t);
        }
    }
}

static void seq_mute_cb(lv_event_t* e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    trackMuted[track] = !trackMuted[track];

    // Send to master only if this track's audible state actually changes
    bool anySolo = false;
    for (int t = 0; t < Config::MAX_TRACKS; t++) if (trackSolo[t]) { anySolo = true; break; }

    if (!anySolo || trackSolo[track]) {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"cmd\":\"mute\",\"track\":%d,\"value\":%s}",
            track, trackMuted[track] ? "true" : "false");
        sendUDPCommand(buf);
    }
    seq_update_solo_mute_visuals(track);
}

static void seq_apply_page() {
    int page_start = seq_page * SEQ_TRACKS_PER_PAGE;
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        bool visible = (t >= page_start && t < page_start + SEQ_TRACKS_PER_PAGE);
        if (seq_track_panels[t]) {
            if (visible) lv_obj_clear_flag(seq_track_panels[t], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(seq_track_panels[t], LV_OBJ_FLAG_HIDDEN);
        }
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            if (seq_grid[t][s]) {
                if (visible) lv_obj_clear_flag(seq_grid[t][s], LV_OBJ_FLAG_HIDDEN);
                else         lv_obj_add_flag(seq_grid[t][s], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (seq_page_lbl) {
        lv_label_set_text_fmt(seq_page_lbl, "PAGE %d/2", seq_page + 1);
    }
}

void ui_create_sequencer_screen() {
    scr_sequencer = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sequencer, RED808_BG, 0);
    ui_create_header(scr_sequencer);

    lv_obj_t* shell = create_section_shell(scr_sequencer, 16, 78, 992, 506);

    // Layout
    int label_col_w = 78;              // wider column for name + S + M
    int grid_x = label_col_w + 8;      // grid starts after label column
    int grid_y = 55;
    int cell_w = 54, cell_h = 58, gap = 2;  // slightly narrower to fit

    // Track panels: container with name + S + M buttons (positioned for 8 visible rows)
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        int row = t % SEQ_TRACKS_PER_PAGE;
        int panel_y = grid_y + row * (cell_h + gap);

        // Panel container
        lv_obj_t* panel = lv_obj_create(scr_sequencer);
        lv_obj_set_size(panel, label_col_w, cell_h);
        lv_obj_set_pos(panel, 2, panel_y);
        lv_obj_set_style_bg_color(panel, RED808_PANEL, 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_40, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_radius(panel, 4, 0);
        lv_obj_set_style_pad_all(panel, 2, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        seq_track_panels[t] = panel;

        // Track name label (top of panel)
        lv_obj_t* lbl = lv_label_create(panel);
        lv_label_set_text(lbl, trackNames[t]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, inst_colors[t], 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);
        seq_track_labels[t] = lbl;

        // S button (Solo) — bottom left
        lv_obj_t* btn_s = lv_btn_create(panel);
        lv_obj_set_size(btn_s, 32, 24);
        lv_obj_align(btn_s, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_bg_color(btn_s, RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(btn_s, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn_s, RED808_SURFACE, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn_s, 4, 0);
        lv_obj_set_style_border_width(btn_s, 1, 0);
        lv_obj_set_style_border_color(btn_s, lv_color_hex(0xFFD700), 0);
        lv_obj_set_style_pad_all(btn_s, 0, 0);
        lv_obj_set_style_shadow_width(btn_s, 0, 0);
        lv_obj_set_style_shadow_width(btn_s, 0, LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn_s, seq_solo_cb, LV_EVENT_PRESSED, (void*)(intptr_t)t);
        lv_obj_t* lbl_s = lv_label_create(btn_s);
        lv_label_set_text(lbl_s, "S");
        lv_obj_set_style_text_font(lbl_s, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_s, RED808_TEXT_DIM, 0);
        lv_obj_center(lbl_s);
        seq_solo_btns[t] = btn_s;

        // M button (Mute) — bottom right
        lv_obj_t* btn_m = lv_btn_create(panel);
        lv_obj_set_size(btn_m, 32, 24);
        lv_obj_align(btn_m, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_set_style_bg_color(btn_m, RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(btn_m, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn_m, RED808_SURFACE, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn_m, 4, 0);
        lv_obj_set_style_border_width(btn_m, 1, 0);
        lv_obj_set_style_border_color(btn_m, lv_color_hex(0xFF3030), 0);
        lv_obj_set_style_pad_all(btn_m, 0, 0);
        lv_obj_set_style_shadow_width(btn_m, 0, 0);
        lv_obj_set_style_shadow_width(btn_m, 0, LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn_m, seq_mute_cb, LV_EVENT_PRESSED, (void*)(intptr_t)t);
        lv_obj_t* lbl_m = lv_label_create(btn_m);
        lv_label_set_text(lbl_m, "M");
        lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_m, RED808_TEXT_DIM, 0);
        lv_obj_center(lbl_m);
        seq_mute_btns[t] = btn_m;
    }

    // Step column highlights (sized for 8 tracks)
    for (int s = 0; s < Config::MAX_STEPS; s++) {
        lv_obj_t* column = lv_obj_create(scr_sequencer);
        lv_obj_set_size(column, cell_w, SEQ_TRACKS_PER_PAGE * (cell_h + gap) - gap);
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

    // Grid cells — all 16 tracks created, positioned in 8-row layout
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        int row = t % SEQ_TRACKS_PER_PAGE;
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            lv_obj_t* cell = lv_btn_create(scr_sequencer);
            lv_obj_set_size(cell, cell_w, cell_h);
            lv_obj_set_pos(cell, grid_x + s * (cell_w + gap), grid_y + row * (cell_h + gap));
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

    // LED indicator strip (below 8-track grid)
    int led_y = grid_y + SEQ_TRACKS_PER_PAGE * (cell_h + gap) + 4;
    for (int s = 0; s < Config::MAX_STEPS; s++) {
        lv_obj_t* led = lv_obj_create(scr_sequencer);
        lv_obj_set_size(led, cell_w, 7);
        lv_obj_set_pos(led, grid_x + s * (cell_w + gap), led_y);
        lv_obj_set_style_bg_color(led, (s % 4 == 0) ? lv_color_hex(0x2A2A2A) : lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(led, 2, 0);
        lv_obj_set_style_border_width(led, 0, 0);
        lv_obj_set_style_shadow_width(led, 0, 0);
        lv_obj_add_flag(led, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(led, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        seq_step_leds[s] = led;
    }

    // Bottom bar
    int bottom_y = led_y + 14;

    lbl_step_indicator = lv_label_create(scr_sequencer);
    lv_label_set_text(lbl_step_indicator, "Step: --");
    lv_obj_set_style_text_font(lbl_step_indicator, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_step_indicator, RED808_WARNING, 0);
    lv_obj_set_pos(lbl_step_indicator, 8, bottom_y);

    // PAGE button
    seq_page_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(seq_page_btn, 130, 26);
    lv_obj_set_pos(seq_page_btn, 550, bottom_y);
    lv_obj_set_style_bg_color(seq_page_btn, lv_color_hex(0x1C300C), 0);
    lv_obj_set_style_bg_opa(seq_page_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(seq_page_btn, 1, 0);
    lv_obj_set_style_border_color(seq_page_btn, lv_color_hex(0x40A000), 0);
    lv_obj_set_style_radius(seq_page_btn, 5, 0);
    lv_obj_add_event_cb(seq_page_btn, [](lv_event_t*) {
        seq_page = (seq_page + 1) % 2;
        seq_apply_page();
    }, LV_EVENT_PRESSED, NULL);
    seq_page_lbl = lv_label_create(seq_page_btn);
    lv_label_set_text(seq_page_lbl, "PAGE 1/2");
    lv_obj_set_style_text_font(seq_page_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(seq_page_lbl, lv_color_hex(0x80DF60), 0);
    lv_obj_center(seq_page_lbl);

    // CIRCLE VIEW button
    lv_obj_t* circle_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(circle_btn, 186, 26);
    lv_obj_set_pos(circle_btn, 826, bottom_y);
    lv_obj_set_style_bg_color(circle_btn, lv_color_hex(0x0C1C30), 0);
    lv_obj_set_style_bg_opa(circle_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circle_btn, 1, 0);
    lv_obj_set_style_border_color(circle_btn, lv_color_hex(0x0060A0), 0);
    lv_obj_set_style_radius(circle_btn, 5, 0);
    lv_obj_add_event_cb(circle_btn, [](lv_event_t*) { nav_to(SCREEN_SEQ_CIRCLE, scr_seq_circle); },
                        LV_EVENT_PRESSED, NULL);
    lv_obj_t* circle_lbl = lv_label_create(circle_btn);
    lv_label_set_text(circle_lbl, "O  CIRCULAR VIEW");
    lv_obj_set_style_text_font(circle_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(circle_lbl, lv_color_hex(0x60AADF), 0);
    lv_obj_center(circle_lbl);

    // Show only page 0 tracks initially
    seq_page = 0;
    seq_apply_page();
}

void ui_update_sequencer() {
    if (!scr_sequencer) return;

    static int prev_step = -1;
    static int prev_column = -2;
    static int prev_page = -1;
    static uint8_t prev_grid_state[Config::MAX_TRACKS][Config::MAX_STEPS];

    int page_start = seq_page * SEQ_TRACKS_PER_PAGE;
    int page_end   = page_start + SEQ_TRACKS_PER_PAGE;

    // On page change, force full grid refresh for visible tracks
    if (seq_page != prev_page) {
        prev_page = seq_page;
        memset(prev_grid_state, 0xFF, sizeof(prev_grid_state));
        prev_column = -2;
    }

    // Update step highlight label only when step changes
    if (lbl_step_indicator && currentStep != prev_step) {
        lv_label_set_text_fmt(lbl_step_indicator, "Step: %02d / %02d", currentStep + 1, Config::MAX_STEPS);
    }

    int active_column = isPlaying ? currentStep : -1;
    if (active_column != prev_column) {
        // --- Previous column: remove highlight ---
        if (prev_column >= 0 && prev_column < Config::MAX_STEPS) {
            if (seq_column_highlights[prev_column]) {
                lv_obj_set_style_bg_opa(seq_column_highlights[prev_column], LV_OPA_0, 0);
                lv_obj_set_style_border_opa(seq_column_highlights[prev_column], LV_OPA_0, 0);
            }
            if (seq_step_leds[prev_column]) {
                lv_obj_set_style_bg_color(seq_step_leds[prev_column],
                    (prev_column % 4 == 0) ? lv_color_hex(0x2A2A2A) : lv_color_hex(0x1A1A1A), 0);
                lv_obj_set_style_shadow_width(seq_step_leds[prev_column], 0, 0);
            }
            // Restyle prev column cells — mark dirty so diff loop handles them
            for (int t = page_start; t < page_end; t++) {
                prev_grid_state[t][prev_column] = 0xFF;
            }
        }
        // --- Active column: show highlight with neon glow ---
        if (active_column >= 0 && active_column < Config::MAX_STEPS) {
            if (seq_column_highlights[active_column]) {
                lv_obj_set_style_bg_opa(seq_column_highlights[active_column], LV_OPA_20, 0);
                lv_obj_set_style_border_opa(seq_column_highlights[active_column], LV_OPA_70, 0);
            }
            if (seq_step_leds[active_column]) {
                lv_obj_set_style_bg_color(seq_step_leds[active_column], RED808_WARNING, 0);
                lv_obj_set_style_shadow_width(seq_step_leds[active_column], 6, 0);
                lv_obj_set_style_shadow_color(seq_step_leds[active_column], RED808_WARNING, 0);
                lv_obj_set_style_shadow_opa(seq_step_leds[active_column], LV_OPA_60, 0);
                lv_obj_set_style_shadow_spread(seq_step_leds[active_column], 2, 0);
            }
            // Mark active column cells dirty
            for (int t = page_start; t < page_end; t++) {
                prev_grid_state[t][active_column] = 0xFF;
            }
        }
        prev_column = active_column;
    }

    // Diff loop — only restyle cells whose state actually changed
    for (int t = page_start; t < page_end; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            if (!seq_grid[t][s]) continue;
            bool active = patterns[currentPattern].steps[t][s];
            bool isHit = (s == active_column);
            uint8_t state = (isHit ? 2 : 0) | (active ? 1 : 0);
            if (state == prev_grid_state[t][s]) continue;
            prev_grid_state[t][s] = state;

            if (isHit) {
                // Active step — bright with neon border
                lv_obj_set_style_bg_color(seq_grid[t][s],
                    active ? lv_color_white() : lv_color_hex(0x504000), 0);
                lv_obj_set_style_border_width(seq_grid[t][s], 2, 0);
                lv_obj_set_style_border_color(seq_grid[t][s], RED808_WARNING, 0);
                lv_obj_set_style_shadow_width(seq_grid[t][s], 0, 0);
                lv_obj_set_style_shadow_opa(seq_grid[t][s], LV_OPA_0, 0);
            } else {
                // Inactive step — normal
                lv_obj_set_style_bg_color(seq_grid[t][s],
                    active ? inst_colors[t] : RED808_SURFACE, 0);
                lv_obj_set_style_border_width(seq_grid[t][s], 0, 0);
                lv_obj_set_style_shadow_width(seq_grid[t][s], 0, 0);
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

    // Analog rotary FX preset badge (top right)
    filter_preset_label = lv_label_create(scr_filters);
    lv_label_set_text(filter_preset_label, "PRESET: --");
    lv_obj_set_style_text_font(filter_preset_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(filter_preset_label, lv_color_hex(0x80FF80), 0);
    lv_obj_set_pos(filter_preset_label, 820, 52);
}

void ui_update_filters() {
    static const char* kPresetNames[12] = {
        "FX OFF", "ROOM", "DELAY", "SLAPBACK",
        "FLANGE LO", "FLANGE HI", "COMP SOFT", "COMP HARD",
        "SPACE", "CHORUS", "FULL FX", "DESTROY"
    };
    static uint8_t prev_amounts[3] = {0xFF, 0xFF, 0xFF};
    static int prev_selectedFX = -1;
    static int prev_target = -999;
    static int prev_preset = -1;
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

    // Rotary FX preset indicator
    if (filter_preset_label && analogFxPreset != prev_preset) {
        prev_preset = analogFxPreset;
        int p = constrain(analogFxPreset, 0, 11);
        if (p == 0) {
            lv_label_set_text(filter_preset_label, "PRESET: OFF");
            lv_obj_set_style_text_color(filter_preset_label, lv_color_hex(0x666666), 0);
        } else {
            lv_label_set_text_fmt(filter_preset_label, "P%02d: %s", p, kPresetNames[p]);
            lv_obj_set_style_text_color(filter_preset_label, lv_color_hex(0x80FF80), 0);
        }
    }
}

// ============================================================================
// SETTINGS SCREEN
// ============================================================================
void ui_create_settings_screen() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, RED808_BG, 0);
    ui_create_header(scr_settings);

    // ── NETWORK Card (full width) ──
    lv_obj_t* net_card = lv_obj_create(scr_settings);
    lv_obj_set_size(net_card, 970, 130);
    lv_obj_set_pos(net_card, 30, 75);
    lv_obj_clear_flag(net_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(net_card, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(net_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(net_card, 12, 0);
    lv_obj_set_style_border_color(net_card, RED808_BORDER, 0);
    lv_obj_set_style_border_width(net_card, 1, 0);
    lv_obj_set_style_pad_all(net_card, 16, 0);

    lv_obj_t* net_icon = lv_label_create(net_card);
    lv_label_set_text(net_icon, LV_SYMBOL_WIFI " NETWORK");
    lv_obj_set_style_text_font(net_icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(net_icon, RED808_INFO, 0);
    lv_obj_set_pos(net_icon, 0, 0);

    // Left info block
    lv_obj_t* wifi_info = lv_label_create(net_card);
    lv_label_set_text_fmt(wifi_info,
        "SSID:    %s\n"
        "Master:  %s:%d",
        WiFiConfig::SSID,
        WiFiConfig::MASTER_IP, WiFiConfig::UDP_PORT);
    lv_obj_set_style_text_font(wifi_info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_info, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(wifi_info, 20, 40);
    lv_obj_set_style_text_line_space(wifi_info, 8, 0);

    // Right info block
    lv_obj_t* role_info = lv_label_create(net_card);
    lv_label_set_text_fmt(role_info,
        "Role:     SURFACE CONTROLLER\n"
        "Cores:    %d x %d MHz", 2, ESP.getCpuFreqMHz());
    lv_obj_set_style_text_font(role_info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(role_info, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(role_info, 480, 40);
    lv_obj_set_style_text_line_space(role_info, 8, 0);

    // ── THEME SELECTOR Section ──
    lv_obj_t* theme_card = lv_obj_create(scr_settings);
    lv_obj_set_size(theme_card, 970, 340);
    lv_obj_set_pos(theme_card, 30, 220);
    lv_obj_clear_flag(theme_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(theme_card, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(theme_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(theme_card, 12, 0);
    lv_obj_set_style_border_color(theme_card, RED808_BORDER, 0);
    lv_obj_set_style_border_width(theme_card, 1, 0);
    lv_obj_set_style_pad_all(theme_card, 16, 0);

    lv_obj_t* theme_title = lv_label_create(theme_card);
    lv_label_set_text(theme_title, LV_SYMBOL_EYE_OPEN " VISUAL THEME");
    lv_obj_set_style_text_font(theme_title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(theme_title, RED808_ACCENT, 0);
    lv_obj_set_pos(theme_title, 0, 0);

    // Theme buttons — each shows a color preview + name, flagged to resist restyling
    static const uint32_t btn_colors[THEME_COUNT] = {
        0xFF4444, 0x4A9EFF, 0x39FF14, 0xFF6B35, 0xFF00AA, 0x999999
    };
    int btn_w = 140;
    int btn_h = 220;
    int btn_gap = 12;
    int btn_x_start = (970 - 32 - THEME_COUNT * btn_w - (THEME_COUNT - 1) * btn_gap) / 2;

    for (int i = 0; i < THEME_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(theme_card);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, btn_x_start + i * (btn_w + btn_gap), 45);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_USER_1);  // protect from theme restyling
        lv_obj_set_style_bg_color(btn, lv_color_hex(theme_presets[i].bg), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, (i == currentTheme) ? 3 : 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(btn_colors[i]), 0);
        lv_obj_set_style_shadow_width(btn, 12, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(btn_colors[i]), 0);
        lv_obj_set_style_shadow_opa(btn, (i == currentTheme) ? LV_OPA_80 : LV_OPA_30, 0);

        // Color preview stripe at top
        lv_obj_t* stripe = lv_obj_create(btn);
        lv_obj_set_size(stripe, btn_w - 20, 8);
        lv_obj_align(stripe, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_add_flag(stripe, LV_OBJ_FLAG_USER_1);
        lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(stripe, lv_color_hex(btn_colors[i]), 0);
        lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(stripe, 4, 0);
        lv_obj_set_style_border_width(stripe, 0, 0);

        // 4 small color dots showing track palette preview
        for (int c = 0; c < 4; c++) {
            lv_obj_t* dot = lv_obj_create(btn);
            lv_obj_set_size(dot, 24, 24);
            lv_obj_set_pos(dot, 10 + c * 30, 32);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_USER_1);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(dot, lv_color_hex(theme_presets[i].track_colors[c * 4]), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        }

        // 4 more color dots — second row of palette preview
        for (int c = 0; c < 4; c++) {
            lv_obj_t* dot = lv_obj_create(btn);
            lv_obj_set_size(dot, 24, 24);
            lv_obj_set_pos(dot, 10 + c * 30, 62);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_USER_1);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(dot, lv_color_hex(theme_presets[i].track_colors[c * 4 + 2]), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        }

        // Theme name
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, theme_presets[i].name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(theme_presets[i].text), 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -14);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_USER_1);

        // Active indicator
        if (i == currentTheme) {
            lv_obj_t* check = lv_label_create(btn);
            lv_label_set_text(check, LV_SYMBOL_OK);
            lv_obj_set_style_text_font(check, &lv_font_montserrat_22, 0);
            lv_obj_set_style_text_color(check, lv_color_hex(btn_colors[i]), 0);
            lv_obj_align(check, LV_ALIGN_BOTTOM_MID, 0, -38);
            lv_obj_add_flag(check, LV_OBJ_FLAG_USER_1);
        }

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            ui_theme_apply((VisualTheme)idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

// ============================================================================
// DIAGNOSTICS SCREEN
// ============================================================================
#define DIAG_ROWS 17
static lv_obj_t* diag_labels[DIAG_ROWS];
static lv_obj_t* diag_values[DIAG_ROWS];

extern bool sd_mounted;  // from SD card screen

void ui_create_diagnostics_screen() {
    scr_diagnostics = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_diagnostics, RED808_BG, 0);
    ui_create_header(scr_diagnostics);

    lv_obj_t* title = lv_label_create(scr_diagnostics);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN " DIAGNOSTICS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 72);

    // Two-column layout
    // Left column (x=24): Hardware peripherals
    // Right column (x=520): ESP32 system info
    static const char* row_names[DIAG_ROWS] = {
        // Left column — hardware (rows 0-10)
        "WiFi",  "UDP",  "Touch GT911",  "LCD RGB",  "I2C Hub PCA9548A",
        "M5 Encoder #1", "M5 Encoder #2", "DFRobot Enc #1", "DFRobot Enc #2",
        "M5 ByteButton", "SD Card",
        // Right column — ESP32 system (rows 11-16)
        "CPU",  "Flash",  "PSRAM",  "Heap Free",  "PSRAM Free", "Uptime"
    };

    // Left column: rows 0-10
    int y = 108;
    int row_h = 32;
    for (int i = 0; i < 11; i++) {
        diag_labels[i] = lv_label_create(scr_diagnostics);
        lv_label_set_text(diag_labels[i], row_names[i]);
        lv_obj_set_style_text_font(diag_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(diag_labels[i], RED808_TEXT, 0);
        lv_obj_set_pos(diag_labels[i], 24, y);

        diag_values[i] = lv_label_create(scr_diagnostics);
        lv_label_set_text(diag_values[i], "---");
        lv_obj_set_style_text_font(diag_values[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(diag_values[i], RED808_TEXT_DIM, 0);
        lv_obj_set_pos(diag_values[i], 220, y);

        y += row_h;
    }

    // Separator
    lv_obj_t* sep = lv_obj_create(scr_diagnostics);
    lv_obj_set_size(sep, 2, 11 * row_h - 6);
    lv_obj_set_pos(sep, 498, 108);
    lv_obj_set_style_bg_color(sep, RED808_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_30, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // Right column title
    lv_obj_t* sys_title = lv_label_create(scr_diagnostics);
    lv_label_set_text(sys_title, "ESP32-S3 SYSTEM");
    lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sys_title, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_pos(sys_title, 520, 108 - 18);

    // Right column: rows 11-16
    y = 108;
    for (int i = 11; i < DIAG_ROWS; i++) {
        diag_labels[i] = lv_label_create(scr_diagnostics);
        lv_label_set_text(diag_labels[i], row_names[i]);
        lv_obj_set_style_text_font(diag_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(diag_labels[i], RED808_TEXT, 0);
        lv_obj_set_pos(diag_labels[i], 520, y);

        diag_values[i] = lv_label_create(scr_diagnostics);
        lv_label_set_text(diag_values[i], "---");
        lv_obj_set_style_text_font(diag_values[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(diag_values[i], RED808_TEXT_DIM, 0);
        lv_obj_set_pos(diag_values[i], 680, y);

        y += row_h;
    }

    // Fill static ESP32 info once
    lv_label_set_text_fmt(diag_values[11], "S3 %d MHz  Dual-Core", ESP.getCpuFreqMHz());
    lv_obj_set_style_text_color(diag_values[11], RED808_INFO, 0);
    lv_label_set_text_fmt(diag_values[12], "%lu KB  (%s)", ESP.getFlashChipSize() / 1024,
        ESP.getFlashChipMode() == FM_QIO ? "QIO" : ESP.getFlashChipMode() == FM_DIO ? "DIO" : "SPI");
    lv_obj_set_style_text_color(diag_values[12], RED808_INFO, 0);
    lv_label_set_text_fmt(diag_values[13], "%lu KB total", ESP.getPsramSize() / 1024);
    lv_obj_set_style_text_color(diag_values[13], RED808_INFO, 0);
}

void ui_update_diagnostics() {
    if (!scr_diagnostics) return;

    static bool prev_vals[DIAG_ROWS] = {};
    static uint32_t prev_heap = 0;
    static uint32_t prev_uptime = 0;
    static uint32_t last_sys_update_ms = 0;

    struct { bool val; const char* ok; const char* fail; } rows[] = {
        { diagInfo.wifiOk,       "Connected",     "Disconnected" },
        { diagInfo.udpConnected, "Port 8888 OK",  "Inactive" },
        { diagInfo.touchOk,      "OK (0x5D)",     "NOT FOUND" },
        { diagInfo.lcdOk,        "1024x600 OK",   "ERROR" },
        { diagInfo.i2cHubOk,     "OK (0x70)",     "NOT FOUND" },
        { diagInfo.m5encoder1Ok, "OK",            "Not detected" },
        { diagInfo.m5encoder2Ok, "OK",            "Not detected" },
        { diagInfo.dfrobot1Ok,   "OK",            "Not detected" },
        { diagInfo.dfrobot2Ok,   "OK",            "Not detected" },
        { diagInfo.byteButtonOk, "OK (0x41)",     "Not detected" },
        { diagInfo.sdOk || sd_mounted, sd_mounted ? "Mounted (SD_MMC)" : "Not mounted", "No card" },
    };

    for (int i = 0; i < 11; i++) {
        if (rows[i].val != prev_vals[i]) {
            prev_vals[i] = rows[i].val;
            lv_label_set_text(diag_values[i], rows[i].val ? rows[i].ok : rows[i].fail);
            lv_obj_set_style_text_color(diag_values[i], rows[i].val ? RED808_SUCCESS : RED808_ERROR, 0);
        }
    }

    // Update dynamic system info every 2s
    uint32_t now_ms = lv_tick_get();
    if ((now_ms - last_sys_update_ms) >= 2000) {
        last_sys_update_ms = now_ms;

        uint32_t heap = ESP.getFreeHeap() / 1024;
        if (heap != prev_heap) {
            prev_heap = heap;
            lv_label_set_text_fmt(diag_values[14], "%lu KB", heap);
            lv_obj_set_style_text_color(diag_values[14], heap > 100 ? RED808_SUCCESS : RED808_WARNING, 0);
        }

        uint32_t psram_free = ESP.getFreePsram() / 1024;
        lv_label_set_text_fmt(diag_values[15], "%lu KB", psram_free);
        lv_obj_set_style_text_color(diag_values[15], psram_free > 1000 ? RED808_SUCCESS : RED808_WARNING, 0);

        uint32_t uptime_s = millis() / 1000;
        if (uptime_s != prev_uptime) {
            prev_uptime = uptime_s;
            uint32_t h = uptime_s / 3600;
            uint32_t m = (uptime_s % 3600) / 60;
            uint32_t s = uptime_s % 60;
            lv_label_set_text_fmt(diag_values[16], "%02lu:%02lu:%02lu", h, m, s);
            lv_obj_set_style_text_color(diag_values[16], RED808_INFO, 0);
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
// SD CARD FILE BROWSER SCREEN
// Browse SD folder/file tree → select file → assign to pad → send loadSample
// UDP: {"cmd":"loadSample","family":"BD","filename":"BD_01.wav","pad":0}
// SD folder structure: /BD/BD_01.wav, /SD/..., etc.
// ============================================================================
#include <SD_MMC.h>
#include <FS.h>

// Layout constants
static constexpr int SD_LEFT_W  = 580;   // file browser panel width
static constexpr int SD_RIGHT_W = 420;   // pad assignment panel width
static constexpr int SD_TOP     = 60;    // below header
static constexpr int SD_H       = 540;   // panel height

// SD state
bool sd_mounted = false;
static bool sd_init_attempted = false;

// Navigation state
static char sd_current_dir[128] = "/";
static char sd_current_family[32] = "";
static char sd_current_file[64] = "";
static int  sd_selected_pad = 0;
static int  sd_file_scroll_offset = 0;

// UI widgets
static lv_obj_t* sd_left_panel  = NULL;
static lv_obj_t* sd_right_panel = NULL;
static lv_obj_t* sd_status_lbl  = NULL;
static lv_obj_t* sd_path_lbl    = NULL;
static lv_obj_t* sd_file_list   = NULL;  // scrollable container
static lv_obj_t* sd_selected_lbl = NULL;
static lv_obj_t* sd_pad_btns[Config::MAX_TRACKS] = {};
static lv_obj_t* sd_load_btn    = NULL;
static lv_obj_t* sd_load_lbl    = NULL;

// Forward declarations
static void sd_refresh_filelist();
static void sd_file_btn_cb(lv_event_t* e);
static void sd_pad_btn_cb(lv_event_t* e);
static void sd_load_btn_cb(lv_event_t* e);
static void sd_back_btn_cb(lv_event_t* e);

// Send loadSample UDP  (also callable from outside)
void ui_sdcard_send_load_sample(int pad, const char* family, const char* filename) {
    JsonDocument doc;
    doc["cmd"]      = "loadSample";
    doc["family"]   = family;
    doc["filename"] = filename;
    doc["pad"]      = pad;
    sendUDPCommand(doc);
    Serial.printf("[SD] LoadSample pad=%d family=%s file=%s\n", pad, family, filename);
}

// Mount SD card via SD_MMC (1-bit mode)
static bool sd_try_mount() {
    if (sd_mounted) return true;
    // Drive EXIO4 (D3/CS) HIGH so the card enters SDMMC mode (not SPI)
    io_ext_sd_disable();  // EXIO4=1 → D3 pulled high
    delay(10);
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
    if (!SD_MMC.begin(SD_MOUNT_POINT, true /* 1-bit mode */)) {
        Serial.println("[SD] Mount failed");
        return false;
    }
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] No SD card");
        SD_MMC.end();
        return false;
    }
    sd_mounted = true;
    Serial.printf("[SD] Mounted OK. Type=%d Size=%lluMB\n", cardType,
                  SD_MMC.cardSize() / (1024ULL * 1024ULL));
    return true;
}

// Populate file list from sd_current_dir
static void sd_refresh_filelist() {
    if (!sd_left_panel) return;
    if (!sd_file_list) return;

    // Clear existing items
    lv_obj_clean(sd_file_list);

    // Update path label
    if (sd_path_lbl) lv_label_set_text(sd_path_lbl, sd_current_dir);

    if (!sd_mounted) {
        lv_obj_t* lbl = lv_label_create(sd_file_list);
        lv_label_set_text(lbl, "SD NOT MOUNTED");
        lv_obj_set_style_text_color(lbl, RED808_ACCENT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        return;
    }

    // "Back" button if we're in a subfolder
    if (strcmp(sd_current_dir, "/") != 0) {
        lv_obj_t* back_btn = lv_btn_create(sd_file_list);
        lv_obj_set_size(back_btn, 540, 44);
        lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333344), 0);
        lv_obj_set_style_radius(back_btn, 6, 0);
        lv_obj_t* back_lbl = lv_label_create(back_btn);
        lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  .. (back)");
        lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xCCCCCC), 0);
        lv_obj_center(back_lbl);
        lv_obj_add_event_cb(back_btn, sd_back_btn_cb, LV_EVENT_CLICKED, NULL);
    }

    // List directory contents
    File dir = SD_MMC.open(sd_current_dir);
    if (!dir || !dir.isDirectory()) {
        lv_obj_t* lbl = lv_label_create(sd_file_list);
        lv_label_set_text(lbl, "Cannot open directory");
        lv_obj_set_style_text_color(lbl, RED808_WARNING, 0);
        return;
    }

    int count = 0;
    File entry = dir.openNextFile();
    while (entry && count < 80) {
        const char* name = entry.name();
        bool is_dir = entry.isDirectory();

        // Skip hidden files
        if (name[0] == '.') { entry.close(); entry = dir.openNextFile(); continue; }
        // Only show .wav/.WAV files and directories
        if (!is_dir) {
            size_t nlen = strlen(name);
            bool is_wav = (nlen > 4 &&
                           (strcasecmp(name + nlen - 4, ".wav") == 0));
            if (!is_wav) { entry.close(); entry = dir.openNextFile(); continue; }
        }

        lv_obj_t* btn = lv_btn_create(sd_file_list);
        lv_obj_set_size(btn, 540, 44);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_color_t btn_color = is_dir ? lv_color_hex(0x1A3A5C) : lv_color_hex(0x1A2A1A);
        lv_obj_set_style_bg_color(btn, btn_color, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x446688), LV_STATE_PRESSED);

        lv_obj_t* lbl = lv_label_create(btn);
        char display[80];
        snprintf(display, sizeof(display), is_dir ? LV_SYMBOL_DIRECTORY "  %s" : LV_SYMBOL_AUDIO "  %s", name);
        lv_label_set_text(lbl, display);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, is_dir ? RED808_CYAN : RED808_SUCCESS, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);

        // Store entry name in user data (heap allocated, freed on lv_obj_clean)
        char* name_copy = (char*)lv_mem_alloc(strlen(name) + 2);
        if (name_copy) {
            name_copy[0] = is_dir ? 'D' : 'F';  // D=dir, F=file prefix
            strcpy(name_copy + 1, name);
            lv_obj_set_user_data(btn, name_copy);
            lv_obj_add_event_cb(btn, sd_file_btn_cb, LV_EVENT_CLICKED, name_copy);
        }

        count++;
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    if (count == 0) {
        lv_obj_t* lbl = lv_label_create(sd_file_list);
        lv_label_set_text(lbl, "No files found (.wav)");
        lv_obj_set_style_text_color(lbl, RED808_TEXT_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    }
}

// Click on a directory → navigate into it; click on file → select it
static void sd_file_btn_cb(lv_event_t* e) {
    char* data = (char*)lv_event_get_user_data(e);
    if (!data) return;
    char type = data[0];
    const char* name = data + 1;

    if (type == 'D') {
        // Navigate into directory
        if (strcmp(sd_current_dir, "/") == 0) {
            snprintf(sd_current_dir, sizeof(sd_current_dir), "/%s", name);
            strncpy(sd_current_family, name, sizeof(sd_current_family) - 1);
            sd_current_family[sizeof(sd_current_family) - 1] = '\0';
        } else {
            // Subdir — append (not typical for drum machines but handle anyway)
            char tmp[128];
            snprintf(tmp, sizeof(tmp), "%s/%s", sd_current_dir, name);
            strncpy(sd_current_dir, tmp, sizeof(sd_current_dir) - 1);
        }
        sd_current_file[0] = '\0';
        sd_refresh_filelist();
        if (sd_selected_lbl) lv_label_set_text(sd_selected_lbl, "");
    } else {
        // Select file
        strncpy(sd_current_file, name, sizeof(sd_current_file) - 1);
        sd_current_file[sizeof(sd_current_file) - 1] = '\0';
        char sel[128];
        snprintf(sel, sizeof(sel), "%s / %s", sd_current_family, sd_current_file);
        if (sd_selected_lbl) lv_label_set_text(sd_selected_lbl, sel);
        if (sd_load_btn) lv_obj_clear_state(sd_load_btn, LV_STATE_DISABLED);
    }
}

// Back button: go up one level
static void sd_back_btn_cb(lv_event_t* e) {
    (void)e;
    // Go to root
    strncpy(sd_current_dir, "/", sizeof(sd_current_dir));
    sd_current_family[0] = '\0';
    sd_current_file[0] = '\0';
    if (sd_selected_lbl) lv_label_set_text(sd_selected_lbl, "");
    if (sd_load_btn) lv_obj_add_state(sd_load_btn, LV_STATE_DISABLED);
    sd_refresh_filelist();
}

// Select target pad (0-15)
static void sd_pad_btn_cb(lv_event_t* e) {
    int pad = (int)(intptr_t)lv_event_get_user_data(e);
    if (pad < 0 || pad >= Config::MAX_TRACKS) return;
    // Update highlight
    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        if (!sd_pad_btns[i]) continue;
        lv_color_t c = (i == pad) ? RED808_ACCENT : lv_color_hex(0x222233);
        lv_obj_set_style_bg_color(sd_pad_btns[i], c, 0);
    }
    sd_selected_pad = pad;
}

// Load button: send loadSample UDP
static void sd_load_btn_cb(lv_event_t* e) {
    (void)e;
    if (sd_current_file[0] == '\0' || sd_current_family[0] == '\0') return;
    ui_sdcard_send_load_sample(sd_selected_pad, sd_current_family, sd_current_file);
    // Visual feedback: flash load button
    if (sd_load_lbl) lv_label_set_text(sd_load_lbl, LV_SYMBOL_OK "  LOADED!");
    lv_timer_t* t = lv_timer_create([](lv_timer_t* t) {
        if (sd_load_lbl) lv_label_set_text(sd_load_lbl,
            LV_SYMBOL_UPLOAD "  LOAD TO PAD");
        lv_timer_del(t);
    }, 1200, NULL);
    (void)t;
}

void ui_create_sdcard_screen() {
    scr_sdcard = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sdcard, RED808_BG, 0);
    ui_create_header(scr_sdcard);

    // ── Left Panel: file browser ──────────────────────────────────────────
    sd_left_panel = lv_obj_create(scr_sdcard);
    lv_obj_set_size(sd_left_panel, SD_LEFT_W, SD_H);
    lv_obj_set_pos(sd_left_panel, 4, SD_TOP);
    lv_obj_set_style_bg_color(sd_left_panel, lv_color_hex(0x0D1520), 0);
    lv_obj_set_style_border_color(sd_left_panel, RED808_INFO, 0);
    lv_obj_set_style_border_width(sd_left_panel, 1, 0);
    lv_obj_set_style_radius(sd_left_panel, 8, 0);
    lv_obj_set_style_pad_all(sd_left_panel, 8, 0);
    lv_obj_clear_flag(sd_left_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Title row
    lv_obj_t* title_lbl = lv_label_create(sd_left_panel);
    lv_label_set_text(title_lbl, LV_SYMBOL_DRIVE "  SD CARD BROWSER");
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_lbl, RED808_CYAN, 0);
    lv_obj_set_pos(title_lbl, 8, 4);

    // Status label
    sd_status_lbl = lv_label_create(sd_left_panel);
    lv_label_set_text(sd_status_lbl, sd_mounted ? "READY" : "NO SD CARD");
    lv_obj_set_style_text_font(sd_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_status_lbl, sd_mounted ? RED808_SUCCESS : RED808_WARNING, 0);
    lv_obj_set_pos(sd_status_lbl, 420, 8);

    // Path label
    sd_path_lbl = lv_label_create(sd_left_panel);
    lv_label_set_text(sd_path_lbl, "/");
    lv_obj_set_style_text_font(sd_path_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_path_lbl, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(sd_path_lbl, 8, 30);

    // Scrollable file list container
    sd_file_list = lv_obj_create(sd_left_panel);
    lv_obj_set_size(sd_file_list, 556, 460);
    lv_obj_set_pos(sd_file_list, 4, 54);
    lv_obj_set_style_bg_opa(sd_file_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sd_file_list, 0, 0);
    lv_obj_set_style_pad_row(sd_file_list, 4, 0);
    lv_obj_set_style_pad_all(sd_file_list, 2, 0);
    lv_obj_set_flex_flow(sd_file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(sd_file_list, LV_DIR_VER);
    lv_obj_add_flag(sd_file_list, LV_OBJ_FLAG_SCROLLABLE);

    // ── Right Panel: pad assignment ───────────────────────────────────────
    sd_right_panel = lv_obj_create(scr_sdcard);
    lv_obj_set_size(sd_right_panel, SD_RIGHT_W, SD_H);
    lv_obj_set_pos(sd_right_panel, SD_LEFT_W + 8, SD_TOP);
    lv_obj_set_style_bg_color(sd_right_panel, lv_color_hex(0x0D1520), 0);
    lv_obj_set_style_border_color(sd_right_panel, RED808_ACCENT, 0);
    lv_obj_set_style_border_width(sd_right_panel, 1, 0);
    lv_obj_set_style_radius(sd_right_panel, 8, 0);
    lv_obj_set_style_pad_all(sd_right_panel, 8, 0);
    lv_obj_clear_flag(sd_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // "ASSIGN TO PAD" title
    lv_obj_t* assign_lbl = lv_label_create(sd_right_panel);
    lv_label_set_text(assign_lbl, "ASSIGN TO PAD");
    lv_obj_set_style_text_font(assign_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(assign_lbl, RED808_ACCENT, 0);
    lv_obj_set_pos(assign_lbl, 8, 4);

    // 4x4 pad grid (pads 0-15)
    int pad_w = 84, pad_h = 50, pad_gap = 6;
    int px_start = 16, py_start = 36;
    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        int col = i % 4;
        int row = i / 4;
        int px = px_start + col * (pad_w + pad_gap);
        int py = py_start + row * (pad_h + pad_gap);

        lv_obj_t* btn = lv_btn_create(sd_right_panel);
        lv_obj_set_size(btn, pad_w, pad_h);
        lv_obj_set_pos(btn, px, py);
        lv_obj_set_style_bg_color(btn, i == 0 ? RED808_ACCENT : lv_color_hex(0x222233), 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x444466), 0);

        lv_obj_t* num_lbl = lv_label_create(btn);
        char num_str[8];
        snprintf(num_str, sizeof(num_str), "%s\n%d", trackNames[i], i);
        lv_label_set_text(num_lbl, num_str);
        lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num_lbl, inst_colors[i], 0);
        lv_obj_set_style_text_align(num_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(num_lbl);

        sd_pad_btns[i] = btn;
        lv_obj_add_event_cb(btn, sd_pad_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    // Selected file label
    sd_selected_lbl = lv_label_create(sd_right_panel);
    lv_label_set_text(sd_selected_lbl, "");
    lv_obj_set_width(sd_selected_lbl, 390);
    lv_obj_set_style_text_font(sd_selected_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_selected_lbl, RED808_SUCCESS, 0);
    lv_obj_set_style_text_align(sd_selected_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(sd_selected_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(sd_selected_lbl, 12, 264);

    // LOAD button
    sd_load_btn = lv_btn_create(sd_right_panel);
    lv_obj_set_size(sd_load_btn, 380, 60);
    lv_obj_set_pos(sd_load_btn, 16, 320);
    lv_obj_set_style_bg_color(sd_load_btn, RED808_ACCENT, 0);
    lv_obj_set_style_bg_color(sd_load_btn, lv_color_hex(0x882200), LV_STATE_DISABLED);
    lv_obj_set_style_radius(sd_load_btn, 10, 0);
    lv_obj_add_state(sd_load_btn, LV_STATE_DISABLED);

    sd_load_lbl = lv_label_create(sd_load_btn);
    lv_label_set_text(sd_load_lbl, LV_SYMBOL_UPLOAD "  LOAD TO PAD");
    lv_obj_set_style_text_font(sd_load_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sd_load_lbl, lv_color_white(), 0);
    lv_obj_center(sd_load_lbl);
    lv_obj_add_event_cb(sd_load_btn, sd_load_btn_cb, LV_EVENT_CLICKED, NULL);

    // Mount SD card now (non-blocking approach — just try once)
    if (!sd_init_attempted) {
        sd_init_attempted = true;
        sd_mounted = sd_try_mount();
        if (sd_status_lbl) {
            lv_label_set_text(sd_status_lbl, sd_mounted ? "READY" : "NO SD CARD");
            lv_obj_set_style_text_color(sd_status_lbl,
                sd_mounted ? RED808_SUCCESS : RED808_WARNING, 0);
        }
    }

    // Populate file list
    sd_refresh_filelist();
}

void ui_update_sdcard() {
    // Nothing to animate; all interaction is event-driven
    (void)0;
}

// ============================================================================
// HW ASSIGN SCREEN - Button & Rotary function assignments
// ============================================================================

// Current HW assignment descriptions (read-only display of what each does)
static const char* const byteButtonAssignNames[] = {
    "Pattern --",
    "Pattern ++",
    "Pattern = 1",
    "Vol Mode SEQ/PAD",
    "FX Delay Toggle",
    "FX Flanger Toggle",
    "FX Compressor Toggle",
    "Clear ALL FX"
};

static const char* const dfRotaryAssignNames[] = {
    "Volume SEQ/PAD  |  BTN: Play/Stop",
    "BPM Tempo       |  BTN: Back/Menu"
};

static const char* const m5EncoderAssignNames[] = {
    "Mod 1 (Tracks 0-3): Vol + Mute",
    "Mod 2 (Tracks 4-7): Vol + Mute"
};

void ui_create_performance_screen() {
    scr_performance = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_performance, RED808_BG, 0);
    ui_create_header(scr_performance);

    lv_obj_t* title = lv_label_create(scr_performance);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  HW ASSIGN");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    int y = 86;
    int col1_x = 24, col2_x = 300;
    int section_gap = 8;

    // ── SECTION: ByteButton (8 buttons) ──
    lv_obj_t* bb_title = lv_label_create(scr_performance);
    lv_label_set_text(bb_title, "M5 BYTEBUTTON  (8 Buttons)");
    lv_obj_set_style_text_font(bb_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bb_title, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_pos(bb_title, col1_x, y);
    y += 24;

    for (int i = 0; i < 8; i++) {
        // Button number chip
        lv_obj_t* chip = lv_obj_create(scr_performance);
        lv_obj_set_size(chip, 42, 26);
        lv_obj_set_pos(chip, col1_x, y);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0x1A3050), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 4, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_border_color(chip, lv_color_hex(0x00D4FF), 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* num = lv_label_create(chip);
        lv_label_set_text_fmt(num, "B%d", i + 1);
        lv_obj_set_style_text_font(num, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num, lv_color_hex(0x00D4FF), 0);
        lv_obj_center(num);

        // Function label
        lv_obj_t* fn = lv_label_create(scr_performance);
        lv_label_set_text(fn, byteButtonAssignNames[i]);
        lv_obj_set_style_text_font(fn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(fn, RED808_TEXT, 0);
        lv_obj_set_pos(fn, col1_x + 52, y + 4);

        // In LIVE mode label (right side)
        if (i < Config::MAX_SAMPLES) {
            lv_obj_t* live_fn = lv_label_create(scr_performance);
            lv_label_set_text_fmt(live_fn, "LIVE: Pad %d (%s)", i + 1, trackNames[i]);
            lv_obj_set_style_text_font(live_fn, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(live_fn, RED808_TEXT_DIM, 0);
            lv_obj_set_pos(live_fn, col2_x, y + 5);
        }

        y += 30;
    }

    y += section_gap;

    // ── SECTION: DFRobot Rotary Encoders ──
    lv_obj_t* df_title = lv_label_create(scr_performance);
    lv_label_set_text(df_title, "DFROBOT ROTARY ENCODERS  (2x)");
    lv_obj_set_style_text_font(df_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(df_title, lv_color_hex(0x39FF14), 0);
    lv_obj_set_pos(df_title, col1_x, y);
    y += 24;

    for (int i = 0; i < 2; i++) {
        lv_obj_t* chip = lv_obj_create(scr_performance);
        lv_obj_set_size(chip, 42, 26);
        lv_obj_set_pos(chip, col1_x, y);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0x1A3020), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 4, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_border_color(chip, lv_color_hex(0x39FF14), 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* num = lv_label_create(chip);
        lv_label_set_text_fmt(num, "R%d", i + 1);
        lv_obj_set_style_text_font(num, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num, lv_color_hex(0x39FF14), 0);
        lv_obj_center(num);

        lv_obj_t* fn = lv_label_create(scr_performance);
        lv_label_set_text(fn, dfRotaryAssignNames[i]);
        lv_obj_set_style_text_font(fn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(fn, RED808_TEXT, 0);
        lv_obj_set_pos(fn, col1_x + 52, y + 4);

        y += 30;
    }

    y += section_gap;

    // ── SECTION: M5 ROTATE8 Encoders ──
    lv_obj_t* m5_title = lv_label_create(scr_performance);
    lv_label_set_text(m5_title, "M5 ROTATE8 ENCODERS  (2 modules x 8 enc)");
    lv_obj_set_style_text_font(m5_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(m5_title, lv_color_hex(0xFF6B35), 0);
    lv_obj_set_pos(m5_title, col1_x, y);
    y += 24;

    for (int i = 0; i < 2; i++) {
        lv_obj_t* chip = lv_obj_create(scr_performance);
        lv_obj_set_size(chip, 42, 26);
        lv_obj_set_pos(chip, col1_x, y);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0x301A0A), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 4, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_border_color(chip, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* num = lv_label_create(chip);
        lv_label_set_text_fmt(num, "M%d", i + 1);
        lv_obj_set_style_text_font(num, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num, lv_color_hex(0xFF6B35), 0);
        lv_obj_center(num);

        lv_obj_t* fn = lv_label_create(scr_performance);
        lv_label_set_text(fn, m5EncoderAssignNames[i]);
        lv_obj_set_style_text_font(fn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(fn, RED808_TEXT, 0);
        lv_obj_set_pos(fn, col1_x + 52, y + 4);

        y += 30;
    }

    // ── SECTION: Touch Screen ──
    y += section_gap;
    lv_obj_t* touch_title = lv_label_create(scr_performance);
    lv_label_set_text(touch_title, "CAPACITIVE TOUCH  GT911 (5-point)");
    lv_obj_set_style_text_font(touch_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(touch_title, lv_color_hex(0xFF00AA), 0);
    lv_obj_set_pos(touch_title, col1_x, y);
    y += 22;

    lv_obj_t* touch_fn = lv_label_create(scr_performance);
    lv_label_set_text(touch_fn, "UI Navigation + Live Pads + Sequencer Grid + All Screens");
    lv_obj_set_style_text_font(touch_fn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(touch_fn, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(touch_fn, col1_x + 52, y);
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

// ============================================================================
// BOOT SCREEN — TRON / 80s terminal intro animation
// ============================================================================

static void boot_dismiss_cb(lv_event_t* e) {
    (void)e;
    if (boot_timer) { lv_timer_del(boot_timer); boot_timer = NULL; }
    if (boot_cursor_lbl) lv_anim_del(boot_cursor_lbl, anim_opa_cb);
    nav_to(SCREEN_MENU, scr_menu);
}

static void boot_timer_cb(lv_timer_t* timer) {
    if (boot_state < kBootLineCount) {
        lv_obj_t* line = boot_text_lines[boot_state];
        if (line) {
            lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(line, LV_OPA_COVER, 0);
            // Last line in bright cyan
            if (boot_state == kBootLineCount - 1) {
                lv_obj_set_style_text_color(line, lv_color_hex(0x00D4FF), 0);
                lv_obj_set_style_text_font(line, &lv_font_montserrat_16, 0);
            }
            if (boot_state > 0 && boot_text_lines[boot_state - 1]) {
                lv_obj_set_style_opa(boot_text_lines[boot_state - 1], LV_OPA_70, 0);
            }
        }
        boot_state++;

        if (boot_state == kBootLineCount) {
            // All lines shown — stop timer, show cursor + tap-to-continue
            lv_timer_del(timer);
            boot_timer = NULL;
            if (boot_cursor_lbl) {
                lv_obj_clear_flag(boot_cursor_lbl, LV_OBJ_FLAG_HIDDEN);
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, boot_cursor_lbl);
                lv_anim_set_exec_cb(&a, anim_opa_cb);
                lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
                lv_anim_set_time(&a, 500);
                lv_anim_set_playback_time(&a, 500);
                lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
                lv_anim_start(&a);
            }
            if (boot_status_lbl) {
                lv_label_set_text(boot_status_lbl,
                    "[ BOOT COMPLETE ]    TOUCH ANYWHERE TO CONTINUE");
                lv_obj_set_style_text_opa(boot_status_lbl, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(boot_status_lbl, lv_color_hex(0x00D4FF), 0);
            }
            // Enable tap-to-dismiss on the screen
            lv_obj_add_event_cb(scr_boot, boot_dismiss_cb, LV_EVENT_PRESSED, NULL);
        }
    }
}

void ui_create_boot_screen() {
    scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, lv_color_hex(0x080810), 0);
    lv_obj_set_style_bg_opa(scr_boot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_boot, LV_OBJ_FLAG_SCROLLABLE);

    // ── Top accent line (cyan glow) ──
    lv_obj_t* top_line = lv_obj_create(scr_boot);
    lv_obj_set_size(top_line, 1024, 2);
    lv_obj_set_pos(top_line, 0, 0);
    lv_obj_set_style_bg_color(top_line, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_bg_opa(top_line, LV_OPA_60, 0);
    lv_obj_set_style_border_width(top_line, 0, 0);
    lv_obj_set_style_shadow_width(top_line, 12, 0);
    lv_obj_set_style_shadow_color(top_line, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_shadow_opa(top_line, LV_OPA_30, 0);
    lv_obj_clear_flag(top_line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Brand name (large, centered) ──
    lv_obj_t* brand = lv_label_create(scr_boot);
    lv_label_set_text(brand, "BLU808");
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(brand, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_letter_space(brand, 8, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 18);

    // ── Subtitle ──
    lv_obj_t* subtitle = lv_label_create(scr_boot);
    lv_label_set_text(subtitle, "SLAVE CONTROLLER  V6");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x39FF14), 0);
    lv_obj_set_style_text_letter_space(subtitle, 4, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 72);

    // ── Separator line ──
    lv_obj_t* sep = lv_obj_create(scr_boot);
    lv_obj_set_size(sep, 600, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x222233), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── POST lines (compact, left-aligned block) ──
    for (int i = 0; i < kBootLineCount; i++) {
        lv_obj_t* lbl = lv_label_create(scr_boot);
        lv_label_set_text(lbl, kBootLines[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x39FF14), 0);
        lv_obj_set_pos(lbl, 42, 110 + i * 27);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        boot_text_lines[i] = lbl;
    }

    // ── Blinking cursor ──
    boot_cursor_lbl = lv_label_create(scr_boot);
    lv_label_set_text(boot_cursor_lbl, "root@blu808:~$ _");
    lv_obj_set_style_text_font(boot_cursor_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(boot_cursor_lbl, lv_color_hex(0x39FF14), 0);
    lv_obj_set_pos(boot_cursor_lbl, 42, 110 + kBootLineCount * 27);
    lv_obj_add_flag(boot_cursor_lbl, LV_OBJ_FLAG_HIDDEN);

    // ── Right-side build info ──
    lv_obj_t* build_info = lv_label_create(scr_boot);
    lv_label_set_text(build_info,
        "BUILD " __DATE__ "\n"
        "ESP32-S3  N16R8\n"
        "PLATFORMIO  IDF 5.5\n"
        "LVGL 8.4  DIRECT");
    lv_obj_set_style_text_font(build_info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(build_info, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_opa(build_info, LV_OPA_30, 0);
    lv_obj_set_style_text_line_space(build_info, 4, 0);
    lv_obj_set_pos(build_info, 780, 110);

    // ── Bottom status bar ──
    boot_status_lbl = lv_label_create(scr_boot);
    lv_label_set_text(boot_status_lbl, "INITIALIZING...");
    lv_obj_set_style_text_font(boot_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(boot_status_lbl, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_opa(boot_status_lbl, LV_OPA_40, 0);
    lv_obj_align(boot_status_lbl, LV_ALIGN_BOTTOM_MID, 0, -12);

    // ── Bottom accent line ──
    lv_obj_t* bot_line = lv_obj_create(scr_boot);
    lv_obj_set_size(bot_line, 1024, 2);
    lv_obj_set_pos(bot_line, 0, 598);
    lv_obj_set_style_bg_color(bot_line, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_bg_opa(bot_line, LV_OPA_40, 0);
    lv_obj_set_style_border_width(bot_line, 0, 0);
    lv_obj_clear_flag(bot_line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    boot_state = 0;
    boot_timer = lv_timer_create(boot_timer_cb, 80, NULL);
}

// ============================================================================
// CIRCULAR SEQUENCER SCREEN
// ============================================================================

static void circ_track_btn_cb(lv_event_t* e) {
    int t = (int)(intptr_t)lv_event_get_user_data(e);
    selectedTrack = t;
}

static void circ_step_btn_cb(lv_event_t* e) {
    int s = (int)(intptr_t)lv_event_get_user_data(e);
    patterns[currentPattern].steps[selectedTrack][s] ^= 1;
}

void ui_create_seq_circle_screen() {
    scr_seq_circle = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_seq_circle, RED808_BG, 0);
    lv_obj_clear_flag(scr_seq_circle, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_seq_circle);

    // --- CIRCLE AREA (left half) ---
    // Decorative ring outline
    lv_obj_t* ring = lv_obj_create(scr_seq_circle);
    lv_coord_t ring_d = (lv_coord_t)(CIRC_R * 2 + CIRC_PAD + 8);
    lv_obj_set_size(ring, ring_d, ring_d);
    lv_obj_set_pos(ring, CIRC_CX - ring_d / 2, CIRC_CY - ring_d / 2);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_0, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Inner ring
    lv_obj_t* inner_ring = lv_obj_create(scr_seq_circle);
    lv_coord_t inn_d = (lv_coord_t)(CIRC_INNER_R * 2);
    lv_obj_set_size(inner_ring, inn_d, inn_d);
    lv_obj_set_pos(inner_ring, CIRC_CX - inn_d / 2, CIRC_CY - inn_d / 2);
    lv_obj_set_style_radius(inner_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(inner_ring, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(inner_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(inner_ring, 1, 0);
    lv_obj_set_style_border_color(inner_ring, lv_color_hex(0x181818), 0);
    lv_obj_clear_flag(inner_ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Center track info labels
    lbl_circ_info = lv_label_create(scr_seq_circle);
    lv_label_set_text_fmt(lbl_circ_info, "STEP  0 / %d", Config::MAX_STEPS);
    lv_obj_set_style_text_font(lbl_circ_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_circ_info, RED808_TEXT_DIM, 0);
    lv_obj_set_style_text_align(lbl_circ_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_circ_info, CIRC_CX - 60, CIRC_CY - 26);
    lv_obj_set_width(lbl_circ_info, 120);

    circ_track_name_lbl = lv_label_create(scr_seq_circle);
    lv_label_set_text(circ_track_name_lbl, trackNames[0]);
    lv_obj_set_style_text_font(circ_track_name_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(circ_track_name_lbl, inst_colors[0], 0);
    lv_obj_set_style_text_align(circ_track_name_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(circ_track_name_lbl, CIRC_CX - 60, CIRC_CY + 0);
    lv_obj_set_width(circ_track_name_lbl, 120);

    // 16 step buttons on the ring
    for (int s = 0; s < Config::MAX_STEPS; s++) {
        float angle = -(float)M_PI / 2.0f + s * 2.0f * (float)M_PI / (float)Config::MAX_STEPS;
        int cx = CIRC_CX + (int)(cosf(angle) * CIRC_R);
        int cy = CIRC_CY + (int)(sinf(angle) * CIRC_R);
        circ_step_cx[s] = cx;
        circ_step_cy[s] = cy;

        lv_obj_t* btn = lv_btn_create(scr_seq_circle);
        lv_obj_set_size(btn, CIRC_PAD, CIRC_PAD);
        lv_obj_set_pos(btn, cx - CIRC_PAD / 2, cy - CIRC_PAD / 2);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, RED808_SURFACE, 0);
        lv_obj_set_style_bg_color(btn, RED808_SURFACE, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, RED808_BORDER, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_event_cb(btn, circ_step_btn_cb, LV_EVENT_PRESSED, (void*)(intptr_t)s);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%d", s + 1);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, RED808_TEXT_DIM, 0);
        lv_obj_center(lbl);

        circ_step_btns[s] = btn;
    }

    // Playhead indicator — glowing dot that moves to the active step
    circ_playhead = lv_obj_create(scr_seq_circle);
    lv_obj_set_size(circ_playhead, CIRC_HEAD_SIZE, CIRC_HEAD_SIZE);
    lv_obj_set_style_radius(circ_playhead, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circ_playhead, RED808_WARNING, 0);
    lv_obj_set_style_bg_opa(circ_playhead, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circ_playhead, 0, 0);
    lv_obj_set_style_shadow_width(circ_playhead, 18, 0);
    lv_obj_set_style_shadow_color(circ_playhead, RED808_WARNING, 0);
    lv_obj_set_style_shadow_opa(circ_playhead, LV_OPA_80, 0);
    lv_obj_add_flag(circ_playhead, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(circ_playhead, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    // Place at top of ring initially
    lv_obj_set_pos(circ_playhead,
        circ_step_cx[0] - CIRC_HEAD_SIZE / 2,
        circ_step_cy[0] - CIRC_HEAD_SIZE / 2);

    // --- TRACK SELECTOR (right panel) ---
    // 16 track buttons in 2 columns of 8
    static constexpr int PANEL_X  = 555;
    static constexpr int BTN_W    = 200;
    static constexpr int BTN_H    = 28;
    static constexpr int BTN_GAP  = 6;
    static constexpr int COL_GAP  = 10;
    // Total height of one 8-button column
    static constexpr int COL_H    = 8 * BTN_H + 7 * BTN_GAP;  // 266
    // Vertically center in available area (y=78 to y=580 = 502px)
    static constexpr int Y_START  = 78 + (502 - COL_H) / 2;   // ~196

    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        int col = t / 8;
        int row = t % 8;
        int x   = PANEL_X + col * (BTN_W + COL_GAP);
        int y   = Y_START + row * (BTN_H + BTN_GAP);

        lv_obj_t* btn = lv_btn_create(scr_seq_circle);
        lv_obj_set_size(btn, BTN_W, BTN_H);
        lv_obj_set_pos(btn, x, y);
        bool sel = (t == selectedTrack);
        lv_obj_set_style_bg_color(btn, sel ? inst_colors[t] : RED808_SURFACE, 0);
        lv_obj_set_style_bg_color(btn, sel ? inst_colors[t] : RED808_SURFACE, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, sel ? inst_colors[t] : RED808_BORDER, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, LV_STATE_PRESSED);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%2d  %s", t + 1, trackNames[t]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, sel ? RED808_BG : inst_colors[t], 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, circ_track_btn_cb, LV_EVENT_PRESSED, (void*)(intptr_t)t);
        circ_track_btns[t] = btn;
    }
}

void ui_update_seq_circle() {
    if (!scr_seq_circle) return;

    static int prev_step  = -2;
    static int prev_track = -1;

    int active_step = (isPlaying && currentStep >= 0 && currentStep < Config::MAX_STEPS)
                      ? currentStep : -1;

    bool stepChanged  = (active_step != prev_step);
    bool trackChanged = (selectedTrack != prev_track);

    if (!stepChanged && !trackChanged) return;

    // --- Update playhead ---
    if (circ_playhead) {
        if (active_step >= 0) {
            lv_obj_set_pos(circ_playhead,
                circ_step_cx[active_step] - CIRC_HEAD_SIZE / 2,
                circ_step_cy[active_step] - CIRC_HEAD_SIZE / 2);
            lv_obj_clear_flag(circ_playhead, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(circ_playhead, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // --- Update center info ---
    if (lbl_circ_info && stepChanged) {
        lv_label_set_text_fmt(lbl_circ_info, "STEP %2d / %d",
            active_step >= 0 ? active_step + 1 : 0, Config::MAX_STEPS);
    }

    // --- Update step buttons ---
    for (int s = 0; s < Config::MAX_STEPS; s++) {
        if (!circ_step_btns[s]) continue;

        bool is_on      = patterns[currentPattern].steps[selectedTrack][s];
        bool is_current = (s == active_step);

        lv_color_t bg_col;
        lv_color_t brd_col;
        lv_coord_t brd_w;
        lv_coord_t shad_w;

        if (is_current) {
            bg_col  = RED808_WARNING;
            brd_col = lv_color_white();
            brd_w   = 3;
            shad_w  = 16;
        } else if (is_on) {
            bg_col  = inst_colors[selectedTrack];
            brd_col = inst_colors[selectedTrack];
            brd_w   = 1;
            shad_w  = 8;
        } else {
            bg_col  = RED808_SURFACE;
            brd_col = RED808_BORDER;
            brd_w   = 1;
            shad_w  = 0;
        }

        lv_obj_set_style_bg_color(circ_step_btns[s], bg_col, 0);
        lv_obj_set_style_border_width(circ_step_btns[s], brd_w, 0);
        lv_obj_set_style_border_color(circ_step_btns[s], brd_col, 0);
        lv_obj_set_style_shadow_width(circ_step_btns[s], shad_w, 0);
        if (shad_w > 0) {
            lv_obj_set_style_shadow_color(circ_step_btns[s],
                is_current ? RED808_WARNING : inst_colors[selectedTrack], 0);
            lv_obj_set_style_shadow_opa(circ_step_btns[s], LV_OPA_60, 0);
        }
    }

    // --- Update track selector buttons (only on track change) ---
    if (trackChanged) {
        for (int t = 0; t < Config::MAX_TRACKS; t++) {
            if (!circ_track_btns[t]) continue;
            bool sel = (t == selectedTrack);
            lv_obj_set_style_bg_color(circ_track_btns[t], sel ? inst_colors[t] : RED808_SURFACE, 0);
            lv_obj_set_style_border_color(circ_track_btns[t], sel ? inst_colors[t] : RED808_BORDER, 0);
            // update label color
            lv_obj_t* lbl = lv_obj_get_child(circ_track_btns[t], 0);
            if (lbl) lv_obj_set_style_text_color(lbl, sel ? RED808_BG : inst_colors[t], 0);
        }
        if (circ_track_name_lbl) {
            lv_obj_set_style_text_color(circ_track_name_lbl, inst_colors[selectedTrack], 0);
            lv_label_set_text(circ_track_name_lbl, trackNames[selectedTrack]);
        }
        prev_track = selectedTrack;
    }

    prev_step = active_step;
}
