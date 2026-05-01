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
#if !PORTRAIT_MODE
static void disp_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)area;
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    // Step 1: swap FB index (bounce buffers defer display switch to frame end)
    esp_lcd_panel_draw_bitmap(panel, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, px_map);

    // Step 2: arm the handshake — must come AFTER draw_bitmap
    xSemaphoreGive(sem_gui_ready);

    // Step 3-5: wait for the first vsync that fires after our swap
    xSemaphoreTake(sem_vsync_end, pdMS_TO_TICKS(500));

    lv_display_flush_ready(disp);
}
#endif

// Portrait mode: PARTIAL render with software 90° CCW rotation per stripe.
// Logical (UI_W=600, UI_H=1024) → physical (SCREEN_WIDTH=1024, SCREEN_HEIGHT=600).
// Mapping: phys_x = ly, phys_y = (UI_W - 1) - lx  (90° CCW)
// A logical rect (lx1..lx2, ly1..ly2) maps to physical rect:
//   px1 = ly1, px2 = ly2   (width = ly2-ly1+1)
//   py1 = UI_W-1-lx2, py2 = UI_W-1-lx1   (height = lx2-lx1+1)
#if PORTRAIT_MODE
static void disp_flush_portrait_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    const int lx1 = area->x1, ly1 = area->y1;
    const int lx2 = area->x2, ly2 = area->y2;
    const int lw  = lx2 - lx1 + 1;
    const int lh  = ly2 - ly1 + 1;

    // Rotated stripe is lh wide × lw tall in physical coords.
    // Reuse a static temp buffer in PSRAM (sized to max LVGL partial draw area).
    static lv_color_t* rot_buf = NULL;
    static size_t      rot_cap = 0;
    const size_t need = (size_t)lw * (size_t)lh;
    if (need > rot_cap) {
        if (rot_buf) heap_caps_free(rot_buf);
        rot_buf = (lv_color_t*)heap_caps_malloc(need * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        rot_cap = rot_buf ? need : 0;
    }
    if (!rot_buf) {
        lv_display_flush_ready(disp);
        return;
    }

    const lv_color_t* src = (const lv_color_t*)px_map;
    // Source stride = lw pixels (LVGL hands us a tightly packed stripe).
    // Dst stride = lh pixels (rotated stripe width).
    for (int ly = 0; ly < lh; ly++) {
        for (int lx = 0; lx < lw; lx++) {
            const int rx = ly;             // dst x within rotated buf = source y
            const int ry = lw - 1 - lx;    // dst y within rotated buf = (lw-1) - source x
            rot_buf[ry * lh + rx] = src[ly * lw + lx];
        }
    }

    const int px1 = ly1;
    const int py1 = UI_W - 1 - lx2;
    const int px2 = px1 + lh;       // exclusive
    const int py2 = py1 + lw;       // exclusive

    esp_lcd_panel_draw_bitmap(panel, px1, py1, px2, py2, rot_buf);
    lv_display_flush_ready(disp);
}
#endif

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
        data->point.x = points[0].x;
        data->point.y = points[0].y;
#endif
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
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
    rgb_lcd_register_vsync_cb(lcd_panel, lvgl_on_vsync, NULL);

#if PORTRAIT_MODE
    // Portrait: PARTIAL render with small stripe buffers in PSRAM.
    // ~40 lines of UI_W pixels = 600*40*2 = 48 KB per buffer (≈100 KB total),
    // far less than the multi-MB FULL buffers that exhausted internal heap.
    const size_t buf_px = (size_t)UI_W * 40;
    lv_color_t* buf_a = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t* buf_b = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf_a || !buf_b) {
        if (buf_a) heap_caps_free(buf_a);
        if (buf_b) heap_caps_free(buf_b);
        ESP_LOGE(TAG, "Failed to allocate portrait partial draw buffers");
        return;
    }
    display = lv_display_create(UI_W, UI_H);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, disp_flush_portrait_cb);
    lv_display_set_user_data(display, lcd_panel);
    lv_display_set_buffers(display, buf_a, buf_b,
                           buf_px * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
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
#endif
    lv_display_set_default(display);

    // Touch
    touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, touch_read_cb);

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
