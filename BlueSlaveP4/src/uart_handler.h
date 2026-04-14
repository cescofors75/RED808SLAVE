// =============================================================================
// uart_handler.h — UART binary protocol receiver (P4 side)
// Receives state from S3, sends touch commands back
// =============================================================================
#pragma once

#include <stdint.h>
#include "../include/uart_protocol.h"

// Initialize UART1 for S3 communication
void uart_handler_init(void);

// Process pending UART packets — call from main loop.
// Returns number of packets processed.
int uart_handler_process(void);

// Send a basic command to S3 (P4→S3 touch commands)
void uart_send_to_s3(uint8_t type, uint8_t id, uint8_t value);

// Check if S3 is alive (heartbeat received recently)
bool uart_s3_alive(void);

// =============================================================================
// P4 LOCAL STATE — updated by UART handler, read by UI
// =============================================================================
struct P4State {
    // System
    int  bpm_int;           // 40-240
    int  bpm_frac;          // 0-9
    int  current_pattern;   // 0-15
    int  current_step;      // 0-15
    bool is_playing;
    bool wifi_connected;
    bool master_connected;
    int  theme;             // 0-5
    int  master_volume;     // 0-150
    int  seq_volume;        // 0-150
    int  live_volume;       // 0-150

    // Encoders (0-127)
    uint8_t enc_value[3];
    bool    enc_muted[3];

    // Pots (MIDI 0-127 raw)
    uint8_t pot_value[4];
    bool    pot_muted[3];   // P2/P3/P4 mute states

    // FX extended
    int  filter_type;       // 0-4
    int  cutoff_hz;         // 20-20000
    int  resonance_x10;    // 10-100
    int  distortion_pct;   // 0-100
    int  bitcrush_bits;    // 4-16
    int  sample_rate_hz;   // 1000-44100
    int  fx_resp_mode;     // 0-1

    // Tracks
    bool track_muted[16];
    bool track_solo[16];
    int  track_volume[16];  // 0-100

    // Pattern step data (updated via extended packets)
    bool steps[16][16];     // [track][step]

    // Pad triggers (flash feedback)
    uint8_t pad_velocity[16];
    unsigned long pad_flash_until[16];

    // Screen
    int  current_screen;    // Screen enum value

    // Connection
    unsigned long last_heartbeat_ms;
    bool s3_connected;
};

extern P4State p4;
