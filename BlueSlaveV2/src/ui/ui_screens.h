// =============================================================================
// ui_screens.h - All screen creation/update functions
// =============================================================================
#pragma once

#include "lvgl.h"
#include <ArduinoJson.h>

// Screen objects
extern lv_obj_t* scr_menu;
extern lv_obj_t* scr_live;
extern lv_obj_t* scr_sequencer;
extern lv_obj_t* scr_volumes;
extern lv_obj_t* scr_filters;
extern lv_obj_t* scr_settings;
extern lv_obj_t* scr_diagnostics;
extern lv_obj_t* scr_patterns;
extern lv_obj_t* scr_sdcard;
extern lv_obj_t* scr_performance;
extern lv_obj_t* scr_samples;
extern lv_obj_t* scr_boot;
extern lv_obj_t* scr_seq_circle;
extern lv_obj_t* scr_melody;        // v2.6 — piano roll / score editor
extern lv_obj_t* scr_piano_params;  // v2.7 — synth engine parameter editor

// Create screens
void ui_create_menu_screen();
void ui_create_live_screen();
void ui_create_sequencer_screen();
void ui_create_volumes_screen();
void ui_create_filters_screen();
void ui_create_settings_screen();
void ui_create_diagnostics_screen();
void ui_create_patterns_screen();
void ui_create_sdcard_screen();
void ui_create_performance_screen();
void ui_create_samples_screen();
void ui_create_boot_screen();
void ui_create_seq_circle_screen();
void ui_create_melody_screen();      // v2.6 — piano roll editor
void ui_create_piano_params_screen(); // v2.7 — synth params editor

// v2.9 — Set by nav_to(SCREEN_MELODY) so loop() can request a fresh
// melody_sync from master once the LVGL render is done.
extern volatile bool g_mel_screen_entered;

// v2.7 — MELODY: write a MIDI note into the active step (called from
// receiveUDPData when "melodyRecNote" arrives, or from local sources).
// Returns true if recording is currently active (note was captured).
bool melody_record_midi_note(uint8_t midi);

// v2.8 — MELODY: replace the displayed grid from a remote melodyAssign packet
// (origin: P4 piano "→ ASSIGN"). steps: 16 columns of MIDI note arrays.
// engine 3..6, octave 1..7. The grid is rebuilt and the pad selector updated.
void melody_apply_assign_payload(JsonVariantConst doc);

// v2.9 — MELODY: apply master-authoritative melody_sync (engine/octave/rec/
// step/pad/grid). Called from receiveUDPData when "melody_sync" arrives.
void melody_apply_sync_payload(JsonVariantConst doc);
void melody_apply_basic_sync(uint8_t engine, uint8_t octave, uint8_t rec, uint8_t pad);

// v2.9 — Push current S3 melody state (engine/octave/rec/pad) to P4 over UART.
// Used on screen entry so PIANO mirrors S3 immediately.
void melody_uart_broadcast_state(void);

// Update functions (called from timer)
void ui_update_sequencer();
void ui_update_seq_circle();
void ui_update_volumes();
void ui_update_filters();
void ui_update_diagnostics();
void ui_update_performance();
void ui_update_header();
void ui_update_menu_status();
void ui_update_live_pads();
void ui_live_pads_invalidate();
void ui_live_set_sync(bool on);
void ui_volumes_retheme();
void ui_melody_retheme();
void ui_piano_params_retheme();
void ui_update_sdcard();

// SD card helpers (used by main.cpp for P4 remote browse)
bool sd_try_mount();
void ui_sdcard_send_load_sample(int pad, const char* family, const char* filename);
void ui_refresh_theme_buttons();  // refreshes active indicator on settings theme selector
void ui_sdcard_send_load_sample(int pad, const char* family, const char* filename);
void ui_update_patterns();
int ui_live_pad_hit_test(int x, int y);

// MIDI pattern loading: called from sd_midi_load_btn_cb (ui_screens) into main.cpp
// Fills patterns[slot], sends to Master via UDP, sends to P4 via UART
void handleMidiPatternLoaded(int slot, const bool steps[16][64], const char* name, int patternLength = 16);

// Footer bar (shared across screens)
void ui_create_header(lv_obj_t* parent);
