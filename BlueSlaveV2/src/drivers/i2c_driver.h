// =============================================================================
// i2c_driver.h - I2C bus abstraction
// =============================================================================
#pragma once

#include <Wire.h>
#include <freertos/semphr.h>
#include "config.h"

// Global I2C mutex - MUST be used by ALL Wire operations across all cores
extern SemaphoreHandle_t i2c_bus_mutex;
bool i2c_lock(int timeout_ms = 50);
void i2c_unlock();

void i2c_init();
bool i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t data);
bool i2c_write_bytes(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len);
uint8_t i2c_read_byte(uint8_t addr, uint8_t reg);
bool i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t* data, size_t len);
bool i2c_device_present(uint8_t addr);
void i2c_hub_select(uint8_t channel);
void i2c_hub_deselect();
// Raw versions - caller must hold i2c_lock
void i2c_hub_select_raw(uint8_t channel);
void i2c_hub_deselect_raw();
