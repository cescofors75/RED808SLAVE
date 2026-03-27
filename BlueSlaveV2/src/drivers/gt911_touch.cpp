// =============================================================================
// gt911_touch.cpp - GT911 Capacitive Touch driver
// Uses global i2c_bus_mutex for thread-safe Wire access
// =============================================================================
#include "gt911_touch.h"
#include "i2c_driver.h"
#include "io_extension.h"

#define GT911_REG_STATUS   0x814E
#define GT911_REG_POINT1   0x8150

static bool gt911_ok = false;
static uint8_t gt911_addr = GT911_ADDR;

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

void gt911_init() {
    Serial.println("[GT911] Starting init sequence...");
    
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
    
    Serial.println("[GT911] Reset sequence complete, probing...");

    // Probe (init runs on Core 0, no concurrency yet)
    if (gt911_probe(0x5D)) {
        gt911_addr = 0x5D;
        gt911_ok = true;
        Serial.println("[GT911] Found at 0x5D");
    } else if (gt911_probe(0x14)) {
        gt911_addr = 0x14;
        gt911_ok = true;
        Serial.println("[GT911] Found at 0x14");
    } else {
        // Retry once with longer delay
        Serial.println("[GT911] Not found, retrying...");
        delay(200);
        if (gt911_probe(0x5D)) {
            gt911_addr = 0x5D;
            gt911_ok = true;
            Serial.println("[GT911] Found at 0x5D (retry)");
        } else if (gt911_probe(0x14)) {
            gt911_addr = 0x14;
            gt911_ok = true;
            Serial.println("[GT911] Found at 0x14 (retry)");
        } else {
            gt911_ok = false;
            Serial.println("[GT911] ERROR: Not found at 0x5D or 0x14!");
            return;
        }
    }

    // Read product ID to confirm communication
    uint8_t id[4] = {0};
    if (gt911_read_reg(0x8140, id, 4)) {
        Serial.printf("[GT911] Product ID: %c%c%c%c\n", id[0], id[1], id[2], id[3]);
    } else {
        Serial.println("[GT911] WARNING: Cannot read product ID");
    }
    
    // Read firmware version
    uint8_t fw[2] = {0};
    if (gt911_read_reg(0x8144, fw, 2)) {
        Serial.printf("[GT911] Firmware: 0x%02X%02X\n", fw[1], fw[0]);
    }
    
    // Read resolution config
    uint8_t res[4] = {0};
    if (gt911_read_reg(0x8146, res, 4)) {
        uint16_t xRes = res[0] | (res[1] << 8);
        uint16_t yRes = res[2] | (res[3] << 8);
        Serial.printf("[GT911] Resolution config: %dx%d\n", xRes, yRes);
    }
    
    Serial.printf("[GT911] Init complete, addr=0x%02X, ok=%d\n", gt911_addr, gt911_ok);
}

bool gt911_is_ready() {
    return gt911_ok;
}

TouchPoint gt911_read() {
    TouchPoint tp = {0, 0, false};
    if (!gt911_ok) return tp;

    // Use global I2C mutex (called from LVGL task on Core 1)
    if (!i2c_lock(10)) return tp;

    uint8_t status = 0;
    if (!gt911_read_reg(GT911_REG_STATUS, &status, 1)) {
        i2c_unlock();
        return tp;
    }

    uint8_t touchCount = status & 0x0F;
    bool bufferReady = (status & 0x80) != 0;

    if (bufferReady && touchCount > 0 && touchCount <= 5) {
        uint8_t data[7];
        if (gt911_read_reg(GT911_REG_POINT1, data, 7)) {
            tp.x = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
            tp.y = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
            tp.pressed = true;
        }
    }

    // Clear status register
    gt911_write_reg(GT911_REG_STATUS, 0);

    i2c_unlock();
    return tp;
}
