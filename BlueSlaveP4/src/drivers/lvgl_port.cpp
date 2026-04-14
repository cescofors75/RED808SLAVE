// =============================================================================
// lvgl_port.cpp — LVGL display/touch integration for ESP32-P4
// Guition JC1060P470C: 1024×600 MIPI-DSI + GT911 touch
// =============================================================================

#include "lvgl_port.h"
#include "display_init.h"
#include "../include/config.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <esp_lcd_panel_ops.h>

// =============================================================================
// LVGL DRAW BUFFERS — allocated in PSRAM
// =============================================================================
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf1 = NULL;
static lv_color_t* buf2 = NULL;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t touch_drv;
static lv_indev_t* touch_indev = NULL;

// Full-screen buffer for DPI panel (required for proper DMA2D flush)
#define LVGL_BUF_SIZE   (LCD_H_RES * LCD_V_RES)

// =============================================================================
// DISPLAY FLUSH CALLBACK
// =============================================================================
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    esp_lcd_panel_handle_t panel = display_get_panel();
    if (panel) {
        esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                                  area->x2 + 1, area->y2 + 1, color_p);
    }
    lv_disp_flush_ready(drv);
}

// =============================================================================
// GT911 TOUCH READ
// =============================================================================
static bool gt911_initialized = false;

static void gt911_init(void) {
    Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL, 400000);

    // Reset GT911 into address 0x5D mode
    pinMode(TOUCH_INT_GPIO, OUTPUT);
    pinMode(TOUCH_RST_GPIO, OUTPUT);
    digitalWrite(TOUCH_INT_GPIO, LOW);
    digitalWrite(TOUCH_RST_GPIO, LOW);
    delay(10);
    digitalWrite(TOUCH_RST_GPIO, HIGH);
    delay(50);
    pinMode(TOUCH_INT_GPIO, INPUT);
    delay(50);

    // Verify GT911 is present
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
        gt911_initialized = true;
        P4_LOG_PRINTLN("[Touch] GT911 detected at 0x5D");
    } else {
        P4_LOG_PRINTLN("[Touch] GT911 NOT found!");
    }
}

static void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    data->state = LV_INDEV_STATE_REL;
    if (!gt911_initialized) return;

    // Read GT911 status register (0x814E)
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x81);
    Wire.write(0x4E);
    if (Wire.endTransmission(false) != 0) return;
    if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)1) != 1) return;
    uint8_t status = Wire.read();

    uint8_t touches = status & 0x0F;
    if (!(status & 0x80) || touches == 0 || touches > 5) {
        // Clear buffer-ready flag
        if (status & 0x80) {
            Wire.beginTransmission(TOUCH_I2C_ADDR);
            Wire.write(0x81);
            Wire.write(0x4E);
            Wire.write((uint8_t)0);
            Wire.endTransmission();
        }
        return;
    }

    // Read first touch point (0x8150..0x8153: x_lo, x_hi, y_lo, y_hi)
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x81);
    Wire.write(0x50);
    if (Wire.endTransmission(false) != 0) return;
    if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)4) != 4) return;

    uint16_t x = Wire.read() | ((uint16_t)Wire.read() << 8);
    uint16_t y = Wire.read() | ((uint16_t)Wire.read() << 8);

    // Clear buffer-ready flag
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write((uint8_t)0);
    Wire.endTransmission();

    // Transform for portrait mode if enabled
#if PORTRAIT_MODE
    uint16_t vx = (LCD_V_RES - 1) - y;
    uint16_t vy = x;
    data->point.x = vx;
    data->point.y = vy;
#else
    data->point.x = x;
    data->point.y = y;
#endif
    data->state = LV_INDEV_STATE_PR;
}

// =============================================================================
// INIT
// =============================================================================
void lvgl_port_init(void) {
    P4_LOG_PRINTLN("[LVGL] Initializing...");

    lv_init();

    // Allocate draw buffers in PSRAM
    buf1 = (lv_color_t*)heap_caps_malloc(LVGL_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t*)heap_caps_malloc(LVGL_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        P4_LOG_PRINTLN("[LVGL] FATAL: Failed to allocate draw buffers in PSRAM!");
        return;
    }
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LVGL_BUF_SIZE);

    // Display driver — DPI panels MUST use full_refresh
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;   // 1024
    disp_drv.ver_res = LCD_V_RES;   // 600
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;      // Required for MIPI-DPI
    lv_disp_drv_register(&disp_drv);

    // Touch input
    gt911_init();
    lv_indev_drv_init(&touch_drv);
    touch_drv.type = LV_INDEV_TYPE_POINTER;
    touch_drv.read_cb = touch_read_cb;
    touch_indev = lv_indev_drv_register(&touch_drv);

    P4_LOG_PRINTF("[LVGL] Ready: %dx%d, portrait=%d\n", UI_W, UI_H, PORTRAIT_MODE);
}

void lvgl_port_update(void) {
    lv_timer_handler();
}

lv_indev_t* lvgl_port_get_touch_indev(void) {
    return touch_indev;
}
