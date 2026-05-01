// =============================================================================
// uart_bridge.h — S3→P4 UART binary bridge
// Sends state updates to P4 Visual Beast; receives touch commands back.
// =============================================================================
#pragma once

#include <stdint.h>

// Initialize UART1 on GPIO15(RX)/GPIO16(TX) at 921600 baud
void uart_bridge_init(void);

// Send a basic 5-byte packet to P4
void uart_bridge_send(uint8_t type, uint8_t id, uint8_t value);

// Send an extended variable-length packet to P4
void uart_bridge_send_extended(uint8_t type, uint8_t id, const uint8_t* data, uint16_t len);

// Process incoming P4→S3 touch commands. Call from loop().
// Returns number of commands processed.
int uart_bridge_receive(void);

// Called when P4 pushes a full pattern_data packet
// (defined in main.cpp, implemented as external handler)
extern void handleP4PatternData(int pattern, const bool steps[16][16]);

// Send heartbeat to P4 (call periodically, e.g. every 500ms)
void uart_bridge_heartbeat(void);

// UART link statistics (for diagnostics)
struct UartStats {
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t rx_checksum_errors;
    uint32_t rx_framing_errors;   // unexpected start byte
};
extern UartStats uart_stats;

// ---- Convenience senders (call from state-change sites) ----

// System state
void uart_bridge_send_bpm(float bpmPrecise);
void uart_bridge_send_play_state(bool playing);
void uart_bridge_send_pattern(int pattern);
void uart_bridge_send_step(int step);
void uart_bridge_send_volume(int master, int seq, int live);
void uart_bridge_send_wifi_state(bool wifi, bool master);
void uart_bridge_send_theme(int theme);
void uart_bridge_send_screen(int screen);

// Encoders & Pots
void uart_bridge_send_encoder(int lane, uint8_t value);
void uart_bridge_send_encoder_mute(int lane, bool muted);
void uart_bridge_send_pot(int pot, uint8_t midi);
void uart_bridge_send_pot_mute(int pot, bool muted);

// FX extended params
void uart_bridge_send_fx_resonance(int resonanceX10);
void uart_bridge_send_fx_distortion(int percent);

// Tracks
void uart_bridge_send_track_mute(int track, bool muted);
void uart_bridge_send_track_volume(int track, int volume);

// Pad trigger visual feedback
void uart_bridge_send_pad_trigger(int pad, uint8_t velocity);

// Pattern data (extended packet — 32 bytes for 16×16 grid sent to P4)
// Only packs the first STEPS_PER_BANK (16) steps — P4 protocol is fixed at 16.
void uart_bridge_send_pattern_data(int pattern, const bool steps[][64], int numTracks);
void uart_bridge_send_pattern_push(int pattern, const bool steps[][64], int numTracks);

// v2.9 — Melody sync over UART (S3→P4, P4 forwards UDP to master)
void uart_bridge_send_melody_engine(uint8_t engine);  // engine code 3-6
void uart_bridge_send_melody_octave(uint8_t octave);  // 1-7
void uart_bridge_send_melody_rec(bool active);
void uart_bridge_send_melody_clear(void);
void uart_bridge_send_melody_pad(uint8_t pad);         // 0-15

// v2.10 — Piano/synth params over UART (S3→P4, P4 forwards UDP to master)
void uart_bridge_send_synth_param(uint8_t engine, uint8_t instrument, uint8_t paramId, float value);
void uart_bridge_send_synth_preset(uint8_t engine, uint8_t preset);

// v2.9 — Pending melody state received from P4 (forwarded from master's melody_sync)
// Written by uart_bridge_receive (Core 0 UART ISR context); consumed by main loop
// under LVGL lock.
struct PendingMelodyFromP4 {
    uint8_t engine = 3;
    uint8_t octave = 4;
    uint8_t rec    = 0;
    uint8_t pad    = 0;
    volatile bool pending = false;
};
extern PendingMelodyFromP4 g_pending_melody_from_p4;
