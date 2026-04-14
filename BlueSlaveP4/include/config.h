// =============================================================================
// BlueSlaveP4 - config.h
// RED808 V6 Visual Beast — Guition ESP32-P4 JC1060P470C (7" MIPI-DSI)
// =============================================================================
#pragma once

#include <Arduino.h>

// =============================================================================
// DEBUG LOGGING
// =============================================================================
#ifndef P4_ENABLE_DEBUG_LOG
#define P4_ENABLE_DEBUG_LOG 1
#endif

#if P4_ENABLE_DEBUG_LOG
#define P4_LOG_PRINT(...)   Serial.print(__VA_ARGS__)
#define P4_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define P4_LOG_PRINTF(...)  Serial.printf(__VA_ARGS__)
#else
#define P4_LOG_PRINT(...)   ((void)0)
#define P4_LOG_PRINTLN(...) ((void)0)
#define P4_LOG_PRINTF(...)  ((void)0)
#endif

// =============================================================================
// DISPLAY — Guition JC1060P470C (7" MIPI-DSI, JD9165BA)
// =============================================================================
#ifndef LCD_H_RES
#define LCD_H_RES   1024
#endif
#ifndef LCD_V_RES
#define LCD_V_RES   600
#endif

// Portrait mode: display is landscape 1024×600 natively.
// For portrait, LVGL renders 600×1024 with sw_rotate.
#ifndef PORTRAIT_MODE
#define PORTRAIT_MODE 1
#endif

#if PORTRAIT_MODE
#define UI_W  600
#define UI_H  1024
#else
#define UI_W  LCD_H_RES
#define UI_H  LCD_V_RES
#endif

// MIPI-DSI configuration
#ifndef MIPI_DSI_LANES
#define MIPI_DSI_LANES          2
#endif
#ifndef MIPI_DSI_LANE_BITRATE_MBPS
#define MIPI_DSI_LANE_BITRATE_MBPS 550
#endif

// JD9165BA timing (1024×600 @ 60Hz)
#define LCD_HSYNC_PULSE     24
#define LCD_HSYNC_BACK      136
#define LCD_HSYNC_FRONT     160
#define LCD_VSYNC_PULSE     2
#define LCD_VSYNC_BACK      21
#define LCD_VSYNC_FRONT     12

// GPIO pins
#ifndef LCD_BL_GPIO
#define LCD_BL_GPIO     23
#endif
#ifndef LCD_RST_GPIO
#define LCD_RST_GPIO    27
#endif

// =============================================================================
// TOUCH — GT911 (I2C)
// =============================================================================
#ifndef TOUCH_I2C_SDA
#define TOUCH_I2C_SDA   7
#endif
#ifndef TOUCH_I2C_SCL
#define TOUCH_I2C_SCL   8
#endif
#ifndef TOUCH_I2C_ADDR
#define TOUCH_I2C_ADDR  0x5D
#endif
#ifndef TOUCH_RST_GPIO
#define TOUCH_RST_GPIO  22
#endif
#ifndef TOUCH_INT_GPIO
#define TOUCH_INT_GPIO  21
#endif

#ifndef TOUCH_CAL_X_MIN
#define TOUCH_CAL_X_MIN  0
#endif
#ifndef TOUCH_CAL_X_MAX
#define TOUCH_CAL_X_MAX  (LCD_V_RES - 1)
#endif
#ifndef TOUCH_CAL_Y_MIN
#define TOUCH_CAL_Y_MIN  0
#endif
#ifndef TOUCH_CAL_Y_MAX
#define TOUCH_CAL_Y_MAX  (LCD_H_RES - 1)
#endif

// =============================================================================
// UART — Connection to ESP32-S3 (binary protocol)
// =============================================================================
#ifndef UART_S3_TX_PIN
#define UART_S3_TX_PIN  24      // P4 TX → S3 RX (GPIO15)
#endif
#ifndef UART_S3_RX_PIN
#define UART_S3_RX_PIN  25      // P4 RX ← S3 TX (GPIO16)
#endif
#define UART_S3_PORT    1       // UART1 (UART0 used for USB debug)
#define UART_RX_BUF     512
#define UART_TX_BUF     256

// =============================================================================
// SEQUENCER (mirror of S3 constants for UI rendering)
// =============================================================================
namespace Config {
    constexpr int MAX_STEPS     = 16;
    constexpr int MAX_TRACKS    = 16;
    constexpr int TRACKS_PER_PAGE = 8;
    constexpr int MAX_PATTERNS  = 16;
    constexpr int MAX_SAMPLES   = 16;
    constexpr int MAX_VOLUME    = 150;
    constexpr int DEFAULT_BPM   = 120;

    // UI timing
    constexpr uint32_t SCREEN_UPDATE_MS = 10;   // 100Hz — P4 can handle it
    constexpr uint32_t UART_RX_TIMEOUT_MS = 50;
    constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 3000;
}
