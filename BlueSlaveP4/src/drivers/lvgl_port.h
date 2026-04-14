// =============================================================================
// lvgl_port.h — LVGL display/touch integration for ESP32-P4
// =============================================================================
#pragma once

#include <lvgl.h>

// Initialize LVGL display driver + touch input device.
// Call after display_init() returns successfully.
void lvgl_port_init(void);

// Call from main loop (handles lv_timer_handler)
void lvgl_port_update(void);

// Get the touch input device (for screen callbacks)
lv_indev_t* lvgl_port_get_touch_indev(void);
