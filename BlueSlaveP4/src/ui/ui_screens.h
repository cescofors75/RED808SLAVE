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
extern lv_obj_t* scr_sdcard;
extern lv_obj_t* scr_performance;

// Create all screens (call once after LVGL init)
void ui_create_all_screens(void);

// Update current screen from P4State (call every frame)
void ui_update_current_screen(void);

// Drain pad event queue — call from loop() on Core 1 (outside LVGL mutex)
void ui_process_pad_queue(void);

// Map absolute (x,y) touch coordinate to pad index 0..15 on the LIVE screen.
// Returns -1 if not on a pad or the LIVE screen is not active.
int ui_pad_from_xy(uint16_t x, uint16_t y);

// Per-frame touch state update — call from GT911 touch_task (Core 0, 200Hz).
// `pressed[p]` is true while any finger currently sits on pad p. `velocity[p]`
// is the instantaneous MIDI velocity (40..127) derived from the GT911 area.
// Handles rising/falling edges, note-repeat scheduling and 16-levels routing.
void ui_pad_frame_update(const bool pressed[16], const uint8_t velocity[16]);

// Navigate to a screen
void ui_navigate_to(int screen_id);

// Helper: create styled section shell
lv_obj_t* create_section_shell(lv_obj_t* parent, int x, int y, int w, int h);

// Header bar (shared across screens)
void ui_create_header(lv_obj_t* parent);
void ui_update_header(void);

// Sync pads state (called from UART handler)
void ui_live_set_sync_p4(bool on);
