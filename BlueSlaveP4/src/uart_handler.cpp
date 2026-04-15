// =============================================================================
// uart_handler.cpp — UART binary protocol receiver (P4 side)
// =============================================================================

#include "uart_handler.h"
#include "udp_handler.h"
#include "../include/config.h"
#include <Arduino.h>

#if P4_USB_CDC_ENABLED
#include "usb_cdc_handler.h"
#endif

// P4 local state — single source of truth for UI rendering
P4State p4 = {};

// UART instance
static HardwareSerial UartS3(UART_S3_PORT);

// Circular receive buffer
static uint8_t rxBuf[UART_RX_BUF];
static int rxHead = 0;

// =============================================================================
// INIT
// =============================================================================
void uart_handler_init(void) {
    UartS3.begin(UART_BAUD_RATE, SERIAL_8N1, UART_S3_RX_PIN, UART_S3_TX_PIN);
    UartS3.setRxBufferSize(UART_RX_BUF);

    // Set defaults
    p4.bpm_int = Config::DEFAULT_BPM;
    p4.master_volume = 75;
    p4.seq_volume = 75;
    p4.live_volume = 75;
    p4.cutoff_hz = 20000;
    p4.resonance_x10 = 10;
    p4.bitcrush_bits = 16;
    p4.sample_rate_hz = 44100;
    for (int i = 0; i < 16; i++) p4.track_volume[i] = 75;

    P4_LOG_PRINTF("[UART] Init port %d: TX=%d RX=%d @ %d baud\n",
                  UART_S3_PORT, UART_S3_TX_PIN, UART_S3_RX_PIN, UART_BAUD_RATE);
}

// =============================================================================
// SEND TO S3
// =============================================================================
void uart_send_to_s3(uint8_t type, uint8_t id, uint8_t value) {
    UartBasicPacket pkt;
    uart_build_basic(&pkt, type, id, value);

#if P4_USB_CDC_ENABLED
    // Prefer USB if connected, fall back to UART
    if (usb_cdc_connected()) {
        usb_cdc_write((uint8_t*)&pkt, sizeof(pkt));
    } else {
        UartS3.write((uint8_t*)&pkt, sizeof(pkt));
    }
#else
    UartS3.write((uint8_t*)&pkt, sizeof(pkt));
#endif
}

bool uart_s3_alive(void) {
    return p4.s3_connected;
}

// =============================================================================
// PROCESS BASIC PACKET
// =============================================================================
static void process_basic(const UartBasicPacket* pkt) {
    uint8_t type = pkt->type;
    uint8_t id   = pkt->id;
    uint8_t val  = pkt->value;

    switch (type) {
        case MSG_ENCODER:
            if (id < 3) p4.enc_value[id] = val;
            break;

        case MSG_PAD:
            if (id < 16) {
                p4.pad_velocity[id] = val;
                p4.pad_flash_until[id] = millis() + 120;
                // Relay S3 pad triggers to Master when S3 WiFi is down
                if (!p4.s3_wifi_connected && udp_wifi_connected()) {
                    udp_send_trigger(id, val);
                }
            }
            break;

        case MSG_POT:
            if (id < 4) p4.pot_value[id] = val;
            break;

        case MSG_SYSTEM:
            switch (id) {
                case SYS_BPM_INT:
                    p4.bpm_int = val;
                    break;
                case SYS_BPM_FRAC:
                    p4.bpm_frac = val;
                    // Relay BPM to Master (frac arrives after int)
                    if (udp_wifi_connected()) {
                        float bpm = p4.bpm_int + p4.bpm_frac * 0.1f;
                        udp_send_tempo(bpm);
                    }
                    break;
                case SYS_PATTERN:
                    p4.current_pattern = val;
                    // Relay pattern selection to Master
                    if (udp_wifi_connected()) udp_send_select_pattern(val);
                    break;
                case SYS_PLAY_STATE:
                    p4.is_playing = (val != 0);
                    // Relay play/stop to Master
                    if (udp_wifi_connected()) {
                        if (p4.is_playing) udp_send_start();
                        else               udp_send_stop();
                    }
                    break;
                case SYS_STEP:        p4.current_step = val;               break;
                case SYS_WIFI_STATE:  p4.s3_wifi_connected = (val != 0);   break;
                case SYS_MASTER_CONN: p4.master_connected = (val != 0);    break;
                case SYS_THEME:       p4.theme = val;                      break;
                case SYS_VOLUME:
                    p4.master_volume = val;
                    if (udp_wifi_connected()) udp_send_set_volume(val);
                    break;
                case SYS_SEQ_VOL:
                    p4.seq_volume = val;
                    if (udp_wifi_connected()) udp_send_set_seq_volume(val);
                    break;
                case SYS_LIVE_VOL:
                    p4.live_volume = val;
                    if (udp_wifi_connected()) udp_send_set_live_volume(val);
                    break;
                case SYS_HEARTBEAT:
                    p4.last_heartbeat_ms = millis();
                    p4.s3_connected = true;
                    break;
            }
            break;

        case MSG_FX:
            switch (id) {
                case FX_ENC0_MUTE:    p4.enc_muted[0] = (val != 0);       break;
                case FX_ENC1_MUTE:    p4.enc_muted[1] = (val != 0);       break;
                case FX_ENC2_MUTE:    p4.enc_muted[2] = (val != 0);       break;
                case FX_POT0_MUTE:    p4.pot_muted[0] = (val != 0);       break;
                case FX_POT1_MUTE:    p4.pot_muted[1] = (val != 0);       break;
                case FX_POT2_MUTE:    p4.pot_muted[2] = (val != 0);       break;
                case FX_FILTER_TYPE:  p4.filter_type = val;                break;
                case FX_CUTOFF_H:     p4.cutoff_hz = (p4.cutoff_hz & 0xFF) | (val << 8); break;
                case FX_CUTOFF_L:     p4.cutoff_hz = (p4.cutoff_hz & 0xFF00) | val;      break;
                case FX_RESONANCE:    p4.resonance_x10 = val;             break;
                case FX_DISTORTION:   p4.distortion_pct = val;            break;
                case FX_BITCRUSH:     p4.bitcrush_bits = val;             break;
                case FX_SAMPLERATE_H: p4.sample_rate_hz = (p4.sample_rate_hz & 0xFF) | (val << 8); break;
                case FX_SAMPLERATE_L: p4.sample_rate_hz = (p4.sample_rate_hz & 0xFF00) | val;      break;
                case FX_RESP_MODE:    p4.fx_resp_mode = val;              break;
            }
            break;

        case MSG_TRACK: {
            uint8_t sub = id & 0xF0;
            uint8_t trk = id & 0x0F;
            if (trk < 16) {
                switch (sub) {
                    case TRK_MUTE_BIT:
                        p4.track_muted[trk] = (val != 0);
                        if (udp_wifi_connected()) udp_send_mute(trk, val != 0);
                        break;
                    case TRK_SOLO_BIT:
                        p4.track_solo[trk] = (val != 0);
                        break;
                    case TRK_VOLUME:
                        p4.track_volume[trk] = val;
                        if (udp_wifi_connected()) udp_send_set_track_volume(trk, val);
                        break;
                }
            }
            break;
        }

        case MSG_SCREEN:
            if (id == SCR_NAVIGATE) p4.current_screen = val;
            break;
    }
}

// =============================================================================
// PROCESS EXTENDED PACKET (pattern data, etc.)
// =============================================================================
static void process_extended(uint8_t type, uint8_t id, const uint8_t* payload, int len) {
    if (type == MSG_PATTERN_DATA && len == 32) {
        // 32 bytes = 256 bits = 16 tracks × 16 steps
        for (int track = 0; track < 16; track++) {
            uint16_t row = (uint16_t)payload[track * 2] | ((uint16_t)payload[track * 2 + 1] << 8);
            for (int step = 0; step < 16; step++) {
                p4.steps[track][step] = (row >> step) & 1;
            }
        }
    }
}

// =============================================================================
// MAIN PROCESS — call from loop()
// =============================================================================
static int s_totalDiscarded = 0;
static int s_processed = 0;

// Feed one byte into the protocol parser (shared by UART and USB sources)
static void feed_byte(uint8_t b) {
    rxBuf[rxHead] = b;

    // Look for start byte
    if (rxHead == 0) {
        if (b != UART_START_BASIC && b != UART_START_EXTENDED) {
            s_totalDiscarded++;
            if (s_totalDiscarded <= 50) {
                P4_LOG_PRINTF("[UART-DISC] 0x%02X\n", b);
            }
            return;  // discard, wait for start byte
        }
    }

    rxHead++;

    // Basic packet complete?
    if (rxBuf[0] == UART_START_BASIC && rxHead >= UART_BASIC_LEN) {
        UartBasicPacket* pkt = (UartBasicPacket*)rxBuf;
        if (uart_validate_basic(pkt)) {
            process_basic(pkt);
            s_processed++;
        }
        rxHead = 0;
    }

    // Extended packet header complete?
    if (rxBuf[0] == UART_START_EXTENDED && rxHead >= UART_EXT_HEADER_LEN) {
        UartExtendedHeader* hdr = (UartExtendedHeader*)rxBuf;
        int payload_len = ((int)hdr->len_h << 8) | hdr->len_l;
        int total = UART_EXT_HEADER_LEN + payload_len + 1; // +1 for checksum

        if (total > (int)sizeof(rxBuf)) {
            // Packet too large — discard
            rxHead = 0;
            return;
        }

        if (rxHead >= total) {
            // Validate checksum (sum of all bytes)
            uint8_t sum = 0;
            for (int i = 0; i < total - 1; i++) sum += rxBuf[i];
            if (sum == rxBuf[total - 1]) {
                process_extended(hdr->type, hdr->id,
                                 &rxBuf[UART_EXT_HEADER_LEN], payload_len);
                s_processed++;
            }
            rxHead = 0;
        }
    }

    // Guard against buffer overflow
    if (rxHead >= (int)sizeof(rxBuf)) {
        rxHead = 0;
    }
}

int uart_handler_process(void) {
    s_processed = 0;

    // Check heartbeat timeout
    if (p4.s3_connected && (millis() - p4.last_heartbeat_ms) > Config::HEARTBEAT_TIMEOUT_MS) {
        p4.s3_connected = false;
        P4_LOG_PRINTLN("[UART] S3 heartbeat lost!");
    }

    // Debug: periodic status
    {
        static unsigned long lastDbg = 0;
        if (millis() - lastDbg >= 3000) {
            lastDbg = millis();
            int avail = UartS3.available();
#if P4_USB_CDC_ENABLED
            int usbAvail = usb_cdc_available();
            P4_LOG_PRINTF("[UART-DBG] uart=%d usb=%d s3=%d disc=%d hb=%lums | USB: %s\n",
                          avail, usbAvail, p4.s3_connected, s_totalDiscarded,
                          millis() - p4.last_heartbeat_ms,
                          usb_cdc_status_str());
#else
            P4_LOG_PRINTF("[UART-DBG] avail=%d s3_conn=%d disc=%d last_hb=%lums ago\n",
                          avail, p4.s3_connected, s_totalDiscarded,
                          millis() - p4.last_heartbeat_ms);
#endif
        }
    }

    // Read from UART
    while (UartS3.available()) {
        feed_byte((uint8_t)UartS3.read());
    }

#if P4_USB_CDC_ENABLED
    // Read from USB CDC (S3 via USB-C)
    while (usb_cdc_available()) {
        int b = usb_cdc_read();
        if (b >= 0) feed_byte((uint8_t)b);
    }
#endif

    return s_processed;
}
