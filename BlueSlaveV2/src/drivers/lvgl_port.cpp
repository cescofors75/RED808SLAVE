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
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

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
#if !PORTRAIT_MODE
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;

    // Step 1: swap FB index (bounce buffers defer display switch to frame end)
    esp_lcd_panel_draw_bitmap(panel, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, color_p);

    // Step 2: arm the handshake — must come AFTER draw_bitmap
    xSemaphoreGive(sem_gui_ready);

    // Step 3-5: wait for the first vsync that fires after our swap
    xSemaphoreTake(sem_vsync_end, pdMS_TO_TICKS(500));

    lv_disp_flush_ready(drv);
}
#endif

// Portrait mode: partial-area flush — sw_rotate already rotated pixels to physical coords.
// Writes the rendered area into the active framebuffer; bounce-buffer DMA picks it up.
#if PORTRAIT_MODE
static void disp_flush_portrait_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    if (area->x2 < 0 || area->y2 < 0 || area->x1 >= SCREEN_WIDTH || area->y1 >= SCREEN_HEIGHT) {
        lv_disp_flush_ready(drv);
        return;
    }
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(drv);
}
#endif

// Touch read
static void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
    if (!gt911_is_ready()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    TouchPoint points[1] = {};
    uint8_t count = gt911_get_points(points, 1);
    if (count > 0 && points[0].pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        // Pass raw physical coordinates — LVGL's sw_rotate handles
        // the coordinate transformation automatically via disp_drv.rotated
        data->point.x = points[0].x;
        data->point.y = points[0].y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// LVGL FreeRTOS task on core 1
static void lvgl_task(void* arg) {
    (void)arg;
    while (!task_started) vTaskDelay(pdMS_TO_TICKS(10));

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        // Touch polling moved to dedicated touch_task (Core 0, pri 3).
        // LVGL indev callback reads from gt911 cache — no I2C here.

        if (lvgl_port_lock(15)) {
            // Check if live pads need visual refresh (set by Core 0 loop)
            if (livePadsVisualDirty.exchange(false) && currentScreen == SCREEN_LIVE) {
                ui_update_live_pads();
            }
            lv_timer_handler();
            lvgl_port_unlock();
        }
        // Match the stable LCD profile more closely to avoid PSRAM/RGB bus saturation.
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(S3_LVGL_TASK_PERIOD_MS));
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
    rgb_lcd_register_vsync_cb(lcd_panel, lvgl_on_vsync, NULL);

#if PORTRAIT_MODE
    // Portrait mode: partial rendering + sw_rotate.
    // Prefer small internal-SRAM buffers to reduce PSRAM contention with RGB DMA.
    static constexpr size_t PORT_BUF_LINES = S3_LVGL_PORT_BUF_LINES;
    const size_t buf_px = SCREEN_WIDTH * PORT_BUF_LINES;
    lv_color_t* buf_a = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_color_t* buf_b = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf_a || !buf_b) {
        if (buf_a) heap_caps_free(buf_a);
        if (buf_b) heap_caps_free(buf_b);
        buf_a = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        buf_b = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_LOGW(TAG, "Portrait draw buffers fell back to PSRAM");
    }
    if (!buf_a || !buf_b) {
        ESP_LOGE(TAG, "Failed to allocate portrait draw buffers");
        return;
    }
    lv_disp_draw_buf_init(&draw_buf, buf_a, buf_b, buf_px);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = SCREEN_WIDTH;   // Physical: 1024
    disp_drv.ver_res      = SCREEN_HEIGHT;  // Physical: 600
    disp_drv.flush_cb     = disp_flush_portrait_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.user_data    = lcd_panel;
    disp_drv.sw_rotate    = 1;
    disp_drv.rotated      = LV_DISP_ROT_90;
    disp_drv.direct_mode  = 0;
    disp_drv.full_refresh = 0;
#else
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
    lv_disp_draw_buf_init(&draw_buf, fb0, fb1, buf_pixels);

    // full_refresh: LVGL calls flush once per frame with full screen area.
    // The swap is zero-copy so sending the "full screen" costs nothing.
    // This avoids multi-flush sync issues that cause flickering in direct_mode.
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = SCREEN_WIDTH;
    disp_drv.ver_res      = SCREEN_HEIGHT;
    disp_drv.flush_cb     = disp_flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.user_data    = lcd_panel;
    disp_drv.direct_mode  = 1;
    disp_drv.full_refresh = 1;
#endif
    lv_disp_drv_register(&disp_drv);

    // Touch
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // LVGL task — core 1, priority 3 (higher = less preemption during flush)
    BaseType_t task_ok = xTaskCreatePinnedToCore(lvgl_task, "lvgl", 12288, NULL, 3, NULL, 1);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
    }

    ESP_LOGI(TAG, "LVGL port: %dx%d %s, %s",
             UI_W, UI_H,
             PORTRAIT_MODE ? "portrait sw_rotate" : "landscape zero-copy",
             PORTRAIT_MODE ? "partial render" : "dual-semaphore vsync sync");
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
