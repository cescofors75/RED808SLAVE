// =============================================================================
// gt911_touch.h - GT911 Capacitive Touch driver
// =============================================================================
#pragma once

#include <Arduino.h>
#include "../../include/config.h"

struct TouchPoint {
    uint16_t x;
    uint16_t y;
    bool pressed;
};

void gt911_init();
bool gt911_is_ready();
TouchPoint gt911_read();
