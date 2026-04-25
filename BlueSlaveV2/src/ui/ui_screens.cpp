// =============================================================================
// ui_screens.cpp - Screen implementations with LVGL
// RED808 V6 BlueSlaveV2 - 1024x600 touch interface
// =============================================================================
#include "ui_screens.h"
#include "ui_theme.h"
#include "../../include/system_state.h"
#include "../../include/config.h"
#include "../drivers/io_extension.h"
#include "../sd_midi_loader.h"
#include <Esp.h>
#include <ArduinoJson.h>
#include <math.h>

extern void sendUDPCommand(JsonDocument& doc);
extern void sendUDPCommand(const char* cmd);
extern void sendFilterUDP(int track, int fxType);
extern void updateByteButtonLeds();
extern uint8_t dfFxParamMode[];
extern int dfFxParamValue[];
extern bool analogFxMuted[];
extern int get_sequencer_swing_percent();
extern int get_master_drive_percent();
extern void apply_mpc_preset(bool saveToNvs);

// UART bridge relay (for when S3 WiFi is off — P4 forwards to Master)
extern void uart_bridge_send(uint8_t type, uint8_t id, uint8_t value);
#include "../../include/uart_protocol.h"

// Use SRAM allocator from main.cpp — avoids PSRAM bus contention with LCD DMA
extern ArduinoJson::Allocator* sramAllocatorPtr;

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
static constexpr uint8_t HEADER_SLOT_COUNT = SCREEN_COUNT;
static lv_obj_t* lbl_seq_volume[HEADER_SLOT_COUNT] = {};
static lv_obj_t* chip_seq_volume[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_bpm[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_pattern[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_play[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_wifi[HEADER_SLOT_COUNT] = {};
static lv_obj_t* lbl_fx_values[HEADER_SLOT_COUNT] = {};  // legacy — unused in footer
static lv_obj_t* lbl_master_conn[HEADER_SLOT_COUNT] = {};
static lv_obj_t* chip_master_conn[HEADER_SLOT_COUNT] = {};

// Menu status card labels
static lv_obj_t* lbl_master = NULL;
static lv_obj_t* lbl_menu_state = NULL;
static lv_obj_t* lbl_menu_transport = NULL;

// Status center runtime panel
static lv_obj_t* diag_runtime_values[12] = {};
// diag_event_labels removed (EVENT LOG disabled)

static lv_obj_t* perf_runtime_values[8] = {};
static lv_obj_t* perf_bb_action_labels[BYTEBUTTON_TOTAL_BUTTONS] = {};

// diag_event_log removed (EVENT LOG disabled)

// Sequencer grid buttons — sized to STEPS_PER_BANK (16 visible cols at once)
static lv_obj_t* seq_grid[Config::MAX_TRACKS][Config::STEPS_PER_BANK];
static lv_obj_t* seq_track_labels[Config::MAX_TRACKS];
static lv_obj_t* seq_track_panels[Config::MAX_TRACKS];
static lv_obj_t* seq_solo_btns[Config::MAX_TRACKS];
static lv_obj_t* seq_mute_btns[Config::MAX_TRACKS];
static lv_obj_t* lbl_step_indicator = NULL;
static lv_obj_t* seq_step_labels[Config::STEPS_PER_BANK] = {};
static lv_obj_t* seq_track_meta_labels[Config::MAX_TRACKS] = {};
static lv_obj_t* seq_track_volume_labels[Config::MAX_TRACKS] = {};
static lv_obj_t* seq_info_pattern = NULL;
static lv_obj_t* seq_info_track = NULL;
static lv_obj_t* seq_info_transport = NULL;
static lv_obj_t* seq_info_page = NULL;
static lv_obj_t* seq_column_highlights[Config::STEPS_PER_BANK];
static lv_obj_t* seq_step_leds[Config::STEPS_PER_BANK];
static lv_obj_t* seq_playhead_line = NULL;   // Lightweight moving line indicator
static int seq_page = 0;
static int seq_step_bank = 0;  // which 16-step bank is visible (0=steps 0-15, 1=16-31, ...)
// Cached grid geometry for playhead positioning
static int seq_cached_grid_x = 0;
static int seq_cached_grid_y = 0;
static int seq_cached_cell_w = 0;
static int seq_cached_gap    = 0;
static int seq_cached_grid_h = 0;
static constexpr int SEQ_TRACKS_PER_PAGE = 8;
static lv_obj_t* seq_page_btn = NULL;
static lv_obj_t* seq_page_lbl = NULL;
static lv_obj_t* seq_page_prev_btn = NULL;
static lv_obj_t* seq_page_next_btn = NULL;
static lv_obj_t* seq_bank_lbl = NULL;       // "BAR 1/2" label
static lv_obj_t* seq_bank_prev_btn = NULL;
static lv_obj_t* seq_bank_next_btn = NULL;
static lv_obj_t* seq_play_btn = NULL;
static lv_obj_t* seq_play_lbl = NULL;
static lv_obj_t* seq_unmute_btn = NULL;
static lv_obj_t* seq_mpc_btn = NULL;
static lv_obj_t* seq_mpc_lbl = NULL;

// Volume sliders
static lv_obj_t* vol_strip_panels[Config::MAX_TRACKS] = {};
static lv_obj_t* vol_sliders[Config::MAX_TRACKS];
static lv_obj_t* vol_labels[Config::MAX_TRACKS];

// Filter UI
static lv_obj_t* filter_arcs[3];
static lv_obj_t* filter_labels[3];
static lv_obj_t* filter_value_labels[3];
static lv_obj_t* filter_toggle_btns[3] = {};
static lv_obj_t* filter_toggle_labels[3] = {};
static lv_obj_t* filter_legacy_sliders[3] = {};
static lv_obj_t* filter_type_btns[5] = {};
static lv_obj_t* filter_type_labels[5] = {};
static lv_obj_t* filter_bit_btns[4] = {};
static lv_obj_t* filter_bit_labels[4] = {};
static lv_obj_t* filter_ext_sliders[4] = {};
static lv_obj_t* filter_ext_value_labels[4] = {};
static lv_obj_t* filter_fun_status = NULL;
static lv_obj_t* filter_master_tag = NULL;
static lv_obj_t* filter_target_prev_btn = NULL;
static lv_obj_t* filter_target_next_btn = NULL;
static lv_obj_t* filter_target_label = NULL;
static lv_obj_t* filter_preset_label = NULL;  // rotary theme indicator
static lv_obj_t* filter_df_mode_labels[6] = {};
static lv_obj_t* filter_df_value_labels[6] = {};
static lv_obj_t* filter_df_unit_labels[6] = {};
static lv_obj_t* filter_scene_value_labels[3] = {};
static lv_obj_t* filter_grid_arcs[6] = {};
static lv_obj_t* filter_grid_mute_labels[6] = {};
static lv_obj_t* filter_scene_cell_title = NULL;
static lv_obj_t* filter_resp_label = NULL;
static lv_obj_t* filter_resp_btns[3] = {};
static lv_obj_t* filter_resp_btn_labels[3] = {};

// Live pads
static lv_obj_t* live_pads[Config::MAX_SAMPLES];
static lv_obj_t* live_pad_names[Config::MAX_SAMPLES];
static lv_obj_t* live_pad_desc[Config::MAX_SAMPLES];
static lv_obj_t* live_pad_accents[Config::MAX_SAMPLES];
static lv_obj_t* live_pad_glows[Config::MAX_SAMPLES];
static lv_coord_t live_pad_x[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_y[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_w[Config::MAX_SAMPLES] = {};
static lv_coord_t live_pad_h[Config::MAX_SAMPLES] = {};

static constexpr int LIVE_PAD_COLS = 4;
static constexpr int LIVE_PAD_GAP = 12;
static constexpr int LIVE_PAD_AREA_TOP = 4;
static constexpr int LIVE_VIEW_MODE_COUNT = 5;
static const uint8_t live_view_options[LIVE_VIEW_MODE_COUNT] = {16, 8, 4, 2, 1};
static lv_obj_t* live_view_btns[LIVE_VIEW_MODE_COUNT] = {};
static lv_obj_t* live_view_labels[LIVE_VIEW_MODE_COUNT] = {};
static lv_obj_t* live_view_page_label = NULL;
static lv_obj_t* live_view_prev_btn = NULL;
static lv_obj_t* live_view_next_btn = NULL;
static uint8_t livePadVisibleCount = 16;
static uint8_t livePadViewStart = 0;

// Ratchet controls (repeat count 1-16)
static lv_obj_t* live_ratchet_label = NULL;

// LivePad sequencer sync
static bool livePadSyncMode = false;
static lv_obj_t* live_sync_btn = NULL;
static lv_obj_t* live_sync_lbl = NULL;

static int live_pad_visible_index_count() {
    return constrain((int)livePadVisibleCount, 1, Config::MAX_SAMPLES);
}

static int live_pad_block_count() {
    int visible = live_pad_visible_index_count();
    return (Config::MAX_SAMPLES + visible - 1) / visible;
}

static void live_update_view_controls() {
    for (int i = 0; i < LIVE_VIEW_MODE_COUNT; i++) {
        if (!live_view_btns[i] || !live_view_labels[i]) continue;
        bool selected = live_view_options[i] == livePadVisibleCount;
        lv_color_t color = selected ? RED808_ACCENT : RED808_SURFACE;
        lv_obj_set_style_bg_color(live_view_btns[i], color, 0);
        lv_obj_set_style_border_color(live_view_btns[i], selected ? RED808_ACCENT : RED808_BORDER, 0);
        lv_obj_set_style_text_color(live_view_labels[i], selected ? RED808_BG : RED808_TEXT, 0);
    }

    if (live_view_page_label) {
        int visible = live_pad_visible_index_count();
        int page = (livePadViewStart / visible) + 1;
        int pages = live_pad_block_count();
        if (pages <= 1) {
            lv_label_set_text(live_view_page_label, "ALL");
        } else {
            lv_label_set_text_fmt(live_view_page_label, "%d/%d", page, pages);
        }
    }

    if (live_view_prev_btn) {
        lv_obj_add_flag(live_view_prev_btn, live_pad_block_count() <= 1 ? LV_OBJ_FLAG_HIDDEN : 0);
        if (live_pad_block_count() > 1) lv_obj_clear_flag(live_view_prev_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (live_view_next_btn) {
        lv_obj_add_flag(live_view_next_btn, live_pad_block_count() <= 1 ? LV_OBJ_FLAG_HIDDEN : 0);
        if (live_pad_block_count() > 1) lv_obj_clear_flag(live_view_next_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void live_layout_pads() {
    int visible = live_pad_visible_index_count();
    if (visible <= 0) return;

    if (livePadViewStart >= Config::MAX_SAMPLES) livePadViewStart = 0;
    livePadViewStart = (livePadViewStart / visible) * visible;

    const int left_panel_w = 76;
    const int left_panel_x = 12;
    const int right_panel_w = 88;
    const int right_panel_margin = 12;
#if PORTRAIT_MODE
    const int grid_left = 12;
    const int grid_right = UI_W - 12;
    const int grid_width = grid_right - grid_left;
    const int ctrl_zone = 210;
    const int grid_height = UI_H - 72 - LIVE_PAD_AREA_TOP - ctrl_zone;
#else
    const int grid_left = left_panel_x + left_panel_w + 12;
    const int grid_right = 1024 - right_panel_w - right_panel_margin;
    const int grid_width = grid_right - grid_left;
    const int grid_height = 508 - LIVE_PAD_AREA_TOP;
#endif

    int cols = 4;
    int rows = 4;
    switch (visible) {
        case 16: cols = 4; rows = 4; break;
        case 8:  cols = 4; rows = 2; break;
        case 4:  cols = 2; rows = 2; break;
        case 2:  cols = 2; rows = 1; break;
        case 1:  cols = 1; rows = 1; break;
        default: cols = min(4, visible); rows = (visible + cols - 1) / cols; break;
    }

    lv_coord_t pad_w = (grid_width - (cols - 1) * LIVE_PAD_GAP) / cols;
    lv_coord_t pad_h = (grid_height - (rows - 1) * LIVE_PAD_GAP) / rows;

    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        bool visibleNow = pad >= livePadViewStart && pad < min<int>(Config::MAX_SAMPLES, livePadViewStart + visible);
        if (!live_pads[pad]) continue;

        if (!visibleNow) {
            lv_obj_add_flag(live_pads[pad], LV_OBJ_FLAG_HIDDEN);
            live_pad_x[pad] = -1;
            live_pad_y[pad] = -1;
            live_pad_w[pad] = 0;
            live_pad_h[pad] = 0;
            continue;
        }

        int localIndex = pad - livePadViewStart;
        int col = localIndex % cols;
        int row = localIndex / cols;
        lv_coord_t x = grid_left + col * (pad_w + LIVE_PAD_GAP);
        lv_coord_t y = LIVE_PAD_AREA_TOP + row * (pad_h + LIVE_PAD_GAP);

        lv_obj_clear_flag(live_pads[pad], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(live_pads[pad], pad_w, pad_h);
        lv_obj_set_pos(live_pads[pad], x, y);
        live_pad_x[pad] = x;
        live_pad_y[pad] = y;
        live_pad_w[pad] = pad_w;
        live_pad_h[pad] = pad_h;

        if (live_pad_accents[pad]) {
            lv_obj_set_size(live_pad_accents[pad], 6, pad_h - 16);
            lv_obj_set_pos(live_pad_accents[pad], 8, 8);
        }
        if (live_pad_names[pad]) {
            lv_obj_center(live_pad_names[pad]);
        }
        if (live_pad_desc[pad]) {
            lv_obj_set_pos(live_pad_desc[pad], 22, pad_h - 28);
        }
        if (live_pad_glows[pad]) {
            lv_obj_set_size(live_pad_glows[pad], pad_w - 24, 3);
            lv_obj_set_pos(live_pad_glows[pad], 12, pad_h - 8);
        }
    }

    live_update_view_controls();
}

static void live_view_prev_cb(lv_event_t* e) {
    (void)e;
    int visible = live_pad_visible_index_count();
    if (visible >= Config::MAX_SAMPLES) return;
    if (livePadViewStart >= visible) livePadViewStart -= visible;
    else livePadViewStart = (live_pad_block_count() - 1) * visible;
    live_layout_pads();
    ui_live_pads_invalidate();
}

static void live_view_next_cb(lv_event_t* e) {
    (void)e;
    int visible = live_pad_visible_index_count();
    if (visible >= Config::MAX_SAMPLES) return;
    livePadViewStart += visible;
    if (livePadViewStart >= Config::MAX_SAMPLES) livePadViewStart = 0;
    live_layout_pads();
    ui_live_pads_invalidate();
}

static void live_view_mode_cb(lv_event_t* e) {
    int count = (int)(intptr_t)lv_event_get_user_data(e);
    if (count <= 0) return;
    if (livePadVisibleCount == count) {
        live_view_next_cb(e);
        return;
    }
    livePadVisibleCount = (uint8_t)count;
    livePadViewStart = 0;
    live_layout_pads();
    ui_live_pads_invalidate();
}

static uint32_t last_nav_ms = 0;

static const char* screen_name(Screen screen) {
    switch (screen) {
        case SCREEN_BOOT: return "BOOT";
        case SCREEN_MENU: return "MENU";
        case SCREEN_LIVE: return "LIVE";
        case SCREEN_SEQUENCER: return "SEQUENCER";
        case SCREEN_SETTINGS: return "SETTINGS";
        case SCREEN_DIAGNOSTICS: return "STATUS";
        case SCREEN_PATTERNS: return "PATTERNS";
        case SCREEN_VOLUMES: return "VOLUMES";
        case SCREEN_FILTERS: return "FILTERS";
        case SCREEN_SDCARD: return "SDCARD";
        case SCREEN_PERFORMANCE: return "BUTTONS";
        case SCREEN_SAMPLES: return "SAMPLES";
        case SCREEN_SEQ_CIRCLE: return "SEQ CIRCLE";
        default: return "UNKNOWN";
    }
}

static void ui_runtime_note(const char*) { /* removed for performance */ }

// ============================================================================
// BOOT SCREEN (TRON / 80s terminal intro)
// ============================================================================
static constexpr int kBootLineCount = 16;
static lv_obj_t* boot_text_lines[kBootLineCount] = {};
static lv_obj_t* boot_cursor_lbl = NULL;
static lv_obj_t* boot_status_lbl = NULL;
static lv_obj_t* boot_progress_fill = NULL;
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
    "[BTN ]  BYTEBUTTON 8x2 MATRIX  I2C 0x47.......... OK",
    "[SDIO]  MMC 1-BIT  CLK:12 CMD:11 D0:13........... OK",
    "[WLAN]  802.11b/g/n STA  AP RED808  CH AUTO....... OK",
    "[UDP ]  PORT 8888  MASTER 192.168.4.1............. OK",
    "[SEQ ]  16 TRACKS x 16 STEPS  64 PATTERNS........ OK",
    "\n> BLUE808SLAVE CONTROLLER V6 ............... READY",
};

// ============================================================================
// CIRCULAR SEQUENCER — always shows STEPS_PER_BANK (16) steps
// ============================================================================
static lv_obj_t* circ_step_btns[Config::STEPS_PER_BANK] = {};
static lv_obj_t* circ_playhead = NULL;
static lv_obj_t* circ_track_name_lbl = NULL;
static lv_obj_t* circ_track_btns[Config::MAX_TRACKS] = {};
static lv_obj_t* lbl_circ_info = NULL;
static int circ_step_cx[Config::STEPS_PER_BANK];
static int circ_step_cy[Config::STEPS_PER_BANK];
#if PORTRAIT_MODE
static constexpr int CIRC_CX = UI_W / 2;       // 300
static constexpr int CIRC_CY = 340;
#else
static constexpr int CIRC_CX = 295;
static constexpr int CIRC_CY = 250;
#endif
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

static lv_obj_t* create_info_chip(lv_obj_t* parent, const lv_color_t accent_color, lv_obj_t** out_label) {
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_set_width(chip, LV_SIZE_CONTENT);
    lv_obj_set_height(chip, 30);
    lv_obj_set_style_radius(chip, 6, 0);
    lv_obj_set_style_bg_color(chip, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_border_side(chip, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(chip, 3, 0);
    lv_obj_set_style_border_color(chip, accent_color, 0);
    lv_obj_set_style_pad_left(chip, 10, 0);
    lv_obj_set_style_pad_right(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 4, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, accent_color, 0);
    lv_obj_center(label);
    if (out_label) {
        *out_label = label;
    }
    return chip;
}

// P4-style status tile: title (top, dim) + value (bottom, colored), border-bottom colored
static lv_obj_t* create_status_tile(lv_obj_t* parent, const char* title, lv_color_t color, lv_obj_t** out_val) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_set_size(tile, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_min_width(tile, 80, 0);
    lv_obj_set_style_radius(tile, 8, 0);
    lv_obj_set_style_bg_color(tile, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_border_side(tile, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(tile, 3, 0);
    lv_obj_set_style_border_color(tile, color, 0);
    lv_obj_set_style_pad_hor(tile, 8, 0);
    lv_obj_set_style_pad_top(tile, 3, 0);
    lv_obj_set_style_pad_bottom(tile, 3, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(tile);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t, RED808_TEXT_DIM, 0);
    lv_obj_set_width(t, LV_PCT(100));
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t* v = lv_label_create(tile);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(v, color, 0);
    lv_obj_set_width(v, LV_PCT(100));
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, -2);

    if (out_val) *out_val = v;
    return tile;
}

static lv_obj_t* create_section_shell(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* shell = lv_obj_create(parent);
    lv_obj_set_pos(shell, x, y);
    lv_obj_set_size(shell, w, h);
    lv_obj_set_style_bg_color(shell, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(shell, LV_OPA_90, 0);
    lv_obj_set_style_border_width(shell, 1, 0);
    lv_obj_set_style_border_color(shell, RED808_BORDER, 0);
    lv_obj_set_style_radius(shell, 18, 0);
    lv_obj_set_style_pad_all(shell, 14, 0);
    lv_obj_clear_flag(shell, LV_OBJ_FLAG_SCROLLABLE);
    return shell;
}

// Zero-duration transition to kill theme animations that leave buttons in
// intermediate colours after a fast press-navigate-back cycle.
static lv_style_transition_dsc_t _no_tr;
static bool _no_tr_init = false;

static void apply_stable_button_style(lv_obj_t* obj, lv_color_t base_color, lv_color_t border_color) {
    if (!obj) return;

    // Lazy-init: create a transition descriptor with 0 duration (no animation)
    if (!_no_tr_init) {
        static const lv_style_prop_t _no_props[] = { (lv_style_prop_t)0 };
        lv_style_transition_dsc_init(&_no_tr, _no_props, lv_anim_path_linear, 0, 0, NULL);
        _no_tr_init = true;
    }

    // Cover ALL possible states so the LVGL default theme never shows through.
    static const lv_state_t states[] = {
        0,
        LV_STATE_PRESSED,
        LV_STATE_FOCUSED,
        LV_STATE_FOCUS_KEY,
        LV_STATE_CHECKED,
        LV_STATE_PRESSED | LV_STATE_FOCUSED,
        LV_STATE_PRESSED | LV_STATE_CHECKED,
    };
    for (auto st : states) {
        lv_obj_set_style_bg_color(obj, base_color, st);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, st);
        lv_obj_set_style_border_color(obj, border_color, st);
        lv_obj_set_style_shadow_width(obj, 0, st);
        lv_obj_set_style_outline_width(obj, 0, st);
        lv_obj_set_style_transform_width(obj, 0, st);
        lv_obj_set_style_transform_height(obj, 0, st);
        lv_obj_set_style_translate_x(obj, 0, st);
        lv_obj_set_style_translate_y(obj, 0, st);
        lv_obj_set_style_transition(obj, &_no_tr, st);
    }
    // Kill any gradient the theme may sneak in
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
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
        resetLivePadRuntimeState();
        // Sync ByteButton edge-detection state: assume all buttons "were pressed"
        // so no phantom edge fires on first handleByteButton() after entering LIVE.
        for (int m = 0; m < BYTEBUTTON_COUNT; m++) prevByteButtonState[m] = 0xFF;
        memset(byteButtonLivePressed, 0, sizeof(bool) * BYTEBUTTON_TOTAL_BUTTONS);
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

static void set_bpm_label_text(lv_obj_t* label, const char* prefix) {
    if (!label) return;
    int bpm10 = (int)lroundf(currentBPMPrecise * 10.0f);
    int whole = bpm10 / 10;
    int frac = bpm10 % 10;
    if (frac < 0) frac = -frac;
    lv_label_set_text_fmt(label, "%s %d.%d", prefix, whole, frac);
}

void ui_create_header(lv_obj_t* parent) {
    Screen screen = screen_from_parent(parent);
    uint8_t slot = static_cast<uint8_t>(screen);

    lv_obj_t* header = lv_obj_create(parent);
#if PORTRAIT_MODE
    lv_obj_set_size(header, UI_W - 16, 60);
    lv_obj_set_pos(header, 8, UI_H - 66);
#else
    lv_obj_set_size(header, UI_W - 16, 80);
    lv_obj_set_pos(header, 8, UI_H - 86);
#endif
    lv_obj_set_style_bg_color(header, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_border_color(header, RED808_ACCENT, 0);
    lv_obj_set_style_radius(header, 12, 0);
    lv_obj_set_style_pad_left(header, 8, 0);
    lv_obj_set_style_pad_right(header, 8, 0);
    lv_obj_set_style_pad_top(header, 4, 0);
    lv_obj_set_style_pad_bottom(header, 4, 0);
    lv_obj_set_style_shadow_width(header, 8, 0);
    lv_obj_set_style_shadow_color(header, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(header, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(header, -2, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(header, 6, 0);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Back button (shown on all screens except menu)
    if (parent != scr_menu) {
        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 90, 50);
        apply_stable_button_style(btn_back, RED808_SURFACE, RED808_ACCENT);
        lv_obj_set_style_radius(btn_back, 8, 0);
        lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_t* bl = lv_label_create(btn_back);
        lv_label_set_text(bl, LV_SYMBOL_LEFT " BACK");
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(bl, RED808_TEXT, 0);
        lv_obj_center(bl);
    }

    // HEADER ROW: 5 status tiles (P4-style — title + value, border-bottom colored)
    lv_obj_t* status_top = lv_obj_create(header);
    lv_obj_set_height(status_top, 56);
    lv_obj_set_style_bg_opa(status_top, LV_OPA_0, 0);
    lv_obj_set_style_border_width(status_top, 0, 0);
    lv_obj_set_style_pad_all(status_top, 0, 0);
    lv_obj_set_style_pad_gap(status_top, 6, 0);
    lv_obj_clear_flag(status_top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(status_top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_top, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(status_top, 1);

    // BPM tile
    lv_obj_t* tile_bpm = create_status_tile(status_top, "BPM", RED808_WARNING, &lbl_bpm[slot]);
    { int b10 = (int)lroundf(currentBPMPrecise * 10.0f); lv_label_set_text_fmt(lbl_bpm[slot], "%d.%d", b10/10, abs(b10%10)); }
    lv_obj_set_flex_grow(tile_bpm, 1);

    // PLAY tile — reuse chip_seq_volume slot for tile ref (to update border color)
    chip_seq_volume[slot] = create_status_tile(status_top, "PLAY", isPlaying ? RED808_SUCCESS : RED808_ERROR, &lbl_play[slot]);
    lv_label_set_text(lbl_play[slot], isPlaying ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP);
    lv_obj_set_flex_grow(chip_seq_volume[slot], 1);

    // VOL tile
    lv_obj_t* tile_vol = create_status_tile(status_top, "VOL", RED808_ACCENT, &lbl_seq_volume[slot]);
    lv_label_set_text_fmt(lbl_seq_volume[slot], "%d", masterVolume);
    lv_obj_set_flex_grow(tile_vol, 1);

    // PTN tile
    lv_obj_t* tile_ptn = create_status_tile(status_top, "PTN", RED808_INFO, &lbl_pattern[slot]);
    lv_label_set_text_fmt(lbl_pattern[slot], "%d", currentPattern + 1);
    lv_obj_set_flex_grow(tile_ptn, 1);

    // LINK P4 tile
    chip_master_conn[slot] = create_status_tile(status_top, "LINK P4", masterConnected ? RED808_SUCCESS : RED808_ERROR, &lbl_master_conn[slot]);
    lv_label_set_text(lbl_master_conn[slot], masterConnected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_flex_grow(chip_master_conn[slot], 1);
}

void ui_update_header() {
    static int prev_bpm10 = -1;
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
        prev_bpm10 = -1;
        prev_pattern = -1;
        prev_playing = -1;
        prev_wifi = -1;
    }

    int bpm10 = (int)lroundf(currentBPMPrecise * 10.0f);
    if (bpm10 != prev_bpm10) {
        prev_bpm10 = bpm10;
        if (lbl_bpm[slot]) {
            lv_label_set_text_fmt(lbl_bpm[slot], "%d.%d", bpm10/10, abs(bpm10%10));
        }
    }
    if (currentPattern != prev_pattern) {
        prev_pattern = currentPattern;
        if (lbl_pattern[slot]) {
            lv_label_set_text_fmt(lbl_pattern[slot], "%d", currentPattern + 1);
        }
    }
    if ((int)isPlaying != prev_playing) {
        prev_playing = (int)isPlaying;
        if (lbl_play[slot]) {
            lv_label_set_text(lbl_play[slot], isPlaying ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP);
            lv_obj_set_style_text_color(lbl_play[slot], isPlaying ? RED808_SUCCESS : RED808_ERROR, 0);
        }
        if (chip_seq_volume[slot]) {
            lv_obj_set_style_border_color(chip_seq_volume[slot], isPlaying ? RED808_SUCCESS : RED808_ERROR, 0);
        }
    }
#if S3_WIFI_ENABLED
    if ((int)wifiConnected != prev_wifi) {
        prev_wifi = (int)wifiConnected;
        if (lbl_wifi[slot]) {
            lv_label_set_text(lbl_wifi[slot], wifiConnected ? LV_SYMBOL_WIFI " ON" : LV_SYMBOL_CLOSE " OFF");
            lv_obj_set_style_text_color(lbl_wifi[slot], wifiConnected ? RED808_SUCCESS : RED808_ERROR, 0);
        }
    }
#endif
    static int prev_master_conn = -1;
    if (slotChanged) {
        prev_master_conn = -1;
    }
    if ((int)masterConnected != prev_master_conn) {
        prev_master_conn = (int)masterConnected;
        if (lbl_master_conn[slot]) {
            lv_label_set_text(lbl_master_conn[slot], masterConnected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_color(lbl_master_conn[slot], masterConnected ? RED808_SUCCESS : RED808_ERROR, 0);
        }
        if (chip_master_conn[slot]) {
            lv_obj_set_style_border_color(chip_master_conn[slot], masterConnected ? RED808_SUCCESS : RED808_ERROR, 0);
        }
    }

    static int prev_master_volume = -1;
    if (slotChanged) {
        prev_master_volume = -1;
    }
    if (masterVolume != prev_master_volume) {
        prev_master_volume = masterVolume;
        if (lbl_seq_volume[slot]) {
            lv_label_set_text_fmt(lbl_seq_volume[slot], "%d", masterVolume);
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
        case 3: nav_to(SCREEN_PATTERNS, scr_patterns); break;
        case 4: nav_to(SCREEN_SDCARD, scr_sdcard); break;
        case 5: nav_to(SCREEN_PERFORMANCE, scr_performance); break;
        case 6: nav_to(SCREEN_SETTINGS, scr_settings); break;
        case 7: nav_to(SCREEN_DIAGNOSTICS, scr_diagnostics); break;
    }
}

void ui_create_menu_screen() {
    scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_menu, RED808_BG, 0);

    ui_create_header(scr_menu);

    // 3×3 grid — full height fill, P4-style neon ring buttons (8 items: 3+3+2)
    static const char* menu_names[] = {
        LV_SYMBOL_AUDIO "\nLIVE PADS",
        LV_SYMBOL_LIST "\nSEQUENCER",
        LV_SYMBOL_VOLUME_MAX "\nVOLUMES",
        LV_SYMBOL_DRIVE "\nPATTERNS",
        LV_SYMBOL_DRIVE "\nSD BROWSER",
        LV_SYMBOL_SETTINGS "\nBUTTONS",
        LV_SYMBOL_HOME "\nSETTINGS",
        LV_SYMBOL_EYE_OPEN "\nSTATUS"
    };
    const lv_color_t menu_colors[] = {
        RED808_ACCENT,   RED808_INFO,     RED808_SUCCESS,
        RED808_CYAN,     RED808_ACCENT2,  RED808_ERROR,
        lv_color_hex(0xFF8C00), RED808_TEXT_DIM  // SETTINGS: naranja ámbar vistoso
    };
    static const int menu_count = 8;   // botones activos
    static const int menu_total = 10;  // incluye 2 placeholders

    // Placeholder labels (índices 8 y 9)
    static const char* placeholder_labels[] = {
        LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET "\nPRÓXIMAMENTE",
        LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET "\nPRÓXIMAMENTE"
    };

#if PORTRAIT_MODE
    // Portrait: 2 cols, fill height
    static const int cols    = 2;
    static const int x_start = 10;
    static const int y_start = 6;
    static const int gap     = 10;
    static const int btn_w   = (UI_W - 2 * x_start - gap) / cols;
    static const int btn_h   = (UI_H - 72 - y_start - 4 * gap) / 5;
#else
    // Landscape 1024×600: 3 cols, 4 rows, 10 botones (8 activos + 2 placeholder)
    // Footer at bottom (86px). Content area 0..508.
    static const int cols    = 3;
    static const int x_start = 12;
    static const int y_start = 8;
    static const int gap     = 12;
    static const int btn_w   = (UI_W - 2 * x_start - 2 * gap) / cols;  // 325px
    static const int btn_h   = (UI_H - 92 - y_start - 3 * gap) / 4;    // 116px
#endif

    for (int i = 0; i < menu_total; i++) {
        int col = i % cols;
        int row = i / cols;
        bool is_placeholder = (i >= menu_count);

        lv_obj_t* btn = lv_btn_create(scr_menu);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x_start + col * (btn_w + gap), y_start + row * (btn_h + gap));
        lv_obj_set_style_radius(btn, 18, 0);

        if (is_placeholder) {
            // Estilo placeholder: fondo muy tenue, borde punteado simulado con outline + baja opa
            apply_stable_button_style(btn, RED808_SURFACE, RED808_TEXT_DIM);
            lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
            lv_obj_set_style_outline_width(btn, 2, 0);
            lv_obj_set_style_outline_color(btn, RED808_TEXT_DIM, 0);
            lv_obj_set_style_outline_opa(btn, LV_OPA_20, 0);
            lv_obj_set_style_outline_pad(btn, 4, 0);
            // Sin callback — no hace nada al pulsar
            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, placeholder_labels[i - menu_count]);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl, RED808_TEXT_DIM, 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_opa(lbl, LV_OPA_50, 0);
            lv_obj_center(lbl);
        } else {
            // P4-style: dark surface + neon colored border + outer glow outline
            apply_stable_button_style(btn, RED808_SURFACE, menu_colors[i]);
            lv_obj_set_style_outline_width(btn, 3, 0);
            lv_obj_set_style_outline_color(btn, menu_colors[i], 0);
            lv_obj_set_style_outline_opa(btn, LV_OPA_50, 0);
            lv_obj_set_style_outline_pad(btn, 2, 0);
            lv_obj_add_event_cb(btn, menu_btn_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, menu_names[i]);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(lbl, menu_colors[i], 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(lbl);
        }
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
    // GT911 delivers physical LCD coordinates (0..1023 x 0..599).
    // Pad geometry is in LVGL portrait space (0..599 x 0..1023).
    // LVGL sw_rotate + ROT_90: swap X↔Y, invert new X.
#if PORTRAIT_MODE
    int vx = (SCREEN_HEIGHT - 1) - y;  // physical Y (0..599) → virtual X (599..0)
    int vy = x;                         // physical X (0..1023) → virtual Y (0..1023)
#else
    int vx = x;
    int vy = y;
#endif

    int bestPad = -1;
    int bestDist2 = INT32_MAX;

    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (live_pad_w[pad] <= 0 || live_pad_h[pad] <= 0) continue;

        int margin = (int)(min(live_pad_w[pad], live_pad_h[pad]) * Config::LIVE_PAD_HIT_MARGIN_PCT / 100);
        if (margin < Config::LIVE_PAD_HIT_MARGIN_MIN) margin = Config::LIVE_PAD_HIT_MARGIN_MIN;
        if (margin > Config::LIVE_PAD_HIT_MARGIN_MAX) margin = Config::LIVE_PAD_HIT_MARGIN_MAX;

        int left = live_pad_x[pad] - margin;
        int top = live_pad_y[pad] - margin;
        int right = live_pad_x[pad] + live_pad_w[pad] + margin;
        int bottom = live_pad_y[pad] + live_pad_h[pad] + margin;

        if (vx < left || vy < top || vx >= right || vy >= bottom) continue;

        int cx = live_pad_x[pad] + live_pad_w[pad] / 2;
        int cy = live_pad_y[pad] + live_pad_h[pad] / 2;
        int dx = vx - cx;
        int dy = vy - cy;
        int dist2 = dx * dx + dy * dy;

        if (dist2 < bestDist2) {
            bestDist2 = dist2;
            bestPad = pad;
        }
    }

    return bestPad;
}

static void live_sync_cb(lv_event_t* e) {
    livePadSyncMode = !livePadSyncMode;
    if (live_sync_btn) {
        lv_obj_set_style_bg_color(live_sync_btn,
            livePadSyncMode ? RED808_ACCENT : RED808_SURFACE, 0);
        lv_obj_set_style_border_color(live_sync_btn,
            livePadSyncMode ? RED808_ACCENT : RED808_ACCENT, 0);
    }
    if (live_sync_lbl) {
        lv_obj_set_style_text_color(live_sync_lbl,
            livePadSyncMode ? RED808_BG : RED808_TEXT, 0);
    }
    // Sync state to P4
    extern void uart_bridge_send(uint8_t type, uint8_t id, uint8_t value);
    uart_bridge_send(MSG_TOUCH_CMD, TCMD_SYNC_PADS, livePadSyncMode ? 1 : 0);
}

// Called when P4 sends sync toggle — update UI without re-sending
void ui_live_set_sync(bool on) {
    livePadSyncMode = on;
    if (live_sync_btn) {
        lv_obj_set_style_bg_color(live_sync_btn,
            on ? RED808_ACCENT : RED808_SURFACE, 0);
        lv_obj_set_style_border_color(live_sync_btn,
            on ? RED808_ACCENT : RED808_ACCENT, 0);
    }
    if (live_sync_lbl) {
        lv_obj_set_style_text_color(live_sync_lbl,
            on ? RED808_BG : RED808_TEXT, 0);
    }
}

void ui_create_live_screen() {
    scr_live = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_live, RED808_BG, 0);
    ui_create_header(scr_live);

    const int left_panel_w = 76;
    const int left_panel_x = 12;
    const int right_panel_w = 88;
    const int right_panel_margin = 12;
#if PORTRAIT_MODE
    const int grid_left = 12;
    const int grid_right = UI_W - 12;
    const int grid_width = grid_right - grid_left;
    // Reserve 200px for controls + footer (footer at UI_H-52)
    const int ctrl_zone = 210;
    const int row_h = (UI_H - 72 - LIVE_PAD_AREA_TOP - ctrl_zone - 3 * LIVE_PAD_GAP) / 4;
    const int default_pad_w = (grid_width - 3 * LIVE_PAD_GAP) / LIVE_PAD_COLS;
#else
    const int grid_left = left_panel_x + left_panel_w + 12;
    const int grid_right = 1024 - right_panel_w - right_panel_margin;
    const int grid_width = grid_right - grid_left;
    const int row_h = (508 - LIVE_PAD_AREA_TOP - 3 * LIVE_PAD_GAP) / 4;
    const int default_pad_w = (grid_width - 3 * LIVE_PAD_GAP) / LIVE_PAD_COLS;
#endif

#if PORTRAIT_MODE
    // In portrait, controls go below the pad grid, above footer
    int pad_grid_bottom = LIVE_PAD_AREA_TOP + 4 * (row_h + LIVE_PAD_GAP);
    int bottom_y = pad_grid_bottom + 6;
    int ctrl_h = UI_H - 72 - bottom_y;  // stop above footer
    lv_obj_t* left_panel = create_section_shell(scr_live, 12, bottom_y, UI_W / 2 - 18, ctrl_h);
#else
    lv_obj_t* left_panel = create_section_shell(scr_live, left_panel_x, LIVE_PAD_AREA_TOP, left_panel_w, 504);
#endif
    lv_obj_set_style_pad_all(left_panel, 8, 0);
#if PORTRAIT_MODE
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(left_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#else
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#endif
    lv_obj_set_style_pad_gap(left_panel, 8, 0);

    lv_obj_t* left_title = lv_label_create(left_panel);
    lv_label_set_text(left_title, "VIEW");
    lv_obj_set_style_text_font(left_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(left_title, RED808_TEXT_DIM, 0);

    for (int i = 0; i < LIVE_VIEW_MODE_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(left_panel);
#if PORTRAIT_MODE
        lv_obj_set_size(btn, 48, 32);
#else
        lv_obj_set_size(btn, 56, 42);
#endif
        apply_stable_button_style(btn, RED808_SURFACE, RED808_BORDER);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_add_event_cb(btn, live_view_mode_cb, LV_EVENT_CLICKED, (void*)(intptr_t)live_view_options[i]);
        lv_obj_t* lbl = lv_label_create(btn);
#if PORTRAIT_MODE
        lv_label_set_text(lbl, live_view_options[i] == 16 ? "ALL" : "");
        if (live_view_options[i] != 16)
            lv_label_set_text_fmt(lbl, "%d", live_view_options[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
#else
        lv_label_set_text_fmt(lbl, "%d", live_view_options[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
#endif
        lv_obj_center(lbl);
        live_view_btns[i] = btn;
        live_view_labels[i] = lbl;
    }

    live_view_prev_btn = lv_btn_create(left_panel);
#if PORTRAIT_MODE
    lv_obj_set_size(live_view_prev_btn, 40, 26);
#else
    lv_obj_set_size(live_view_prev_btn, 56, 32);
#endif
    apply_stable_button_style(live_view_prev_btn, RED808_SURFACE, RED808_ACCENT2);
    lv_obj_set_style_radius(live_view_prev_btn, 10, 0);
    lv_obj_add_event_cb(live_view_prev_btn, live_view_prev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* left_prev_lbl = lv_label_create(live_view_prev_btn);
    lv_label_set_text(left_prev_lbl, LV_SYMBOL_UP);
    lv_obj_center(left_prev_lbl);

    live_view_page_label = lv_label_create(left_panel);
    lv_label_set_text(live_view_page_label, "ALL");
    lv_obj_set_style_text_font(live_view_page_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(live_view_page_label, RED808_TEXT, 0);

    live_view_next_btn = lv_btn_create(left_panel);
#if PORTRAIT_MODE
    lv_obj_set_size(live_view_next_btn, 40, 26);
#else
    lv_obj_set_size(live_view_next_btn, 56, 32);
#endif
    apply_stable_button_style(live_view_next_btn, RED808_SURFACE, RED808_ACCENT2);
    lv_obj_set_style_radius(live_view_next_btn, 10, 0);
    lv_obj_add_event_cb(live_view_next_btn, live_view_next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* left_next_lbl = lv_label_create(live_view_next_btn);
    lv_label_set_text(left_next_lbl, LV_SYMBOL_DOWN);
    lv_obj_center(left_next_lbl);

    for (int i = 0; i < 16; i++) {
        int col = i % 4;
        int row = i / 4;
        lv_coord_t x = grid_left + col * (default_pad_w + LIVE_PAD_GAP);
        lv_coord_t y = LIVE_PAD_AREA_TOP + row * (row_h + LIVE_PAD_GAP);
        lv_coord_t h = row_h - LIVE_PAD_GAP;

        lv_obj_t* pad = lv_obj_create(scr_live);
        lv_obj_set_size(pad, default_pad_w, h);
        lv_obj_set_pos(pad, x, y);
        lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        // Neon ring style — identical look to P4 pads
        lv_obj_set_style_bg_color(pad, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_90, 0);
        lv_obj_set_style_radius(pad, 14, 0);
        lv_obj_set_style_border_width(pad, 3, 0);
        lv_obj_set_style_border_color(pad, inst_colors[i], 0);
        lv_obj_set_style_outline_width(pad, 3, 0);
        lv_obj_set_style_outline_color(pad, inst_colors[i], 0);
        lv_obj_set_style_outline_opa(pad, LV_OPA_60, 0);
        lv_obj_set_style_outline_pad(pad, 2, 0);
        lv_obj_set_style_shadow_width(pad, 0, 0);
        lv_obj_set_style_shadow_opa(pad, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(pad, 0, 0);

        live_pad_accents[i] = NULL; // no accent bar in neon ring style

        // Track name — centered, neon color
        lv_obj_t* lbl = lv_label_create(pad);
        lv_label_set_text(lbl, trackNames[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, inst_colors[i], 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        live_pad_names[i] = lbl;

        live_pad_desc[i] = NULL;  // no desc in neon ring style
        live_pad_glows[i] = NULL; // no glow in neon ring style

        live_pads[i] = pad;
        live_pad_x[i] = x;
        live_pad_y[i] = y;
        live_pad_w[i] = default_pad_w;
        live_pad_h[i] = h;
    }

    // SYNC button — inside VIEW panel
    live_sync_btn = lv_obj_create(left_panel);
#if PORTRAIT_MODE
    lv_obj_set_size(live_sync_btn, 48, 32);
#else
    lv_obj_set_size(live_sync_btn, 56, 36);
#endif
    lv_obj_clear_flag(live_sync_btn, LV_OBJ_FLAG_SCROLLABLE);
    apply_stable_button_style(live_sync_btn, RED808_SURFACE, RED808_ACCENT);
    lv_obj_set_style_radius(live_sync_btn, 8, 0);
    lv_obj_set_style_border_width(live_sync_btn, 2, 0);
    lv_obj_add_event_cb(live_sync_btn, live_sync_cb, LV_EVENT_PRESSED, NULL);

    live_sync_lbl = lv_label_create(live_sync_btn);
    lv_label_set_text(live_sync_lbl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(live_sync_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(live_sync_lbl, RED808_TEXT, 0);
    lv_obj_center(live_sync_lbl);

    // Ratchet controls — right side of pad grid: [ - ] Nx [ + ]
    {
#if PORTRAIT_MODE
        int ctrl_x = UI_W / 2 + 6;
        int ctrl_w = UI_W / 2 - 18;
        int btn_h = (ctrl_h - 50) / 2;  // fit within available height
        if (btn_h > 50) btn_h = 50;
        int label_h = 30;
        int gap_v = 6;
        int total_h = btn_h + label_h + btn_h + gap_v * 2;
        int y_start = bottom_y + (ctrl_h - total_h) / 2;
#else
        int ctrl_x = grid_right + 4;
        int ctrl_w = 1024 - ctrl_x - 8;  // fill remaining width
        int btn_h = 80;
        int label_h = 50;
        int gap_v = 12;
        int total_h = btn_h + label_h + btn_h + gap_v*2;  // 2 gaps
        int y_start = LIVE_PAD_AREA_TOP + (508 - LIVE_PAD_AREA_TOP - total_h) / 2;
#endif

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
        apply_stable_button_style(btn_plus, RED808_SURFACE, RED808_ACCENT);
        lv_obj_set_style_border_width(btn_plus, 2, 0);
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
        lv_obj_set_pos(live_ratchet_label, ctrl_x + ctrl_w / 2 - 16, y_start + btn_h + gap_v + 4);

        // [ - ] button (bottom = decrease)
        lv_obj_t* btn_minus = lv_btn_create(scr_live);
        lv_obj_set_size(btn_minus, ctrl_w, btn_h);
        lv_obj_set_pos(btn_minus, ctrl_x, y_start + btn_h + gap_v + label_h + gap_v);
        apply_stable_button_style(btn_minus, RED808_SURFACE, RED808_ACCENT);
        lv_obj_set_style_border_width(btn_minus, 2, 0);
        lv_obj_set_style_radius(btn_minus, 10, 0);
        lv_obj_add_event_cb(btn_minus, ratchet_minus_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t* lbl_minus = lv_label_create(btn_minus);
        lv_label_set_text(lbl_minus, "-");
        lv_obj_set_style_text_font(lbl_minus, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl_minus, RED808_ACCENT, 0);
        lv_obj_center(lbl_minus);
    }

    livePadVisibleCount = 16;
    livePadViewStart = 0;
    live_layout_pads();
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
        if (!active && millis() < livePadFlashUntilMs[pad]) {
            active = true;
        }
        // Note: sequencer steps flash via livePadFlashUntilMs set in main.cpp step clock
        // livePadSyncMode is kept as a toggle but visual flash is timer-driven (no always-on)
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
            // === NEON RING HIT — thick bright border + outline glow ===
            lv_obj_set_style_border_width(live_pads[pad], 5, 0);
            lv_obj_set_style_border_color(live_pads[pad], lv_color_white(), 0);
            lv_obj_set_style_outline_width(live_pads[pad], 5, 0);
            lv_obj_set_style_outline_color(live_pads[pad], inst_colors[pad], 0);
            lv_obj_set_style_outline_opa(live_pads[pad], LV_OPA_COVER, 0);
            if (live_pad_names[pad])
                lv_obj_set_style_text_color(live_pad_names[pad], lv_color_white(), 0);
        } else {
            // === IDLE — thin colored border ===
            lv_obj_set_style_border_width(live_pads[pad], 3, 0);
            lv_obj_set_style_border_color(live_pads[pad], inst_colors[pad], 0);
            lv_obj_set_style_outline_width(live_pads[pad], 3, 0);
            lv_obj_set_style_outline_color(live_pads[pad], inst_colors[pad], 0);
            lv_obj_set_style_outline_opa(live_pads[pad], LV_OPA_60, 0);
            if (live_pad_names[pad])
                lv_obj_set_style_text_color(live_pad_names[pad], inst_colors[pad], 0);
        }
    }

    prev_state_mask = state_mask;
}

// ============================================================================
// SEQUENCER SCREEN - 8-track paged grid (2 pages of 8 tracks)
// ============================================================================
static void seq_step_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int track = idx / Config::STEPS_PER_BANK;
    int rel_s = idx % Config::STEPS_PER_BANK;
    int abs_s = seq_step_bank * Config::STEPS_PER_BANK + rel_s;
    if (abs_s >= Config::MAX_STEPS) return;
    selectedTrack = track;
    patterns[currentPattern].steps[track][abs_s] = !patterns[currentPattern].steps[track][abs_s];
    bool active = patterns[currentPattern].steps[track][abs_s];
    lv_obj_set_style_bg_color(lv_event_get_target(e),
        active ? inst_colors[track] : RED808_SURFACE, 0);
    // Send step update to master
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setStep\",\"track\":%d,\"step\":%d,\"active\":%s}",
        track, abs_s, active ? "true" : "false");
    sendUDPCommand(buf);
    // Relay to P4 when WiFi is off (P4 forwards to Master via its own WiFi)
    uart_bridge_send(MSG_TOUCH_CMD, TCMD_STEP_TOGGLE, (uint8_t)((track << 4) | rel_s));
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
            lv_obj_set_style_bg_opa(seq_track_panels[track], LV_OPA_90, 0);
            lv_obj_set_style_border_width(seq_track_panels[track], 2, 0);
            lv_obj_set_style_border_color(seq_track_panels[track], lv_color_hex(0xFFD700), 0);
        } else if (effMuted) {
            lv_obj_set_style_bg_opa(seq_track_panels[track], LV_OPA_30, 0);
            lv_obj_set_style_border_width(seq_track_panels[track], 1, 0);
            lv_obj_set_style_border_color(seq_track_panels[track], lv_color_hex(0xFF3030), 0);
        } else {
            lv_obj_set_style_bg_opa(seq_track_panels[track], LV_OPA_70, 0);
            lv_obj_set_style_border_width(seq_track_panels[track], 0, 0);
        }
    }
    // Track label dim when muted
    if (seq_track_labels[track]) {
        lv_obj_set_style_text_opa(seq_track_labels[track],
            effMuted ? LV_OPA_40 : LV_OPA_COVER, 0);
    }
    // Grid cells: dim when effectively muted
    for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
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
            uart_bridge_send(MSG_TRACK, TRK_MUTE_BIT | (t & 0x0F), trackMuted[t] ? 1 : 0);
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
            uart_bridge_send(MSG_TRACK, TRK_MUTE_BIT | (t & 0x0F), shouldMute ? 1 : 0);
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
    // Always relay mute to P4 regardless of WiFi/solo state
    uart_bridge_send(MSG_TRACK, TRK_MUTE_BIT | (track & 0x0F), trackMuted[track] ? 1 : 0);
    seq_update_solo_mute_visuals(track);
}

static void seq_play_pause_cb(lv_event_t* e) {
    (void)e;
    isPlaying = !isPlaying;
    // Send play state via UART → P4 → Master
    uart_bridge_send(MSG_SYSTEM, SYS_PLAY_STATE, isPlaying ? 1 : 0);
    sendUDPCommand(isPlaying ? "{\"cmd\":\"start\"}" : "{\"cmd\":\"stop\"}");
    if (seq_play_lbl) {
        lv_label_set_text(seq_play_lbl, isPlaying ? LV_SYMBOL_PAUSE " PAUSE" : LV_SYMBOL_PLAY " PLAY");
    }
    if (seq_play_btn) {
        lv_obj_set_style_bg_color(seq_play_btn, isPlaying ? lv_color_hex(0x1C300C) : lv_color_hex(0x0C1C30), 0);
        lv_obj_set_style_border_color(seq_play_btn, isPlaying ? lv_color_hex(0x40A000) : lv_color_hex(0x0060A0), 0);
    }
}

static void seq_unmute_all_cb(lv_event_t* e) {
    (void)e;
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        trackMuted[t] = false;
        trackSolo[t] = false;
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"cmd\":\"mute\",\"track\":%d,\"value\":false}", t);
        sendUDPCommand(buf);
        seq_update_solo_mute_visuals(t);
    }
}

static void seq_apply_page() {
#if PORTRAIT_MODE
    return;  // All 16 tracks always visible in portrait
#endif
    int page_start = seq_page * SEQ_TRACKS_PER_PAGE;
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        bool visible = (t >= page_start && t < page_start + SEQ_TRACKS_PER_PAGE);
        if (seq_track_panels[t]) {
            if (visible) lv_obj_clear_flag(seq_track_panels[t], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(seq_track_panels[t], LV_OBJ_FLAG_HIDDEN);
        }
        if (seq_solo_btns[t]) {
            if (visible) lv_obj_clear_flag(seq_solo_btns[t], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(seq_solo_btns[t], LV_OBJ_FLAG_HIDDEN);
        }
        if (seq_mute_btns[t]) {
            if (visible) lv_obj_clear_flag(seq_mute_btns[t], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(seq_mute_btns[t], LV_OBJ_FLAG_HIDDEN);
        }
        for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
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

// Update the visible 16-step bank: relabel step chips and recolor all cells.
// Call after seq_step_bank changes (manual or auto-follow).
static void seq_apply_step_bank(bool force_redraw = false) {
    int bank_start = seq_step_bank * Config::STEPS_PER_BANK;
    int pat_len    = patterns[currentPattern].length;
    int total_banks = (pat_len + Config::STEPS_PER_BANK - 1) / Config::STEPS_PER_BANK;
    if (total_banks < 1) total_banks = 1;

    // Update step number labels
    for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
        if (seq_step_labels[s])
            lv_label_set_text_fmt(seq_step_labels[s], "%02d", bank_start + s + 1);
    }

    // Update bank indicator button
    if (seq_bank_lbl)
        lv_label_set_text_fmt(seq_bank_lbl, "BAR %d/%d", seq_step_bank + 1, total_banks);

    // Recolor all grid cells for the current bank
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
            if (!seq_grid[t][s]) continue;
            int abs_s = bank_start + s;
            bool active = (abs_s < pat_len) && patterns[currentPattern].steps[t][abs_s];
            lv_obj_set_style_bg_color(seq_grid[t][s],
                active ? inst_colors[t] : RED808_SURFACE, 0);
        }
    }

    // Dim all step labels; playing step will be re-highlighted in ui_update_sequencer
    for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
        if (seq_step_labels[s])
            lv_obj_set_style_text_color(seq_step_labels[s], RED808_TEXT_DIM, 0);
    }
    // Reset LED strip
    for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
        if (seq_step_leds[s])
            lv_obj_set_style_bg_color(seq_step_leds[s],
                (s % 4 == 0) ? lv_color_hex(0x2A2A2A) : lv_color_hex(0x1A1A1A), 0);
    }
    (void)force_redraw;
}

void ui_create_sequencer_screen() {
    scr_sequencer = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sequencer, RED808_BG, 0);
    lv_obj_clear_flag(scr_sequencer, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_sequencer);

    // Unused info labels (kept as NULL for ui_update_sequencer null guards)
    seq_info_pattern = NULL;
    seq_info_track = NULL;
    seq_info_transport = NULL;
    seq_info_page = NULL;

    // ── Layout constants ──
#if PORTRAIT_MODE
    // Portrait: all 16 tracks visible, taller cells to fill vertical space
    // Name column doubles as MUTE button — no separate MUTE column
    int name_x  = 4;
    int name_w  = 50;
    int grid_x  = name_x + name_w + 2; // 56
    int cell_w  = 30, cell_h = 50, gap = 2;
    int grid_w  = Config::STEPS_PER_BANK * (cell_w + gap) - gap; // 510
    int solo_x  = grid_x + grid_w + 2; // 568
    int solo_w  = UI_W - solo_x - 4;   // fill remaining (~28px)
    int step_label_y = 6;
    int grid_y       = 26;
    int row_pitch    = cell_h + gap;  // 52
#else
    // Columns: [track_name 68px] [grid 16*(48+3)-3=813] [S 44px gap 4] [M 44px]
    int name_x  = 16;
    int name_w  = 68;
    int grid_x  = name_x + name_w + 4; // 88
    int cell_w  = 48, cell_h = 48, gap = 3;
    int grid_w  = Config::STEPS_PER_BANK * (cell_w + gap) - gap; // 813
    int solo_x  = grid_x + grid_w + 4; // 905
    int solo_w  = 44;
    int mute_x  = solo_x + solo_w + 4; // 953
    int mute_w  = 44;
    int step_label_y = 4;
    int grid_y       = 28;
    int row_pitch    = cell_h + gap;  // 51
#endif

    // ── Step column chips (top row) ──
    for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
        lv_obj_t* chip = lv_obj_create(scr_sequencer);
        lv_obj_set_size(chip, cell_w, 20);
        lv_obj_set_pos(chip, grid_x + s * (cell_w + gap), step_label_y);
        lv_obj_set_style_bg_color(chip, (s % 4 == 0) ? lv_color_hex(0x223347) : lv_color_hex(0x16222E), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_radius(chip, 6, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        seq_step_labels[s] = lv_label_create(chip);
        lv_label_set_text_fmt(seq_step_labels[s], "%02d", s + 1);
        lv_obj_set_style_text_font(seq_step_labels[s], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(seq_step_labels[s], RED808_TEXT_DIM, 0);
        lv_obj_center(seq_step_labels[s]);
    }

    // ── "SOLO" / "MUTE" column headers ──
    lv_obj_t* s_hdr = lv_label_create(scr_sequencer);
    lv_label_set_text(s_hdr, "S");
    lv_obj_set_style_text_font(s_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hdr, lv_color_hex(0xFFD700), 0);
    lv_obj_set_pos(s_hdr, solo_x + 4, step_label_y + 3);

#if !PORTRAIT_MODE
    // Mute column header only in landscape (portrait uses name panel for mute)
    lv_obj_t* m_hdr = lv_label_create(scr_sequencer);
    lv_label_set_text(m_hdr, "MUTE");
    lv_obj_set_style_text_font(m_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m_hdr, lv_color_hex(0xFF3030), 0);
    lv_obj_set_pos(m_hdr, mute_x + 4, step_label_y + 3);
#endif

    // ── Track rows (16 total, 8 visible per page in landscape, all in portrait) ──
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
#if PORTRAIT_MODE
        int row = t;  // All 16 tracks visible
#else
        int row = t % SEQ_TRACKS_PER_PAGE;
#endif
        int panel_y = grid_y + row * row_pitch;

        // Track name panel (compact)
        lv_obj_t* panel = lv_obj_create(scr_sequencer);
        lv_obj_set_size(panel, name_w, cell_h);
        lv_obj_set_pos(panel, name_x, panel_y);
        lv_obj_set_style_bg_color(panel, RED808_PANEL, 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_70, 0);
        lv_obj_set_style_border_width(panel, 1, 0);
        lv_obj_set_style_border_color(panel, RED808_BORDER, 0);
        lv_obj_set_style_radius(panel, 8, 0);
        lv_obj_set_style_pad_all(panel, 2, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
#if PORTRAIT_MODE
        // In portrait, tapping the name panel toggles mute
        lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(panel, seq_mute_cb, LV_EVENT_PRESSED, (void*)(intptr_t)t);
#endif
        seq_track_panels[t] = panel;

        // Track short name (BD, SD, etc.)
        lv_obj_t* lbl = lv_label_create(panel);
        lv_label_set_text(lbl, trackNames[t]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, inst_colors[t], 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);
        seq_track_labels[t] = lbl;

        // Instrument name (small, under track name)
        lv_obj_t* meta = lv_label_create(panel);
        lv_label_set_text(meta, instrumentNames[t]);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(meta, RED808_TEXT_DIM, 0);
        lv_obj_set_width(meta, name_w - 4);
        lv_label_set_long_mode(meta, LV_LABEL_LONG_CLIP);
        lv_obj_align(meta, LV_ALIGN_BOTTOM_MID, 0, 0);
        seq_track_meta_labels[t] = meta;

        // Volume label (hidden — not needed, volumes screen exists)
        seq_track_volume_labels[t] = NULL;

        // ── S (Solo) button — own column ──
        lv_obj_t* btn_s = lv_btn_create(scr_sequencer);
        lv_obj_set_size(btn_s, solo_w, cell_h);
        lv_obj_set_pos(btn_s, solo_x, panel_y);
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
        lv_label_set_text(lbl_s, "SOLO");
        lv_obj_set_style_text_font(lbl_s, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_s, RED808_TEXT_DIM, 0);
        lv_obj_center(lbl_s);
        seq_solo_btns[t] = btn_s;

        // ── M (Mute) button — own column (landscape only; portrait uses name panel) ──
#if PORTRAIT_MODE
        seq_mute_btns[t] = NULL;  // mute via name panel tap
#else
        lv_obj_t* btn_m = lv_btn_create(scr_sequencer);
        lv_obj_set_size(btn_m, mute_w, cell_h);
        lv_obj_set_pos(btn_m, mute_x, panel_y);
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
        lv_label_set_text(lbl_m, "MUTE");
        lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_m, RED808_TEXT_DIM, 0);
        lv_obj_center(lbl_m);
        seq_mute_btns[t] = btn_m;
#endif
    }

    // ── Cache grid geometry for playhead positioning ──
    seq_cached_grid_x = grid_x;
    seq_cached_grid_y = grid_y;
    seq_cached_cell_w = cell_w;
    seq_cached_gap    = gap;
#if PORTRAIT_MODE
    seq_cached_grid_h = Config::MAX_TRACKS * row_pitch + 8; // covers grid + LED strip
#else
    seq_cached_grid_h = SEQ_TRACKS_PER_PAGE * row_pitch + 8;
#endif

    // ── Single playhead line (replaces 16 heavy column-highlight objects) ──
    {
        seq_playhead_line = lv_obj_create(scr_sequencer);
        lv_obj_set_size(seq_playhead_line, 3, seq_cached_grid_h);
        lv_obj_set_pos(seq_playhead_line, grid_x, grid_y);
        lv_obj_set_style_bg_color(seq_playhead_line, RED808_WARNING, 0);
        lv_obj_set_style_bg_opa(seq_playhead_line, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(seq_playhead_line, 1, 0);
        lv_obj_set_style_border_width(seq_playhead_line, 0, 0);
        lv_obj_clear_flag(seq_playhead_line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(seq_playhead_line, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(seq_playhead_line, LV_OBJ_FLAG_FLOATING);
        lv_obj_move_foreground(seq_playhead_line);
        if (!isPlaying) lv_obj_add_flag(seq_playhead_line, LV_OBJ_FLAG_HIDDEN);
    }
    // Initialize column highlights array to NULL (no longer used)
    memset(seq_column_highlights, 0, sizeof(seq_column_highlights));

    // ── Grid cells (16 tracks × STEPS_PER_BANK columns, bank-mapped) ──
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
#if PORTRAIT_MODE
        int row = t;
#else
        int row = t % SEQ_TRACKS_PER_PAGE;
#endif
        for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
            lv_obj_t* cell = lv_btn_create(scr_sequencer);
            lv_obj_set_size(cell, cell_w, cell_h);
            lv_obj_set_pos(cell, grid_x + s * (cell_w + gap), grid_y + row * row_pitch);
            bool active = patterns[currentPattern].steps[t][s];  // bank 0 at startup
            apply_stable_button_style(cell, active ? inst_colors[t] : RED808_SURFACE, RED808_BORDER);
            lv_obj_set_style_radius(cell, 3, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            int idx = t * Config::STEPS_PER_BANK + s;
            lv_obj_add_event_cb(cell, seq_step_cb, LV_EVENT_PRESSED, (void*)(intptr_t)idx);
            seq_grid[t][s] = cell;
        }
    }

    // ── LED indicator strip (below grid) ──
#if PORTRAIT_MODE
    int led_y = grid_y + Config::MAX_TRACKS * row_pitch + 2;
#else
    int led_y = grid_y + SEQ_TRACKS_PER_PAGE * row_pitch + 2;
#endif
    for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
        lv_obj_t* led = lv_obj_create(scr_sequencer);
        lv_obj_set_size(led, cell_w, 6);
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

    // ── Bottom bar ──
    int bottom_y = led_y + 12;
#if PORTRAIT_MODE
    int bb_btn_w = 90;
    int bb_gap = 6;
#endif

    // PLAY / PAUSE button
    seq_play_btn = lv_btn_create(scr_sequencer);
#if PORTRAIT_MODE
    lv_obj_set_size(seq_play_btn, bb_btn_w, 26);
    lv_obj_set_pos(seq_play_btn, 6, bottom_y);
#else
    lv_obj_set_size(seq_play_btn, 110, 26);
    lv_obj_set_pos(seq_play_btn, 16, bottom_y);
#endif
    lv_obj_set_style_bg_color(seq_play_btn, isPlaying ? lv_color_hex(0x1C300C) : lv_color_hex(0x0C1C30), 0);
    lv_obj_set_style_bg_opa(seq_play_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(seq_play_btn, 1, 0);
    lv_obj_set_style_border_color(seq_play_btn, isPlaying ? lv_color_hex(0x40A000) : lv_color_hex(0x0060A0), 0);
    lv_obj_set_style_radius(seq_play_btn, 5, 0);
    lv_obj_set_style_shadow_width(seq_play_btn, 0, 0);
    lv_obj_add_event_cb(seq_play_btn, seq_play_pause_cb, LV_EVENT_PRESSED, NULL);
    seq_play_lbl = lv_label_create(seq_play_btn);
    lv_label_set_text(seq_play_lbl, isPlaying ? LV_SYMBOL_PAUSE " PAUSE" : LV_SYMBOL_PLAY " PLAY");
    lv_obj_set_style_text_font(seq_play_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(seq_play_lbl, isPlaying ? lv_color_hex(0x80DF60) : lv_color_hex(0x60AADF), 0);
    lv_obj_center(seq_play_lbl);

    // Step indicator
    lbl_step_indicator = lv_label_create(scr_sequencer);
    lv_label_set_text(lbl_step_indicator, "Step: --");
    lv_obj_set_style_text_font(lbl_step_indicator, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_step_indicator, RED808_WARNING, 0);
#if PORTRAIT_MODE
    lv_obj_set_pos(lbl_step_indicator, 6 + bb_btn_w + bb_gap, bottom_y + 4);
#else
    lv_obj_set_pos(lbl_step_indicator, 136, bottom_y + 4);
#endif

    // UNMUTE ALL button
    seq_unmute_btn = lv_btn_create(scr_sequencer);
#if PORTRAIT_MODE
    lv_obj_set_size(seq_unmute_btn, bb_btn_w, 26);
    lv_obj_set_pos(seq_unmute_btn, 200, bottom_y);
#else
    lv_obj_set_size(seq_unmute_btn, 120, 26);
    lv_obj_set_pos(seq_unmute_btn, 300, bottom_y);
#endif
    lv_obj_set_style_bg_color(seq_unmute_btn, lv_color_hex(0x1A1218), 0);
    lv_obj_set_style_bg_opa(seq_unmute_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(seq_unmute_btn, 1, 0);
    lv_obj_set_style_border_color(seq_unmute_btn, lv_color_hex(0x8050A0), 0);
    lv_obj_set_style_radius(seq_unmute_btn, 5, 0);
    lv_obj_set_style_shadow_width(seq_unmute_btn, 0, 0);
    lv_obj_add_event_cb(seq_unmute_btn, seq_unmute_all_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t* unmute_lbl = lv_label_create(seq_unmute_btn);
    lv_label_set_text(unmute_lbl, LV_SYMBOL_VOLUME_MAX " UNMUTE");
    lv_obj_set_style_text_font(unmute_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(unmute_lbl, lv_color_hex(0xB080D0), 0);
    lv_obj_center(unmute_lbl);

    // BAR / STEP-BANK navigation (landscape only; shows which 16-step bank is visible)
#if !PORTRAIT_MODE
    seq_bank_prev_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(seq_bank_prev_btn, 28, 26);
    lv_obj_set_pos(seq_bank_prev_btn, 430, bottom_y);
    lv_obj_set_style_shadow_width(seq_bank_prev_btn, 0, 0);
    lv_obj_set_style_bg_color(seq_bank_prev_btn, lv_color_hex(0x0C1C30), 0);
    lv_obj_set_style_border_width(seq_bank_prev_btn, 1, 0);
    lv_obj_set_style_border_color(seq_bank_prev_btn, lv_color_hex(0x204060), 0);
    lv_obj_set_style_radius(seq_bank_prev_btn, 4, 0);
    lv_obj_add_event_cb(seq_bank_prev_btn, [](lv_event_t*) {
        int max_bank = (patterns[currentPattern].length + Config::STEPS_PER_BANK - 1) / Config::STEPS_PER_BANK - 1;
        if (max_bank < 0) max_bank = 0;
        seq_step_bank = (seq_step_bank > 0) ? seq_step_bank - 1 : max_bank;
        seq_apply_step_bank(true);
    }, LV_EVENT_PRESSED, NULL);
    lv_obj_t* bank_prev_lbl = lv_label_create(seq_bank_prev_btn);
    lv_label_set_text(bank_prev_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(bank_prev_lbl);

    lv_obj_t* seq_bank_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(seq_bank_btn, 96, 26);
    lv_obj_set_pos(seq_bank_btn, 460, bottom_y);
    lv_obj_set_style_bg_color(seq_bank_btn, lv_color_hex(0x0A1828), 0);
    lv_obj_set_style_bg_opa(seq_bank_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(seq_bank_btn, 1, 0);
    lv_obj_set_style_border_color(seq_bank_btn, lv_color_hex(0x204060), 0);
    lv_obj_set_style_radius(seq_bank_btn, 5, 0);
    lv_obj_set_style_shadow_width(seq_bank_btn, 0, 0);
    lv_obj_add_event_cb(seq_bank_btn, [](lv_event_t*) {
        int max_bank = (patterns[currentPattern].length + Config::STEPS_PER_BANK - 1) / Config::STEPS_PER_BANK;
        if (max_bank < 1) max_bank = 1;
        seq_step_bank = (seq_step_bank + 1) % max_bank;
        seq_apply_step_bank(true);
    }, LV_EVENT_PRESSED, NULL);
    seq_bank_lbl = lv_label_create(seq_bank_btn);
    lv_label_set_text(seq_bank_lbl, "BAR 1/1");
    lv_obj_set_style_text_font(seq_bank_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(seq_bank_lbl, lv_color_hex(0x5090C0), 0);
    lv_obj_center(seq_bank_lbl);

    seq_bank_next_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(seq_bank_next_btn, 28, 26);
    lv_obj_set_pos(seq_bank_next_btn, 558, bottom_y);
    lv_obj_set_style_shadow_width(seq_bank_next_btn, 0, 0);
    lv_obj_set_style_bg_color(seq_bank_next_btn, lv_color_hex(0x0C1C30), 0);
    lv_obj_set_style_border_width(seq_bank_next_btn, 1, 0);
    lv_obj_set_style_border_color(seq_bank_next_btn, lv_color_hex(0x204060), 0);
    lv_obj_set_style_radius(seq_bank_next_btn, 4, 0);
    lv_obj_add_event_cb(seq_bank_next_btn, [](lv_event_t*) {
        int max_bank = (patterns[currentPattern].length + Config::STEPS_PER_BANK - 1) / Config::STEPS_PER_BANK;
        if (max_bank < 1) max_bank = 1;
        seq_step_bank = (seq_step_bank + 1) % max_bank;
        seq_apply_step_bank(true);
    }, LV_EVENT_PRESSED, NULL);
    lv_obj_t* bank_next_lbl = lv_label_create(seq_bank_next_btn);
    lv_label_set_text(bank_next_lbl, LV_SYMBOL_RIGHT);
    lv_obj_center(bank_next_lbl);
#endif // !PORTRAIT_MODE

    // PAGE controls (landscape only — portrait shows all 16 tracks)
#if !PORTRAIT_MODE
    seq_page_prev_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(seq_page_prev_btn, 32, 26);
    lv_obj_set_pos(seq_page_prev_btn, 598, bottom_y);
    lv_obj_set_style_shadow_width(seq_page_prev_btn, 0, 0);
    lv_obj_add_event_cb(seq_page_prev_btn, [](lv_event_t*) {
        seq_page = (seq_page == 0) ? 1 : 0;
        seq_apply_page();
    }, LV_EVENT_PRESSED, NULL);
    lv_obj_t* page_prev_lbl = lv_label_create(seq_page_prev_btn);
    lv_label_set_text(page_prev_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(page_prev_lbl);

    seq_page_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(seq_page_btn, 100, 26);
    lv_obj_set_pos(seq_page_btn, 632, bottom_y);
    lv_obj_set_style_bg_color(seq_page_btn, lv_color_hex(0x1C300C), 0);
    lv_obj_set_style_bg_opa(seq_page_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(seq_page_btn, 1, 0);
    lv_obj_set_style_border_color(seq_page_btn, lv_color_hex(0x40A000), 0);
    lv_obj_set_style_radius(seq_page_btn, 5, 0);
    lv_obj_set_style_shadow_width(seq_page_btn, 0, 0);
    lv_obj_add_event_cb(seq_page_btn, [](lv_event_t*) {
        seq_page = (seq_page + 1) % 2;
        seq_apply_page();
    }, LV_EVENT_PRESSED, NULL);
    seq_page_lbl = lv_label_create(seq_page_btn);
    lv_label_set_text(seq_page_lbl, "PAGE 1/2");
    lv_obj_set_style_text_font(seq_page_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(seq_page_lbl, lv_color_hex(0x80DF60), 0);
    lv_obj_center(seq_page_lbl);

    seq_page_next_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(seq_page_next_btn, 32, 26);
    lv_obj_set_pos(seq_page_next_btn, 734, bottom_y);
    lv_obj_set_style_shadow_width(seq_page_next_btn, 0, 0);
    lv_obj_add_event_cb(seq_page_next_btn, [](lv_event_t*) {
        seq_page = (seq_page + 1) % 2;
        seq_apply_page();
    }, LV_EVENT_PRESSED, NULL);
    lv_obj_t* page_next_lbl = lv_label_create(seq_page_next_btn);
    lv_label_set_text(page_next_lbl, LV_SYMBOL_RIGHT);
    lv_obj_center(page_next_lbl);
#endif // !PORTRAIT_MODE

    // CIRCLE VIEW button
    lv_obj_t* circle_btn = lv_btn_create(scr_sequencer);
#if PORTRAIT_MODE
    lv_obj_set_size(circle_btn, 120, 26);
    lv_obj_set_pos(circle_btn, UI_W - 126, bottom_y);
#else
    lv_obj_set_size(circle_btn, 108, 26);
    lv_obj_set_pos(circle_btn, 778, bottom_y);
#endif
    lv_obj_set_style_bg_color(circle_btn, lv_color_hex(0x0C1C30), 0);
    lv_obj_set_style_bg_opa(circle_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circle_btn, 1, 0);
    lv_obj_set_style_border_color(circle_btn, lv_color_hex(0x0060A0), 0);
    lv_obj_set_style_radius(circle_btn, 5, 0);
    lv_obj_set_style_shadow_width(circle_btn, 0, 0);
    lv_obj_add_event_cb(circle_btn, [](lv_event_t*) { nav_to(SCREEN_SEQ_CIRCLE, scr_seq_circle); },
                        LV_EVENT_PRESSED, NULL);
    lv_obj_t* circle_lbl = lv_label_create(circle_btn);
    lv_label_set_text(circle_lbl, LV_SYMBOL_REFRESH " CIRCLE");
    lv_obj_set_style_text_font(circle_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(circle_lbl, lv_color_hex(0x60AADF), 0);
    lv_obj_center(circle_lbl);

    // MPC preset button: applies and stores fixed punch profile.
    seq_mpc_btn = lv_btn_create(scr_sequencer);
#if PORTRAIT_MODE
    lv_obj_set_size(seq_mpc_btn, 136, 26);
    lv_obj_set_pos(seq_mpc_btn, 332, bottom_y);
#else
    lv_obj_set_size(seq_mpc_btn, 124, 26);
    lv_obj_set_pos(seq_mpc_btn, 892, bottom_y);
#endif
    lv_obj_set_style_bg_color(seq_mpc_btn, lv_color_hex(0x2D1208), 0);
    lv_obj_set_style_bg_opa(seq_mpc_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(seq_mpc_btn, 1, 0);
    lv_obj_set_style_border_color(seq_mpc_btn, lv_color_hex(0xD05A24), 0);
    lv_obj_set_style_radius(seq_mpc_btn, 5, 0);
    lv_obj_set_style_shadow_width(seq_mpc_btn, 0, 0);
    lv_obj_add_event_cb(seq_mpc_btn, [](lv_event_t*) {
        apply_mpc_preset(true);
        if (seq_mpc_lbl) {
            lv_label_set_text_fmt(seq_mpc_lbl, "MPC S%d D%d",
                                  get_sequencer_swing_percent(),
                                  get_master_drive_percent());
        }
    }, LV_EVENT_PRESSED, NULL);

    seq_mpc_lbl = lv_label_create(seq_mpc_btn);
    lv_label_set_text_fmt(seq_mpc_lbl, "MPC S%d D%d",
                          get_sequencer_swing_percent(),
                          get_master_drive_percent());
    lv_obj_set_style_text_font(seq_mpc_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(seq_mpc_lbl, lv_color_hex(0xFF9A54), 0);
    lv_obj_center(seq_mpc_lbl);

    // Initial state
    seq_page = 0;
    seq_step_bank = 0;
    seq_apply_page();
    seq_apply_step_bank();
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        seq_update_solo_mute_visuals(t);
    }
}

void ui_update_sequencer() {
    if (!scr_sequencer) return;

    static int prev_step = -1;
    static int prev_column = -2;
    static int prev_page = -1;
    static int prev_pattern = -1;
    static int prev_selected_track = -1;
    static bool prev_playing = false;
    static int prev_bpm10 = -1;
    static int prev_swing = -1;
    static int prev_drive = -1;
    static uint8_t prev_grid_state[Config::MAX_TRACKS][Config::STEPS_PER_BANK];
    static int prev_track_volumes[Config::MAX_TRACKS] = {};

#if PORTRAIT_MODE
    int page_start = 0;
    int page_end   = Config::MAX_TRACKS;
#else
    int page_start = seq_page * SEQ_TRACKS_PER_PAGE;
    int page_end   = page_start + SEQ_TRACKS_PER_PAGE;
#endif

    // On track page change, force full grid refresh for visible tracks
    if (seq_page != prev_page) {
        prev_page = seq_page;
        memset(prev_grid_state, 0xFF, sizeof(prev_grid_state));
        prev_column = -2;
    }

    // Auto-follow step bank: when currentStep crosses a bank boundary, switch bank
    {
        int cur_bank = currentStep / Config::STEPS_PER_BANK;
        if (cur_bank != seq_step_bank) {
            seq_step_bank = cur_bank;
            seq_apply_step_bank();
            memset(prev_grid_state, 0xFF, sizeof(prev_grid_state));
            prev_column = -2;
        }
    }

    // Step indicator label
    if (lbl_step_indicator && currentStep != prev_step) {
        lv_label_set_text_fmt(lbl_step_indicator, "Step: %02d / %02d", currentStep + 1, patterns[currentPattern].length);
    }

    if (seq_mpc_lbl) {
        int swing = get_sequencer_swing_percent();
        int drive = get_master_drive_percent();
        if (swing != prev_swing || drive != prev_drive) {
            prev_swing = swing;
            prev_drive = drive;
            lv_label_set_text_fmt(seq_mpc_lbl, "MPC S%d D%d", swing, drive);
        }
    }

    if (seq_info_pattern && currentPattern != prev_pattern) {
        prev_pattern = currentPattern;
        lv_label_set_text_fmt(seq_info_pattern, "PATTERN %02d  %s", currentPattern + 1, patterns[currentPattern].name.c_str());
    }
    if (seq_info_track && selectedTrack != prev_selected_track) {
        prev_selected_track = selectedTrack;
        lv_label_set_text_fmt(seq_info_track, "TRACK %s  %s", trackNames[selectedTrack], instrumentNames[selectedTrack]);
    }
    int bpm10 = (int)lroundf(currentBPMPrecise * 10.0f);
    if (seq_info_transport && (isPlaying != prev_playing || bpm10 != prev_bpm10)) {
        prev_playing = isPlaying;
        prev_bpm10 = bpm10;
        int whole = bpm10 / 10;
        int frac = bpm10 % 10;
        if (frac < 0) frac = -frac;
        lv_label_set_text_fmt(seq_info_transport, "%s  %d.%d BPM", isPlaying ? "RUNNING" : "STOPPED", whole, frac);
        lv_obj_set_style_text_color(seq_info_transport, isPlaying ? RED808_SUCCESS : RED808_WARNING, 0);
    }

    // Sync play/pause button visual with external state changes (master UDP)
    if (isPlaying != prev_playing) {
        prev_playing = isPlaying;
        if (seq_play_lbl) {
            lv_label_set_text(seq_play_lbl, isPlaying ? LV_SYMBOL_PAUSE " PAUSE" : LV_SYMBOL_PLAY " PLAY");
            lv_obj_set_style_text_color(seq_play_lbl, isPlaying ? lv_color_hex(0x80DF60) : lv_color_hex(0x60AADF), 0);
        }
        if (seq_play_btn) {
            lv_obj_set_style_bg_color(seq_play_btn, isPlaying ? lv_color_hex(0x1C300C) : lv_color_hex(0x0C1C30), 0);
            lv_obj_set_style_border_color(seq_play_btn, isPlaying ? lv_color_hex(0x40A000) : lv_color_hex(0x0060A0), 0);
        }
    }

    // Update step label colour — bank-relative column
    if (currentStep != prev_step) {
        int prev_bank_col = (prev_step >= 0) ? (prev_step % Config::STEPS_PER_BANK) : -1;
        int cur_bank_col  = currentStep % Config::STEPS_PER_BANK;
        // Only update labels if same bank (bank-change is handled by seq_apply_step_bank)
        if (currentStep / Config::STEPS_PER_BANK == seq_step_bank) {
            if (prev_bank_col >= 0 && prev_bank_col < Config::STEPS_PER_BANK && seq_step_labels[prev_bank_col])
                lv_obj_set_style_text_color(seq_step_labels[prev_bank_col], RED808_TEXT_DIM, 0);
            if (isPlaying && cur_bank_col < Config::STEPS_PER_BANK && seq_step_labels[cur_bank_col])
                lv_obj_set_style_text_color(seq_step_labels[cur_bank_col], RED808_WARNING, 0);
        }
    }

    for (int t = page_start; t < page_end; t++) {
        if (seq_track_volume_labels[t] && prev_track_volumes[t] != trackVolumes[t]) {
            prev_track_volumes[t] = trackVolumes[t];
            lv_label_set_text_fmt(seq_track_volume_labels[t], "V%03d", trackVolumes[t]);
        }
        if (seq_track_panels[t]) {
            bool selected = (t == selectedTrack);
            if (selected) {
                lv_obj_set_style_border_width(seq_track_panels[t], 2, 0);
                lv_obj_set_style_border_color(seq_track_panels[t], inst_colors[t], 0);
            } else if (!trackSolo[t] && !isTrackEffectivelyMuted(t)) {
                lv_obj_set_style_border_width(seq_track_panels[t], 1, 0);
                lv_obj_set_style_border_color(seq_track_panels[t], RED808_BORDER, 0);
            }
        }
    }

    // Playhead and LED: use bank-relative column
    int bank_col = isPlaying ? (currentStep % Config::STEPS_PER_BANK) : -1;
    // Only show playhead when the playing step is in the visible bank
    bool in_bank = isPlaying && (currentStep / Config::STEPS_PER_BANK == seq_step_bank);
    int active_column = in_bank ? bank_col : -1;

    if (active_column != prev_column) {
        // --- Move playhead line ---
        if (seq_playhead_line) {
            if (active_column >= 0 && active_column < Config::STEPS_PER_BANK) {
                int px = seq_cached_grid_x + active_column * (seq_cached_cell_w + seq_cached_gap) + seq_cached_cell_w / 2 - 1;
                lv_obj_set_pos(seq_playhead_line, px, seq_cached_grid_y);
                lv_obj_clear_flag(seq_playhead_line, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(seq_playhead_line);
            } else {
                lv_obj_add_flag(seq_playhead_line, LV_OBJ_FLAG_HIDDEN);
            }
        }
        // Update LED strip
        if (prev_column >= 0 && prev_column < Config::STEPS_PER_BANK && seq_step_leds[prev_column]) {
            lv_obj_set_style_bg_color(seq_step_leds[prev_column],
                (prev_column % 4 == 0) ? lv_color_hex(0x2A2A2A) : lv_color_hex(0x1A1A1A), 0);
        }
        if (active_column >= 0 && active_column < Config::STEPS_PER_BANK && seq_step_leds[active_column]) {
            lv_obj_set_style_bg_color(seq_step_leds[active_column], RED808_WARNING, 0);
        }
        prev_column = active_column;
    }

    // Diff loop — only restyle cells whose pattern data actually changed (bank-relative)
    for (int t = page_start; t < page_end; t++) {
        for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
            if (!seq_grid[t][s]) continue;
            int abs_s = seq_step_bank * Config::STEPS_PER_BANK + s;
            bool active = (abs_s < patterns[currentPattern].length) && patterns[currentPattern].steps[t][abs_s];
            uint8_t state = active ? 1 : 0;
            if (state == prev_grid_state[t][s]) continue;
            prev_grid_state[t][s] = state;
            lv_obj_set_style_bg_color(seq_grid[t][s],
                active ? inst_colors[t] : RED808_SURFACE, 0);
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
    int newVol = lv_slider_get_value(slider);
    if (newVol == trackVolumes[track]) return;
    trackVolumes[track] = newVol;
    if (vol_labels[track]) {
        lv_label_set_text_fmt(vol_labels[track], "%d", trackVolumes[track]);
    }
    // Send volume to master
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setTrackVolume\",\"track\":%d,\"volume\":%d}", track, newVol);
    sendUDPCommand(buf);
}

static void ui_apply_volume_track_style(int track) {
    if (track < 0 || track >= Config::MAX_TRACKS) return;

    lv_color_t indicator = trackMuted[track] ? RED808_ERROR : inst_colors[track];
    lv_opa_t slider_opa = trackMuted[track] ? LV_OPA_40 : LV_OPA_COVER;
    lv_opa_t knob_opa = trackMuted[track] ? LV_OPA_50 : LV_OPA_COVER;

    if (vol_sliders[track]) {
        lv_obj_set_style_bg_opa(vol_sliders[track], slider_opa, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(vol_sliders[track], knob_opa, LV_PART_KNOB);
        lv_obj_set_style_bg_color(vol_sliders[track], indicator, LV_PART_INDICATOR);
    }
    if (vol_labels[track]) {
        lv_obj_set_style_text_color(vol_labels[track], trackMuted[track] ? RED808_ERROR : RED808_TEXT_DIM, 0);
    }
}

void ui_create_volumes_screen() {
    scr_volumes = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_volumes, RED808_BG, 0);
    lv_obj_clear_flag(scr_volumes, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_volumes);

    // Fader channel strips — full height, no title/name/description rows
#if PORTRAIT_MODE
    // Portrait: 2 rows of 8 strips — account for footer at UI_H - 52
    int strip_w = 64;
    int gap_h = (UI_W - 8 * strip_w) / 9;       // ~7px margins
    int y_top = 6;
    int y_bottom = UI_H - 72;                     // stop above footer
    int total_h = y_bottom - y_top;
    int row_gap = 14;
    int strip_h = (total_h - row_gap) / 2;        // ~426 each
    int name_h  = 20;                              // track name label
    int slider_h = strip_h - name_h - 30;          // room for name + value
    int y_name_row0   = y_top;
    int y_slider_row0 = y_top + name_h + 2;
    int y_value_row0  = y_slider_row0 + slider_h + 4;
    int y_name_row1   = y_top + strip_h + row_gap;
    int y_slider_row1 = y_name_row1 + name_h + 2;
    int y_value_row1  = y_slider_row1 + slider_h + 4;
#else
    int strip_w = 56;
    int gap = (1024 - 16 * strip_w) / 17;  // equal margins
    int y_top = 4;
    int y_bottom = 508;
    int strip_h = y_bottom - y_top;
    int slider_h = strip_h - 40;  // room for value label at bottom
    int y_slider = y_top + 4;
    int y_value = y_slider + slider_h + 4;
#endif

    for (int i = 0; i < Config::MAX_TRACKS; i++) {
#if PORTRAIT_MODE
        int row = i / 8;
        int col = i % 8;
        int x = gap_h + col * (strip_w + gap_h);
        int cx = x + strip_w / 2;
        int y_pos  = (row == 0) ? y_top : y_top + strip_h + row_gap;
        int y_sl   = (row == 0) ? y_slider_row0 : y_slider_row1;
        int y_val  = (row == 0) ? y_value_row0 : y_value_row1;
        int y_name = (row == 0) ? y_name_row0 : y_name_row1;
#else
        int x = gap + i * (strip_w + gap);
        int cx = x + strip_w / 2;
        int y_pos = y_top;
        int y_sl  = y_slider;
        int y_val = y_value;
#endif

        lv_obj_t* strip = lv_obj_create(scr_volumes);
        lv_obj_set_size(strip, strip_w, strip_h);
        lv_obj_set_pos(strip, x, y_pos);
        lv_obj_set_style_bg_color(strip, RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(strip, LV_OPA_70, 0);
        lv_obj_set_style_radius(strip, 10, 0);
        lv_obj_set_style_border_width(strip, 1, 0);
        lv_obj_set_style_border_color(strip, RED808_BORDER, 0);
        lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
        vol_strip_panels[i] = strip;

#if PORTRAIT_MODE
        // Track name label above the fader
        lv_obj_t* name_lbl = lv_label_create(scr_volumes);
        lv_label_set_text(name_lbl, trackNames[i]);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name_lbl, inst_colors[i], 0);
        lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(name_lbl, x, y_name);
        lv_obj_set_width(name_lbl, strip_w);
#endif

        // Slider — thin fader (10px wide, tall)
        lv_obj_t* slider = lv_slider_create(scr_volumes);
        lv_obj_set_size(slider, 10, slider_h);
        lv_obj_set_pos(slider, cx - 5, y_sl);
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
        lv_obj_set_pos(color_bar, x + 4, y_sl + slider_h + 2);
        lv_obj_set_style_bg_color(color_bar, inst_colors[i], 0);
        lv_obj_set_style_bg_opa(color_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(color_bar, 2, 0);
        lv_obj_set_style_border_width(color_bar, 0, 0);
        lv_obj_clear_flag(color_bar, LV_OBJ_FLAG_SCROLLABLE);

        // Value label (bottom, large)
        lv_obj_t* val = lv_label_create(scr_volumes);
        lv_label_set_text_fmt(val, "%d", trackVolumes[i]);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val, RED808_TEXT, 0);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(val, x, y_val);
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

    bool allow_slider_refresh = (lv_tick_get() - last_slider_refresh) >= 16;

    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        bool vol_changed = trackVolumes[i] != prev_vol[i];
        bool mute_changed = trackMuted[i] != prev_mute[i];

        if (mute_changed) {
            prev_mute[i] = trackMuted[i];
            ui_apply_volume_track_style(i);
            if (vol_strip_panels[i]) {
                lv_obj_set_style_border_color(vol_strip_panels[i], trackMuted[i] ? RED808_ERROR : inst_colors[i], 0);
                lv_obj_set_style_bg_opa(vol_strip_panels[i], trackMuted[i] ? LV_OPA_30 : LV_OPA_70, 0);
            }
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

// Called after theme change to refresh dynamic volume styles that
// depend on inst_colors[] (slider indicator, knob glow, strip border).
void ui_volumes_retheme() {
    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        ui_apply_volume_track_style(i);
        if (vol_sliders[i]) {
            // Knob glow & border are track-colored — update them
            lv_obj_set_style_shadow_color(vol_sliders[i], inst_colors[i], LV_PART_KNOB);
            lv_obj_set_style_border_color(vol_sliders[i], inst_colors[i], LV_PART_KNOB);
        }
        if (vol_strip_panels[i]) {
            lv_obj_set_style_border_color(vol_strip_panels[i],
                trackMuted[i] ? RED808_ERROR : inst_colors[i], 0);
        }
    }
}

void ui_update_menu_status() {
    // Menu relies on the shared header; keep this path intentionally empty.
}

// ============================================================================
// FILTERS FX SCREEN - REMOVED (stubs only)
// ============================================================================
void ui_create_filters_screen() { scr_filters = NULL; }
void ui_update_filters() {}

// ============================================================================
// SETTINGS SCREEN
// ============================================================================

// Pointers published by ui_create_settings_screen for runtime refresh
lv_obj_t** ui_theme_btns_ptr   = nullptr;
lv_obj_t** ui_theme_checks_ptr = nullptr;

static const uint32_t _s_btn_colors[THEME_COUNT] = {
    0xFF4444, 0x4A9EFF, 0x39FF14, 0xFF6B35, 0xFF00AA, 0x999999
};

void ui_refresh_theme_buttons() {
    if (!ui_theme_btns_ptr || !ui_theme_checks_ptr) return;
    for (int i = 0; i < THEME_COUNT; i++) {
        lv_obj_t* btn   = ui_theme_btns_ptr[i];
        lv_obj_t* check = ui_theme_checks_ptr[i];
        if (!btn) continue;
        bool active = (i == (int)currentTheme);
        lv_obj_set_style_border_width(btn, active ? 3 : 1, 0);
        lv_obj_set_style_shadow_opa(btn, active ? LV_OPA_80 : LV_OPA_30, 0);
        if (check) {
            lv_label_set_text(check, active ? LV_SYMBOL_OK : "");
        }
    }
}

void ui_create_settings_screen() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, RED808_BG, 0);
    ui_create_header(scr_settings);

    // ── NETWORK Card (full width) ──
#if S3_WIFI_ENABLED
    lv_obj_t* net_card = lv_obj_create(scr_settings);
#if PORTRAIT_MODE
    lv_obj_set_size(net_card, UI_W - 40, 160);
    lv_obj_set_pos(net_card, 20, 4);
#else
    lv_obj_set_size(net_card, 970, 130);
    lv_obj_set_pos(net_card, 30, 4);
#endif
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
#if PORTRAIT_MODE
    lv_obj_set_pos(role_info, 20, 90);
#else
    lv_obj_set_pos(role_info, 480, 40);
#endif
    lv_obj_set_style_text_line_space(role_info, 8, 0);
#endif  // S3_WIFI_ENABLED

    // ── USB CONNECTION INFO (when WiFi disabled) ──
#if !S3_WIFI_ENABLED
    lv_obj_t* usb_card = lv_obj_create(scr_settings);
#if PORTRAIT_MODE
    lv_obj_set_size(usb_card, UI_W - 40, 120);
    lv_obj_set_pos(usb_card, 20, 4);
#else
    lv_obj_set_size(usb_card, 970, 100);
    lv_obj_set_pos(usb_card, 30, 4);
#endif
    lv_obj_clear_flag(usb_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(usb_card, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(usb_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(usb_card, 12, 0);
    lv_obj_set_style_border_color(usb_card, RED808_BORDER, 0);
    lv_obj_set_style_border_width(usb_card, 1, 0);
    lv_obj_set_style_pad_all(usb_card, 16, 0);

    lv_obj_t* usb_icon = lv_label_create(usb_card);
    lv_label_set_text(usb_icon, LV_SYMBOL_USB " CONNECTION");
    lv_obj_set_style_text_font(usb_icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(usb_icon, RED808_INFO, 0);
    lv_obj_set_pos(usb_icon, 0, 0);

    lv_obj_t* usb_info = lv_label_create(usb_card);
    lv_label_set_text_fmt(usb_info,
        "Role: SURFACE CONTROLLER via USB-C to P4\n"
        "Cores: %d x %d MHz", 2, ESP.getCpuFreqMHz());
    lv_obj_set_style_text_font(usb_info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(usb_info, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(usb_info, 20, 36);
    lv_obj_set_style_text_line_space(usb_info, 8, 0);
#endif  // !S3_WIFI_ENABLED

    // ── THEME SELECTOR Section ──
    // Statics to allow refresh without screen rebuild
    static lv_obj_t* s_theme_btns[THEME_COUNT]  = {};
    static lv_obj_t* s_theme_checks[THEME_COUNT] = {};
    for (int x = 0; x < THEME_COUNT; x++) { s_theme_btns[x] = NULL; s_theme_checks[x] = NULL; }

    lv_obj_t* theme_card = lv_obj_create(scr_settings);
#if PORTRAIT_MODE
    lv_obj_set_size(theme_card, UI_W - 40, 740);
#if S3_WIFI_ENABLED
    lv_obj_set_pos(theme_card, 20, 178);
#else
    lv_obj_set_pos(theme_card, 20, 138);
#endif
#else
    lv_obj_set_size(theme_card, 970, 340);
#if S3_WIFI_ENABLED
    lv_obj_set_pos(theme_card, 30, 148);
#else
    lv_obj_set_pos(theme_card, 30, 118);
#endif
#endif
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
#if PORTRAIT_MODE
    int btn_w = 160;
    int btn_h = 200;
    int btn_gap = 12;
    int btn_cols = 3;
    int theme_card_inner_w = UI_W - 40 - 32;
    int btn_x_start = (theme_card_inner_w - btn_cols * btn_w - (btn_cols - 1) * btn_gap) / 2;
#else
    int btn_w = 140;
    int btn_h = 220;
    int btn_gap = 12;
    int btn_cols = THEME_COUNT;
    int btn_x_start = (970 - 32 - THEME_COUNT * btn_w - (THEME_COUNT - 1) * btn_gap) / 2;
#endif

    for (int i = 0; i < THEME_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(theme_card);
        lv_obj_set_size(btn, btn_w, btn_h);
#if PORTRAIT_MODE
        int col = i % btn_cols;
        int row = i / btn_cols;
        lv_obj_set_pos(btn, btn_x_start + col * (btn_w + btn_gap), 45 + row * (btn_h + btn_gap));
#else
        lv_obj_set_pos(btn, btn_x_start + i * (btn_w + btn_gap), 45);
#endif
        lv_obj_add_flag(btn, LV_OBJ_FLAG_USER_1);  // protect from theme restyling
        s_theme_btns[i] = btn;
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
        {
            lv_obj_t* check = lv_label_create(btn);
            lv_label_set_text(check, (i == (int)currentTheme) ? LV_SYMBOL_OK : "");
            lv_obj_set_style_text_font(check, &lv_font_montserrat_22, 0);
            lv_obj_set_style_text_color(check, lv_color_hex(btn_colors[i]), 0);
            lv_obj_align(check, LV_ALIGN_BOTTOM_MID, 0, -38);
            lv_obj_add_flag(check, LV_OBJ_FLAG_USER_1);
            s_theme_checks[i] = check;
        }

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            ui_theme_apply((VisualTheme)idx);
            extern volatile bool themeJustChanged;
            themeJustChanged = true;
            // Sync theme to P4
            extern void uart_bridge_send_theme(int theme);
            uart_bridge_send_theme(idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    // Publish pointers for runtime refresh
    extern lv_obj_t** ui_theme_btns_ptr;
    extern lv_obj_t** ui_theme_checks_ptr;
    ui_theme_btns_ptr   = s_theme_btns;
    ui_theme_checks_ptr = s_theme_checks;
}

// ============================================================================
// DIAGNOSTICS SCREEN — Device / Protocol / Type / Status grid
// ============================================================================
// Each row describes ONE hardware link with four columns:
//   [Device] [Protocol/Address] [Type/Role] [green OK • or red --]
// Status colour is driven directly by DiagnosticInfo flags populated by the
// main firmware (I2C scan, WiFi stack, UART heartbeat, …).
// ----------------------------------------------------------------------------
#define DIAG_ROWS 14
static lv_obj_t* diag_dev_labels[DIAG_ROWS];    // Device name + role
static lv_obj_t* diag_proto_labels[DIAG_ROWS];  // Protocol / bus address
static lv_obj_t* diag_type_labels[DIAG_ROWS];   // Peripheral type tag
static lv_obj_t* diag_status_labels[DIAG_ROWS]; // Green OK / Red -- pill
// Back-compat alias so any legacy update paths still compile.
static lv_obj_t** diag_values = diag_status_labels;
static lv_obj_t** diag_labels = diag_dev_labels;

// Row metadata — index maps 1:1 with DiagnosticInfo flags in update pass.
struct DiagRowMeta {
    const char* device;
    const char* type;   // short tag (ENCODER / ROTARY / HUB / FADER / BYBUTTON…)
};
static const DiagRowMeta DIAG_META[DIAG_ROWS] = {
    { "WiFi",          "NET"      },
    { "UDP Master",    "LINK"     },
    { "P4 Display",    "BRIDGE"   },
    { "I2C Hub",       "HUB"      },
    { "Touch GT911",   "TOUCH"    },
    { "LCD Panel",     "DISPLAY"  },
    { "M5 Encoder 1",  "ROTATE8"  },
    { "M5 Encoder 2",  "ROTATE8"  },
    { "ByteButton 1",  "BYBUTTON" },
    { "ByteButton 2",  "BYBUTTON" },
    { "DFRobot Enc",   "ENCODER"  },
    { "DFRobot Pot",   "ROTARY"   },
    { "Unit Fader",    "FADER"    },
    { "SD Card",       "STORAGE"  },
};

extern bool sd_mounted;  // from SD card screen

void ui_create_diagnostics_screen() {
    scr_diagnostics = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_diagnostics, RED808_BG, 0);
    ui_create_header(scr_diagnostics);

    lv_obj_t* title = lv_label_create(scr_diagnostics);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN " STATUS CENTER");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

#if PORTRAIT_MODE
    int card_w = UI_W - 32;
    lv_obj_t* health_card = create_section_shell(scr_diagnostics, 16, 32, card_w, 420);
    int row_h = 24;
#else
    lv_obj_t* health_card = create_section_shell(scr_diagnostics, 24, 32, 320, 400);
    int row_h = 24;
#endif

    lv_obj_t* health_title = lv_label_create(health_card);
    lv_label_set_text(health_title, LV_SYMBOL_OK "  HARDWARE LINKS");
    lv_obj_set_style_text_font(health_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(health_title, RED808_INFO, 0);
    lv_obj_set_pos(health_title, 0, 0);

    // Column header
    lv_obj_t* hdr_dev = lv_label_create(health_card);
    lv_label_set_text(hdr_dev, "DEVICE");
    lv_obj_set_style_text_font(hdr_dev, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_dev, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(hdr_dev, 0, 22);

    lv_obj_t* hdr_proto = lv_label_create(health_card);
    lv_label_set_text(hdr_proto, "PROTOCOL");
    lv_obj_set_style_text_font(hdr_proto, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_proto, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(hdr_proto, 110, 22);

    lv_obj_t* hdr_type = lv_label_create(health_card);
    lv_label_set_text(hdr_type, "TYPE");
    lv_obj_set_style_text_font(hdr_type, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_type, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(hdr_type, 210, 22);

    lv_obj_t* hdr_st = lv_label_create(health_card);
    lv_label_set_text(hdr_st, "ST");
    lv_obj_set_style_text_font(hdr_st, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_st, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(hdr_st, 275, 22);

    int y = 42;
    for (int i = 0; i < DIAG_ROWS; i++) {
        // Col 1 — device
        diag_dev_labels[i] = lv_label_create(health_card);
        lv_label_set_text(diag_dev_labels[i], DIAG_META[i].device);
        lv_obj_set_style_text_font(diag_dev_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(diag_dev_labels[i], RED808_TEXT, 0);
        lv_obj_set_pos(diag_dev_labels[i], 0, y);

        // Col 2 — protocol (populated live in update pass)
        diag_proto_labels[i] = lv_label_create(health_card);
        lv_label_set_text(diag_proto_labels[i], "…");
        lv_obj_set_style_text_font(diag_proto_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(diag_proto_labels[i], RED808_TEXT_DIM, 0);
        lv_obj_set_pos(diag_proto_labels[i], 110, y + 2);

        // Col 3 — type tag
        diag_type_labels[i] = lv_label_create(health_card);
        lv_label_set_text(diag_type_labels[i], DIAG_META[i].type);
        lv_obj_set_style_text_font(diag_type_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(diag_type_labels[i], RED808_ACCENT2, 0);
        lv_obj_set_pos(diag_type_labels[i], 210, y + 2);

        // Col 4 — status pill
        diag_status_labels[i] = lv_label_create(health_card);
        lv_label_set_text(diag_status_labels[i], "--");
        lv_obj_set_style_text_font(diag_status_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(diag_status_labels[i], RED808_ERROR, 0);
        lv_obj_set_pos(diag_status_labels[i], 275, y);

        y += row_h;
    }

#if PORTRAIT_MODE
    lv_obj_t* runtime_card = create_section_shell(scr_diagnostics, 16, 452, card_w, 440);
#else
    lv_obj_t* runtime_card = create_section_shell(scr_diagnostics, 340, 32, 322, 470);
#endif
    lv_obj_t* runtime_title = lv_label_create(runtime_card);
    lv_label_set_text(runtime_title, LV_SYMBOL_EYE_OPEN "  LIVE TELEMETRY");
    lv_obj_set_style_text_font(runtime_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(runtime_title, RED808_WARNING, 0);
    lv_obj_set_pos(runtime_title, 0, 0);

    static const char* runtime_rows[] = {
        "Screen", "WiFi state", "Master pkt", "Step sync",
        "UI cadence", "UI load", "UDP traffic", "Heap",
        "PSRAM", "Uptime", "POTS 1..4", "ByteButtons"
    };
    y = 38;
        int runtime_row_h =
    #if PORTRAIT_MODE
        30;
    #else
        34;
    #endif
    for (int i = 0; i < 12; i++) {
        lv_obj_t* name = lv_label_create(runtime_card);
        lv_label_set_text(name, runtime_rows[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, RED808_TEXT_DIM, 0);
        lv_obj_set_pos(name, 0, y);

        diag_runtime_values[i] = lv_label_create(runtime_card);
        lv_label_set_text(diag_runtime_values[i], "--");
        lv_obj_set_style_text_font(diag_runtime_values[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(diag_runtime_values[i], RED808_TEXT, 0);
        lv_obj_set_pos(diag_runtime_values[i], 128, y);
        y += runtime_row_h;
    }

// EVENT LOG panel removed for performance
}

void ui_update_diagnostics() {
    if (!scr_diagnostics) return;

    static bool prev_vals[DIAG_ROWS] = {};
    static bool diag_rows_initialized = false;
    static uint32_t prev_heap = 0;
    static uint32_t prev_psram = 0;
    static uint32_t prev_uptime = 0;
    static uint32_t last_sys_update_ms = 0;
    static bool prev_wifi_connected = false;
    static bool prev_udp_connected = false;
    static bool prev_master_connected = false;
    static bool prev_sd_mounted = false;
    static Screen prev_screen = SCREEN_BOOT;
    static uint32_t last_ms = 0;
    static uint32_t prev_ui_updates = 0;
    static uint32_t prev_ui_skips = 0;
    static uint32_t prev_udp_rx = 0;
    static uint32_t prev_json_err = 0;

    // Aggregate flags for compound rows (DFRobot 4 encoders, 2 byte buttons).
    bool dfEncAny = diagInfo.dfrobot1Ok || diagInfo.dfrobot2Ok ||
                    diagInfo.dfrobot3Ok || diagInfo.dfrobot4Ok;
    int dfEncCount = (int)diagInfo.dfrobot1Ok + (int)diagInfo.dfrobot2Ok +
                     (int)diagInfo.dfrobot3Ok + (int)diagInfo.dfrobot4Ok;
    bool sdAlive = diagInfo.sdOk || sd_mounted;

    // Row metadata parallel to DIAG_META — status flag + protocol string.
    // Protocol strings describe where each device lives (bus/address/channel).
    struct DiagRowRuntime { bool val; const char* proto; };
    char proto_m5_1[24], proto_m5_2[24];
    char proto_bb_1[24], proto_bb_2[24];
    char proto_dfenc[24];
    snprintf(proto_m5_1, sizeof(proto_m5_1), "I2C hub ch%d",
             (m5HubChannel[0] >= 0) ? m5HubChannel[0] : 0);
    snprintf(proto_m5_2, sizeof(proto_m5_2), "I2C hub ch%d",
             (m5HubChannel[1] >= 0) ? m5HubChannel[1] : 0);
    snprintf(proto_bb_1, sizeof(proto_bb_1), "I2C 0x47 ch%d",
             (byteButtonHubChannel[0] >= 0) ? byteButtonHubChannel[0] : 0);
    snprintf(proto_bb_2, sizeof(proto_bb_2), "I2C 0x47 ch%d",
             (byteButtonHubChannel[1] >= 0) ? byteButtonHubChannel[1] : 0);
    snprintf(proto_dfenc, sizeof(proto_dfenc), "I2C 4x (%d/4)", dfEncCount);

    DiagRowRuntime rows[DIAG_ROWS] = {
        { diagInfo.wifiOk,        "TCP/IP Red808" },   // WiFi
        { diagInfo.udpConnected,  "UDP :8888 JSON" },  // UDP Master
        { masterConnected,        "UART 921600" },     // P4 Display link (heartbeat)
        { diagInfo.i2cHubOk,      "I2C 0x70" },        // PCA9548A mux
        { diagInfo.touchOk,       "I2C 0x5D" },        // GT911
        { diagInfo.lcdOk,         "RGB666 1024x600" }, // LCD
        { diagInfo.m5encoder1Ok,  proto_m5_1 },        // M5 Encoder 1
        { diagInfo.m5encoder2Ok,  proto_m5_2 },        // M5 Encoder 2
        { diagInfo.byteButton1Ok, proto_bb_1 },        // ByteButton 1
        { diagInfo.byteButton2Ok, proto_bb_2 },        // ByteButton 2
        { dfEncAny,               proto_dfenc },       // DFRobot encoders (4x)
        { diagInfo.dfrobotPotsOk, "I2C ADS1115" },     // DFRobot pot ADC
        { true,                   "ADC GPIO6" },       // Unit fader (passive ADC)
        { sdAlive,                sdAlive ? "SD_MMC FAT" : "not mounted" },
    };

    for (int i = 0; i < DIAG_ROWS; i++) {
        bool v = rows[i].val;
        if (!diag_rows_initialized || v != prev_vals[i]) {
            prev_vals[i] = v;
            lv_label_set_text(diag_status_labels[i], v ? "OK" : "--");
            lv_obj_set_style_text_color(diag_status_labels[i],
                v ? RED808_SUCCESS : RED808_ERROR, 0);
        }
        // Protocol string may change at runtime (channel reassigned after rescan)
        if (diag_proto_labels[i]) {
            lv_label_set_text(diag_proto_labels[i], rows[i].proto);
        }
    }
    diag_rows_initialized = true;

    if (wifiConnected != prev_wifi_connected) {
        prev_wifi_connected = wifiConnected;
        ui_runtime_note(wifiConnected ? "WiFi link connected" : "WiFi link lost");
    }
    if (udpConnected != prev_udp_connected) {
        prev_udp_connected = udpConnected;
        ui_runtime_note(udpConnected ? "UDP port active" : "UDP port inactive");
    }
    if (masterConnected != prev_master_connected) {
        prev_master_connected = masterConnected;
        ui_runtime_note(masterConnected ? "Master heartbeat detected" : "Waiting for master heartbeat");
    }
    if (sd_mounted != prev_sd_mounted) {
        prev_sd_mounted = sd_mounted;
        ui_runtime_note(sd_mounted ? "SD card mounted" : "SD card not mounted");
    }
    if (currentScreen != prev_screen) {
        prev_screen = currentScreen;
        char note[48];
        snprintf(note, sizeof(note), "Screen: %s", screen_name(currentScreen));
        ui_runtime_note(note);
    }

    uint32_t now_ms = lv_tick_get();
    if ((now_ms - last_ms) >= 500 && diag_runtime_values[0]) {
        uint32_t elapsed = last_ms == 0 ? 500 : (now_ms - last_ms);
        uint32_t ui_updates = uiUpdateCount;
        uint32_t ui_skips = uiSkippedCount;
        uint32_t udp_rx = udpRxCount;
        uint32_t json_err = udpJsonErrorCount;

        uint32_t ui_rate = elapsed ? ((ui_updates - prev_ui_updates) * 1000UL) / elapsed : 0;
        uint32_t skip_rate = elapsed ? ((ui_skips - prev_ui_skips) * 1000UL) / elapsed : 0;
        uint32_t udp_rate = elapsed ? ((udp_rx - prev_udp_rx) * 1000UL) / elapsed : 0;
        uint32_t json_err_delta = json_err - prev_json_err;
        unsigned long now_millis = millis();
        unsigned long master_age = lastMasterPacketMs ? (now_millis - lastMasterPacketMs) : 0;
        unsigned long step_age = lastStepUpdateMs ? (now_millis - lastStepUpdateMs) : 0;

        lv_label_set_text(diag_runtime_values[0], screen_name(currentScreen));
#if S3_WIFI_ENABLED
        lv_label_set_text(diag_runtime_values[1],
            wifiConnected ? (wifiReconnecting ? "ONLINE / RECOVER" : "ONLINE") :
                            (wifiReconnecting ? "RECONNECTING" : "OFFLINE"));
#else
        lv_label_set_text(diag_runtime_values[1], "USB-ONLY");
#endif
        lv_label_set_text_fmt(diag_runtime_values[2], lastMasterPacketMs ? "%lu ms" : "No packets", master_age);
        lv_label_set_text_fmt(diag_runtime_values[3], lastStepUpdateMs ? "%lu ms" : "No sync", step_age);
        lv_label_set_text_fmt(diag_runtime_values[4], "%lu ms", (unsigned long)uiLastIntervalMs);
        lv_label_set_text_fmt(diag_runtime_values[5], "%lu/s  skip %lu/s", (unsigned long)ui_rate, (unsigned long)skip_rate);
        lv_label_set_text_fmt(diag_runtime_values[6], "%lu/s  err +%lu", (unsigned long)udp_rate, (unsigned long)json_err_delta);
        lv_label_set_text_fmt(diag_runtime_values[7], "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
        lv_label_set_text_fmt(diag_runtime_values[8], "%lu KB", (unsigned long)(ESP.getFreePsram() / 1024));

        uint32_t uptime_s = now_millis / 1000;
        uint32_t hours = uptime_s / 3600;
        uint32_t minutes = (uptime_s % 3600) / 60;
        uint32_t seconds = uptime_s % 60;
        lv_label_set_text_fmt(diag_runtime_values[9], "%02lu:%02lu:%02lu",
                              (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);

        lv_label_set_text_fmt(diag_runtime_values[10], "M %3u %3u %3u %3u  P %u %u %u %u",
                      (unsigned)dfRobotPotMidi[0],
                      (unsigned)dfRobotPotMidi[1],
                      (unsigned)dfRobotPotMidi[2],
                      (unsigned)dfRobotPotMidi[3],
                      (unsigned)dfRobotPotPos[0],
                      (unsigned)dfRobotPotPos[1],
                      (unsigned)dfRobotPotPos[2],
                      (unsigned)dfRobotPotPos[3]);
        lv_label_set_text_fmt(diag_runtime_values[11], "BB1 %s ch%d | BB2 %s ch%d",
                  byteButtonConnected[0] ? "OK" : "NO", byteButtonHubChannel[0],
                  byteButtonConnected[1] ? "OK" : "NO", byteButtonHubChannel[1]);

        prev_ui_updates = ui_updates;
        prev_ui_skips = ui_skips;
        prev_udp_rx = udp_rx;
        prev_json_err = json_err;
        last_ms = now_ms;
    }

    // Update slower-changing system info every 2s
    if ((now_ms - last_sys_update_ms) >= 2000) {
        last_sys_update_ms = now_ms;

        uint32_t heap = ESP.getFreeHeap() / 1024;
        if (heap != prev_heap) {
            prev_heap = heap;
            if (diag_runtime_values[7]) {
                lv_obj_set_style_text_color(diag_runtime_values[7], heap > 100 ? RED808_SUCCESS : RED808_WARNING, 0);
            }
        }

        uint32_t psram_free = ESP.getFreePsram() / 1024;
        if (psram_free != prev_psram) {
            prev_psram = psram_free;
            if (diag_runtime_values[8]) {
                lv_obj_set_style_text_color(diag_runtime_values[8], psram_free > 1000 ? RED808_SUCCESS : RED808_WARNING, 0);
            }
        }

        uint32_t uptime_s = millis() / 1000;
        if (uptime_s != prev_uptime) {
            prev_uptime = uptime_s;
            if (diag_runtime_values[9]) {
                lv_obj_set_style_text_color(diag_runtime_values[9], RED808_INFO, 0);
            }
        }
        if (diag_runtime_values[10]) {
            lv_obj_set_style_text_color(diag_runtime_values[10], diagInfo.dfrobotPotsOk ? RED808_SUCCESS : RED808_WARNING, 0);
        }
        if (diag_runtime_values[11]) {
            bool allBbOk = diagInfo.byteButton1Ok && diagInfo.byteButton2Ok;
            lv_obj_set_style_text_color(diag_runtime_values[11], allBbOk ? RED808_SUCCESS : RED808_WARNING, 0);
        }
    }
}

// ============================================================================
// PATTERNS SCREEN - 6 patterns, click to select
// ============================================================================
static constexpr int VISIBLE_PATTERNS = 8;
static lv_obj_t* pattern_btns[VISIBLE_PATTERNS] = {};
static lv_obj_t* pattern_labels[VISIBLE_PATTERNS] = {};
static lv_obj_t* pattern_meta_labels[VISIBLE_PATTERNS] = {};
static lv_obj_t* pattern_page_label = NULL;
static lv_obj_t* pattern_summary_label = NULL;
static lv_obj_t* pattern_prev_btn = NULL;
static lv_obj_t* pattern_next_btn = NULL;
static int pattern_page = 0;

static int pattern_page_count() {
    return (Config::MAX_PATTERNS + VISIBLE_PATTERNS - 1) / VISIBLE_PATTERNS;
}

static int pattern_active_steps(int patternIndex) {
    int active = 0;
    if (patternIndex < 0 || patternIndex >= Config::MAX_PATTERNS) return 0;
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            if (s < patterns[patternIndex].length)
                active += patterns[patternIndex].steps[t][s] ? 1 : 0;
        }
    }
    return active;
}

static void pattern_apply_page() {
    int start = pattern_page * VISIBLE_PATTERNS;
    int pages = pattern_page_count();

    if (pattern_page_label) {
        lv_label_set_text_fmt(pattern_page_label, "PAGE %d/%d", pattern_page + 1, pages);
    }
    if (pattern_summary_label) {
        lv_label_set_text_fmt(pattern_summary_label, "CURRENT %02d  %s", currentPattern + 1, patterns[currentPattern].name.c_str());
    }

    for (int i = 0; i < VISIBLE_PATTERNS; i++) {
        int patternIndex = start + i;
        bool visible = patternIndex < Config::MAX_PATTERNS;
        if (!pattern_btns[i]) continue;
        if (!visible) {
            lv_obj_add_flag(pattern_btns[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(pattern_btns[i], LV_OBJ_FLAG_HIDDEN);
        bool active = (patternIndex == currentPattern);
        // FIX: apply_stable_button_style overrides ALL LVGL states — use it for active/inactive
        apply_stable_button_style(pattern_btns[i],
            active ? RED808_ACCENT : RED808_SURFACE,
            active ? lv_color_white() : RED808_BORDER);
        lv_obj_set_style_border_width(pattern_btns[i], active ? 3 : 1, 0);
        if (pattern_labels[i]) {
            lv_label_set_text_fmt(pattern_labels[i], "PATTERN %02d", patternIndex + 1);
            lv_obj_set_style_text_color(pattern_labels[i], active ? lv_color_white() : RED808_TEXT, 0);
        }
        if (pattern_meta_labels[i]) {
            lv_label_set_text_fmt(pattern_meta_labels[i], "%s\n%d active steps", patterns[patternIndex].name.c_str(), pattern_active_steps(patternIndex));
            lv_obj_set_style_text_color(pattern_meta_labels[i], active ? lv_color_white() : RED808_TEXT_DIM, 0);
        }
    }
}

static void pattern_select_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= VISIBLE_PATTERNS) return;
    int patternIndex = pattern_page * VISIBLE_PATTERNS + idx;
    if (patternIndex < 0 || patternIndex >= Config::MAX_PATTERNS) return;
    extern void selectPatternOnMaster(int patternIndex);
    selectPatternOnMaster(patternIndex);  // FIX: sends UART to P4 + UDP to master
    pattern_apply_page();
}

void ui_create_patterns_screen() {
    scr_patterns = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_patterns, RED808_BG, 0);
    lv_obj_clear_flag(scr_patterns, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_patterns);

#if PORTRAIT_MODE
    lv_obj_t* shell = create_section_shell(scr_patterns, 12, 4, UI_W - 24, UI_H - 72);
#else
    lv_obj_t* shell = create_section_shell(scr_patterns, 18, 4, 988, 500);
#endif

    lv_obj_t* title = lv_label_create(shell);
    lv_label_set_text(title, LV_SYMBOL_LIST "  PATTERN BANKS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_set_pos(title, 8, 4);

    lv_obj_t* subtitle = lv_label_create(shell);
    lv_label_set_text(subtitle, "Fast bank access with page browsing and pattern density preview");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subtitle, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(subtitle, 8, 32);

    pattern_summary_label = lv_label_create(shell);
    lv_obj_set_style_text_font(pattern_summary_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pattern_summary_label, RED808_INFO, 0);
#if PORTRAIT_MODE
    lv_obj_set_pos(pattern_summary_label, 8, 50);
#else
    lv_obj_set_pos(pattern_summary_label, 680, 8);
#endif

    pattern_prev_btn = lv_btn_create(shell);
    lv_obj_set_size(pattern_prev_btn, 34, 26);
#if PORTRAIT_MODE
    lv_obj_set_pos(pattern_prev_btn, UI_W - 120, 34);
#else
    lv_obj_set_pos(pattern_prev_btn, 820, 34);
#endif
    lv_obj_add_event_cb(pattern_prev_btn, [](lv_event_t*) {
        pattern_page = (pattern_page == 0) ? (pattern_page_count() - 1) : (pattern_page - 1);
        pattern_apply_page();
    }, LV_EVENT_PRESSED, NULL);
    lv_obj_t* prev_lbl = lv_label_create(pattern_prev_btn);
    lv_label_set_text(prev_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(prev_lbl);

    pattern_page_label = lv_label_create(shell);
    lv_obj_set_style_text_font(pattern_page_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pattern_page_label, RED808_SUCCESS, 0);
#if PORTRAIT_MODE
    lv_obj_set_pos(pattern_page_label, UI_W - 80, 38);
#else
    lv_obj_set_pos(pattern_page_label, 864, 38);
#endif

    pattern_next_btn = lv_btn_create(shell);
    lv_obj_set_size(pattern_next_btn, 34, 26);
#if PORTRAIT_MODE
    lv_obj_set_pos(pattern_next_btn, UI_W - 50, 34);
#else
    lv_obj_set_pos(pattern_next_btn, 950, 34);
#endif
    lv_obj_add_event_cb(pattern_next_btn, [](lv_event_t*) {
        pattern_page = (pattern_page + 1) % pattern_page_count();
        pattern_apply_page();
    }, LV_EVENT_PRESSED, NULL);
    lv_obj_t* next_lbl = lv_label_create(pattern_next_btn);
    lv_label_set_text(next_lbl, LV_SYMBOL_RIGHT);
    lv_obj_center(next_lbl);

#if PORTRAIT_MODE
    int cols = 2, rows = 4;
    int btn_w = 250, btn_h = 180;
    int gap = 12;
    int x_start = (UI_W - cols * btn_w - (cols - 1) * gap) / 2;
    int y_start = 150;
#else
    int cols = 4, rows = 2;
    int btn_w = 224, btn_h = 182;
    int gap = 16;
    int x_start = (1024 - cols * btn_w - (cols - 1) * gap) / 2;
    int y_start = 156;
#endif

    for (int i = 0; i < VISIBLE_PATTERNS; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = x_start + col * (btn_w + gap);
        int y = y_start + row * (btn_h + gap);

        lv_obj_t* btn = lv_btn_create(scr_patterns);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x, y);
        apply_stable_button_style(btn, RED808_SURFACE, RED808_BORDER);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_event_cb(btn, pattern_select_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);
        pattern_btns[i] = btn;

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "PATTERN 01");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, RED808_TEXT, 0);
        lv_obj_set_pos(lbl, 18, 22);
        pattern_labels[i] = lbl;

        lv_obj_t* meta = lv_label_create(btn);
        lv_label_set_text(meta, "Bank snapshot\n0 active steps");
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(meta, RED808_TEXT_DIM, 0);
        lv_obj_set_style_text_line_space(meta, 6, 0);
        lv_obj_set_pos(meta, 18, 84);
        pattern_meta_labels[i] = meta;
    }

    pattern_page = currentPattern / VISIBLE_PATTERNS;
    pattern_apply_page();
}

void ui_update_patterns() {
    static int prevPattern = -1;
    static int prevPage = -1;

    int desiredPage = currentPattern / VISIBLE_PATTERNS;
    if (desiredPage != pattern_page) {
        pattern_page = desiredPage;
    }

    if (currentPattern != prevPattern || pattern_page != prevPage) {
        prevPattern = currentPattern;
        prevPage = pattern_page;
        pattern_apply_page();
    } else if (pattern_summary_label) {
        lv_label_set_text_fmt(pattern_summary_label, "CURRENT %02d  %s", currentPattern + 1, patterns[currentPattern].name.c_str());
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
#if PORTRAIT_MODE
static constexpr int SD_LEFT_W  = 576;   // file browser panel width
static constexpr int SD_RIGHT_W = 576;   // pad assignment panel width
static constexpr int SD_TOP     = 4;
static constexpr int SD_H       = 440;   // panel height
static constexpr int SD_R_TOP   = 454;   // right panel y
#else
static constexpr int SD_LEFT_W  = 580;   // file browser panel width
static constexpr int SD_RIGHT_W = 420;   // pad assignment panel width
static constexpr int SD_TOP     = 4;
static constexpr int SD_H       = 500;   // panel height
static constexpr int SD_R_TOP   = SD_TOP;
#endif

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

// MIDI section widgets
static lv_obj_t* sd_wav_section  = NULL;
static lv_obj_t* sd_midi_section = NULL;
static lv_obj_t* sd_midi_info_lbl    = NULL;
static lv_obj_t* sd_midi_status_lbl  = NULL;
static lv_obj_t* sd_midi_load_btn    = NULL;
static lv_obj_t* sd_midi_pat_btns[6] = {};    // slots 0-5 (P01-P06, valid master patterns)
static int       sd_midi_target_slot  = 0;    // currently selected target pattern slot
static bool      sd_is_midi_selected  = false;

// Forward declarations
static void sd_refresh_filelist();
static void sd_file_btn_cb(lv_event_t* e);
static void sd_pad_btn_cb(lv_event_t* e);
static void sd_load_btn_cb(lv_event_t* e);
static void sd_back_btn_cb(lv_event_t* e);
static void sd_midi_pat_btn_cb(lv_event_t* e);
static void sd_midi_load_btn_cb(lv_event_t* e);

// Send loadSample UDP  (also callable from outside)
void ui_sdcard_send_load_sample(int pad, const char* family, const char* filename) {
    JsonDocument doc(sramAllocatorPtr);
    doc["cmd"]      = "loadSample";
    doc["family"]   = family;
    doc["filename"] = filename;
    doc["pad"]      = pad;
    sendUDPCommand(doc);
    RED808_LOG_PRINTF("[SD] LoadSample pad=%d family=%s file=%s\n", pad, family, filename);
}

// Mount SD card via SD_MMC (1-bit mode)
bool sd_try_mount() {
    if (sd_mounted) return true;
    // Drive EXIO4 (D3/CS) HIGH so the card enters SDMMC mode (not SPI)
    io_ext_sd_disable();  // EXIO4=1 → D3 pulled high
    delay(10);
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
    if (!SD_MMC.begin(SD_MOUNT_POINT, true /* 1-bit mode */)) {
        RED808_LOG_PRINTLN("[SD] Mount failed");
        return false;
    }
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        RED808_LOG_PRINTLN("[SD] No SD card");
        SD_MMC.end();
        return false;
    }
    sd_mounted = true;
    RED808_LOG_PRINTF("[SD] Mounted OK. Type=%d Size=%lluMB\n", cardType,
                  SD_MMC.cardSize() / (1024ULL * 1024ULL));
    return true;
}

// Free heap-allocated user data from file buttons before clearing the list
static void sd_free_file_userdata() {
    if (!sd_file_list) return;
    uint32_t cnt = lv_obj_get_child_cnt(sd_file_list);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(sd_file_list, i);
        void* ud = lv_obj_get_user_data(child);
        if (ud) { lv_mem_free(ud); lv_obj_set_user_data(child, NULL); }
    }
}

// Populate file list from sd_current_dir
static void sd_refresh_filelist() {
    if (!sd_left_panel) return;
    if (!sd_file_list) return;

    // Free user data before clearing items (prevents memory leak)
    sd_free_file_userdata();
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
        // Only show .wav/.WAV, .mid/.MID files and directories
        if (!is_dir) {
            size_t nlen = strlen(name);
            bool is_wav = (nlen > 4 && strcasecmp(name + nlen - 4, ".wav") == 0);
            bool is_mid = (nlen > 4 && strcasecmp(name + nlen - 4, ".mid") == 0);
            if (!is_wav && !is_mid) { entry.close(); entry = dir.openNextFile(); continue; }
        }

        lv_obj_t* btn = lv_btn_create(sd_file_list);
        lv_obj_set_size(btn, 540, 44);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_color_t btn_color = is_dir ? lv_color_hex(0x1A3A5C) : lv_color_hex(0x1A2A1A);
        lv_obj_set_style_bg_color(btn, btn_color, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x446688), LV_STATE_PRESSED);

        lv_obj_t* lbl = lv_label_create(btn);
        char display[80];
        size_t nlen2 = strlen(name);
        bool entry_is_mid = !is_dir && nlen2 > 4 && strcasecmp(name + nlen2 - 4, ".mid") == 0;
        snprintf(display, sizeof(display),
            is_dir      ? LV_SYMBOL_DIRECTORY "  %s" :
            entry_is_mid ? LV_SYMBOL_FILE "  %s [MIDI]" :
                           LV_SYMBOL_AUDIO "  %s", name);
        lv_label_set_text(lbl, display);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl,
            is_dir       ? RED808_CYAN :
            entry_is_mid ? RED808_WARNING :
                           RED808_SUCCESS, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);

        // Store entry name in user data (heap allocated, freed on lv_obj_clean)
        char* name_copy = (char*)lv_mem_alloc(strlen(name) + 2);
        if (name_copy) {
            size_t nlen3 = strlen(name);
            bool ud_is_mid = !is_dir && nlen3 > 4 && strcasecmp(name + nlen3 - 4, ".mid") == 0;
            name_copy[0] = is_dir ? 'D' : (ud_is_mid ? 'M' : 'F');  // D=dir, F=wav, M=midi
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
        // Select file (WAV or MIDI)
        strncpy(sd_current_file, name, sizeof(sd_current_file) - 1);
        sd_current_file[sizeof(sd_current_file) - 1] = '\0';

        sd_is_midi_selected = (type == 'M');

        if (sd_is_midi_selected) {
            // Switch right panel to MIDI mode
            if (sd_wav_section)  lv_obj_add_flag(sd_wav_section,   LV_OBJ_FLAG_HIDDEN);
            if (sd_midi_section) lv_obj_clear_flag(sd_midi_section, LV_OBJ_FLAG_HIDDEN);
            // Update info label
            if (sd_midi_info_lbl) {
                char info[80];
                snprintf(info, sizeof(info), LV_SYMBOL_FILE "  %s", name);
                lv_label_set_text(sd_midi_info_lbl, info);
            }
            if (sd_midi_load_btn) lv_obj_clear_state(sd_midi_load_btn, LV_STATE_DISABLED);
            if (sd_midi_status_lbl) lv_label_set_text(sd_midi_status_lbl, "");
        } else {
            // Switch right panel to WAV mode
            if (sd_midi_section) lv_obj_add_flag(sd_midi_section,  LV_OBJ_FLAG_HIDDEN);
            if (sd_wav_section)  lv_obj_clear_flag(sd_wav_section,  LV_OBJ_FLAG_HIDDEN);
            char sel[128];
            snprintf(sel, sizeof(sel), "%s / %s", sd_current_family, sd_current_file);
            if (sd_selected_lbl) lv_label_set_text(sd_selected_lbl, sel);
            if (sd_load_btn) lv_obj_clear_state(sd_load_btn, LV_STATE_DISABLED);
        }  // end WAV/MIDI else
    }  // end type != 'D'
}

// Back button: go up one level
static void sd_back_btn_cb(lv_event_t* e) {
    (void)e;
    // Go to root
    strncpy(sd_current_dir, "/", sizeof(sd_current_dir));
    sd_current_family[0] = '\0';
    sd_current_file[0] = '\0';
    sd_is_midi_selected = false;
    // Restore WAV section
    if (sd_midi_section) lv_obj_add_flag(sd_midi_section,  LV_OBJ_FLAG_HIDDEN);
    if (sd_wav_section)  lv_obj_clear_flag(sd_wav_section,  LV_OBJ_FLAG_HIDDEN);
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

// Load button (WAV): send loadSample UDP
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

// MIDI pattern slot selector (slots 6-15)
static void sd_midi_pat_btn_cb(lv_event_t* e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot < 0 || slot > 5) return;
    sd_midi_target_slot = slot;
    // Highlight selected button
    for (int i = 0; i < 6; i++) {
        if (!sd_midi_pat_btns[i]) continue;
        bool selected = (i == slot);
        lv_obj_set_style_bg_color(sd_midi_pat_btns[i],
            selected ? RED808_ACCENT : lv_color_hex(0x1A2A3A), 0);
        lv_obj_set_style_border_color(sd_midi_pat_btns[i],
            selected ? RED808_CYAN : lv_color_hex(0x334455), 0);
    }
}

// MIDI load button: parse MIDI → fill pattern → send to master + P4
static void sd_midi_load_btn_cb(lv_event_t* e) {
    (void)e;
    if (sd_current_file[0] == '\0') return;

    // Build full path
    char path[192];
    if (strcmp(sd_current_dir, "/") == 0)
        snprintf(path, sizeof(path), "/%s", sd_current_file);
    else
        snprintf(path, sizeof(path), "%s/%s", sd_current_dir, sd_current_file);

    // Show progress
    if (sd_midi_status_lbl) lv_label_set_text(sd_midi_status_lbl, "Parsing MIDI...");

    // Parse MIDI file
    bool steps[Config::MAX_TRACKS][Config::MAX_STEPS];
    memset(steps, 0, sizeof(steps));
    char name[12] = "";
    int   found = 0;
    float midi_bpm = 0.0f;
    int   plen = Config::STEPS_PER_BANK;
    bool ok = midi_load_pattern(path, steps, name, sizeof(name), &found, &midi_bpm, 9, &plen);

    if (!ok || found == 0) {
        if (sd_midi_status_lbl)
            lv_label_set_text(sd_midi_status_lbl,
                LV_SYMBOL_WARNING " No GM drum steps found (ch.10)");
        return;
    }

    // Apply BPM from MIDI file if present
    if (midi_bpm > 0.0f) {
        extern void applyBPMPrecise(float bpm, bool sendToMaster);
        applyBPMPrecise(midi_bpm, true);
    }

    // Apply pattern locally + send to Master + P4
    handleMidiPatternLoaded(sd_midi_target_slot, steps, name, plen);

    // Feedback + navigate to sequencer after delay
    char msg[72];
    if (midi_bpm > 0.0f)
        snprintf(msg, sizeof(msg), LV_SYMBOL_OK " %d steps → P%02d '%s' @ %.0f BPM",
                 found, sd_midi_target_slot + 1, name, midi_bpm);
    else
        snprintf(msg, sizeof(msg), LV_SYMBOL_OK " %d steps → P%02d '%s'",
                 found, sd_midi_target_slot + 1, name);
    if (sd_midi_status_lbl) lv_label_set_text(sd_midi_status_lbl, msg);

    lv_timer_t* t = lv_timer_create([](lv_timer_t* t) {
        nav_to(SCREEN_SEQUENCER, scr_sequencer);
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
#if PORTRAIT_MODE
    lv_obj_set_pos(sd_status_lbl, 300, 8);
#else
    lv_obj_set_pos(sd_status_lbl, 420, 8);
#endif

    // Path label
    sd_path_lbl = lv_label_create(sd_left_panel);
    lv_label_set_text(sd_path_lbl, "/");
    lv_obj_set_style_text_font(sd_path_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_path_lbl, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(sd_path_lbl, 8, 30);

    // Scrollable file list container
    sd_file_list = lv_obj_create(sd_left_panel);
#if PORTRAIT_MODE
    lv_obj_set_size(sd_file_list, SD_LEFT_W - 24, SD_H - 80);
#else
    lv_obj_set_size(sd_file_list, 556, 460);
#endif
    lv_obj_set_pos(sd_file_list, 4, 54);
    lv_obj_set_style_bg_opa(sd_file_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sd_file_list, 0, 0);
    lv_obj_set_style_pad_row(sd_file_list, 4, 0);
    lv_obj_set_style_pad_all(sd_file_list, 2, 0);
    lv_obj_set_flex_flow(sd_file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(sd_file_list, LV_DIR_VER);
    lv_obj_add_flag(sd_file_list, LV_OBJ_FLAG_SCROLLABLE);

    // ── Right Panel: pad assignment (WAV) / pattern slot (MIDI) ──────────
    sd_right_panel = lv_obj_create(scr_sdcard);
    lv_obj_set_size(sd_right_panel, SD_RIGHT_W, SD_H);
#if PORTRAIT_MODE
    lv_obj_set_pos(sd_right_panel, 4, SD_R_TOP);
#else
    lv_obj_set_pos(sd_right_panel, SD_LEFT_W + 8, SD_TOP);
#endif
    lv_obj_set_style_bg_color(sd_right_panel, lv_color_hex(0x0D1520), 0);
    lv_obj_set_style_border_color(sd_right_panel, RED808_ACCENT, 0);
    lv_obj_set_style_border_width(sd_right_panel, 1, 0);
    lv_obj_set_style_radius(sd_right_panel, 8, 0);
    lv_obj_set_style_pad_all(sd_right_panel, 8, 0);
    lv_obj_clear_flag(sd_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // ── WAV section (default visible) ─────────────────────────────────────
    sd_wav_section = lv_obj_create(sd_right_panel);
    lv_obj_set_size(sd_wav_section, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(sd_wav_section, 0, 0);
    lv_obj_set_style_bg_opa(sd_wav_section, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sd_wav_section, 0, 0);
    lv_obj_set_style_pad_all(sd_wav_section, 0, 0);
    lv_obj_clear_flag(sd_wav_section, LV_OBJ_FLAG_SCROLLABLE);

    // "ASSIGN TO PAD" title
    lv_obj_t* assign_lbl = lv_label_create(sd_wav_section);
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

        lv_obj_t* btn = lv_btn_create(sd_wav_section);
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
    sd_selected_lbl = lv_label_create(sd_wav_section);
    lv_label_set_text(sd_selected_lbl, "");
    lv_obj_set_width(sd_selected_lbl, 390);
    lv_obj_set_style_text_font(sd_selected_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_selected_lbl, RED808_SUCCESS, 0);
    lv_obj_set_style_text_align(sd_selected_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(sd_selected_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(sd_selected_lbl, 12, 264);

    // LOAD WAV button
    sd_load_btn = lv_btn_create(sd_wav_section);
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

    // ── MIDI section (hidden by default) ──────────────────────────────────
    sd_midi_section = lv_obj_create(sd_right_panel);
    lv_obj_set_size(sd_midi_section, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(sd_midi_section, 0, 0);
    lv_obj_set_style_bg_opa(sd_midi_section, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sd_midi_section, 0, 0);
    lv_obj_set_style_pad_all(sd_midi_section, 0, 0);
    lv_obj_clear_flag(sd_midi_section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sd_midi_section, LV_OBJ_FLAG_HIDDEN);

    // MIDI title
    lv_obj_t* midi_title = lv_label_create(sd_midi_section);
    lv_label_set_text(midi_title, LV_SYMBOL_AUDIO "  LOAD MIDI TO PATTERN");
    lv_obj_set_style_text_font(midi_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(midi_title, RED808_WARNING, 0);
    lv_obj_set_pos(midi_title, 8, 4);

    // MIDI file info label
    sd_midi_info_lbl = lv_label_create(sd_midi_section);
    lv_label_set_text(sd_midi_info_lbl, "");
    lv_obj_set_style_text_font(sd_midi_info_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_midi_info_lbl, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(sd_midi_info_lbl, 8, 32);
    lv_obj_set_width(sd_midi_info_lbl, SD_RIGHT_W - 24);
    lv_label_set_long_mode(sd_midi_info_lbl, LV_LABEL_LONG_DOT);

    // "SELECT TARGET SLOT:"
    lv_obj_t* slot_lbl = lv_label_create(sd_midi_section);
    lv_label_set_text(slot_lbl, "SELECT TARGET PATTERN SLOT:");
    lv_obj_set_style_text_font(slot_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(slot_lbl, RED808_CYAN, 0);
    lv_obj_set_pos(slot_lbl, 8, 58);

    // Pattern slot grid 2×3 (P01-P06 = slots 0-5, valid master patterns)
    {
        int rp_cw     = SD_RIGHT_W - 16;   // right panel content width
        int mp_btn_w  = (rp_cw - 10) / 2;  // 2 cols, gap=10
        int mp_btn_h  = 44;
        int mp_gap    = 6;
        int mp_x0     = 0, mp_y0 = 80;
        for (int i = 0; i < 6; i++) {
            int col = i % 2, row = i / 2;
            int slot_id = i;  // slots 0-5
            int bx = mp_x0 + col * (mp_btn_w + 10);
            int by = mp_y0 + row * (mp_btn_h + mp_gap);

            lv_obj_t* btn = lv_btn_create(sd_midi_section);
            lv_obj_set_size(btn, mp_btn_w, mp_btn_h);
            lv_obj_set_pos(btn, bx, by);
            lv_obj_set_style_bg_color(btn,
                slot_id == sd_midi_target_slot ? RED808_ACCENT : lv_color_hex(0x1A2A3A), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn,
                slot_id == sd_midi_target_slot ? RED808_CYAN : lv_color_hex(0x334455), 0);
            lv_obj_set_style_radius(btn, 8, 0);

            lv_obj_t* bl = lv_label_create(btn);
            char bname[8];
            snprintf(bname, sizeof(bname), "P%02d", slot_id + 1);
            lv_label_set_text(bl, bname);
            lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(bl,
                slot_id == sd_midi_target_slot ? lv_color_white() : RED808_TEXT_DIM, 0);
            lv_obj_center(bl);

            sd_midi_pat_btns[i] = btn;
            lv_obj_add_event_cb(btn, sd_midi_pat_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)slot_id);
        }
    }

    // LOAD MIDI PATTERN button
    sd_midi_load_btn = lv_btn_create(sd_midi_section);
    lv_obj_set_size(sd_midi_load_btn, SD_RIGHT_W - 16, 56);
    lv_obj_set_pos(sd_midi_load_btn, 0, 338);
    lv_obj_set_style_bg_color(sd_midi_load_btn, RED808_WARNING, 0);
    lv_obj_set_style_bg_color(sd_midi_load_btn, lv_color_hex(0x554400), LV_STATE_DISABLED);
    lv_obj_set_style_radius(sd_midi_load_btn, 10, 0);
    lv_obj_add_state(sd_midi_load_btn, LV_STATE_DISABLED);
    lv_obj_add_event_cb(sd_midi_load_btn, sd_midi_load_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* midi_load_lbl = lv_label_create(sd_midi_load_btn);
    lv_label_set_text(midi_load_lbl, LV_SYMBOL_DOWNLOAD "  LOAD MIDI PATTERN");
    lv_obj_set_style_text_font(midi_load_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(midi_load_lbl, lv_color_black(), 0);
    lv_obj_center(midi_load_lbl);

    // MIDI status label
    sd_midi_status_lbl = lv_label_create(sd_midi_section);
    lv_label_set_text(sd_midi_status_lbl, "");
    lv_obj_set_style_text_font(sd_midi_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_midi_status_lbl, RED808_SUCCESS, 0);
    lv_obj_set_pos(sd_midi_status_lbl, 8, 402);
    lv_obj_set_width(sd_midi_status_lbl, SD_RIGHT_W - 24);
    lv_label_set_long_mode(sd_midi_status_lbl, LV_LABEL_LONG_WRAP);

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
// PERFORMANCE SCREEN - Hardware assignment map / control map
// ============================================================================

static const char* const dfRotaryAssignNames[] = {
    "R1: DELAY Param A/B/C  | BTN: Cycle Param",
    "R2: FLANGER Param A/B/C| BTN: Cycle Param",
    "R3: COMP Param A/B/C   | BTN: Cycle Param",
    "R4: BPM Tempo    | BTN: Reset BPM"
};

static const char* const analogPotAssignNames[] = {
    "P1: Master Volume (coarse)",
    "P2: Filter Cutoff (20..20kHz)",
    "P3: Resonance Q (1.0..10.0)",
    "P4: Distortion Drive (0..100%)"
};

static const char* const m5EncoderAssignNames[] = {
    "Mod 1 (Tracks 0-3): Vol + Mute",
    "Mod 2 (Tracks 4-7): Vol + Mute"
};

static void perf_update_bb_assignment_labels() {
    for (int i = 0; i < BYTEBUTTON_TOTAL_BUTTONS; i++) {
        if (!perf_bb_action_labels[i]) continue;
        uint8_t action = byteButtonActionMap[i];
        if (action >= BB_ACTION_COUNT) action = BB_ACTION_MENU;
        lv_label_set_text(perf_bb_action_labels[i], byteButtonActionNames[action]);
    }
}

static void perf_bb_action_next_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= BYTEBUTTON_TOTAL_BUTTONS) return;
    byteButtonActionMap[idx] = (uint8_t)((byteButtonActionMap[idx] + 1) % BB_ACTION_COUNT);
    perf_update_bb_assignment_labels();
    updateByteButtonLeds();
}

void ui_create_performance_screen() {
    scr_performance = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_performance, RED808_BG, 0);
    ui_create_header(scr_performance);

    lv_obj_t* title = lv_label_create(scr_performance);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  BUTTONS & CONTROL MAP");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    int y = 30;
    int col1_x = 24, col2_x = 300;
    int section_gap = 8;

    // ── SECTION: ByteButton Module 1 (I2C Hub CH4) ──
    lv_obj_t* bb1_title = lv_label_create(scr_performance);
    lv_label_set_text(bb1_title, LV_SYMBOL_UP "  BB MODULE 1  (Hub CH4)  — Transport + FX mute");
    lv_obj_set_style_text_font(bb1_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bb1_title, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_pos(bb1_title, col1_x, y);
    y += 24;

    // ── SECTION: ByteButton Module 2 (I2C Hub CH5) ──
    lv_obj_t* bb2_title = lv_label_create(scr_performance);
    lv_label_set_text(bb2_title, LV_SYMBOL_UP "  BB MODULE 2  (Hub CH5)  — Navigation + BPM");
    lv_obj_set_style_text_font(bb2_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bb2_title, lv_color_hex(0xFF8C00), 0);
    lv_obj_set_pos(bb2_title, col1_x + 330, y - 24);  // same row as BB1 header, right column

    for (int i = 0; i < BYTEBUTTON_TOTAL_BUTTONS; i++) {
        int bbCol = (i < 8) ? 0 : 1;
        int bbRow = i % 8;
        int baseX = (bbCol == 0) ? col1_x : (col1_x + 330);
        int rowY = y + bbRow * 30;

        // Button number chip
        lv_obj_t* chip = lv_obj_create(scr_performance);
        lv_obj_set_size(chip, 42, 26);
        lv_obj_set_pos(chip, baseX, rowY);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0x1A3050), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 4, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_color_t chipColor = (i < BYTEBUTTON_BUTTONS) ? lv_color_hex(0x00D4FF) : lv_color_hex(0xFF8C00);
        lv_obj_set_style_border_color(chip, chipColor, 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chip, perf_bb_action_next_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* num = lv_label_create(chip);
        lv_label_set_text_fmt(num, "%d-%d", (i / BYTEBUTTON_BUTTONS) + 1, (i % BYTEBUTTON_BUTTONS) + 1);
        lv_obj_set_style_text_font(num, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num, (i < BYTEBUTTON_BUTTONS) ? lv_color_hex(0x00D4FF) : lv_color_hex(0xFF8C00), 0);
        lv_obj_center(num);

        // Function label
        lv_obj_t* fn = lv_label_create(scr_performance);
        lv_label_set_text(fn, "--");
        lv_obj_set_style_text_font(fn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(fn, RED808_TEXT, 0);
        lv_obj_set_pos(fn, baseX + 52, rowY + 4);
        perf_bb_action_labels[i] = fn;

        // In LIVE mode label (right side)
        if (i < Config::MAX_SAMPLES) {
            lv_obj_t* live_fn = lv_label_create(scr_performance);
            lv_label_set_text_fmt(live_fn, "LIVE: Pad %d (%s)", i + 1, trackNames[i]);
            lv_obj_set_style_text_font(live_fn, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(live_fn, RED808_TEXT_DIM, 0);
            lv_obj_set_pos(live_fn, col2_x + 320, rowY + 5);
        }
    }

    y += 8 * 30 + 4;

    perf_update_bb_assignment_labels();

    y += section_gap;

    // ── SECTION: DFRobot Gravity Rotary+Button ──
    lv_obj_t* df_title = lv_label_create(scr_performance);
    lv_label_set_text(df_title, "DFROBOT ROTARY+BUTTON  (4x)");
    lv_obj_set_style_text_font(df_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(df_title, lv_color_hex(0x39FF14), 0);
    lv_obj_set_pos(df_title, col1_x, y);
    y += 24;

    for (int i = 0; i < 4; i++) {
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

    // ── SECTION: Analog Pots (I2C ADC) + Unit Fader ──
    lv_obj_t* pot_title = lv_label_create(scr_performance);
    lv_label_set_text(pot_title, "ANALOG CONTROLS  (4x POT + 1x UNIT FADER)");
    lv_obj_set_style_text_font(pot_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pot_title, lv_color_hex(0xD4FF00), 0);
    lv_obj_set_pos(pot_title, col1_x, y);
    y += 24;

    for (int i = 0; i < 4; i++) {
        lv_obj_t* chip = lv_obj_create(scr_performance);
        lv_obj_set_size(chip, 42, 26);
        lv_obj_set_pos(chip, col1_x, y);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0x2A2F12), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 4, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_border_color(chip, lv_color_hex(0xD4FF00), 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* num = lv_label_create(chip);
        lv_label_set_text_fmt(num, "P%d", i + 1);
        lv_obj_set_style_text_font(num, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num, lv_color_hex(0xD4FF00), 0);
        lv_obj_center(num);

        lv_obj_t* fn = lv_label_create(scr_performance);
        lv_label_set_text(fn, analogPotAssignNames[i]);
        lv_obj_set_style_text_font(fn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(fn, RED808_TEXT, 0);
        lv_obj_set_pos(fn, col1_x + 52, y + 4);
        y += 30;
    }

    lv_obj_t* fader_fn = lv_label_create(scr_performance);
    lv_label_set_text(fader_fn, "F1: M5 Unit Fader -> BPM fine (+/-0.1)");
    lv_obj_set_style_text_font(fader_fn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fader_fn, RED808_TEXT, 0);
    lv_obj_set_pos(fader_fn, col1_x + 52, y + 2);

    y += section_gap + 28;

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

    for (int i = 0; i < 8; i++) perf_runtime_values[i] = NULL;
}

void ui_update_performance() {
    if (!scr_performance || !perf_runtime_values[0]) return;

    static uint32_t last_ms = 0;
    if ((lv_tick_get() - last_ms) < 250) return;

    static const char* fx_names[] = {"Chorus", "Delay", "Reverb"};
    lv_label_set_text(perf_runtime_values[0], volumeMode == VOL_SEQUENCER ? "Sequencer" : "Live Pads");
    lv_label_set_text(perf_runtime_values[1], theme_presets[currentTheme].name);
    lv_label_set_text(perf_runtime_values[2], filterSelectedTrack == -1 ? "Master" : trackNames[filterSelectedTrack]);
    lv_label_set_text(perf_runtime_values[3], fx_names[constrain(filterSelectedFX, 0, 2)]);
    lv_label_set_text_fmt(perf_runtime_values[4], "%d / %s", currentPattern + 1, kitNames[constrain(currentKit, 0, 2)]);
    lv_label_set_text(perf_runtime_values[5], isPlaying ? "Running" : "Stopped");
    lv_label_set_text_fmt(perf_runtime_values[6], "%s | %s | %s",
                          wifiConnected ? "WIFI" : "OFF",
                          udpConnected ? "UDP" : "NO UDP",
                          masterConnected ? "MASTER" : "WAIT");
    lv_obj_set_style_text_color(perf_runtime_values[6],
        (wifiConnected && udpConnected) ? (masterConnected ? RED808_SUCCESS : RED808_WARNING) : RED808_ERROR, 0);

    const uint32_t h_total = SCREEN_WIDTH + LCD_HSYNC_PULSE_WIDTH + LCD_HSYNC_BACK_PORCH + LCD_HSYNC_FRONT_PORCH;
    const uint32_t v_total = SCREEN_HEIGHT + LCD_VSYNC_PULSE_WIDTH + LCD_VSYNC_BACK_PORCH + LCD_VSYNC_FRONT_PORCH;
    const float panel_hz = (float)LCD_PCLK_HZ / (float)(h_total * v_total);
    lv_label_set_text_fmt(perf_runtime_values[7], "touch:%dms ui:%lums lcd:%.1fHz",
                          (int)LV_INDEV_DEF_READ_PERIOD,
                          (unsigned long)uiLastIntervalMs,
                          (double)panel_hz);
    last_ms = lv_tick_get();
}

void ui_create_samples_screen() { scr_samples = NULL; }

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
            // All POST lines shown — go to menu
            lv_timer_del(timer);
            boot_timer = NULL;
            nav_to(SCREEN_MENU, scr_menu);
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
    lv_obj_set_size(top_line, UI_W, 2);
    lv_obj_set_pos(top_line, 0, 0);
    lv_obj_set_style_bg_color(top_line, lv_color_hex(0x0066AA), 0);
    lv_obj_set_style_bg_opa(top_line, LV_OPA_60, 0);
    lv_obj_set_style_border_width(top_line, 0, 0);
    lv_obj_clear_flag(top_line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Brand name (large, centered) ──
    lv_obj_t* brand = lv_label_create(scr_boot);
    lv_label_set_text(brand, "BLUE808");
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(brand, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_letter_space(brand, 8, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 18);

    // ── Subtitle ──
    lv_obj_t* subtitle = lv_label_create(scr_boot);
    lv_label_set_text(subtitle, "SLAVE CONTROLLER  V6");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x66AADD), 0);
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
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x4488CC), 0);
        lv_obj_set_pos(lbl, 42, 110 + i * 27);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        boot_text_lines[i] = lbl;
    }

    // ── Blinking cursor ──
    boot_cursor_lbl = lv_label_create(scr_boot);
    lv_label_set_text(boot_cursor_lbl, "root@blue808:~$ _");
    lv_obj_set_style_text_font(boot_cursor_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(boot_cursor_lbl, lv_color_hex(0x66AADD), 0);
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
#if PORTRAIT_MODE
    lv_obj_set_pos(build_info, UI_W - 220, 110);
#else
    lv_obj_set_pos(build_info, 780, 110);
#endif

    // ── Bottom status bar ──
    boot_status_lbl = lv_label_create(scr_boot);
    lv_label_set_text(boot_status_lbl, "INITIALIZING...");
    lv_obj_set_style_text_font(boot_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(boot_status_lbl, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_opa(boot_status_lbl, LV_OPA_40, 0);
    lv_obj_align(boot_status_lbl, LV_ALIGN_BOTTOM_MID, 0, -12);

    // ── Bottom accent line ──
    lv_obj_t* bot_line = lv_obj_create(scr_boot);
    lv_obj_set_size(bot_line, UI_W, 2);
    lv_obj_set_pos(bot_line, 0, UI_H - 2);
    lv_obj_set_style_bg_color(bot_line, lv_color_hex(0x0066AA), 0);
    lv_obj_set_style_bg_opa(bot_line, LV_OPA_40, 0);
    lv_obj_set_style_border_width(bot_line, 0, 0);
    lv_obj_clear_flag(bot_line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Developer branding (bottom-center): name only ──
    extern const lv_img_dsc_t img_dev_name;

    lv_obj_t* dev_name = lv_img_create(scr_boot);
    lv_img_set_src(dev_name, &img_dev_name);
    lv_obj_set_pos(dev_name, UI_W - 220, UI_H - 170);

    // ── Footer: dedication ──
    lv_obj_t* footer_lbl = lv_label_create(scr_boot);
    lv_label_set_text(footer_lbl, "Un legado de a\xC3\xB1os de amistad");
    lv_obj_set_style_text_font(footer_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(footer_lbl, lv_color_hex(0x66AADD), 0);
    lv_obj_set_style_text_opa(footer_lbl, LV_OPA_50, 0);
    lv_obj_align(footer_lbl, LV_ALIGN_BOTTOM_MID, 0, -36);

    boot_state = 0;

    boot_timer = lv_timer_create(boot_timer_cb, 40, NULL);
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
    lv_label_set_text_fmt(lbl_circ_info, "STEP  0 / %d", Config::STEPS_PER_BANK);
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
    for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
        float angle = -(float)M_PI / 2.0f + s * 2.0f * (float)M_PI / (float)Config::STEPS_PER_BANK;
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

    // --- TRACK SELECTOR (right panel / below circle in portrait) ---
    // 16 track buttons
#if PORTRAIT_MODE
    static constexpr int PANEL_X  = 12;
    static constexpr int BTN_W    = 130;
    static constexpr int BTN_H    = 28;
    static constexpr int BTN_GAP  = 6;
    static constexpr int COL_GAP  = 6;
    static constexpr int COLS     = 4;
    static constexpr int Y_START  = 600;
#else
    static constexpr int PANEL_X  = 555;
    static constexpr int BTN_W    = 200;
    static constexpr int BTN_H    = 28;
    static constexpr int BTN_GAP  = 6;
    static constexpr int COL_GAP  = 10;
    static constexpr int COLS     = 2;
    // Total height of one 8-button column
    static constexpr int COL_H    = 8 * BTN_H + 7 * BTN_GAP;  // 266
    // Vertically center in available area (y=78 to y=580 = 502px)
    static constexpr int Y_START  = 78 + (502 - COL_H) / 2;   // ~196
#endif

    for (int t = 0; t < Config::MAX_TRACKS; t++) {
#if PORTRAIT_MODE
        int col = t % COLS;
        int row = t / COLS;
#else
        int col = t / 8;
        int row = t % 8;
#endif
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

    int active_step = (isPlaying && currentStep >= 0 && currentStep < patterns[currentPattern].length)
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
            active_step >= 0 ? active_step + 1 : 0, patterns[currentPattern].length);
    }

    // --- Update step buttons ---
    for (int s = 0; s < Config::STEPS_PER_BANK; s++) {
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
