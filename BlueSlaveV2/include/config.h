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
#define PORTRAIT_MODE  1
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

// DFRobot SEN0502 Visual Rotary Encoders (4x)
#define DFROBOT_ENCODER_COUNT  4
#define DFROBOT_ENCODER_ADDR   0x54  // Both encoders, separated by hub
// DFRobot #1: Sequencer/Live volume (button=Play/Stop)
// DFRobot #2: BPM (button=reset BPM)
// DFRobot #3: Master volume (button=toggle volume mode)
// DFRobot #4: Pattern select (button=request pattern sync)

// M5 Unit ByteButton (up to 2x via hub channels)
#define BYTEBUTTON_COUNT       2
#define BYTEBUTTON_ADDR        0x47
#define BYTEBUTTON_BUTTONS     8
#define BYTEBUTTON_TOTAL_BUTTONS (BYTEBUTTON_COUNT * BYTEBUTTON_BUTTONS)
#define BYTEBUTTON1_HUB_CH     4
#define BYTEBUTTON2_HUB_CH     5

// DFRobot 4x analog pot hub via I2C ADC converter (ADS1115-compatible)
#define DFROBOT_POT_COUNT      4
#define DFROBOT_POT_ADC_ADDR   0x48
#define DFROBOT_POT_ADC_ADDR_ALT 0x49

// =============================================================================
// SD CARD (SDMMC 1-bit mode, CS via CH32V003 EXIO4)
// GPIO 11=CLK, 12=CMD, 13=D0 (confirmed free pins on Waveshare ESP32-S3-7B)
// =============================================================================
#define SD_CLK_PIN     12  // GPIO12 = SCK  (confirmed from schematic)
#define SD_CMD_PIN     11  // GPIO11 = MOSI (confirmed from schematic)
#define SD_D0_PIN      13  // GPIO13 = MISO (confirmed from schematic)
// D3/CS via EXIO4 — must be HIGH before SD_MMC.begin() so card enters SDMMC mode
// Mount point for SD_MMC
#define SD_MOUNT_POINT "/sdcard"

// =============================================================================
// WiFi / UDP
// =============================================================================
namespace WiFiConfig {
    constexpr const char* SSID     = "RED808";
    constexpr const char* PASSWORD = "red808esp32";
    constexpr const char* MASTER_IP = "192.168.4.1";
    constexpr uint16_t UDP_PORT    = 8888;
    constexpr uint32_t TIMEOUT_MS  = 3000;
    constexpr uint32_t RECONNECT_INTERVAL_MS       = 5000;
    constexpr uint32_t RECONNECT_ATTEMPT_TIMEOUT_MS = 8000;
    constexpr uint32_t DISCONNECT_GRACE_MS = 1500;
    constexpr uint32_t MASTER_HELLO_RETRY_MS = 8000;
    constexpr uint32_t UDP_RECEIVE_MS = 8;
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
    constexpr int DEFAULT_TRACK_VOLUME = 75;

    // Timing
    constexpr uint32_t ENCODER_READ_MS    = 5;   // 200Hz polling — fast encoder response
    constexpr uint32_t BUTTON_DEBOUNCE_MS = 25;
    constexpr uint32_t LED_FLASH_MS       = 100;
    constexpr uint32_t SCREEN_UPDATE_MS   = 12;  // ~83fps UI update — smoother step animation
    constexpr uint32_t UDP_CHECK_MS       = 30000;
    constexpr uint32_t TOUCH_ENCODER_READ_MS = 20;  // slower encoder poll in touch-heavy screens to free I2C bus
    constexpr bool ENABLE_MICROTIMING = false;      // disable random jitter for tighter live/demo response

    // Touch tuning (Waveshare 7B). Fine tune offsets if hitboxes feel shifted.
    constexpr bool TOUCH_SWAP_XY = false;
    constexpr bool TOUCH_INVERT_X = false;
    constexpr bool TOUCH_INVERT_Y = false;
    constexpr int TOUCH_X_OFFSET = 0;
    constexpr int TOUCH_Y_OFFSET = 0;
    constexpr int TOUCH_X_SCALE_PCT = 100;
    constexpr int TOUCH_Y_SCALE_PCT = 100;
    constexpr int TOUCH_JITTER_PX = 3;
    constexpr uint8_t LIVE_PAD_HIT_MARGIN_PCT = 10;   // adaptive pad hit expansion
    constexpr uint8_t LIVE_PAD_HIT_MARGIN_MIN = 6;
    constexpr uint8_t LIVE_PAD_HIT_MARGIN_MAX = 24;
    constexpr uint8_t TOUCH_MAX_POINTS = 5;
    // Raw touch range calibration (GT911 reported range before transform)
    constexpr int TOUCH_RAW_MIN_X = 0;
    constexpr int TOUCH_RAW_MAX_X = 1023;
    constexpr int TOUCH_RAW_MIN_Y = 0;
    constexpr int TOUCH_RAW_MAX_Y = 599;

    // DFRobot rotary tuning
    constexpr int DF_DELTA_CLAMP = 16;
    constexpr int DF_GLITCH_THRESHOLD = 64;
    constexpr int DF_COUNTS_PER_STEP = 2;
    constexpr int DF_VOLUME_STEP = 3;   // Master volume change per encoder step
    constexpr int DF_BPM_STEP = 1;      // BPM change per encoder step
    constexpr int DF_FX_STEP_FINE = 4;  // DF1-DF3 sensitivity (fine)
    constexpr int DF_FX_STEP_AGGR = 8;  // DF1-DF3 sensitivity (aggressive)
    constexpr int DF_FX_STEP = DF_FX_STEP_AGGR;
    constexpr uint32_t DF_BUTTON_GUARD_MS = 250;
    constexpr uint32_t DF_POT_READ_MS = 10;           // 100Hz poll for fast live response
    constexpr uint8_t DF_POT_MIDI_DEADBAND = 2;       // ignore ±1 MIDI noise, send on ±2 change
    constexpr uint8_t DF_POT_STABLE_READS = 1;        // no extra hold before applying change
    constexpr uint16_t DF_POT_MIN_SPAN = 8;           // lower threshold so narrow-range pots work sooner
    constexpr uint16_t DF_POT_RAW_IDLE_DB = 12;       // tighter response, still filters idle noise
    constexpr uint8_t DF_POT_HYST_NUM = 35;           // detent hysteresis numerator (~0.35 step)
    constexpr uint8_t DF_POT_HYST_DEN = 100;          // detent hysteresis denominator
    constexpr int DF_IDLE_DELTA_DB = 0;               // pass all non-zero deltas (gain=10 gives clean steps)
    constexpr int DF_NEAR_ZERO_REPEAT = 2;            // light jitter filter (require 2 same-direction reads)

    // M5 Unit Fader on analog pin (replaces old analog rotary)
    constexpr int UNIT_FADER_PIN = 6;        // GPIO6 signal pin
    constexpr int UNIT_FADER_DEADBAND = 1;   // finer response for 0.1 BPM tuning
    constexpr uint32_t UNIT_FADER_READ_MS = 12;
    constexpr int UNIT_FADER_COUNTS_PER_TENTH = 12;

    // Menu
    constexpr int MENU_ITEMS = 6;
}
