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

// SD remote browse commands (P4→S3)
void uart_send_sd_mount(void);
void uart_send_sd_select(uint8_t index);
void uart_send_sd_back(void);
void uart_send_sd_load(uint8_t pad);
void uart_send_sd_load_midi(uint8_t slot);

// Send full pattern step data to S3 as MSG_PATTERN_DATA extended packet
void uart_send_pattern_to_s3(int pattern, const bool steps[16][16]);

// Check if S3 is alive (heartbeat received recently)
bool uart_s3_alive(void);

// UART link statistics (for diagnostics)
struct UartStats {
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t rx_checksum_errors;
    uint32_t rx_framing_errors;
};
extern UartStats uart_stats;

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
    bool s3_wifi_connected;  // S3's own WiFi status (from UART SYS_WIFI_STATE)
};

extern P4State p4;

// =============================================================================
// SD REMOTE BROWSE STATE — populated by S3 via MSG_SD_DATA
// =============================================================================
#define P4_SD_MAX_ENTRIES 64

struct P4SdEntry {
    char name[48];
    bool is_dir;
    bool is_midi;
};

struct P4SdState {
    bool mounted;
    char path[128];
    char selected_file[64];
    int  selected_pad;
    bool selected_is_midi;   // true when selected entry is a .mid file
    // MIDI load result status (for UI feedback):
    //   -2 = idle / no request in flight
    //   -1 = request in flight (waiting for S3 response)
    //    0..N = loaded OK into pattern slot N
    //  0x7F = parse/load failed on S3 (payload == 0xFF)
    int8_t midi_load_result;
    P4SdEntry entries[P4_SD_MAX_ENTRIES];
    int  entry_count;
    bool list_complete;
    volatile bool needs_refresh;
};

extern P4SdState p4sd;
