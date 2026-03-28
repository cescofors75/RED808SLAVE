// =============================================================================
// lvgl_port.cpp - LVGL port for Waveshare ESP32-S3-Touch-LCD-7B
// =============================================================================
#include "lvgl_port.h"
#include "gt911_touch.h"
#include "rgb_lcd.h"
#include "../../include/config.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char* TAG = "LVGL_PORT";

static SemaphoreHandle_t lvgl_mutex = NULL;
static volatile bool task_started = false;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_color_t* draw_buf_1 = NULL;
static lv_color_t* draw_buf_2 = NULL;

// Display flush callback - direct copy, no blocking wait
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(drv);
}

// Touch read callback
static void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (!gt911_is_ready()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    TouchPoint tp = gt911_read();
    if (tp.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tp.x;
        data->point.y = tp.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// LVGL FreeRTOS task
static void lvgl_task(void* arg) {
    // Wait until UI is created
    while (!task_started) {
        vTaskDelay(pdMS_TO_TICKS(8));
    }

    while (true) {
        if (lvgl_port_lock(10)) {
            lv_timer_handler();
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

void lvgl_port_init(esp_lcd_panel_handle_t lcd_handle) {
    lcd_panel = lcd_handle;

    // Create mutex
    lvgl_mutex = xSemaphoreCreateMutex();

    // Init LVGL
    lv_init();

    // Partial double buffer in PSRAM to reduce full-screen redraw flicker
    const size_t buf_lines = 40;
    const size_t buf_size = SCREEN_WIDTH * buf_lines;
    draw_buf_1 = (lv_color_t*)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    draw_buf_2 = (lv_color_t*)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!draw_buf_1 || !draw_buf_2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        return;
    }

    lv_disp_draw_buf_init(&draw_buf, draw_buf_1, draw_buf_2, buf_size);

    // Display driver - partial refresh minimizes visible redraw artifacts
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = lcd_panel;
    disp_drv.full_refresh = 0;
    lv_disp_drv_register(&disp_drv);

    // Touch input driver
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // Create LVGL task on core 1 with larger stack for UI
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 16384, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "LVGL port initialized: %dx%d, partial refresh, double partial buffer (%d lines)",
             SCREEN_WIDTH, SCREEN_HEIGHT, (int)buf_lines);
}

bool lvgl_port_lock(int timeout_ms) {
    return xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvgl_port_unlock() {
    xSemaphoreGive(lvgl_mutex);
}

void lvgl_port_task_start() {
    task_started = true;
}
