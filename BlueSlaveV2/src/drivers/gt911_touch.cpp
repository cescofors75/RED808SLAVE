// =============================================================================
// gt911_touch.cpp - GT911 Capacitive Touch driver
// =============================================================================
#include "gt911_touch.h"
#include "i2c_driver.h"
#include "io_extension.h"

#define GT911_REG_STATUS   0x814E
#define GT911_REG_POINT1   0x8150

static bool gt911_ok = false;
static uint8_t gt911_addr = GT911_ADDR;

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
    // GT911 address depends on INT pin state during reset:
    // INT LOW  → 0x5D
    // INT HIGH → 0x14
    // Set INT LOW before reset to select 0x5D
    pinMode(GT911_INT_PIN, OUTPUT);
    digitalWrite(GT911_INT_PIN, LOW);
    delay(1);

    // Reset sequence via IO expander
    io_ext_touch_reset();

    // Release INT pin after address latch
    delay(50);
    pinMode(GT911_INT_PIN, INPUT);

    // Wait for GT911 to be ready
    delay(100);

    // Try primary address 0x5D
    if (gt911_probe(0x5D)) {
        gt911_addr = 0x5D;
        gt911_ok = true;
        Serial.println("[GT911] Found at 0x5D");
    } else if (gt911_probe(0x14)) {
        gt911_addr = 0x14;
        gt911_ok = true;
        Serial.println("[GT911] Found at 0x14");
    } else {
        gt911_ok = false;
        Serial.println("[GT911] ERROR: Not found at 0x5D or 0x14!");
        return;
    }

    // Read and verify product ID
    uint8_t id[4] = {0};
    if (gt911_read_reg(0x8140, id, 4)) {
        Serial.printf("[GT911] Product ID: %c%c%c%c\n", id[0], id[1], id[2], id[3]);
    } else {
        Serial.println("[GT911] WARNING: Cannot read product ID");
    }
}

bool gt911_is_ready() {
    return gt911_ok;
}

TouchPoint gt911_read() {
    TouchPoint tp = {0, 0, false};
    if (!gt911_ok) return tp;

    uint8_t status = 0;
    if (!gt911_read_reg(GT911_REG_STATUS, &status, 1)) return tp;

    uint8_t touchCount = status & 0x0F;
    bool bufferReady = (status & 0x80) != 0;

    if (bufferReady && touchCount > 0 && touchCount <= 5) {
        uint8_t data[7];
        gt911_read_reg(GT911_REG_POINT1, data, 7);

        tp.x = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
        tp.y = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
        tp.pressed = true;
    }

    // Clear status
    gt911_write_reg(GT911_REG_STATUS, 0);

    return tp;
}
