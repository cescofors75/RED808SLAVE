// =============================================================================
// lvgl_port.cpp - LVGL port for Waveshare ESP32-S3-Touch-LCD-7B
// ESP-IDF 4.4 single-framebuffer RGB panel — vsync-aligned direct_mode
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
static lv_color_t* draw_buf_1 = NULL;
static lv_color_t* draw_buf_2 = NULL;

// Vsync ISR — signals that the current frame scan just finished
static bool IRAM_ATTR lvgl_on_vsync(esp_lcd_panel_handle_t panel,
                                     esp_lcd_rgb_panel_event_data_t* edata,
                                     void* user_ctx) {
    (void)panel; (void)edata; (void)user_ctx;
    BaseType_t woken = pdFALSE;
    if (vsync_sem) xSemaphoreGiveFromISR(vsync_sem, &woken);
    return woken == pdTRUE;
}

// Wait for vsync — call right before writing to the panel framebuffer
static inline void wait_vsync() {
    if (!vsync_sem) return;
    xSemaphoreTake(vsync_sem, 0);               // drain any stale signal
    xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(50)); // wait for real vsync
}

// Flush: direct_mode — LVGL rendered into our buffer, copy dirty rects to panel FB
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;

    // Wait for vsync so DMA just started a new scan → we have ~16ms to write
    wait_vsync();

    // Copy the dirty area to the panel framebuffer
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_p);

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
        // Fixed 8ms period (~125 Hz timer handler, but actual LCD is ~60 Hz)
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(8));
    }
}

void lvgl_port_init(esp_lcd_panel_handle_t lcd_handle) {
    lcd_panel = lcd_handle;

    lvgl_mutex = xSemaphoreCreateMutex();
    vsync_sem  = xSemaphoreCreateBinary();

    lv_init();
    rgb_lcd_set_vsync_cb(lvgl_on_vsync, NULL);

    // Two full-screen buffers in PSRAM — required for direct_mode
    // Align to 64 bytes for optimal DMA transfer bandwidth
    const size_t buf_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    draw_buf_1 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    draw_buf_2 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!draw_buf_1 || !draw_buf_2) {
        ESP_LOGE(TAG, "FATAL: Cannot allocate LVGL draw buffers in PSRAM!");
        return;
    }
    // Zero both buffers so first frame is clean black
    memset(draw_buf_1, 0, buf_pixels * sizeof(lv_color_t));
    memset(draw_buf_2, 0, buf_pixels * sizeof(lv_color_t));

    lv_disp_draw_buf_init(&draw_buf, draw_buf_1, draw_buf_2, buf_pixels);

    // direct_mode: LVGL renders only dirty areas into the buffer.
    // On flush we copy just those dirty rect(s) to the panel FB.
    // This is MUCH faster than full_refresh (which copies 1.2MB every frame).
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = SCREEN_WIDTH;
    disp_drv.ver_res      = SCREEN_HEIGHT;
    disp_drv.flush_cb     = disp_flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.user_data    = lcd_panel;
    disp_drv.direct_mode  = 1;   // Only flush changed regions
    disp_drv.full_refresh = 0;
    lv_disp_drv_register(&disp_drv);

    // Touch
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // LVGL task — core 1, priority 3 (higher = less preemption during flush)
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 16384, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "LVGL port: %dx%d direct_mode, 2x full PSRAM buffers, vsync-synced",
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
