#pragma once

#include "drivers/input_manager.h"

void udp_handler_init();
void udp_handler_process();
void udp_send_event(const InputEvent& ev);
void udp_send_heartbeat();
bool udp_is_ready();
