#pragma once

#include <Arduino.h>

bool i2c_driver_init();
bool i2c_driver_is_ready();
bool i2c_driver_reinit();
bool i2c_lock(uint32_t timeoutMs = 50);
void i2c_unlock();
bool i2c_hub_select(uint8_t channel);
void i2c_hub_deselect();
