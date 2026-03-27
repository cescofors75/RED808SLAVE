// =============================================================================
// i2c_driver.cpp - I2C bus abstraction
// =============================================================================
#include "i2c_driver.h"

void i2c_init() {
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
}

bool i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t data) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(data);
    return Wire.endTransmission() == 0;
}

bool i2c_write_bytes(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(data, len);
    return Wire.endTransmission() == 0;
}

uint8_t i2c_read_byte(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

bool i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(addr, (uint8_t)len);
    for (size_t i = 0; i < len && Wire.available(); i++) {
        data[i] = Wire.read();
    }
    return true;
}

bool i2c_device_present(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void i2c_hub_select(uint8_t channel) {
    Wire.beginTransmission(I2C_HUB_ADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

void i2c_hub_deselect() {
    Wire.beginTransmission(I2C_HUB_ADDR);
    Wire.write(0);
    Wire.endTransmission();
}
