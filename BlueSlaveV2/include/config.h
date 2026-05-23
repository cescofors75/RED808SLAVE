// =============================================================================
// BlueSlaveV2 - config.h
// RED808 V6 Surface Controller
// Waveshare ESP32-S3-Touch-LCD-7B (1024x600)
// =============================================================================
#pragma once

#include <Arduino.h>

#ifndef RED808_ENABLE_DEBUG_LOG
#define RED808_ENABLE_DEBUG_LOG 0
#endif

#if RED808_ENABLE_DEBUG_LOG
#define RED808_LOG_PRINT(...) Serial.print(__VA_ARGS__)
#define RED808_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define RED808_LOG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define RED808_LOG_PRINT(...) ((void)0)
#define RED808_LOG_PRINTLN(...) ((void)0)
#define RED808_LOG_PRINTF(...) ((void)0)
#endif

// =============================================================================
// DISPLAY - Waveshare ESP32-S3-Touch-LCD-7B
// =============================================================================
#define SCREEN_WIDTH   1024
#define SCREEN_HEIGHT  600

// Portrait mode: 0 = landscape (1024x600), 1 = portrait (600x1024 via SW rotation)
#ifndef PORTRAIT_MODE
#define PORTRAIT_MODE  0
#endif

#if PORTRAIT_MODE
#define UI_W  600
#define UI_H  1024
#else
#define UI_W  SCREEN_WIDTH
#define UI_H  SCREEN_HEIGHT
#endif

// RGB LCD Signals
#define LCD_VSYNC      GPIO_NUM_3
#define LCD_HSYNC      GPIO_NUM_46
#define LCD_DE         GPIO_NUM_5
#define LCD_PCLK       GPIO_NUM_7

// RGB performance profile:
// 0 = stable (16MHz), 1 = balanced (21MHz), 2 = low-latency demo (24MHz).
// If you see artifacts, drop one profile level.
#ifndef LCD_PERF_PROFILE
#define LCD_PERF_PROFILE 0
#endif

#if LCD_PERF_PROFILE == 0
#define LCD_PCLK_HZ    (16 * 1000 * 1000)
#elif LCD_PERF_PROFILE == 1
#define LCD_PCLK_HZ    (21 * 1000 * 1000)
#else
#define LCD_PCLK_HZ    (24 * 1000 * 1000)
#endif

// RGB565 Data (16-bit)
#define LCD_B3         GPIO_NUM_14
#define LCD_B4         GPIO_NUM_38
#define LCD_B5         GPIO_NUM_18
#define LCD_B6         GPIO_NUM_17
#define LCD_B7         GPIO_NUM_10
#define LCD_G2         GPIO_NUM_39
#define LCD_G3         GPIO_NUM_0
#define LCD_G4         GPIO_NUM_45
#define LCD_G5         GPIO_NUM_48
#define LCD_G6         GPIO_NUM_47
#define LCD_G7         GPIO_NUM_21
#define LCD_R3         GPIO_NUM_1
#define LCD_R4         GPIO_NUM_2
#define LCD_R5         GPIO_NUM_42
#define LCD_R6         GPIO_NUM_41
#define LCD_R7         GPIO_NUM_40

// LCD Timing (ST7262) — large porch profile known to produce a stable image with this RGB driver.
// Approx refresh with this porch set:
// 16MHz=17.5Hz, 21MHz=22.9Hz, 24MHz=26.2Hz.
#define LCD_HSYNC_PULSE_WIDTH  162
#define LCD_HSYNC_BACK_PORCH   152
#define LCD_HSYNC_FRONT_PORCH  48
#define LCD_VSYNC_PULSE_WIDTH  45
#define LCD_VSYNC_BACK_PORCH   13
#define LCD_VSYNC_FRONT_PORCH  3

// Frame buffers
#define LCD_NUM_FB     2   // Double buffer
// Bounce buffer size: larger = fewer fills/frame = less WiFi-induced DMA underruns.
// * 10 = 60 fills/frame (~720µs/fill window) — underruns when WiFi is active
// * 20 = 30 fills/frame (~1.7ms/fill window at 18MHz) — fits WiFi DMA comfortably
// Each buffer: 20480px × 2B = 40KB SRAM. Total: 80KB internal SRAM.
#define LCD_BOUNCE_BUF (SCREEN_WIDTH * 20)

// LVGL portrait render tuning. Keep the draw buffers small and prefer internal
// SRAM so LCD DMA and LVGL rendering do not fight over PSRAM bandwidth.
#ifndef S3_LVGL_PORT_BUF_LINES
#define S3_LVGL_PORT_BUF_LINES 20
#endif

#ifndef S3_LVGL_TASK_PERIOD_MS
#define S3_LVGL_TASK_PERIOD_MS 16
#endif

// =============================================================================
// I2C BUS (shared: GT911 touch + CH32V003 IO + PCA9548A hub)
// =============================================================================
#define I2C_SDA        GPIO_NUM_8
#define I2C_SCL        GPIO_NUM_9
#define I2C_FREQ       100000  // 100kHz — stable for active 8-port hub + long harnesses

// =============================================================================
// ON-BOARD I2C DEVICES
// =============================================================================
#define IO_EXT_ADDR    0x24   // CH32V003 IO Expander

// CH32V003 IO Expander Pin Assignments
#define EXIO_BL        2  // LCD backlight
#define EXIO_LCD_RST   3  // LCD reset

// =============================================================================
// EXTERNAL I2C DEVICES (via PCA9548A hub)
// =============================================================================
#define I2C_HUB_ADDR   0x70  // PCA9548A / TCA9548A

// M5 ROTATE8 Modules (2x)
#define M5_ENCODER_MODULES     2
#define ENCODERS_PER_MODULE    8
#define M5_ENCODER_ADDR        0x41  // Both modules, separated by hub
#define M5_ENCODER1_CHANNEL    1
#define M5_ENCODER2_CHANNEL    2

// M5 Unit ByteButton (up to 2x via hub channels)
#define BYTEBUTTON_COUNT       2
#define BYTEBUTTON_ADDR        0x47
#define BYTEBUTTON_BUTTONS     8
#define BYTEBUTTON_TOTAL_BUTTONS (BYTEBUTTON_COUNT * BYTEBUTTON_BUTTONS)
#define BYTEBUTTON1_CHANNEL    3
#define BYTEBUTTON2_CHANNEL    4

// =============================================================================
// WiFi / UDP — S3 connects to Master directly (set to 0 when P4 handles WiFi)
// =============================================================================
#ifndef S3_WIFI_ENABLED
#define S3_WIFI_ENABLED  1   // 0 = S3 is UART-only slave to P4, 1 = S3 has own WiFi
#endif

namespace WiFiConfig {
    constexpr const char* SSID     = "RED808";
    constexpr const char* PASSWORD = "red808esp32";
    constexpr const char* MASTER_IP = "192.168.4.1";
    constexpr uint16_t UDP_PORT    = 8888;
    constexpr uint32_t RECONNECT_INTERVAL_MS       = 1500;
}
