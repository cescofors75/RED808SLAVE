// =============================================================================
// uart_handler.cpp — UART binary protocol receiver (P4 side)
// =============================================================================

#include "uart_handler.h"
#include "udp_handler.h"
#include "ui/ui_screens.h"
#include "../include/config.h"
#include <Arduino.h>

#if P4_USB_CDC_ENABLED
#include "usb_cdc_handler.h"
#endif

// P4 local state — single source of truth for UI rendering
P4State p4 = {};

// P4 SD remote browse state
P4SdState p4sd = {};

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

// =============================================================================
// SEND PATTERN DATA TO S3 (extended packet)
// =============================================================================
void uart_send_pattern_to_s3(int pattern, const bool steps[16][16]) {
    // Pack 16 tracks × 16 steps into 32 bytes (2 bytes/track, big-endian, bit per step)
    uint8_t packed[32];
    for (int t = 0; t < 16; t++) {
        uint16_t bits = 0;
        for (int s = 0; s < 16; s++) { if (steps[t][s]) bits |= (1u << s); }
        packed[t * 2]     = (bits >> 8) & 0xFF;
        packed[t * 2 + 1] = bits & 0xFF;
    }
    // Build extended header: 0xAB, type, id(pattern), len_h, len_l
    const uint16_t plen = 32;
    uint8_t hdr[UART_EXT_HEADER_LEN];
    hdr[0] = UART_START_EXTENDED;
    hdr[1] = MSG_PATTERN_DATA;
    hdr[2] = (uint8_t)constrain(pattern, 0, 15);
    hdr[3] = (plen >> 8) & 0xFF;
    hdr[4] = plen & 0xFF;
    uint8_t cs = 0;
    for (int i = 0; i < UART_EXT_HEADER_LEN; i++) cs += hdr[i];
    for (int i = 0; i < 32; i++) cs += packed[i];

#if P4_USB_CDC_ENABLED
    if (usb_cdc_connected()) {
        usb_cdc_write(hdr, UART_EXT_HEADER_LEN);
        usb_cdc_write(packed, 32);
        usb_cdc_write(&cs, 1);
        return;
    }
#endif
    UartS3.write(hdr, UART_EXT_HEADER_LEN);
    UartS3.write(packed, 32);
    UartS3.write(cs);
}

bool uart_s3_alive(void) {
    return p4.s3_connected;
}

// =============================================================================
// SD REMOTE BROWSE — send commands to S3
// =============================================================================
void uart_send_sd_mount(void) {
    p4sd.entry_count = 0;
    p4sd.list_complete = false;
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_SD_MOUNT, 0);
}

void uart_send_sd_select(uint8_t index) {
    p4sd.entry_count = 0;
    p4sd.list_complete = false;
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_SD_SELECT, index);
}

void uart_send_sd_back(void) {
    p4sd.entry_count = 0;
    p4sd.list_complete = false;
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_SD_BACK, 0);
}

void uart_send_sd_load(uint8_t pad) {
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_SD_LOAD, pad);
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
            if (id < 3) {
                p4.enc_value[id] = val;
                // Relay encoder FX values to Master (P4 is the WiFi gateway)
                if (udp_wifi_connected()) {
                    udp_send_fx_enc(id, val, p4.enc_muted[id]);
                }
            }
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
            if (id < 4) {
                p4.pot_value[id] = val;
                // Relay pot FX values to Master
                if (udp_wifi_connected()) {
                    // Pot 0 → Distortion, Pot 1 → Cutoff, Pot 2 → Resonance
                    if (id >= 1 && id <= 3) {
                        udp_send_fx_pot(id - 1, val, p4.pot_muted[id - 1]);
                    }
                }
            }
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
                    // Echo heartbeat back so S3 knows P4 is alive
                    uart_send_to_s3(MSG_SYSTEM, SYS_HEARTBEAT, 0x01);
                    break;
            }
            break;

        case MSG_FX:
            switch (id) {
                case FX_ENC0_MUTE:
                    p4.enc_muted[0] = (val != 0);
                    if (udp_wifi_connected()) udp_send_fx_enc(0, p4.enc_value[0], p4.enc_muted[0]);
                    break;
                case FX_ENC1_MUTE:
                    p4.enc_muted[1] = (val != 0);
                    if (udp_wifi_connected()) udp_send_fx_enc(1, p4.enc_value[1], p4.enc_muted[1]);
                    break;
                case FX_ENC2_MUTE:
                    p4.enc_muted[2] = (val != 0);
                    if (udp_wifi_connected()) udp_send_fx_enc(2, p4.enc_value[2], p4.enc_muted[2]);
                    break;
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
                        if (udp_wifi_connected()) udp_send_solo(trk, val != 0);
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

        case MSG_TOUCH_CMD:
            // S3 relays a step toggle when it has no WiFi (P4 forwards to Master)
            if (id == TCMD_STEP_TOGGLE) {
                int trk = (val >> 4) & 0xF;
                int stp = val & 0xF;
                if (trk < 16 && stp < 16) {
                    p4.steps[trk][stp] = !p4.steps[trk][stp];
                    if (udp_wifi_connected())
                        udp_send_set_step(trk, stp, p4.steps[trk][stp]);
                }
            }
            else if (id == TCMD_SYNC_PADS) {
                ui_live_set_sync_p4(val != 0);
            }
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
    else if (type == MSG_SD_DATA) {
        switch (id) {
            case SD_RESP_STATUS:
                if (len >= 1) p4sd.mounted = (payload[0] != 0);
                p4sd.needs_refresh = true;
                break;
            case SD_RESP_ENTRY:
                if (len >= 3 && p4sd.entry_count < P4_SD_MAX_ENTRIES) {
                    int idx = p4sd.entry_count++;
                    p4sd.entries[idx].is_dir = (payload[1] == 'D');
                    int nameLen = len - 2;
                    if (nameLen > 47) nameLen = 47;
                    memcpy(p4sd.entries[idx].name, &payload[2], nameLen);
                    p4sd.entries[idx].name[nameLen] = '\0';
                }
                break;
            case SD_RESP_LIST_END:
                p4sd.list_complete = true;
                p4sd.needs_refresh = true;
                break;
            case SD_RESP_PATH:
                if (len > 0 && len < (int)sizeof(p4sd.path) - 1) {
                    memcpy(p4sd.path, payload, len);
                    p4sd.path[len] = '\0';
                }
                break;
            case SD_RESP_SELECTED:
                if (len > 0 && len < (int)sizeof(p4sd.selected_file) - 1) {
                    memcpy(p4sd.selected_file, payload, len);
                    p4sd.selected_file[len] = '\0';
                    p4sd.needs_refresh = true;
                }
                break;
            case SD_RESP_LOAD_OK:
                p4sd.needs_refresh = true;
                break;
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
