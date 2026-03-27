// =============================================================================
// BlueSlaveV2 - config.h
// RED808 V6 Surface Controller
// Waveshare ESP32-S3-Touch-LCD-7B (1024x600)
// =============================================================================
#pragma once

#include <Arduino.h>

// =============================================================================
// DISPLAY - Waveshare ESP32-S3-Touch-LCD-7B
// =============================================================================
#define SCREEN_WIDTH   1024
#define SCREEN_HEIGHT  600

// RGB LCD Signals
#define LCD_VSYNC      GPIO_NUM_3
#define LCD_HSYNC      GPIO_NUM_46
#define LCD_DE         GPIO_NUM_5
#define LCD_PCLK       GPIO_NUM_7
#define LCD_PCLK_HZ    (16 * 1000 * 1000)  // 16MHz - stable with PSRAM

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

// LCD Timing (ST7262) - ESPHome confirmed values
#define LCD_HSYNC_PULSE_WIDTH  162
#define LCD_HSYNC_BACK_PORCH   152
#define LCD_HSYNC_FRONT_PORCH  48
#define LCD_VSYNC_PULSE_WIDTH  45
#define LCD_VSYNC_BACK_PORCH   13
#define LCD_VSYNC_FRONT_PORCH  3

// Frame buffers
#define LCD_NUM_FB     2   // Double buffer
#define LCD_BOUNCE_BUF (SCREEN_WIDTH * 10)

// =============================================================================
// I2C BUS (shared: GT911 touch + CH32V003 IO + PCA9548A hub)
// =============================================================================
#define I2C_SDA        GPIO_NUM_8
#define I2C_SCL        GPIO_NUM_9
#define I2C_FREQ       400000  // 400kHz

// =============================================================================
// ON-BOARD I2C DEVICES
// =============================================================================
#define IO_EXT_ADDR    0x24   // CH32V003 IO Expander
#define GT911_ADDR     0x5D   // Touch controller (alt: 0x14)
#define GT911_INT_PIN  GPIO_NUM_4

// CH32V003 IO Expander Pin Assignments
#define EXIO_GP        0  // General purpose
#define EXIO_TP_RST    1  // Touch reset
#define EXIO_BL        2  // LCD backlight
#define EXIO_LCD_RST   3  // LCD reset
#define EXIO_SD_CS     4  // SD card CS
#define EXIO_IF_SEL    5  // Interface select (CAN/USB)

// =============================================================================
// EXTERNAL I2C DEVICES (via PCA9548A hub)
// =============================================================================
#define I2C_HUB_ADDR   0x70  // PCA9548A / TCA9548A

// M5 ROTATE8 Modules (2x)
#define M5_ENCODER_MODULES     2
#define ENCODERS_PER_MODULE    8
#define M5_ENCODER_ADDR        0x41  // Both modules, separated by hub

// DFRobot SEN0502 Visual Rotary Encoders (2x)
#define DFROBOT_ENCODER_COUNT  2
#define DFROBOT_ENCODER_ADDR   0x54  // Both encoders, separated by hub
// DFRobot #1: FX control (rotation=amount, button=cycle effect)
// DFRobot #2: Pattern select (rotation=navigate, button=reset)

// =============================================================================
// SD CARD (SDMMC interface, CS via CH32V003 EXIO4)
// =============================================================================
// SD card uses SDMMC, CS pin managed by IO Extension (EXIO_SD_CS)

// =============================================================================
// WiFi / UDP
// =============================================================================
namespace WiFiConfig {
    constexpr const char* SSID     = "RED808_NET";
    constexpr const char* PASSWORD = "red808pass";
    constexpr uint16_t UDP_PORT    = 8888;
    constexpr uint32_t TIMEOUT_MS  = 20000;
    constexpr uint32_t RECONNECT_INTERVAL_MS = 8000;
}

// =============================================================================
// SEQUENCER
// =============================================================================
namespace Config {
    constexpr int MAX_STEPS     = 16;
    constexpr int MAX_TRACKS    = 16;
    constexpr int TRACKS_PER_PAGE = 8;
    constexpr int MAX_PATTERNS  = 16;
    constexpr int MAX_KITS      = 3;

    constexpr int MIN_BPM       = 40;
    constexpr int MAX_BPM       = 240;
    constexpr int DEFAULT_BPM   = 120;

    constexpr int DEFAULT_VOLUME = 75;
    constexpr int MAX_VOLUME    = 150;
    constexpr int MAX_SAMPLES   = 16;
    constexpr int DEFAULT_TRACK_VOLUME = 100;

    // Timing
    constexpr uint32_t ENCODER_READ_MS    = 50;
    constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
    constexpr uint32_t LED_FLASH_MS       = 100;
    constexpr uint32_t SCREEN_UPDATE_MS   = 16;  // ~60 FPS
    constexpr uint32_t UDP_CHECK_MS       = 30000;

    // Menu
    constexpr int MENU_ITEMS = 6;
}
