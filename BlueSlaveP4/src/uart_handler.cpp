// =============================================================================
// uart_handler.cpp — UART binary protocol receiver (P4 side)
// =============================================================================

#include "uart_handler.h"
#include "udp_handler.h"
#include "dsp_task.h"
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

// Link statistics
UartStats uart_stats = {};

// UART instance
static HardwareSerial UartS3(UART_S3_PORT);

// Circular receive buffer
static uint8_t rxBuf[UART_RX_BUF];
static int rxHead = 0;

// Tempo lock: absolute millis() until which incoming BPM updates from S3 are
// ignored (set by uart_lock_tempo() after applying a MIDI-file tempo).
static uint32_t s_tempo_lock_until_ms = 0;

// -----------------------------------------------------------------------------
// Deferred "pattern push to Master" — avoids blocking the main loop (~260 ms)
// when a MIDI pattern arrives via MSG_PATTERN_PUSH. The heavy UDP burst
// (selectPattern + 256 clear setSteps + active setSteps + start) is staged
// here and drained a few packets at a time from uart_handler_process().
// -----------------------------------------------------------------------------
enum PendingPushPhase {
    PP_IDLE = 0,
    PP_SELECT,
    PP_CLEAR,
    PP_ACTIVE,
    PP_START,
};
struct PendingPush {
    PendingPushPhase phase;
    uint8_t  slot;
    int      idx;          // index within current phase
    uint32_t next_ms;      // earliest millis() to send next packet
    bool     step_bits[16][16];  // snapshot of steps to broadcast
};
static PendingPush s_push = {PP_IDLE, 0, 0, 0, {{false}}};

// Tunables — small delays keep WiFi/stack happy without stalling the loop.
static constexpr int PP_PACKETS_PER_TICK = 6;   // packets drained per loop()
static constexpr uint32_t PP_INTER_MS    = 2;   // pacing between bursts

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

    // SD / MIDI load state
    p4sd.midi_load_result = -2;  // idle

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
    uart_stats.tx_packets++;
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

void uart_send_sd_load_midi(uint8_t slot) {
    // Mark load as in-flight so the UI shows a waiting state until the
    // S3 responds with SD_RESP_LOAD_OK (success slot) or 0xFF (fail).
    p4sd.midi_load_result = -1;
    p4sd.needs_refresh = true;
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_SD_LOAD_MIDI, slot);
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
                dsp_notify_pad(id, val);
                // Relay S3 pad triggers to Master when S3 WiFi is down
                if (!p4.s3_wifi_connected && udp_wifi_connected()) {
                    udp_send_trigger(id, val);
                }
            }
            break;

        case MSG_POT:
            if (id < 4) {
                p4.pot_value[id] = val;
                // Relay pot FX values to Master (see BlueSlaveV2/main.cpp mapping):
                //   S3 pot 0 = Master volume (NOT an FX, ignored here — handled elsewhere)
                //   S3 pot 1 = reserved/disabled (was Cutoff, no audible effect now)
                //   S3 pot 2 = Resonance  → udp_send_fx_pot(2) → setFilterResonance
                //   S3 pot 3 = Distortion → udp_send_fx_pot(0) → setDistortion
                // Previous mapping was `id-1` which swapped Resonance/Distortion/Cutoff
                // and caused Master to apply the wrong DSP (silent/inaudible FX).
                if (udp_wifi_connected()) {
                    if (id == 2) {
                        udp_send_fx_pot(2, val, p4.pot_muted[2]);
                    } else if (id == 3) {
                        udp_send_fx_pot(0, val, p4.pot_muted[0]);
                    }
                }
            }
            break;

        case MSG_SYSTEM:
            switch (id) {
                case SYS_BPM_INT:
                    // Tempo lock window: ignore S3-initiated BPM updates for
                    // a short time after we applied a MIDI file's tempo, so
                    // the S3's stale cached BPM doesn't overwrite it.
                    if (millis() < s_tempo_lock_until_ms) break;
                    p4.bpm_int = val;
                    break;
                case SYS_BPM_FRAC:
                    if (millis() < s_tempo_lock_until_ms) break;
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
                    // State sync from S3 — do NOT re-send start/stop to
                    // Master here. The origin UI (P4 header or S3 touch)
                    // already sent the UDP command; echoing it would
                    // duplicate traffic and can cause feedback loops.
                    p4.is_playing = (val != 0);
                    // Snap local step counter when playback starts/stops
                    // so the fallback clock / UI label reset cleanly.
                    if (!p4.is_playing) p4.current_step = 0;
                    break;
                case SYS_STEP:
                    // S3 drives the sequencer clock (local 16th-note timer)
                    // and sends SYS_STEP over UART on every advance. This is
                    // THE authoritative step signal — UART is low-latency and
                    // lossless, unlike Master's 8 Hz UDP step_sync. Always
                    // follow it while playing.
                    if (p4.is_playing) p4.current_step = val;
                    else               p4.current_step = 0;
                    break;
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
                // 16-bit values arrive as two packets (H then L). Accumulate
                // into a staging variable and commit to p4.* only when the
                // low byte arrives so UI never reads a half-updated value.
                case FX_CUTOFF_H: {
                    static int s_cutoff_staging = 20000;
                    s_cutoff_staging = (s_cutoff_staging & 0x00FF) | (val << 8);
                    // Expose staging until L arrives — keeps high byte visible.
                    p4.cutoff_hz = s_cutoff_staging;
                    break;
                }
                case FX_CUTOFF_L: {
                    // Commit: take last high byte from current value.
                    p4.cutoff_hz = (p4.cutoff_hz & 0xFF00) | val;
                    break;
                }
                case FX_RESONANCE:    p4.resonance_x10 = val;             break;
                case FX_DISTORTION:   p4.distortion_pct = val;            break;
                case FX_BITCRUSH:     p4.bitcrush_bits = val;             break;
                case FX_SAMPLERATE_H: {
                    static int s_sr_staging = 44100;
                    s_sr_staging = (s_sr_staging & 0x00FF) | (val << 8);
                    p4.sample_rate_hz = s_sr_staging;
                    break;
                }
                case FX_SAMPLERATE_L: {
                    p4.sample_rate_hz = (p4.sample_rate_hz & 0xFF00) | val;
                    break;
                }
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
        // 32 bytes = 256 bits = 16 tracks × 16 steps (big-endian: MSB first per track)
        for (int track = 0; track < 16; track++) {
            uint16_t row = ((uint16_t)payload[track * 2] << 8) | payload[track * 2 + 1];
            for (int step = 0; step < 16; step++) {
                p4.steps[track][step] = (row >> step) & 1;
            }
        }
        // NOTE: do NOT forward the whole pattern as N×setStep to Master.
        // The Master already owns the canonical pattern and pushes it via
        // `pattern_sync` (see udp_handler.cpp). Re-broadcasting 256 UDP
        // packets per pattern change floods the link and can overflow the
        // socket. selectPattern (SYS_PATTERN) is enough for Master to sync.
    }
    else if (type == MSG_PATTERN_PUSH && len == 32) {
        // S3 just parsed a MIDI file from SD and handed us a freshly-loaded
        // pattern. The Master does NOT yet know about this pattern — P4 is
        // the owner of the Master UDP channel for this path.
        //
        // Flow:
        //   1. Decode the 32-byte packed payload into p4.steps[][].
        //   2. Select that pattern slot on Master.
        //   3. Clear the slot (setStep active:false for all 16 tracks × 16 steps).
        //   4. Send setStep active:true for every hit.
        //   5. Send start so the pattern plays immediately.
        int slot = (int)id;
        if (slot < 0 || slot > 15) return;

        // 1) decode into local UI state (authoritative for P4 display)
        for (int track = 0; track < 16; track++) {
            uint16_t row = ((uint16_t)payload[track * 2] << 8) | payload[track * 2 + 1];
            for (int step = 0; step < 16; step++) {
                p4.steps[track][step] = (row >> step) & 1;
            }
        }
        p4.current_pattern = slot;

        if (!udp_wifi_connected()) {
            // No master link — UI is updated but master won't play the new
            // pattern until WiFi is available again.
            return;
        }

        // 2) Stage a deferred push to Master. The heavy UDP burst is drained
        //    a few packets per main-loop tick by uart_handler_tick_pending_push()
        //    so LVGL / touch / pad queue never stall.
        s_push.phase   = PP_SELECT;
        s_push.slot    = (uint8_t)slot;
        s_push.idx     = 0;
        s_push.next_ms = millis();
        for (int t = 0; t < 16; t++)
            for (int s = 0; s < 16; s++)
                s_push.step_bits[t][s] = p4.steps[t][s];
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
                    p4sd.entries[idx].is_dir  = (payload[1] == 'D');
                    p4sd.entries[idx].is_midi = (payload[1] == 'M');
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
                // Update UI feedback from LVGL context (p4sd state read in sd_refresh_ui)
                // The payload[0]==0xFF signals MIDI parse failure.
                // IMPORTANT: do NOT overwrite p4sd.selected_pad — that is the
                // user-selected drum pad for WAV sample load. Use a dedicated
                // status field so the two flows don't interfere.
                if (len >= 1 && p4sd.selected_is_midi) {
                    p4sd.midi_load_result = (payload[0] == 0xFF) ? 0x7F : (int8_t)payload[0];
                }
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
            uart_stats.rx_framing_errors++;
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
            uart_stats.rx_packets++;
            s_processed++;
        } else {
            uart_stats.rx_checksum_errors++;
        }
        rxHead = 0;
    }

    // Extended packet header complete?
    if (rxBuf[0] == UART_START_EXTENDED && rxHead >= UART_EXT_HEADER_LEN) {
        UartExtendedHeader* hdr = (UartExtendedHeader*)rxBuf;
        int payload_len = ((int)hdr->len_h << 8) | hdr->len_l;
        int total = UART_EXT_HEADER_LEN + payload_len + 1; // +1 for checksum

        // Reject oversized payload up front (also catches garbage in len bytes)
        if (payload_len > UART_EXT_MAX_PAYLOAD || total > (int)sizeof(rxBuf)) {
            // Packet too large — discard and resync
            uart_stats.rx_checksum_errors++;
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
                uart_stats.rx_packets++;
                s_processed++;
            } else {
                uart_stats.rx_checksum_errors++;
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
        // Reset framing-error display throttle so [UART-DISC] logs resume
        // for diagnosing the recovery window.
        s_totalDiscarded = 0;
        P4_LOG_PRINTLN("[UART] S3 heartbeat lost!");
    }

    // Proactive heartbeat: even when S3 is silent, poke the link every 1s so
    // a partially-alive S3 (e.g. display thread busy) gets a chance to reply
    // and we keep the USB/UART endpoint warm. Cheap (1 byte per second).
    {
        static unsigned long lastTx = 0;
        unsigned long nowMs = millis();
        if (nowMs - lastTx >= 1000) {
            lastTx = nowMs;
            uart_send_to_s3(MSG_SYSTEM, SYS_HEARTBEAT, 0x01);
        }
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

void uart_lock_tempo(uint32_t duration_ms) {
    s_tempo_lock_until_ms = millis() + duration_ms;
}

// Stage a pattern push to the Master from a raw 16×16 step grid.
// Used by the MEM MIDI loader on P4 — keeps P4 as the sole owner of the
// master UDP channel regardless of whether the MIDI came from SD (via S3)
// or from local SPIFFS.
void uart_stage_pattern_push_from_steps(uint8_t slot, const bool steps[16][16]) {
    if (slot > 15) return;

    // 1) Update local UI state immediately
    for (int t = 0; t < 16; t++)
        for (int s = 0; s < 16; s++)
            p4.steps[t][s] = steps[t][s];
    p4.current_pattern = slot;

    // 2) Skip staging if no Wi-Fi link to Master — UI is still correct.
    if (!udp_wifi_connected()) return;

    // 3) Arm deferred drain
    s_push.phase   = PP_SELECT;
    s_push.slot    = slot;
    s_push.idx     = 0;
    s_push.next_ms = millis();
    for (int t = 0; t < 16; t++)
        for (int s = 0; s < 16; s++)
            s_push.step_bits[t][s] = steps[t][s];
}

// Drain a few UDP packets per main-loop tick so MIDI loads don't stall the UI.
// Safe no-op when idle. Called from main loop().
void uart_handler_tick_pending_push(void) {
    if (s_push.phase == PP_IDLE) return;
    if (!udp_wifi_connected()) { s_push.phase = PP_IDLE; return; }    uint32_t now = millis();
    if ((int32_t)(now - s_push.next_ms) < 0) return;

    int budget = PP_PACKETS_PER_TICK;

    while (budget > 0 && s_push.phase != PP_IDLE) {
        switch (s_push.phase) {
            case PP_SELECT:
                udp_send_select_pattern(s_push.slot);
                s_push.phase = PP_CLEAR;
                s_push.idx   = 0;
                budget--;
                break;

            case PP_CLEAR: {
                // 256 clear packets (16 tracks × 16 steps)
                int t = s_push.idx / 16;
                int st = s_push.idx % 16;
                udp_send_set_step(t, st, false);
                s_push.idx++;
                budget--;
                if (s_push.idx >= 256) {
                    s_push.phase = PP_ACTIVE;
                    s_push.idx   = 0;
                }
                break;
            }

            case PP_ACTIVE: {
                // Scan forward to next active step
                bool sent = false;
                while (s_push.idx < 256) {
                    int t = s_push.idx / 16;
                    int st = s_push.idx % 16;
                    s_push.idx++;
                    if (s_push.step_bits[t][st]) {
                        udp_send_set_step(t, st, true);
                        budget--;
                        sent = true;
                        break;
                    }
                }
                if (!sent || s_push.idx >= 256) {
                    if (s_push.idx >= 256) s_push.phase = PP_START;
                }
                break;
            }

            case PP_START:
                if (!p4.is_playing) {
                    udp_send_start();
                    p4.is_playing = true;
                }
                s_push.phase = PP_IDLE;
                budget--;
                break;

            default:
                s_push.phase = PP_IDLE;
                break;
        }
    }

    s_push.next_ms = millis() + PP_INTER_MS;
}
