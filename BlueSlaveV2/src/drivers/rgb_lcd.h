// =============================================================================
// rgb_lcd.h - RGB LCD driver for Waveshare ESP32-S3-Touch-LCD-7B
// =============================================================================
#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "../../include/config.h"

esp_lcd_panel_handle_t rgb_lcd_init();
void rgb_lcd_get_frame_buffers(esp_lcd_panel_handle_t panel, void** fb0, void** fb1);
void rgb_lcd_register_vsync_cb(esp_lcd_panel_handle_t panel,
                                bool (*cb)(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*),
                                void* user_ctx);
