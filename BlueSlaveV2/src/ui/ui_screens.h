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

// Create screens
void ui_create_menu_screen();
void ui_create_live_screen();
void ui_create_sequencer_screen();
void ui_create_volumes_screen();
void ui_create_filters_screen();
void ui_create_settings_screen();
void ui_create_diagnostics_screen();
void ui_create_patterns_screen();

// Update functions (called from timer)
void ui_update_sequencer();
void ui_update_volumes();
void ui_update_filters();
void ui_update_diagnostics();
void ui_update_header();
void ui_update_menu_status();

// Header bar (shared across screens)
void ui_create_header(lv_obj_t* parent);
