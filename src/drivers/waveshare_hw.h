/**
 * @file waveshare_hw.h
 * @brief Hardware drivers for Waveshare ESP32-S3-Touch-LCD-7
 * 
 * TCA9554 IO expander, RGB LCD 1024x600, GT911 touch, LVGL port
 */

#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Display resolution
#define WS_LCD_H_RES  1024
#define WS_LCD_V_RES  600

// TCA9554 IO expander pin assignments (matches BlueSlaveV2)
#define WS_EXIO_TP_RST   1   // Touch Panel Reset
#define WS_EXIO_LCD_BL   2   // Backlight
#define WS_EXIO_LCD_RST  3   // LCD Reset
#define WS_EXIO_SD_CS    4   // SD Card CS

// GT911 touch
#define WS_TOUCH_INT_PIN 4   // GPIO4

// I2C pins (shared bus for TCA9554, GT911, PCA9548A, etc.)
#define WS_I2C_SDA       8
#define WS_I2C_SCL       9

/**
 * Initialize ALL Waveshare hardware:
 * 1. I2C bus
 * 2. CH422G IO expander (backlight, resets)
 * 3. RGB LCD panel (1024x600, ST7262)
 * 4. GT911 touch controller
 * 5. LVGL display/input drivers
 * 
 * @return true if display initialized successfully
 */
bool waveshare_init(void);

/**
 * Call from loop() - now a no-op since LVGL runs in FreeRTOS task.
 */
void waveshare_lvgl_handler(void);

/**
 * Lock LVGL mutex for safe UI updates from main loop.
 */
bool waveshare_lvgl_lock(int timeout_ms);

/**
 * Unlock LVGL mutex after UI updates.
 */
void waveshare_lvgl_unlock(void);

/**
 * Signal that UI has been created and LVGL task can start rendering.
 */
void waveshare_lvgl_task_start(void);

/**
 * Get the active LVGL display object.
 */
lv_disp_t* waveshare_get_display(void);
