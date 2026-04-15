// =============================================================================
// udp_handler.h — WiFi/UDP communication to Master via ESP32-C6
// P4 connects directly to Master AP (RED808) through SDIO ESP-Hosted
// =============================================================================
#pragma once

#include <stdint.h>

// Initialize WiFi + UDP
void udp_handler_init(void);

// Process WiFi state + incoming UDP packets — call from main loop
void udp_handler_process(void);

// Connection state
bool udp_wifi_connected(void);
bool udp_master_connected(void);

// =============================================================================
// Commands to Master (P4 → Master)
// =============================================================================
void udp_send_trigger(uint8_t pad, uint8_t velocity);
void udp_send_start(void);
void udp_send_stop(void);
void udp_send_tempo(float bpm);
void udp_send_select_pattern(int index);
void udp_send_get_pattern(int pattern);
void udp_send_set_step(int track, int step, bool active);
void udp_send_mute(int track, bool muted);
void udp_send_set_volume(int value);
void udp_send_set_seq_volume(int value);
void udp_send_set_live_volume(int value);
void udp_send_set_track_volume(int track, int volume);

// FX commands — legacy filter
void udp_send_set_filter(int type);
void udp_send_set_filter_cutoff(int hz);
void udp_send_set_filter_resonance(float val);
void udp_send_set_distortion(float val);

// FX live commands (encoder/pot → Master)
// id: 0=Flanger, 1=Reverb, 2=Phaser
void udp_send_fx_enc(int enc_id, uint8_t value, bool muted);
// pot FX: 0=Distortion, 1=Cutoff, 2=Resonance
void udp_send_fx_pot(int pot_id, uint8_t value, bool muted);

// Request full sync from Master
void udp_request_master_sync(void);
