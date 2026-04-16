// =============================================================================
// uart_protocol.h — Binary UART protocol S3 ↔ P4
// COPY of BlueSlaveP4/include/uart_protocol.h — keep in sync!
// =============================================================================
#pragma once

#include <stdint.h>

// =============================================================================
// TRANSPORT
// =============================================================================
#define UART_BAUD_RATE      921600
#define UART_BASIC_LEN      5
#define UART_START_BASIC    0xAA
#define UART_START_EXTENDED 0xAB
#define UART_EXT_HEADER_LEN 5

// =============================================================================
// MESSAGE TYPES (Byte 1)
// =============================================================================
#define MSG_ENCODER     0x01
#define MSG_PAD         0x02
#define MSG_POT         0x03
#define MSG_SYSTEM      0x04
#define MSG_FX          0x05
#define MSG_TRACK       0x06
#define MSG_SCREEN      0x07
#define MSG_TOUCH_CMD   0x08
#define MSG_DIAG        0x09    // RESERVED (unused)
#define MSG_PATTERN_DATA 0x0A
#define MSG_SD_DATA     0x0B    // S3→P4: SD card data (extended packets)

// ENCODER IDs
#define ENC_FLANGER     0x00
#define ENC_DELAY       0x01
#define ENC_REVERB      0x02
#define ENC_BPM         0x03

// POT IDs
#define POT_VOLUME      0x00
#define POT_UNUSED      0x01
#define POT_RESONANCE   0x02
#define POT_DRIVE       0x03

// SYSTEM IDs
#define SYS_BPM_INT     0x00
#define SYS_BPM_FRAC    0x01
#define SYS_PATTERN     0x02
#define SYS_PLAY_STATE  0x03
#define SYS_STEP        0x04
#define SYS_WIFI_STATE  0x05
#define SYS_MASTER_CONN 0x06
#define SYS_THEME       0x07
#define SYS_VOLUME      0x08
#define SYS_SEQ_VOL     0x09
#define SYS_LIVE_VOL    0x0A
#define SYS_HEARTBEAT   0x0F

// FX IDs
#define FX_ENC0_MUTE    0x00
#define FX_ENC1_MUTE    0x01
#define FX_ENC2_MUTE    0x02
#define FX_POT0_MUTE    0x03
#define FX_POT1_MUTE    0x04
#define FX_POT2_MUTE    0x05
#define FX_FILTER_TYPE  0x06
#define FX_CUTOFF_H     0x07
#define FX_CUTOFF_L     0x08
#define FX_RESONANCE    0x09
#define FX_DISTORTION   0x0A
#define FX_BITCRUSH     0x0B
#define FX_SAMPLERATE_H 0x0C
#define FX_SAMPLERATE_L 0x0D
#define FX_RESP_MODE    0x0E

// TRACK IDs
#define TRK_MUTE_BIT    0x00
#define TRK_SOLO_BIT    0x10
#define TRK_VOLUME      0x20

// SCREEN IDs
#define SCR_NAVIGATE     0x00

// TOUCH CMD IDs (P4→S3)
#define TCMD_PAD_TAP       0x00
#define TCMD_PLAY_TOGGLE   0x01
#define TCMD_PATTERN_SEL   0x02
#define TCMD_FX_TOGGLE     0x03    // RESERVED (unused)
#define TCMD_THEME_NEXT    0x04
#define TCMD_SCREEN_NAV    0x05
#define TCMD_STEP_TOGGLE   0x06  // S3→P4: toggle step. value=(track<<4)|step
#define TCMD_SD_MOUNT      0x07  // P4→S3: mount SD + list root dir
#define TCMD_SD_SELECT     0x08  // P4→S3: select entry N (0-based index)
#define TCMD_SD_BACK       0x09  // P4→S3: go to parent directory
#define TCMD_SD_LOAD       0x0A  // P4→S3: load selected file to pad N
#define TCMD_SYNC_PADS     0x0B  // Bidirectional: toggle pad LED sync mode
#define TCMD_SD_LOAD_MIDI  0x0C  // P4→S3: load selected MIDI file into pattern slot N (value=slot 6-15)

// SD DATA sub-IDs (MSG_SD_DATA extended, Byte 2)
#define SD_RESP_STATUS     0x00
#define SD_RESP_ENTRY      0x01
#define SD_RESP_LIST_END   0x02
#define SD_RESP_PATH       0x03
#define SD_RESP_SELECTED   0x04
#define SD_RESP_LOAD_OK    0x05

// =============================================================================
// PACKET STRUCTURES
// =============================================================================
typedef struct __attribute__((packed)) {
    uint8_t start;
    uint8_t type;
    uint8_t id;
    uint8_t value;
    uint8_t checksum;
} UartBasicPacket;

typedef struct __attribute__((packed)) {
    uint8_t start;
    uint8_t type;
    uint8_t id;
    uint8_t len_h;
    uint8_t len_l;
} UartExtendedHeader;

// =============================================================================
// HELPERS
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
