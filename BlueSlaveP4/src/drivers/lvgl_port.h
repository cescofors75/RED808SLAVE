// =============================================================================
// lvgl_port.h — LVGL display/touch integration for ESP32-P4
// Zero-copy double-buffer, vsync sync, FreeRTOS task
// =============================================================================
#pragma once

#include <lvgl.h>

// Initialize LVGL display driver + touch input device.
// Call after display_init() returns successfully.
void lvgl_port_init(void);

// No-op (LVGL now runs in dedicated FreeRTOS task)
void lvgl_port_update(void);

// Get the touch input device (for screen callbacks)
lv_indev_t* lvgl_port_get_touch_indev(void);

// Thread safety — wrap LVGL API calls from outside the LVGL task
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);

// Start LVGL task (call after UI setup is complete)
void lvgl_port_task_start(void);
