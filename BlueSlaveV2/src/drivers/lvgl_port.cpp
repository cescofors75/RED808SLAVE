// =============================================================================
// lvgl_port.cpp - LVGL port for Waveshare ESP32-S3-Touch-LCD-7B
// ESP-IDF 5.x — zero-copy double-buffer, dual-semaphore vsync sync (Espressif pattern)
// =============================================================================
#include "lvgl_port.h"
#include "gt911_touch.h"
#include "rgb_lcd.h"
#include "../../include/config.h"
#include "../../include/system_state.h"
#include "../ui/ui_screens.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char* TAG = "LVGL_PORT";

static SemaphoreHandle_t lvgl_mutex   = NULL;
static SemaphoreHandle_t sem_vsync_end = NULL;  // vsync that acked our swap
static SemaphoreHandle_t sem_gui_ready = NULL;  // swap is pending, wait for vsync
static volatile bool task_started = false;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static lv_display_t* display = NULL;
static lv_indev_t* touch_indev = NULL;

// Vsync ISR — Espressif dual-semaphore handshake pattern.
//
// Only unblocks flush() when a swap is genuinely pending (sem_gui_ready was given
// by the flush callback AFTER draw_bitmap).  Spurious vsyncs that fire before
// any swap are silently ignored, making the busy-drain loop unnecessary and
// eliminating the race where a valid post-swap vsync can be accidentally drained.
static bool IRAM_ATTR lvgl_on_vsync(esp_lcd_panel_handle_t panel,
                                     const esp_lcd_rgb_panel_event_data_t* edata,
                                     void* user_ctx) {
    (void)panel; (void)edata; (void)user_ctx;
    BaseType_t woken = pdFALSE;
    // Consume the gui_ready token; if one was present, release vsync_end.
    if (sem_gui_ready && xSemaphoreTakeFromISR(sem_gui_ready, &woken) == pdTRUE) {
        xSemaphoreGiveFromISR(sem_vsync_end, &woken);
    }
    return woken == pdTRUE;
}

// Flush: zero-copy swap + vsync-gated completion (Espressif recommended pattern).
//
// Flow:
//   1. draw_bitmap(FB_B) — sets cur_fb_index=B (bounce buffers still serve FB_A)
//   2. give sem_gui_ready — signals ISR "a swap is waiting for vsync ack"
//   3. Frame N finishes: bb_fb_index wraps to B (end-of-frame bounce wrap)
//   4. Vsync fires → ISR takes sem_gui_ready → gives sem_vsync_end
//   5. We take sem_vsync_end — FB_A is no longer displayed, safe for LVGL
//   6. flush_ready — LVGL renders next frame into FB_A
//
// Because sem_gui_ready is given AFTER draw_bitmap, the ISR cannot fire for a
// vsync that precedes the swap, eliminating the drain-loop race condition.
static void disp_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)area;
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    xSemaphoreTake(sem_gui_ready, 0);
    xSemaphoreTake(sem_vsync_end, 0);

    // Step 1: swap FB index (bounce buffers defer display switch to frame end)
    esp_lcd_panel_draw_bitmap(panel, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, px_map);

    // Step 2: arm the handshake — must come AFTER draw_bitmap
    xSemaphoreGive(sem_gui_ready);

    // Step 3: complete the flush in the LVGL task, matching the safer P4 path.
    // Timeout still releases LVGL if a VSYNC interrupt is missed.
    xSemaphoreTake(sem_vsync_end, pdMS_TO_TICKS(34));
    lv_display_flush_ready(disp);
}

// Touch read
static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (!gt911_is_ready()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    TouchPoint points[1] = {};
    uint8_t count = gt911_get_points(points, 1);
    if (count > 0 && points[0].pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
#if PORTRAIT_MODE
        data->point.x = UI_W - 1 - points[0].y;
        data->point.y = points[0].x;
#else
    data->point.x = constrain((int)points[0].x, 0, UI_W - 1);
    data->point.y = constrain((int)points[0].y, 0, UI_H - 1);
#endif
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// LVGL FreeRTOS task on core 1
static void lvgl_task(void* arg) {
    (void)arg;
    while (!task_started) vTaskDelay(pdMS_TO_TICKS(10));
    while (true) {
        if (lvgl_port_lock(15)) {
            // Check if live pads need visual refresh (set by Core 0 loop)
            if (livePadsVisualDirty.exchange(false) && currentScreen == SCREEN_LIVE) {
                ui_update_live_pads();
            }
            lv_timer_handler();
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void lvgl_port_init(esp_lcd_panel_handle_t lcd_handle) {
    lcd_panel = lcd_handle;

    lvgl_mutex    = xSemaphoreCreateMutex();
    sem_vsync_end = xSemaphoreCreateBinary();
    sem_gui_ready = xSemaphoreCreateBinary();
    if (!lvgl_mutex || !sem_vsync_end || !sem_gui_ready) {
        ESP_LOGE(TAG, "Failed to create LVGL synchronization primitives");
        return;
    }

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
    rgb_lcd_register_vsync_cb(lcd_panel, lvgl_on_vsync, NULL);

    // Landscape: zero-copy double-buffer (original fast path).
    // Get the panel's own PSRAM framebuffers — LVGL renders directly into
    // these. On flush, draw_bitmap recognises the pointer and does a
    // zero-copy swap instead of memcpy. Saves 2.4MB PSRAM and eliminates
    // the entire copy step.
    void* fb0 = NULL;
    void* fb1 = NULL;
    rgb_lcd_get_frame_buffers(lcd_panel, &fb0, &fb1);
    if (!fb0 || !fb1) {
        ESP_LOGE(TAG, "Panel framebuffers unavailable");
        return;
    }

    const size_t buf_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    // full_refresh: LVGL calls flush once per frame with full screen area.
    // The swap is zero-copy so sending the "full screen" costs nothing.
    // This avoids multi-flush sync issues that cause flickering in direct_mode.
    display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, disp_flush_cb);
    lv_display_set_user_data(display, lcd_panel);
    lv_display_set_buffers(display, fb0, fb1,
                           buf_pixels * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
#if PORTRAIT_MODE
    // Keep the proven 1024x600 RGB framebuffer path, but expose a 600x1024
    // logical canvas to the UI. LVGL rotates drawing into the direct buffers.
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
    lv_display_set_matrix_rotation(display, true);
#endif
    lv_display_set_default(display);

    // Touch
    touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch_indev, display);
    lv_indev_set_read_cb(touch_indev, touch_read_cb);

    // LVGL task — core 1, priority 3 (higher = less preemption during flush)
    BaseType_t task_ok = xTaskCreatePinnedToCore(lvgl_task, "lvgl", 12288, NULL, 3, NULL, 1);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
    }

    ESP_LOGI(TAG, "LVGL port: %dx%d %s, %s",
             UI_W, UI_H,
             PORTRAIT_MODE ? "portrait experimental" : "landscape zero-copy",
             S3_LCD_ROTATE_180 ? "panel mirror 180 + direct" : (PORTRAIT_MODE ? "matrix rotation" : "sync-vsync direct"));
}

bool lvgl_port_lock(int timeout_ms) {
    if (!lvgl_mutex) return false;
    return xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvgl_port_unlock() {
    if (lvgl_mutex) xSemaphoreGive(lvgl_mutex);
}

void lvgl_port_task_start() {
    task_started = true;
}
