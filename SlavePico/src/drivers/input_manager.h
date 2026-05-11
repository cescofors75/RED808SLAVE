#pragma once

#include <stdint.h>

struct InputEvent {
  uint8_t controlId;
  uint8_t elementId;
  int16_t value;
  uint8_t eventType; // 0=delta, 1=button, 2=absolute
};

void input_manager_init();
void input_manager_poll_i2c();
void input_manager_poll_analog();
bool input_manager_pop_event(InputEvent& out);
