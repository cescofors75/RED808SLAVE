// =============================================================================
// lvgl_port.h - LVGL port for Waveshare ESP32-S3-Touch-LCD-7B
// =============================================================================
#pragma once

#include "lvgl.h"
#include "esp_lcd_panel_ops.h"

void lvgl_port_init(esp_lcd_panel_handle_t lcd_handle);
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock();
void lvgl_port_task_start();
