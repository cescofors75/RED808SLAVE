// =============================================================================
// ui_screens.h - All screen creation/update functions
// =============================================================================
#pragma once

#include "lvgl.h"

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

// Update functions (called from timer)
void ui_update_sequencer();
void ui_update_seq_circle();
void ui_update_volumes();
void ui_update_filters();
void ui_update_diagnostics();
void ui_update_header();
void ui_update_menu_status();
void ui_update_live_pads();
void ui_update_sdcard();
void ui_sdcard_send_load_sample(int pad, const char* family, const char* filename);
void ui_update_patterns();
int ui_live_pad_hit_test(int x, int y);

// Header bar (shared across screens)
void ui_create_header(lv_obj_t* parent);
