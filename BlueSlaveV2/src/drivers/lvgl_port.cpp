// =============================================================================
// lvgl_port.cpp - LVGL port for Waveshare ESP32-S3-Touch-LCD-7B
// ESP-IDF 5.x — zero-copy double-buffer via bounce buffers
// =============================================================================
#include "lvgl_port.h"
#include "gt911_touch.h"
#include "rgb_lcd.h"
#include "../../include/config.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char* TAG = "LVGL_PORT";

static SemaphoreHandle_t lvgl_mutex = NULL;
static SemaphoreHandle_t vsync_sem = NULL;
static volatile bool task_started = false;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Vsync ISR — signals that the current frame scan just finished
static bool IRAM_ATTR lvgl_on_vsync(esp_lcd_panel_handle_t panel,
                                     const esp_lcd_rgb_panel_event_data_t* edata,
                                     void* user_ctx) {
    (void)panel; (void)edata; (void)user_ctx;
    BaseType_t woken = pdFALSE;
    if (vsync_sem) xSemaphoreGiveFromISR(vsync_sem, &woken);
    return woken == pdTRUE;
}

// Flush: zero-copy framebuffer swap.
// LVGL rendered directly into one of the panel's own PSRAM framebuffers.
// draw_bitmap recognises the pointer and atomically swaps the active FB
// — no memcpy happens. Then we wait for vsync so DMA starts reading the
// new buffer before LVGL writes to the old one (now the back buffer).
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;

    // Zero-copy swap: panel driver recognises its own FB pointer
    esp_lcd_panel_draw_bitmap(panel, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, color_p);

    // Wait for vsync — ensures DMA switched to new FB before we return
    xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(100));

    lv_disp_flush_ready(drv);
}

// Touch read
static void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
    if (!gt911_is_ready()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    TouchPoint tp = gt911_read();
    data->state = tp.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = tp.x;
    data->point.y = tp.y;
}

// LVGL FreeRTOS task on core 1
static void lvgl_task(void* arg) {
    (void)arg;
    while (!task_started) vTaskDelay(pdMS_TO_TICKS(10));

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        if (lvgl_port_lock(15)) {
            lv_timer_handler();
            lvgl_port_unlock();
        }
        // 16ms period — matches 60Hz LCD refresh. Halves Core 1 load &
        // I2C contention vs 8ms, with no visible difference.
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(16));
    }
}

void lvgl_port_init(esp_lcd_panel_handle_t lcd_handle) {
    lcd_panel = lcd_handle;

    lvgl_mutex = xSemaphoreCreateMutex();
    vsync_sem  = xSemaphoreCreateBinary();

    lv_init();
    rgb_lcd_register_vsync_cb(lcd_panel, lvgl_on_vsync, NULL);

    // Get the panel's own PSRAM framebuffers — LVGL renders directly into
    // these. On flush, draw_bitmap recognises the pointer and does a
    // zero-copy swap instead of memcpy. Saves 2.4MB PSRAM and eliminates
    // the entire copy step.
    void* fb0 = NULL;
    void* fb1 = NULL;
    rgb_lcd_get_frame_buffers(lcd_panel, &fb0, &fb1);

    const size_t buf_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    lv_disp_draw_buf_init(&draw_buf, fb0, fb1, buf_pixels);

    // direct_mode: LVGL renders only dirty areas into the panel FB.
    // Flush swaps the active FB (zero-copy). LVGL then syncs dirty areas
    // from the old front buffer to the new back buffer automatically.
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = SCREEN_WIDTH;
    disp_drv.ver_res      = SCREEN_HEIGHT;
    disp_drv.flush_cb     = disp_flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.user_data    = lcd_panel;
    disp_drv.direct_mode  = 1;
    disp_drv.full_refresh = 0;
    lv_disp_drv_register(&disp_drv);

    // Touch
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // LVGL task — core 1, priority 3 (higher = less preemption during flush)
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 12288, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "LVGL port: %dx%d direct_mode, zero-copy double-buffer, bounce DMA",
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
