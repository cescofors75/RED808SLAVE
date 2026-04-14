// =============================================================================
// uart_bridge.cpp — S3→P4 UART binary bridge implementation
// =============================================================================

#include "uart_bridge.h"
#include "../include/uart_protocol.h"
#include "../include/config.h"
#include <Arduino.h>
#include <HardwareSerial.h>

// UART1 for P4 communication
static HardwareSerial& P4Serial = Serial1;

// TX buffer to coalesce sends
static uint8_t txBuf[UART_BASIC_LEN];

// =============================================================================
// INIT
// =============================================================================
void uart_bridge_init(void) {
    P4Serial.begin(UART_BAUD_RATE, SERIAL_8N1, Config::UART_P4_RX_PIN, Config::UART_P4_TX_PIN);
    P4Serial.setRxBufferSize(512);
    P4Serial.setTxBufferSize(256);
    RED808_LOG_PRINTF("[UART] Bridge init: TX=%d RX=%d @ %d baud\n",
                      Config::UART_P4_TX_PIN, Config::UART_P4_RX_PIN, UART_BAUD_RATE);
}

// =============================================================================
// BASIC SEND (5 bytes)
// =============================================================================
void uart_bridge_send(uint8_t type, uint8_t id, uint8_t value) {
    UartBasicPacket pkt;
    uart_build_basic(&pkt, type, id, value);
    P4Serial.write((const uint8_t*)&pkt, UART_BASIC_LEN);
}

// =============================================================================
// EXTENDED SEND (variable length)
// =============================================================================
void uart_bridge_send_extended(uint8_t type, uint8_t id, const uint8_t* data, uint16_t len) {
    uint8_t hdr[UART_EXT_HEADER_LEN];
    hdr[0] = UART_START_EXTENDED;
    hdr[1] = type;
    hdr[2] = id;
    hdr[3] = (len >> 8) & 0xFF;
    hdr[4] = len & 0xFF;
    P4Serial.write(hdr, UART_EXT_HEADER_LEN);
    if (len > 0 && data) P4Serial.write(data, len);
    // Checksum: sum of all bytes & 0xFF
    uint8_t cs = 0;
    for (int i = 0; i < UART_EXT_HEADER_LEN; i++) cs += hdr[i];
    for (uint16_t i = 0; i < len; i++) cs += data[i];
    P4Serial.write(cs);
}

// =============================================================================
// RECEIVE (P4→S3 touch commands)
// =============================================================================
int uart_bridge_receive(void) {
    int count = 0;
    while (P4Serial.available() >= UART_BASIC_LEN) {
        uint8_t peek = P4Serial.peek();
        if (peek != UART_START_BASIC) {
            P4Serial.read(); // discard
            continue;
        }
        uint8_t buf[UART_BASIC_LEN];
        P4Serial.readBytes(buf, UART_BASIC_LEN);
        UartBasicPacket* pkt = (UartBasicPacket*)buf;
        if (!uart_validate_basic(pkt)) continue;

        // Dispatch P4→S3 touch commands
        if (pkt->type == MSG_TOUCH_CMD) {
            // Forward to external handler (defined in main.cpp)
            extern void handleP4TouchCommand(uint8_t cmdId, uint8_t value);
            handleP4TouchCommand(pkt->id, pkt->value);
            count++;
        }
    }
    return count;
}

// =============================================================================
// HEARTBEAT
// =============================================================================
void uart_bridge_heartbeat(void) {
    uart_bridge_send(MSG_SYSTEM, SYS_HEARTBEAT, 0x01);
}

// =============================================================================
// CONVENIENCE SENDERS
// =============================================================================

void uart_bridge_send_bpm(float bpmPrecise) {
    int bpmInt = (int)bpmPrecise;
    int bpmFrac = (int)((bpmPrecise - bpmInt) * 10.0f + 0.5f);
    uart_bridge_send(MSG_SYSTEM, SYS_BPM_INT, (uint8_t)constrain(bpmInt, 0, 255));
    uart_bridge_send(MSG_SYSTEM, SYS_BPM_FRAC, (uint8_t)constrain(bpmFrac, 0, 9));
}

void uart_bridge_send_play_state(bool playing) {
    uart_bridge_send(MSG_SYSTEM, SYS_PLAY_STATE, playing ? 1 : 0);
}

void uart_bridge_send_pattern(int pattern) {
    uart_bridge_send(MSG_SYSTEM, SYS_PATTERN, (uint8_t)constrain(pattern, 0, 15));
}

void uart_bridge_send_step(int step) {
    uart_bridge_send(MSG_SYSTEM, SYS_STEP, (uint8_t)constrain(step, 0, 15));
}

void uart_bridge_send_volume(int master, int seq, int live) {
    uart_bridge_send(MSG_SYSTEM, SYS_VOLUME, (uint8_t)constrain(master, 0, 150));
    uart_bridge_send(MSG_SYSTEM, SYS_SEQ_VOL, (uint8_t)constrain(seq, 0, 150));
    uart_bridge_send(MSG_SYSTEM, SYS_LIVE_VOL, (uint8_t)constrain(live, 0, 150));
}

void uart_bridge_send_wifi_state(bool wifi, bool master) {
    uart_bridge_send(MSG_SYSTEM, SYS_WIFI_STATE, wifi ? 1 : 0);
    uart_bridge_send(MSG_SYSTEM, SYS_MASTER_CONN, master ? 1 : 0);
}

void uart_bridge_send_theme(int theme) {
    uart_bridge_send(MSG_SYSTEM, SYS_THEME, (uint8_t)constrain(theme, 0, 5));
}

void uart_bridge_send_screen(int screen) {
    uart_bridge_send(MSG_SCREEN, SCR_NAVIGATE, (uint8_t)screen);
}

void uart_bridge_send_encoder(int lane, uint8_t value) {
    uint8_t id = ENC_FLANGER + lane;
    uart_bridge_send(MSG_ENCODER, id, value);
}

void uart_bridge_send_encoder_mute(int lane, bool muted) {
    uart_bridge_send(MSG_FX, FX_ENC0_MUTE + lane, muted ? 1 : 0);
}

void uart_bridge_send_pot(int pot, uint8_t midi) {
    uart_bridge_send(MSG_POT, pot, midi);
}

void uart_bridge_send_pot_mute(int pot, bool muted) {
    uart_bridge_send(MSG_FX, FX_POT0_MUTE + pot, muted ? 1 : 0);
}

void uart_bridge_send_fx_resonance(int resonanceX10) {
    uart_bridge_send(MSG_FX, FX_RESONANCE, (uint8_t)constrain(resonanceX10, 10, 100));
}

void uart_bridge_send_fx_distortion(int percent) {
    uart_bridge_send(MSG_FX, FX_DISTORTION, (uint8_t)constrain(percent, 0, 100));
}

void uart_bridge_send_track_mute(int track, bool muted) {
    uart_bridge_send(MSG_TRACK, TRK_MUTE_BIT | (track & 0x0F), muted ? 1 : 0);
}

void uart_bridge_send_track_volume(int track, int volume) {
    uart_bridge_send(MSG_TRACK, TRK_VOLUME | (track & 0x0F), (uint8_t)constrain(volume, 0, 127));
}

void uart_bridge_send_pad_trigger(int pad, uint8_t velocity) {
    uart_bridge_send(MSG_PAD, (uint8_t)constrain(pad, 0, 15), velocity);
}

void uart_bridge_send_pattern_data(int pattern, const bool steps[][16], int numTracks) {
    // Pack 16 tracks × 16 steps into 32 bytes (1 bit per step, 2 bytes per track)
    uint8_t packed[32];
    memset(packed, 0, sizeof(packed));
    int tracks = constrain(numTracks, 0, 16);
    for (int t = 0; t < tracks; t++) {
        uint16_t bits = 0;
        for (int s = 0; s < 16; s++) {
            if (steps[t][s]) bits |= (1 << s);
        }
        packed[t * 2]     = (bits >> 8) & 0xFF;
        packed[t * 2 + 1] = bits & 0xFF;
    }
    uart_bridge_send_extended(MSG_PATTERN_DATA, (uint8_t)pattern, packed, 32);
}
