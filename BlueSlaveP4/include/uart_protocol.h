// =============================================================================
// uart_protocol.h — Binary UART protocol S3 ↔ P4
// Shared between BlueSlaveV2 (S3) and BlueSlaveP4 (P4)
// =============================================================================
#pragma once

#include <stdint.h>

// =============================================================================
// TRANSPORT
// =============================================================================
#define UART_BAUD_RATE      921600   // High speed for low latency
#define UART_BASIC_LEN      5       // Fixed-length basic packet
#define UART_START_BASIC    0xAA    // Basic 5-byte packet marker
#define UART_START_EXTENDED 0xAB    // Variable-length packet marker
#define UART_EXT_HEADER_LEN 5      // Header before payload

// =============================================================================
// MESSAGE TYPES (Byte 1)
// =============================================================================
#define MSG_ENCODER     0x01    // Rotary encoder change
#define MSG_PAD         0x02    // Pad trigger / button press
#define MSG_POT         0x03    // Analog potentiometer
#define MSG_SYSTEM      0x04    // System state (BPM, pattern, play)
#define MSG_FX          0x05    // FX state / mute
#define MSG_TRACK       0x06    // Per-track info (mute, volume)
#define MSG_SCREEN      0x07    // Screen navigation
#define MSG_TOUCH_CMD   0x08    // P4→S3: touch-initiated command
#define MSG_DIAG        0x09    // RESERVED (unused)
#define MSG_PATTERN_DATA 0x0A   // Extended: pattern step data
#define MSG_SD_DATA     0x0B    // S3→P4: SD card data (extended packets)

// =============================================================================
// ENCODER IDs (MSG_ENCODER, Byte 2)
// =============================================================================
#define ENC_FLANGER     0x00    // DFRobot encoder #0 — Flanger mix 0-127
#define ENC_DELAY       0x01    // DFRobot encoder #1 — Delay mix 0-127
#define ENC_REVERB      0x02    // DFRobot encoder #2 — Reverb mix 0-127
#define ENC_BPM         0x03    // DFRobot encoder #3 — BPM (delta)

// =============================================================================
// POT IDs (MSG_POT, Byte 2)
// =============================================================================
#define POT_VOLUME      0x00    // P1: Master volume (MIDI 0-127)
#define POT_UNUSED      0x01    // P2: (disabled — cutoff removed)
#define POT_RESONANCE   0x02    // P3: Filter resonance (MIDI 0-127)
#define POT_DRIVE       0x03    // P4: Distortion drive (MIDI 0-127)

// =============================================================================
// SYSTEM IDs (MSG_SYSTEM, Byte 2)
// =============================================================================
#define SYS_BPM_INT     0x00    // Value = BPM integer part (40-240)
#define SYS_BPM_FRAC    0x01    // Value = BPM fractional ×10 (0-9)
#define SYS_PATTERN     0x02    // Value = current pattern index (0-15)
#define SYS_PLAY_STATE  0x03    // Value = 0=stop, 1=play
#define SYS_STEP        0x04    // Value = current step (0-15)
#define SYS_WIFI_STATE  0x05    // Value = 0=disconnected, 1=connected
#define SYS_MASTER_CONN 0x06    // Value = 0=no master, 1=master ok
#define SYS_THEME       0x07    // Value = theme index (0-5)
#define SYS_VOLUME      0x08    // Value = master volume (0-150)
#define SYS_SEQ_VOL     0x09    // Value = sequencer volume (0-150)
#define SYS_LIVE_VOL    0x0A    // Value = live pads volume (0-150)
#define SYS_HEARTBEAT   0x0F    // Keepalive (value = uptime seconds & 0xFF)

// =============================================================================
// FX IDs (MSG_FX, Byte 2)
// =============================================================================
#define FX_ENC0_MUTE    0x00    // Encoder 0 (Flanger) mute: value 0/1
#define FX_ENC1_MUTE    0x01    // Encoder 1 (Delay) mute:  value 0/1
#define FX_ENC2_MUTE    0x02    // Encoder 2 (Reverb) mute:  value 0/1
#define FX_POT0_MUTE    0x03    // Pot P2 mute: value 0/1
#define FX_POT1_MUTE    0x04    // Pot P3 (Res) mute: value 0/1
#define FX_POT2_MUTE    0x05    // Pot P4 (Drive) mute: value 0/1
#define FX_FILTER_TYPE  0x06    // Filter type (0=Off,1=LP,2=HP,3=BP,4=Notch)
#define FX_CUTOFF_H     0x07    // Cutoff Hz high byte
#define FX_CUTOFF_L     0x08    // Cutoff Hz low byte
#define FX_RESONANCE    0x09    // Resonance ×10 (10-100)
#define FX_DISTORTION   0x0A    // Distortion % (0-100)
#define FX_BITCRUSH     0x0B    // BitCrush bits (4,8,12,16)
#define FX_SAMPLERATE_H 0x0C    // SampleRate Hz high byte
#define FX_SAMPLERATE_L 0x0D    // SampleRate Hz low byte
#define FX_RESP_MODE    0x0E    // FX response mode (0=precision,1=live)

// =============================================================================
// TRACK IDs (MSG_TRACK, Byte 2)
// Sub-command in high nibble, track in low nibble
// =============================================================================
#define TRK_MUTE_BIT    0x00    // 0x0T: track T mute — value 0/1
#define TRK_SOLO_BIT    0x10    // 0x1T: track T solo — value 0/1
#define TRK_VOLUME      0x20    // 0x2T: track T volume — value 0-100

// =============================================================================
// SCREEN IDs (MSG_SCREEN, Byte 2)
// =============================================================================
#define SCR_NAVIGATE     0x00   // Value = target Screen enum value

// =============================================================================
// PAD IDs (MSG_PAD, Byte 2 = pad 0-15, Byte 3 = velocity 0-127)
// =============================================================================
// Byte 2 = pad index (0-15), Byte 3 = value (velocity or 0=release)

// =============================================================================
// TOUCH COMMAND IDs — P4→S3 (MSG_TOUCH_CMD, Byte 2)
// =============================================================================
#define TCMD_PAD_TAP       0x00 // Byte 3 = pad index (velocity fixed 127)
#define TCMD_PLAY_TOGGLE   0x01 // Byte 3 = unused
#define TCMD_PATTERN_SEL   0x02 // Byte 3 = pattern index
#define TCMD_FX_TOGGLE     0x03 // RESERVED (unused)
#define TCMD_THEME_NEXT    0x04 // Byte 3 = unused
#define TCMD_SCREEN_NAV    0x05 // Byte 3 = screen enum
#define TCMD_STEP_TOGGLE   0x06 // S3→P4: toggle step. id=TCMD_STEP_TOGGLE, value=(track<<4)|step
#define TCMD_SD_MOUNT      0x07 // P4→S3: mount SD + list root dir
#define TCMD_SD_SELECT     0x08 // P4→S3: select entry N (0-based index)
#define TCMD_SD_BACK       0x09 // P4→S3: go to parent directory
#define TCMD_SD_LOAD       0x0A // P4→S3: load selected file to pad N
#define TCMD_SYNC_PADS     0x0B  // Bidirectional: toggle pad LED sync mode
// =============================================================================
// SD DATA sub-IDs (MSG_SD_DATA extended, Byte 2)
// =============================================================================
#define SD_RESP_STATUS     0x00 // payload: [mounted(1)]
#define SD_RESP_ENTRY      0x01 // payload: [index(1), type(1='D'/'F'), name...]
#define SD_RESP_LIST_END   0x02 // payload: [total_count(1)]
#define SD_RESP_PATH       0x03 // payload: [path string...]
#define SD_RESP_SELECTED   0x04 // payload: [filename string...]
#define SD_RESP_LOAD_OK    0x05 // payload: [pad(1)]

// =============================================================================
// PACKET STRUCTURES
// =============================================================================

// Basic 5-byte packet (vast majority of messages)
typedef struct __attribute__((packed)) {
    uint8_t start;      // UART_START_BASIC (0xAA)
    uint8_t type;       // MSG_*
    uint8_t id;         // sub-ID (encoder/pot/system/fx/track/pad index)
    uint8_t value;      // 0-255
    uint8_t checksum;   // (start + type + id + value) & 0xFF
} UartBasicPacket;

// Extended variable-length packet header (for pattern data, bulk state)
typedef struct __attribute__((packed)) {
    uint8_t start;      // UART_START_EXTENDED (0xAB)
    uint8_t type;       // MSG_*
    uint8_t id;         // sub-ID
    uint8_t len_h;      // payload length high byte
    uint8_t len_l;      // payload length low byte
    // Followed by: [payload bytes ...] [checksum: sum of all bytes & 0xFF]
} UartExtendedHeader;

// =============================================================================
// HELPER FUNCTIONS (inline)
// =============================================================================

static inline uint8_t uart_checksum_basic(uint8_t start, uint8_t type, uint8_t id, uint8_t value) {
    return (uint8_t)((start + type + id + value) & 0xFF);
}

static inline void uart_build_basic(UartBasicPacket* pkt, uint8_t type, uint8_t id, uint8_t value) {
    pkt->start = UART_START_BASIC;
    pkt->type = type;
    pkt->id = id;
    pkt->value = value;
    pkt->checksum = uart_checksum_basic(UART_START_BASIC, type, id, value);
}

static inline bool uart_validate_basic(const UartBasicPacket* pkt) {
    return (pkt->start == UART_START_BASIC) &&
           (pkt->checksum == uart_checksum_basic(pkt->start, pkt->type, pkt->id, pkt->value));
}
