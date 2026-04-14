// =============================================================================
// ui_screens.h — P4 screen declarations
// =============================================================================
#pragma once

#include <lvgl.h>

// Screen objects
extern lv_obj_t* scr_boot;
extern lv_obj_t* scr_live;
extern lv_obj_t* scr_sequencer;
extern lv_obj_t* scr_fx;
extern lv_obj_t* scr_volumes;
extern lv_obj_t* scr_settings;
extern lv_obj_t* scr_performance;

// Create all screens (call once after LVGL init)
void ui_create_all_screens(void);

// Update current screen from P4State (call every frame)
void ui_update_current_screen(void);

// Navigate to a screen
void ui_navigate_to(int screen_id);

// Helper: create styled section shell
lv_obj_t* create_section_shell(lv_obj_t* parent, int x, int y, int w, int h);

// Header bar (shared across screens)
void ui_create_header(lv_obj_t* parent);
void ui_update_header(void);
