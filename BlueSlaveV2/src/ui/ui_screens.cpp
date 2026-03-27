// =============================================================================
// ui_screens.cpp - Screen implementations with LVGL
// RED808 V6 BlueSlaveV2 - 1024x600 touch interface
// =============================================================================
#include "ui_screens.h"
#include "ui_theme.h"
#include "system_state.h"
#include "config.h"

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
static lv_obj_t* lbl_bpm = NULL;
static lv_obj_t* lbl_pattern = NULL;
static lv_obj_t* lbl_play = NULL;
static lv_obj_t* lbl_wifi = NULL;

// Sequencer grid buttons
static lv_obj_t* seq_grid[Config::MAX_TRACKS][Config::MAX_STEPS];
static lv_obj_t* seq_track_labels[Config::MAX_TRACKS];
static lv_obj_t* lbl_step_indicator = NULL;

// Volume sliders
static lv_obj_t* vol_sliders[Config::MAX_TRACKS];
static lv_obj_t* vol_labels[Config::MAX_TRACKS];

// Filter UI
static lv_obj_t* filter_arcs[3];       // Delay, Flanger, Compressor
static lv_obj_t* filter_labels[3];
static lv_obj_t* filter_value_labels[3];

// Live pads
static lv_obj_t* live_pads[Config::MAX_SAMPLES];

// ============================================================================
// HEADER BAR (shared across screens)
// ============================================================================
void ui_create_header(lv_obj_t* parent) {
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_size(header, 1024, 48);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, RED808_PANEL, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Logo
    lv_obj_t* logo = lv_label_create(header);
    lv_label_set_text(logo, LV_SYMBOL_AUDIO " RED808 V6");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(logo, RED808_ACCENT, 0);

    // BPM
    lbl_bpm = lv_label_create(header);
    lv_label_set_text_fmt(lbl_bpm, "BPM: %d", currentBPM);
    lv_obj_set_style_text_font(lbl_bpm, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_bpm, RED808_WARNING, 0);

    // Pattern
    lbl_pattern = lv_label_create(header);
    lv_label_set_text_fmt(lbl_pattern, "PTN: %d", currentPattern + 1);
    lv_obj_set_style_text_font(lbl_pattern, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_pattern, RED808_INFO, 0);

    // Play state
    lbl_play = lv_label_create(header);
    lv_label_set_text(lbl_play, isPlaying ? LV_SYMBOL_PAUSE " PLAY" : LV_SYMBOL_PLAY " STOP");
    lv_obj_set_style_text_font(lbl_play, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_play, isPlaying ? RED808_SUCCESS : RED808_ERROR, 0);

    // WiFi
    lbl_wifi = lv_label_create(header);
    lv_label_set_text(lbl_wifi, wifiConnected ? LV_SYMBOL_WIFI " OK" : LV_SYMBOL_CLOSE " OFF");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_wifi, wifiConnected ? RED808_SUCCESS : RED808_TEXT_DIM, 0);
}

void ui_update_header() {
    if (lbl_bpm) lv_label_set_text_fmt(lbl_bpm, "BPM: %d", currentBPM);
    if (lbl_pattern) lv_label_set_text_fmt(lbl_pattern, "PTN: %d", currentPattern + 1);
    if (lbl_play) {
        lv_label_set_text(lbl_play, isPlaying ? LV_SYMBOL_PAUSE " PLAY" : LV_SYMBOL_PLAY " STOP");
        lv_obj_set_style_text_color(lbl_play, isPlaying ? RED808_SUCCESS : RED808_ERROR, 0);
    }
    if (lbl_wifi) {
        lv_label_set_text(lbl_wifi, wifiConnected ? LV_SYMBOL_WIFI " OK" : LV_SYMBOL_CLOSE " OFF");
        lv_obj_set_style_text_color(lbl_wifi, wifiConnected ? RED808_SUCCESS : RED808_TEXT_DIM, 0);
    }
}

// ============================================================================
// MENU SCREEN - 6 big touch buttons
// ============================================================================
static void menu_btn_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0: currentScreen = SCREEN_LIVE; lv_scr_load_anim(scr_live, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false); break;
        case 1: currentScreen = SCREEN_SEQUENCER; lv_scr_load_anim(scr_sequencer, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false); break;
        case 2: currentScreen = SCREEN_VOLUMES; lv_scr_load_anim(scr_volumes, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false); break;
        case 3: currentScreen = SCREEN_FILTERS; lv_scr_load_anim(scr_filters, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false); break;
        case 4: currentScreen = SCREEN_SETTINGS; lv_scr_load_anim(scr_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false); break;
        case 5: currentScreen = SCREEN_DIAGNOSTICS; lv_scr_load_anim(scr_diagnostics, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false); break;
    }
}

void ui_create_menu_screen() {
    scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_menu, RED808_BG, 0);

    ui_create_header(scr_menu);

    // Title
    lv_obj_t* title = lv_label_create(scr_menu);
    lv_label_set_text(title, "MAIN MENU");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    // 3x2 grid of menu buttons
    static const char* menu_names[] = {
        LV_SYMBOL_AUDIO "\nLIVE PAD",
        LV_SYMBOL_LIST "\nSEQUENCER",
        LV_SYMBOL_VOLUME_MAX "\nVOLUMES",
        LV_SYMBOL_SETTINGS "\nFILTERS FX",
        LV_SYMBOL_DRIVE "\nSETTINGS",
        LV_SYMBOL_EYE_OPEN "\nDIAGNOSTICS"
    };
    static const lv_color_t menu_colors[] = {
        RED808_ACCENT, RED808_INFO, RED808_SUCCESS,
        RED808_WARNING, RED808_CYAN, RED808_TEXT_DIM
    };

    int x_start = 28, y_start = 100;
    int btn_w = 300, btn_h = 220, gap = 24;

    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;

        lv_obj_t* btn = lv_btn_create(scr_menu);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x_start + col * (btn_w + gap), y_start + row * (btn_h + gap));
        lv_obj_set_style_bg_color(btn, RED808_SURFACE, 0);
        lv_obj_set_style_bg_color(btn, menu_colors[i], LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, menu_colors[i], 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_add_event_cb(btn, menu_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menu_names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, RED808_TEXT, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}

// ============================================================================
// LIVE PADS SCREEN - 4x4 grid of pads
// ============================================================================
static void live_pad_cb(lv_event_t* e) {
    int pad = (int)(intptr_t)lv_event_get_user_data(e);
    // Flash feedback
    lv_obj_set_style_bg_color(lv_event_get_target(e), lv_color_white(), 0);
    // TODO: Send UDP trigger
    (void)pad;
}

static void live_pad_release_cb(lv_event_t* e) {
    int pad = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_set_style_bg_color(lv_event_get_target(e), inst_colors[pad], 0);
}

void ui_create_live_screen() {
    scr_live = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_live, RED808_BG, 0);
    ui_create_header(scr_live);

    int pad_size = 220;
    int gap = 12;
    int x_start = (1024 - 4 * pad_size - 3 * gap) / 2;
    int y_start = 65;

    for (int i = 0; i < 16; i++) {
        int col = i % 4;
        int row = i / 4;

        lv_obj_t* pad = lv_btn_create(scr_live);
        lv_obj_set_size(pad, pad_size, (600 - y_start - 16) / 4 - gap);
        lv_obj_set_pos(pad, x_start + col * (pad_size + gap),
                       y_start + row * ((600 - y_start - 16) / 4));
        lv_obj_set_style_bg_color(pad, inst_colors[i], 0);
        lv_obj_set_style_radius(pad, 8, 0);
        lv_obj_set_style_border_width(pad, 0, 0);
        lv_obj_add_event_cb(pad, live_pad_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);
        lv_obj_add_event_cb(pad, live_pad_release_cb, LV_EVENT_RELEASED, (void*)(intptr_t)i);

        lv_obj_t* lbl = lv_label_create(pad);
        lv_label_set_text(lbl, trackNames[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(lbl, RED808_BG, 0);
        lv_obj_center(lbl);

        live_pads[i] = pad;
    }
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
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            lv_obj_t* cell = lv_btn_create(scr_sequencer);
            lv_obj_set_size(cell, cell_w, cell_h);
            lv_obj_set_pos(cell, grid_x + s * (cell_w + gap), grid_y + t * (cell_h + gap));
            bool active = patterns[currentPattern].steps[t][s];
            lv_obj_set_style_bg_color(cell, active ? inst_colors[t] : RED808_SURFACE, 0);
            lv_obj_set_style_radius(cell, 3, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            int idx = t * Config::MAX_STEPS + s;
            lv_obj_add_event_cb(cell, seq_step_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
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

    // Update step highlight
    if (lbl_step_indicator) {
        lv_label_set_text_fmt(lbl_step_indicator, "Step: %d", currentStep + 1);
    }

    // Update grid state
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            if (seq_grid[t][s]) {
                bool active = patterns[currentPattern].steps[t][s];
                bool isCurrentStep = (s == currentStep && isPlaying);
                if (isCurrentStep) {
                    lv_obj_set_style_bg_color(seq_grid[t][s],
                        active ? lv_color_white() : RED808_BORDER, 0);
                } else {
                    lv_obj_set_style_bg_color(seq_grid[t][s],
                        active ? inst_colors[t] : RED808_SURFACE, 0);
                }
            }
        }
    }
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
    }
}

void ui_update_volumes() {
    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        if (vol_sliders[i]) {
            lv_slider_set_value(vol_sliders[i], trackVolumes[i], LV_ANIM_ON);
        }
        if (vol_labels[i]) {
            lv_label_set_text_fmt(vol_labels[i], "%d", trackVolumes[i]);
        }
    }
}

// ============================================================================
// FILTERS FX SCREEN - 3 arc gauges for Delay/Flanger/Compressor
// ============================================================================
void ui_create_filters_screen() {
    scr_filters = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_filters, RED808_BG, 0);
    ui_create_header(scr_filters);

    lv_obj_t* title = lv_label_create(scr_filters);
    lv_label_set_text(title, "FILTERS FX");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

    // Target indicator
    lv_obj_t* target = lv_label_create(scr_filters);
    lv_label_set_text(target, "TARGET: MASTER");
    lv_obj_set_style_text_font(target, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(target, RED808_WARNING, 0);
    lv_obj_set_pos(target, 20, 52);

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
    TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
    uint8_t amounts[] = { f.delayAmount, f.flangerAmount, f.compAmount };

    for (int i = 0; i < 3; i++) {
        if (filter_arcs[i]) {
            lv_arc_set_value(filter_arcs[i], amounts[i]);
            bool isSelected = (i == filterSelectedFX);
            lv_obj_set_style_arc_width(filter_arcs[i], isSelected ? 24 : 18, LV_PART_INDICATOR);
        }
        if (filter_value_labels[i]) {
            int pct = amounts[i] * 100 / 127;
            lv_label_set_text_fmt(filter_value_labels[i], "%d%%", pct);
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
}

// ============================================================================
// DIAGNOSTICS SCREEN
// ============================================================================
void ui_create_diagnostics_screen() {
    scr_diagnostics = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_diagnostics, RED808_BG, 0);
    ui_create_header(scr_diagnostics);

    lv_obj_t* title = lv_label_create(scr_diagnostics);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN " DIAGNOSTICS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    // Will be populated by ui_update_diagnostics()
}

void ui_update_diagnostics() {
    // Placeholder: update diagnostic labels with current state
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
