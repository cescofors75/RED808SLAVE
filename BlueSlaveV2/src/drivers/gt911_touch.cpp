// =============================================================================
// gt911_touch.cpp - GT911 Capacitive Touch driver
// Uses global i2c_bus_mutex for thread-safe Wire access
// =============================================================================
#include "gt911_touch.h"
#include "i2c_driver.h"
#include "io_extension.h"

#include "freertos/FreeRTOS.h"

#define GT911_REG_STATUS   0x814E
#define GT911_REG_POINT1   0x8150

static bool gt911_ok = false;
static uint8_t gt911_addr = GT911_ADDR;
static portMUX_TYPE gt911_cache_mux = portMUX_INITIALIZER_UNLOCKED;
static TouchPoint gt911_cached_points[Config::TOUCH_MAX_POINTS] = {};
static uint8_t gt911_cached_count = 0;

static int32_t remap_touch_axis(int32_t raw, int32_t raw_min, int32_t raw_max, int32_t screen_max) {
    if (raw_max <= raw_min || screen_max <= 0) {
        return constrain(raw, 0, screen_max);
    }

    raw = constrain(raw, raw_min, raw_max);
    return ((raw - raw_min) * screen_max) / (raw_max - raw_min);
}

// All GT911 Wire operations use the global i2c_bus_mutex via i2c_lock/i2c_unlock

static bool gt911_write_reg(uint16_t reg, uint8_t data) {
    Wire.beginTransmission(gt911_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(data);
    return Wire.endTransmission() == 0;
}

static bool gt911_read_reg(uint16_t reg, uint8_t* data, size_t len) {
    Wire.beginTransmission(gt911_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    size_t got = Wire.requestFrom(gt911_addr, (uint8_t)len);
    for (size_t i = 0; i < len && Wire.available(); i++) {
        data[i] = Wire.read();
    }
    return got == len;
}

static bool gt911_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static void gt911_update_cache(const TouchPoint* points, uint8_t count) {
    taskENTER_CRITICAL(&gt911_cache_mux);
    gt911_cached_count = (count > Config::TOUCH_MAX_POINTS) ? Config::TOUCH_MAX_POINTS : count;
    for (uint8_t i = 0; i < gt911_cached_count; i++) {
        gt911_cached_points[i] = points[i];
    }
    for (uint8_t i = gt911_cached_count; i < Config::TOUCH_MAX_POINTS; i++) {
        gt911_cached_points[i] = {0, 0, false};
    }
    taskEXIT_CRITICAL(&gt911_cache_mux);
}

static bool gt911_map_point(uint16_t raw_x, uint16_t raw_y, TouchPoint* out_point) {
    int32_t x = raw_x;
    int32_t y = raw_y;
    int32_t raw_min_x = Config::TOUCH_RAW_MIN_X;
    int32_t raw_max_x = Config::TOUCH_RAW_MAX_X;
    int32_t raw_min_y = Config::TOUCH_RAW_MIN_Y;
    int32_t raw_max_y = Config::TOUCH_RAW_MAX_Y;

    if (Config::TOUCH_SWAP_XY) {
        int32_t tmp = x;
        x = y;
        y = tmp;

        tmp = raw_min_x;
        raw_min_x = raw_min_y;
        raw_min_y = tmp;

        tmp = raw_max_x;
        raw_max_x = raw_max_y;
        raw_max_y = tmp;
    }

    x = remap_touch_axis(x, raw_min_x, raw_max_x, SCREEN_WIDTH - 1);
    y = remap_touch_axis(y, raw_min_y, raw_max_y, SCREEN_HEIGHT - 1);

    if (Config::TOUCH_INVERT_X) x = (SCREEN_WIDTH - 1) - x;
    if (Config::TOUCH_INVERT_Y) y = (SCREEN_HEIGHT - 1) - y;

    x = (x * Config::TOUCH_X_SCALE_PCT) / 100 + Config::TOUCH_X_OFFSET;
    y = (y * Config::TOUCH_Y_SCALE_PCT) / 100 + Config::TOUCH_Y_OFFSET;

    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return false;
    }

    out_point->x = (uint16_t)x;
    out_point->y = (uint16_t)y;
    out_point->pressed = true;
    return true;
}

void gt911_init() {
    RED808_LOG_PRINTLN("[GT911] Starting init sequence...");
    
    // GT911 address selection via INT pin during reset:
    // INT LOW during reset rising edge -> 0x5D
    // INT HIGH during reset rising edge -> 0x14
    
    // Step 1: Set INT LOW to select address 0x5D
    pinMode(GT911_INT_PIN, OUTPUT);
    digitalWrite(GT911_INT_PIN, LOW);
    delay(1);
    
    // Step 2: Assert reset LOW via IO expander
    io_ext_output(EXIO_TP_RST, 0);
    delay(20);  // Hold reset low ≥10ms
    
    // Step 3: Release reset (HIGH) - INT is still LOW = address 0x5D latched
    io_ext_output(EXIO_TP_RST, 1);
    delay(10);  // Wait ≥5ms after reset release with INT still LOW
    
    // Step 4: Release INT pin to input (high-Z)
    pinMode(GT911_INT_PIN, INPUT);
    
    // Step 5: Wait for GT911 to boot up
    delay(100);
    
    RED808_LOG_PRINTLN("[GT911] Reset sequence complete, probing...");

    // Probe (init runs on Core 0, no concurrency yet)
    if (gt911_probe(0x5D)) {
        gt911_addr = 0x5D;
        gt911_ok = true;
        RED808_LOG_PRINTLN("[GT911] Found at 0x5D");
    } else if (gt911_probe(0x14)) {
        gt911_addr = 0x14;
        gt911_ok = true;
        RED808_LOG_PRINTLN("[GT911] Found at 0x14");
    } else {
        // Retry once with longer delay
        RED808_LOG_PRINTLN("[GT911] Not found, retrying...");
        delay(200);
        if (gt911_probe(0x5D)) {
            gt911_addr = 0x5D;
            gt911_ok = true;
            RED808_LOG_PRINTLN("[GT911] Found at 0x5D (retry)");
        } else if (gt911_probe(0x14)) {
            gt911_addr = 0x14;
            gt911_ok = true;
            RED808_LOG_PRINTLN("[GT911] Found at 0x14 (retry)");
        } else {
            gt911_ok = false;
            RED808_LOG_PRINTLN("[GT911] ERROR: Not found at 0x5D or 0x14!");
            return;
        }
    }

    // Read product ID to confirm communication
    uint8_t id[4] = {0};
    if (gt911_read_reg(0x8140, id, 4)) {
        RED808_LOG_PRINTF("[GT911] Product ID: %c%c%c%c\n", id[0], id[1], id[2], id[3]);
    } else {
        RED808_LOG_PRINTLN("[GT911] WARNING: Cannot read product ID");
    }
    
    // Read firmware version
    uint8_t fw[2] = {0};
    if (gt911_read_reg(0x8144, fw, 2)) {
        RED808_LOG_PRINTF("[GT911] Firmware: 0x%02X%02X\n", fw[1], fw[0]);
    }
    
    // Read resolution config
    uint8_t res[4] = {0};
    if (gt911_read_reg(0x8146, res, 4)) {
        uint16_t xRes = res[0] | (res[1] << 8);
        uint16_t yRes = res[2] | (res[3] << 8);
        RED808_LOG_PRINTF("[GT911] Resolution config: %dx%d\n", xRes, yRes);
    }
    
    RED808_LOG_PRINTF("[GT911] Init complete, addr=0x%02X, ok=%d\n", gt911_addr, gt911_ok);
}

bool gt911_is_ready() {
    return gt911_ok;
}

TouchPoint gt911_read() {
    TouchPoint tp = {0, 0, false};
    TouchPoint frame_points[Config::TOUCH_MAX_POINTS] = {};
    uint8_t valid_points = 0;
    if (!gt911_ok) return tp;

    // Use global I2C mutex (called from LVGL task on Core 1)
    // Timeout must be short to avoid deadlock with Core 0 encoder reads
    if (!i2c_lock(15)) return tp;

    uint8_t status = 0;
    if (!gt911_read_reg(GT911_REG_STATUS, &status, 1)) {
        i2c_unlock();
        return tp;
    }

    uint8_t touchCount = status & 0x0F;
    bool bufferReady = (status & 0x80) != 0;

    // No new scan from GT911 yet — keep cache with previous valid data.
    // Clearing cache here would cause phantom "release" events between GT911
    // internal samples (~100Hz), making debounce fire on every gap.
    if (!bufferReady) {
        i2c_unlock();
        return tp;
    }

    static bool prev_valid = false;
    static int32_t prev_x = 0;
    static int32_t prev_y = 0;

    if (touchCount > 0 && touchCount <= Config::TOUCH_MAX_POINTS) {
        uint8_t data[Config::TOUCH_MAX_POINTS * 8] = {};
        uint8_t read_len = touchCount * 8;
        if (gt911_read_reg(GT911_REG_POINT1, data, read_len)) {
            for (uint8_t point_idx = 0; point_idx < touchCount; point_idx++) {
                uint8_t base = point_idx * 8;
                uint16_t raw_x = (uint16_t)data[base + 0] | ((uint16_t)data[base + 1] << 8);
                uint16_t raw_y = (uint16_t)data[base + 2] | ((uint16_t)data[base + 3] << 8);
                TouchPoint mapped = {0, 0, false};
                if (!gt911_map_point(raw_x, raw_y, &mapped)) {
                    continue;
                }

                if (valid_points == 0) {
                    int32_t x = mapped.x;
                    int32_t y = mapped.y;
                    if (prev_valid) {
                        // Jitter filter only — 220px jump filter removed because
                        // LIVE pads are ~250px wide, cross-pad taps were rejected.
                        if (abs((int)(x - prev_x)) <= Config::TOUCH_JITTER_PX) x = prev_x;
                        if (abs((int)(y - prev_y)) <= Config::TOUCH_JITTER_PX) y = prev_y;
                    }
                    mapped.x = (uint16_t)x;
                    mapped.y = (uint16_t)y;
                    prev_x = x;
                    prev_y = y;
                    prev_valid = true;
                    tp = mapped;
                }

                frame_points[valid_points++] = mapped;
                if (valid_points >= Config::TOUCH_MAX_POINTS) {
                    break;
                }
            }
        }
    } else {
        prev_valid = false;
    }

    if (valid_points == 0) {
        prev_valid = false;
    }

    gt911_update_cache(frame_points, valid_points);

    // Clear status register
    gt911_write_reg(GT911_REG_STATUS, 0);

    i2c_unlock();
    return tp;
}

void gt911_poll() {
    if (!gt911_ok) return;
    (void)gt911_read();
}

uint8_t gt911_get_points(TouchPoint* points, uint8_t maxPoints) {
    if (!points || maxPoints == 0) return 0;

    uint8_t count = 0;
    taskENTER_CRITICAL(&gt911_cache_mux);
    count = gt911_cached_count;
    if (count > maxPoints) count = maxPoints;
    for (uint8_t i = 0; i < count; i++) {
        points[i] = gt911_cached_points[i];
    }
    taskEXIT_CRITICAL(&gt911_cache_mux);
    return count;
}
