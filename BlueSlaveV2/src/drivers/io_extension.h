// =============================================================================
// io_extension.h - CH32V003 IO Expander driver
// =============================================================================
#pragma once

#include <Arduino.h>
#include "../../include/config.h"

void io_ext_init();
void io_ext_output(uint8_t pin, uint8_t value);
void io_ext_backlight_on();
void io_ext_backlight_off();
void io_ext_touch_reset();
void io_ext_lcd_reset();
void io_ext_sd_enable();   // Pull SD CS low (assert)
void io_ext_sd_disable();  // Pull SD CS high (deassert)
