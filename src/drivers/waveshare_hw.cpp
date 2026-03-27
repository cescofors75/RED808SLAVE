/**
 * @file waveshare_hw.cpp
 * @brief Hardware drivers for Waveshare ESP32-S3-Touch-LCD-7
 *
 * Initializes TCA9554, RGB LCD 1024x600, GT911 touch, LVGL port.
 * Based on working BlueSlaveV2 driver implementation.
 * Uses esp_lcd RGB panel API (ESP-IDF 4.4 compatible).
 */

#include "waveshare_hw.h"
#include <Wire.h>
#include <lvgl.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_heap_caps.h"

// ============================================
// TCA9554 IO Expander (register-based protocol)
// ============================================
#define TCA9554_ADDR        0x24
#define TCA9554_OUTPUT_REG  0x01
#define TCA9554_CONFIG_REG  0x03

static uint8_t tca9554_output_state = 0xFF;  // All HIGH initially

static bool tca9554_write(uint8_t reg, uint8_t data) {
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(reg);
    Wire.write(data);
    return (Wire.endTransmission() == 0);
}

static bool tca9554_init() {
    // Configure all pins as outputs (0 = output)
    if (!tca9554_write(TCA9554_CONFIG_REG, 0x00)) {
        Serial.println("[TCA9554] FAILED - not found at 0x24");
        return false;
    }
    // All outputs HIGH initially
    tca9554_output_state = 0xFF;
    tca9554_write(TCA9554_OUTPUT_REG, tca9554_output_state);
    Serial.println("[TCA9554] IO expander initialized");
    return true;
}

static void tca9554_set_pin(uint8_t pin, bool value) {
    if (value)
        tca9554_output_state |= (1 << pin);
    else
        tca9554_output_state &= ~(1 << pin);
    tca9554_write(TCA9554_OUTPUT_REG, tca9554_output_state);
}

static void io_backlight(bool on) {
    tca9554_set_pin(WS_EXIO_LCD_BL, on ? 1 : 0);
    Serial.printf("[IO] Backlight %s\n", on ? "ON" : "OFF");
}

static void io_lcd_reset() {
    tca9554_set_pin(WS_EXIO_LCD_RST, 0);
    delay(10);
    tca9554_set_pin(WS_EXIO_LCD_RST, 1);
    delay(50);
    Serial.println("[IO] LCD reset done");
}

static void io_touch_reset() {
    tca9554_set_pin(WS_EXIO_TP_RST, 0);
    delay(10);
    tca9554_set_pin(WS_EXIO_TP_RST, 1);
    delay(50);
    Serial.println("[IO] Touch reset done");
}

// ============================================
// RGB LCD Panel (ST7262, 1024x600)
// ============================================
#define LCD_PCLK_PIN   GPIO_NUM_7
#define LCD_HSYNC_PIN  GPIO_NUM_46
#define LCD_VSYNC_PIN  GPIO_NUM_3
#define LCD_DE_PIN     GPIO_NUM_5

static esp_lcd_panel_handle_t lcd_panel = NULL;
static SemaphoreHandle_t lvgl_mutex = NULL;
static volatile bool lvgl_task_started = false;

static bool IRAM_ATTR on_frame_done(esp_lcd_panel_handle_t panel,
                                     esp_lcd_rgb_panel_event_data_t *edata,
                                     void *user_ctx) {
    return false;
}

static bool rgb_lcd_init() {
    Serial.println("[LCD] Initializing RGB panel 1024x600...");

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_PLL160M;
    panel_config.timings.pclk_hz = 30 * 1000 * 1000;  // 30MHz
    panel_config.timings.h_res = WS_LCD_H_RES;
    panel_config.timings.v_res = WS_LCD_V_RES;
    // Timings from confirmed working ESPHome config for 7B
    panel_config.timings.hsync_pulse_width = 162;
    panel_config.timings.hsync_back_porch  = 152;
    panel_config.timings.hsync_front_porch = 48;
    panel_config.timings.vsync_pulse_width = 45;
    panel_config.timings.vsync_back_porch  = 13;
    panel_config.timings.vsync_front_porch = 3;
    panel_config.timings.flags.pclk_active_neg = true;
    panel_config.data_width = 16;
    panel_config.psram_trans_align = 64;
    panel_config.on_frame_trans_done = on_frame_done;
    panel_config.hsync_gpio_num = LCD_HSYNC_PIN;
    panel_config.vsync_gpio_num = LCD_VSYNC_PIN;
    panel_config.de_gpio_num    = LCD_DE_PIN;
    panel_config.pclk_gpio_num  = LCD_PCLK_PIN;
    panel_config.disp_gpio_num  = -1;

    // RGB565 data pins: B[3:7], G[2:7], R[3:7]
    panel_config.data_gpio_nums[0]  = GPIO_NUM_14;  // B3
    panel_config.data_gpio_nums[1]  = GPIO_NUM_38;  // B4
    panel_config.data_gpio_nums[2]  = GPIO_NUM_18;  // B5
    panel_config.data_gpio_nums[3]  = GPIO_NUM_17;  // B6
    panel_config.data_gpio_nums[4]  = GPIO_NUM_10;  // B7
    panel_config.data_gpio_nums[5]  = GPIO_NUM_39;  // G2
    panel_config.data_gpio_nums[6]  = GPIO_NUM_0;   // G3
    panel_config.data_gpio_nums[7]  = GPIO_NUM_45;  // G4
    panel_config.data_gpio_nums[8]  = GPIO_NUM_48;  // G5
    panel_config.data_gpio_nums[9]  = GPIO_NUM_47;  // G6
    panel_config.data_gpio_nums[10] = GPIO_NUM_21;  // G7
    panel_config.data_gpio_nums[11] = GPIO_NUM_1;   // R3
    panel_config.data_gpio_nums[12] = GPIO_NUM_2;   // R4
    panel_config.data_gpio_nums[13] = GPIO_NUM_42;  // R5
    panel_config.data_gpio_nums[14] = GPIO_NUM_41;  // R6
    panel_config.data_gpio_nums[15] = GPIO_NUM_40;  // R7

    panel_config.flags.fb_in_psram = true;

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &lcd_panel);
    if (err != ESP_OK) {
        Serial.printf("[LCD] FAILED to create panel: 0x%x\n", err);
        return false;
    }

    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);

    // TEST: Fill screen with red to verify LCD works
    uint16_t *test_line = (uint16_t *)heap_caps_malloc(WS_LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (test_line) {
        for (int i = 0; i < WS_LCD_H_RES; i++) test_line[i] = 0xF800; // Red
        for (int y = 0; y < WS_LCD_V_RES; y++) {
            esp_lcd_panel_draw_bitmap(lcd_panel, 0, y, WS_LCD_H_RES, y + 1, test_line);
        }
        free(test_line);
        Serial.println("[LCD] Test pattern drawn (RED)");
    }

    Serial.println("[LCD] RGB panel initialized OK");
    return true;
}

// ============================================
// GT911 Touch Controller
// ============================================
#define GT911_ADDR_PRIMARY   0x5D
#define GT911_ADDR_SECONDARY 0x14

static uint8_t gt911_addr = 0;
static bool gt911_ready = false;

static bool gt911_write_reg(uint16_t reg, uint8_t data) {
    Wire.beginTransmission(gt911_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(data);
    return (Wire.endTransmission() == 0);
}

static bool gt911_read_reg(uint16_t reg, uint8_t *data, uint8_t len) {
    Wire.beginTransmission(gt911_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    size_t got = Wire.requestFrom(gt911_addr, len);
    for (uint8_t i = 0; i < len && Wire.available(); i++) {
        data[i] = Wire.read();
    }
    return (got == len);
}

static bool gt911_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

static bool gt911_init() {
    Serial.println("[GT911] Initializing touch controller...");

    // INT LOW before reset to select address 0x5D
    pinMode(WS_TOUCH_INT_PIN, OUTPUT);
    digitalWrite(WS_TOUCH_INT_PIN, LOW);
    delay(1);

    // Reset via IO expander
    io_touch_reset();

    // Release INT after address latch
    delay(50);
    pinMode(WS_TOUCH_INT_PIN, INPUT);
    delay(100);

    // Probe addresses
    if (gt911_probe(0x5D)) {
        gt911_addr = 0x5D;
        gt911_ready = true;
        Serial.println("[GT911] Found at 0x5D");
    } else if (gt911_probe(0x14)) {
        gt911_addr = 0x14;
        gt911_ready = true;
        Serial.println("[GT911] Found at 0x14");
    } else {
        gt911_ready = false;
        Serial.println("[GT911] ERROR: Not found!");
        return false;
    }

    // Read product ID
    uint8_t id[4] = {0};
    if (gt911_read_reg(0x8140, id, 4)) {
        Serial.printf("[GT911] Product ID: %c%c%c%c\n", id[0], id[1], id[2], id[3]);
    }
    return true;
}

static bool gt911_read_touch(uint16_t *x, uint16_t *y) {
    if (!gt911_ready) return false;

    uint8_t status = 0;
    if (!gt911_read_reg(0x814E, &status, 1)) return false;

    uint8_t touch_count = status & 0x0F;
    bool buffer_ready = (status & 0x80) != 0;

    if (!buffer_ready || touch_count == 0 || touch_count > 5) {
        if (buffer_ready) gt911_write_reg(0x814E, 0);
        return false;
    }

    uint8_t data[4];
    if (!gt911_read_reg(0x8150, data, 4)) {
        gt911_write_reg(0x814E, 0);
        return false;
    }

    *x = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    *y = (uint16_t)data[2] | ((uint16_t)data[3] << 8);

    gt911_write_reg(0x814E, 0);
    return true;
}

// ============================================
// LVGL Port
// ============================================
#define LVGL_BUF_LINES  100

static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_indev_drv_t indev_drv;
static lv_disp_t *lvgl_disp = NULL;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    if (panel) {
        esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    }
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t x, y;
    if (gt911_read_touch(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// FreeRTOS task for LVGL (runs on core 1)
static void lvgl_task(void *arg) {
    while (!lvgl_task_started) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    while (true) {
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static bool lvgl_port_init() {
    Serial.println("[LVGL] Initializing...");

    lvgl_mutex = xSemaphoreCreateMutex();
    lv_init();

    // Double buffer in PSRAM (100 lines each)
    size_t buf_size = WS_LCD_H_RES * LVGL_BUF_LINES;
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    if (!buf1 || !buf2) {
        Serial.println("[LVGL] FAILED to allocate buffers in PSRAM");
        return false;
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_size);

    // Display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = WS_LCD_H_RES;
    disp_drv.ver_res = WS_LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = lcd_panel;
    lvgl_disp = lv_disp_drv_register(&disp_drv);

    // Touch input driver
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    // LVGL task on core 1
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 2, NULL, 1);

    Serial.printf("[LVGL] Ready - %dx%d, double buffer %d lines\n", WS_LCD_H_RES, WS_LCD_V_RES, LVGL_BUF_LINES);
    return true;
}

// ============================================
// Public API
// ============================================

bool waveshare_init(void) {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║  Waveshare ESP32-S3 LCD-7 Init    ║");
    Serial.println("╚════════════════════════════════════╝");

    // 1. I2C bus
    Wire.begin(WS_I2C_SDA, WS_I2C_SCL);
    Wire.setClock(400000);
    Serial.printf("[I2C] Bus started SDA=%d SCL=%d @ 400kHz\n", WS_I2C_SDA, WS_I2C_SCL);

    // 2. TCA9554 IO expander
    if (!tca9554_init()) {
        Serial.println("[INIT] WARNING: TCA9554 not found, trying without IO expander");
    }

    // 3. Backlight OFF during init
    io_backlight(false);

    // 4. LCD Reset
    io_lcd_reset();

    // 5. RGB LCD panel
    if (!rgb_lcd_init()) {
        Serial.println("[INIT] FATAL: LCD init failed");
        return false;
    }

    // 6. GT911 touch
    gt911_init();

    // 7. LVGL port (includes FreeRTOS task)
    if (!lvgl_port_init()) {
        Serial.println("[INIT] FATAL: LVGL init failed");
        return false;
    }

    // 8. Backlight ON
    delay(100);
    io_backlight(true);

    Serial.println("[INIT] Waveshare display ready!\n");
    return true;
}

void waveshare_lvgl_handler(void) {
    // LVGL now runs in its own FreeRTOS task.
    // This function provides lock/unlock for safe UI updates from main loop.
    // The main loop can call this as a no-op or use lock/unlock.
}

bool waveshare_lvgl_lock(int timeout_ms) {
    if (!lvgl_mutex) return false;
    return xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void waveshare_lvgl_unlock(void) {
    if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
}

void waveshare_lvgl_task_start(void) {
    lvgl_task_started = true;
}

lv_disp_t* waveshare_get_display(void) {
    return lvgl_disp;
}
