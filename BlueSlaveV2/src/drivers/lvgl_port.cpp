// =============================================================================
// lvgl_port.cpp - LVGL port for Waveshare ESP32-S3-Touch-LCD-7B
// =============================================================================
#include "lvgl_port.h"
#include "gt911_touch.h"
#include "rgb_lcd.h"
#include "config.h"
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

// Display flush callback - immediate copy (no vsync wait needed for single FB)
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
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (true) {
        if (lvgl_port_lock(10)) {
            lv_timer_handler();
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void lvgl_port_init(esp_lcd_panel_handle_t lcd_handle) {
    lcd_panel = lcd_handle;

    // Create mutex
    lvgl_mutex = xSemaphoreCreateMutex();

    // Init LVGL
    lv_init();

    // Allocate single full-screen buffer in PSRAM (fewer, larger copies = less flicker)
    size_t buf_size = SCREEN_WIDTH * SCREEN_HEIGHT;
    lv_color_t* buf1 = (lv_color_t*)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, buf_size);

    // Display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = lcd_panel;
    disp_drv.full_refresh = 1;  // One full copy per frame instead of many partial
    lv_disp_drv_register(&disp_drv);

    // Touch input driver
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // Create LVGL task on core 1
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "LVGL port initialized: %dx%d, double buffer, direct mode",
             SCREEN_WIDTH, SCREEN_HEIGHT);
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
