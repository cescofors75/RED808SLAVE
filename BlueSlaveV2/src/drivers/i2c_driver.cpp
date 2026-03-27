// =============================================================================
// i2c_driver.cpp - I2C bus abstraction
// Thread-safe: all Wire operations protected by global mutex
// =============================================================================
#include "i2c_driver.h"

// Global I2C mutex - shared across all files that use Wire
SemaphoreHandle_t i2c_bus_mutex = NULL;

bool i2c_lock(int timeout_ms) {
    if (!i2c_bus_mutex) return true; // Before init, allow access
    return xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void i2c_unlock() {
    if (i2c_bus_mutex) xSemaphoreGive(i2c_bus_mutex);
}

void i2c_init() {
    if (!i2c_bus_mutex) {
        i2c_bus_mutex = xSemaphoreCreateMutex();
    }
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
}

bool i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t data) {
    if (!i2c_lock()) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(data);
    bool ok = Wire.endTransmission() == 0;
    i2c_unlock();
    return ok;
}

bool i2c_write_bytes(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
    if (!i2c_lock()) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(data, len);
    bool ok = Wire.endTransmission() == 0;
    i2c_unlock();
    return ok;
}

uint8_t i2c_read_byte(uint8_t addr, uint8_t reg) {
    if (!i2c_lock()) return 0;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, (uint8_t)1);
    uint8_t val = Wire.available() ? Wire.read() : 0;
    i2c_unlock();
    return val;
}

bool i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
    if (!i2c_lock()) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) { i2c_unlock(); return false; }
    Wire.requestFrom(addr, (uint8_t)len);
    for (size_t i = 0; i < len && Wire.available(); i++) {
        data[i] = Wire.read();
    }
    i2c_unlock();
    return true;
}

bool i2c_device_present(uint8_t addr) {
    if (!i2c_lock()) return false;
    Wire.beginTransmission(addr);
    bool ok = Wire.endTransmission() == 0;
    i2c_unlock();
    return ok;
}

void i2c_hub_select(uint8_t channel) {
    if (!i2c_lock()) return;
    Wire.beginTransmission(I2C_HUB_ADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
    i2c_unlock();
}

void i2c_hub_deselect() {
    if (!i2c_lock()) return;
    Wire.beginTransmission(I2C_HUB_ADDR);
    Wire.write(0);
    Wire.endTransmission();
    i2c_unlock();
}
