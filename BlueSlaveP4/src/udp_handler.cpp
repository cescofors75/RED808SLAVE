// =============================================================================
// udp_handler.cpp — WiFi/UDP communication to Master via ESP32-C6
// P4 connects directly to Master AP (RED808) through SDIO ESP-Hosted
// =============================================================================

#include "udp_handler.h"
#include "uart_handler.h"   // for P4State p4
#include "../include/config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <math.h>

// =============================================================================
// CONFIGURATION
// =============================================================================
static const char*     WIFI_SSID      = P4_WIFI_SSID;
static const char*     WIFI_PASS      = P4_WIFI_PASS;
static const IPAddress MASTER_IP(192, 168, 4, 1);
static const uint16_t  UDP_PORT       = 8888;

// Timing
static const unsigned long WIFI_RETRY_MS    = 5000;
static const unsigned long MASTER_TIMEOUT_MS = 3000;
static const unsigned long SYNC_REQUEST_MS  = 500;

// =============================================================================
// STATE
// =============================================================================
static WiFiUDP udp;
static bool wifiConnected     = false;
static bool udpStarted        = false;
static bool masterAlive        = false;
static unsigned long lastWifiAttempt  = 0;
static unsigned long lastMasterPacket = 0;
static unsigned long lastSyncRequest  = 0;

// JSON parse buffer
static char rxBuf[1024];

static int clamp_int(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float clamp_float(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

// =============================================================================
// SEND HELPERS
// =============================================================================
static void sendJson(const char* json) {
    if (!udpStarted) return;
    udp.beginPacket(MASTER_IP, UDP_PORT);
    udp.print(json);
    udp.endPacket();
}

static void sendCmd(const char* cmd) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}", cmd);
    sendJson(buf);
}

// =============================================================================
// PUBLIC SEND API
// =============================================================================
void udp_send_trigger(uint8_t pad, uint8_t velocity) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"trigger\",\"pad\":%d,\"vel\":%d}", pad, velocity);
    sendJson(buf);
}

void udp_send_start(void) { sendCmd("start"); }
void udp_send_stop(void)  { sendCmd("stop"); }

void udp_send_tempo(float bpm) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"tempo\",\"value\":%.1f}", bpm);
    sendJson(buf);
}

void udp_send_select_pattern(int index) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"selectPattern\",\"index\":%d}", index);
    sendJson(buf);
}

void udp_send_get_pattern(int pattern) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"get_pattern\",\"pattern\":%d}", pattern);
    sendJson(buf);
}

void udp_send_set_step(int track, int step, bool active) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"setStep\",\"track\":%d,\"step\":%d,\"active\":%s}",
             track, step, active ? "true" : "false");
    sendJson(buf);
}

void udp_send_mute(int track, bool muted) {
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"mute\",\"track\":%d,\"value\":%s}",
             track, muted ? "true" : "false");
    sendJson(buf);
}

void udp_send_solo(int track, bool soloed) {
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"solo\",\"track\":%d,\"value\":%s}",
             track, soloed ? "true" : "false");
    sendJson(buf);
}

void udp_send_set_volume(int value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setVolume\",\"value\":%d}", value);
    sendJson(buf);
}

void udp_send_set_seq_volume(int value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setSequencerVolume\",\"value\":%d}", value);
    sendJson(buf);
}

void udp_send_set_live_volume(int value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setLiveVolume\",\"value\":%d}", value);
    sendJson(buf);
}

void udp_send_set_track_volume(int track, int volume) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"setTrackVolume\",\"track\":%d,\"volume\":%d}",
             track, volume);
    sendJson(buf);
}

void udp_send_set_filter(int type) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setFilter\",\"type\":%d}", type);
    sendJson(buf);
}

void udp_send_set_filter_cutoff(int hz) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setFilterCutoff\",\"value\":%d}", hz);
    sendJson(buf);
}

void udp_send_set_filter_resonance(float val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setFilterResonance\",\"value\":%.1f}", val);
    sendJson(buf);
}

void udp_send_set_distortion(float val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setDistortion\",\"value\":%.2f}", val);
    sendJson(buf);
}

// =============================================================================
// FX LIVE COMMANDS — enc/pot values → Master FX engine
// Aggressive mix curve (sqrt) so small encoder values produce audible FX.
// Params resent periodically to survive UDP packet drops.
// =============================================================================
void udp_send_fx_enc(int enc_id, uint8_t value, bool muted) {
    if (!udpStarted) return;
    if (enc_id < 0 || enc_id > 2) return;

    static bool was_active[3] = {false, false, false};
    static unsigned long last_full_send[3] = {0, 0, 0};
    char buf[96];
    bool active = (!muted && value > 0);
    float norm = (float)value / 127.0f;
    // sqrt curve: value=2→0.13, value=10→0.28, value=30→0.49, value=60→0.69
    // Plus 15% floor so first click is ~25% mix (clearly audible)
    float mix = 0.15f + 0.85f * sqrtf(norm);

    bool justActivated = (active && !was_active[enc_id]);
    unsigned long now = millis();
    // Resend ALL params on activation OR every 2s (survive UDP drops)
    bool fullSend = justActivated || (active && (now - last_full_send[enc_id] > 2000));
    if (fullSend) last_full_send[enc_id] = now;
    was_active[enc_id] = active;

    P4_LOG_PRINTF("[FX] enc%d val=%d muted=%d active=%d mix=%.2f full=%d\n",
                  enc_id, value, muted, active, mix, fullSend);

    switch (enc_id) {
        case 0: // Chorus — lush stereo modulation
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setChorusActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                if (fullSend) {
                    sendJson("{\"cmd\":\"setChorusRate\",\"value\":0.6}");
                    sendJson("{\"cmd\":\"setChorusDepth\",\"value\":0.5}");
                    sendJson("{\"cmd\":\"setChorusStereo\",\"value\":1}");
                }
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setChorusMix\",\"value\":%.3f}", mix);
                sendJson(buf);
            }
            break;
        case 1: // Delay — unmistakable echo effect
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setDelayActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                if (fullSend) {
                    sendJson("{\"cmd\":\"setDelayTime\",\"value\":300}");
                    sendJson("{\"cmd\":\"setDelayFeedback\",\"value\":0.45}");
                    sendJson("{\"cmd\":\"setDelayStereo\",\"value\":1}");
                }
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setDelayMix\",\"value\":%.3f}", mix);
                sendJson(buf);
            }
            break;
        case 2: // Reverb — unmistakable room/hall effect
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setReverbActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                if (fullSend) {
                    sendJson("{\"cmd\":\"setReverbFeedback\",\"value\":0.7}");
                    sendJson("{\"cmd\":\"setReverbLpFreq\",\"value\":5000}");
                    sendJson("{\"cmd\":\"setEarlyRefActive\",\"value\":1}");
                    sendJson("{\"cmd\":\"setEarlyRefMix\",\"value\":0.3}");
                }
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setReverbMix\",\"value\":%.3f}", mix);
                sendJson(buf);
            }
            break;
    }
}

// Latched state for LP-filter auto-enable (in udp_send_fx_pot case 2).
// Cleared on WiFi drop via udp_reset_fx_latch() so it is resent after reconnect.
static bool s_fx_lp_filter_enabled = false;

void udp_send_fx_pot(int pot_id, uint8_t value, bool muted) {
    if (!udpStarted || muted) return;
    char buf[96];
    float norm = (float)value / 127.0f;
    switch (pot_id) {
        case 0: {  // Distortion/Drive
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setDistortion\",\"value\":%.3f}", norm);
            sendJson(buf); break;
        }
        case 1: {  // Cutoff (20-20000 Hz, log)
            int hz = (int)(20.0f * powf(1000.0f, norm));
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setFilterCutoff\",\"value\":%d}", hz);
            sendJson(buf); break;
        }
        case 2: {  // Resonance (1.0-10.0 Q) — LP filter must be active to hear it.
            // Mirror S3 behaviour: auto-enable LowPass so the Q is audible.
            // Latch is cleared in onWiFiDisconnected() so it re-sends after
            // any master/link drop.
            if (!s_fx_lp_filter_enabled) {
                sendJson("{\"cmd\":\"setFilter\",\"type\":1}");
                s_fx_lp_filter_enabled = true;
            }
            float q = 1.0f + norm * 9.0f;
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setFilterResonance\",\"value\":%.2f}", q);
            sendJson(buf); break;
        }
    }
}

// Reset latched FX state so it is resent after (re)connecting to Master.
// Called from WiFi disconnect path.
void udp_reset_fx_latch(void) { s_fx_lp_filter_enabled = false; }

// =============================================================================
// SYNC REQUEST — handshake + request initial data
// =============================================================================
void udp_request_master_sync(void) {
    P4_LOG_PRINTLN("[UDP] Requesting Master sync...");
    sendJson("{\"cmd\":\"hello\",\"device\":\"P4_DISPLAY\"}");
    udp_send_get_pattern(p4.current_pattern);
    sendJson("{\"cmd\":\"getTrackVolumes\"}");

    // Reset ALL FX to safe defaults
    sendJson("{\"cmd\":\"setFlangerActive\",\"value\":0}");
    sendJson("{\"cmd\":\"setDelayActive\",\"value\":0}");
    sendJson("{\"cmd\":\"setReverbActive\",\"value\":0}");
    sendJson("{\"cmd\":\"setChorusActive\",\"value\":0}");
    sendJson("{\"cmd\":\"setTremoloActive\",\"value\":0}");
    sendJson("{\"cmd\":\"setFilter\",\"type\":0}");
    sendJson("{\"cmd\":\"setDistortion\",\"value\":0.0}");

    lastSyncRequest = millis();
}

// =============================================================================
// PARSE INCOMING JSON
// =============================================================================
static void processJson(const char* json, int len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        P4_LOG_PRINTF("[UDP] JSON parse error: %s\n", err.c_str());
        return;
    }

    // Mark master alive
    lastMasterPacket = millis();
    if (!masterAlive) {
        masterAlive = true;
        p4.master_connected = true;
        P4_LOG_PRINTLN("[UDP] Master connected!");
    }

    const char* cmd = doc["cmd"];
    if (!cmd) return;

    // ----- Pattern sync -----
    if (strcmp(cmd, "pattern_sync") == 0) {
        int pat = clamp_int(doc["pattern"] | p4.current_pattern, 0, 15);
        JsonArray data = doc["data"];
        if (data) {
            // Check if incoming is all-empty
            bool incomingEmpty = true;
            for (JsonArray row : data) {
                for (JsonVariant val : row) {
                    if (val.as<int>() != 0) { incomingEmpty = false; break; }
                }
                if (!incomingEmpty) break;
            }
            // If the incoming pattern is empty but we already have local data for
            // the same index, keep local (protects against master sync floods).
            if (incomingEmpty && pat == p4.current_pattern) {
                bool localHasData = false;
                for (int t = 0; t < 16 && !localHasData; t++)
                    for (int s = 0; s < 16 && !localHasData; s++)
                        if (p4.steps[t][s]) localHasData = true;
                if (localHasData) {
                    P4_LOG_PRINTF("[UDP] Skipping empty pattern_sync for pattern %d\n", pat + 1);
                    return;
                }
            }
            p4.current_pattern = pat;
            int track = 0;
            for (JsonArray row : data) {
                if (track >= 16) break;
                int step = 0;
                for (JsonVariant val : row) {
                    if (step >= 16) break;
                    p4.steps[track][step] = (val.as<int>() != 0);
                    step++;
                }
                track++;
            }
        } else {
            p4.current_pattern = pat;
        }
        // Forward pattern data to S3 so it can sync its sequencer + pad-sync
        uart_send_pattern_to_s3(pat, p4.steps);
    }
    // ----- Play state -----
    else if (strcmp(cmd, "play_state") == 0) {
        p4.is_playing = doc["playing"] | false;
        if (p4.is_playing) p4.current_step = 0;
        else p4.current_step = 0;
        uart_send_to_s3(MSG_SYSTEM, SYS_PLAY_STATE, p4.is_playing ? 1 : 0);
        uart_send_to_s3(MSG_SYSTEM, SYS_STEP, 0);
    }
    else if (strcmp(cmd, "start") == 0) {
        p4.is_playing = true;
        p4.current_step = 0;
        uart_send_to_s3(MSG_SYSTEM, SYS_PLAY_STATE, 1);
        uart_send_to_s3(MSG_SYSTEM, SYS_STEP, 0);
    }
    else if (strcmp(cmd, "stop") == 0) {
        p4.is_playing = false;
        p4.current_step = 0;
        uart_send_to_s3(MSG_SYSTEM, SYS_PLAY_STATE, 0);
        uart_send_to_s3(MSG_SYSTEM, SYS_STEP, 0);
    }
    // ----- Tempo -----
    else if (strcmp(cmd, "tempo_sync") == 0 || strcmp(cmd, "tempo") == 0) {
        float bpm = clamp_float(doc["value"] | 120.0f, 40.0f, 240.0f);
        p4.bpm_int = (int)bpm;
        p4.bpm_frac = (int)((bpm - p4.bpm_int) * 10);
    }
    // ----- Step update (visual tick from Master) -----
    else if (strcmp(cmd, "step_update") == 0 || strcmp(cmd, "step_sync") == 0) {
        // P4 local clock is authoritative; ignore Master step hints.
    }
    // ----- Volume -----
    else if (strcmp(cmd, "volume_sync") == 0 || strcmp(cmd, "master_volume_sync") == 0 ||
             strcmp(cmd, "volume_master_sync") == 0 || strcmp(cmd, "setVolume") == 0) {
        int v = clamp_int(doc["value"] | 75, 0, 150);
        p4.master_volume = v;
        p4.seq_volume = v;
        p4.live_volume = v;
    }
    else if (strcmp(cmd, "volume_seq_sync") == 0 || strcmp(cmd, "setSequencerVolume") == 0) {
        p4.seq_volume = clamp_int(doc["value"] | 75, 0, 150);
    }
    else if (strcmp(cmd, "volume_live_sync") == 0 || strcmp(cmd, "setLiveVolume") == 0) {
        p4.live_volume = clamp_int(doc["value"] | 75, 0, 150);
    }
    // ----- Track volumes -----
    else if (strcmp(cmd, "trackVolumes") == 0 || strcmp(cmd, "track_volumes") == 0 ||
             strcmp(cmd, "track_volume_sync") == 0 || strcmp(cmd, "getTrackVolumes") == 0) {
        JsonArray arr;
        if (doc["values"].is<JsonArray>())       arr = doc["values"].as<JsonArray>();
        else if (doc["volumes"].is<JsonArray>()) arr = doc["volumes"].as<JsonArray>();
        else if (doc["data"].is<JsonArray>())    arr = doc["data"].as<JsonArray>();
        if (arr) {
            int i = 0;
            for (JsonVariant v : arr) {
                if (i >= 16) break;
                p4.track_volume[i] = clamp_int(v.as<int>(), 0, 150);
                i++;
            }
        }
    }
    else if (strcmp(cmd, "trackVolume") == 0 || strcmp(cmd, "getTrackVolume") == 0) {
        int trk = doc["track"] | 0;
        int vol = clamp_int(doc["volume"] | doc["value"] | 75, 0, 150);
        if (trk >= 0 && trk < 16) p4.track_volume[trk] = vol;
    }
    // ----- FX -----
    else if (strcmp(cmd, "setFilter") == 0) {
        p4.filter_type = clamp_int(doc["type"] | doc["value"] | 0, 0, 4);
    }
    else if (strcmp(cmd, "setFilterCutoff") == 0) {
        p4.cutoff_hz = clamp_int(doc["value"] | 20000, 20, 20000);
    }
    else if (strcmp(cmd, "setFilterResonance") == 0) {
        float r = clamp_float(doc["value"] | 1.0f, 1.0f, 10.0f);
        p4.resonance_x10 = (int)(r * 10);
    }
    else if (strcmp(cmd, "setBitCrush") == 0) {
        p4.bitcrush_bits = clamp_int(doc["value"] | 16, 4, 16);
    }
    else if (strcmp(cmd, "setDistortion") == 0) {
        float d = clamp_float(doc["value"] | 0.0f, 0.0f, 1.0f);
        p4.distortion_pct = (int)(d * 100);
    }
    else if (strcmp(cmd, "setSampleRate") == 0) {
        p4.sample_rate_hz = clamp_int(doc["value"] | 44100, 1000, 44100);
    }
    // ----- Pattern selection -----
    else if (strcmp(cmd, "selectPattern") == 0 || strcmp(cmd, "pattern_select") == 0 ||
             strcmp(cmd, "current_pattern") == 0) {
        int idx = clamp_int(doc["index"] | doc["pattern"] | 0, 0, 15);
        if (idx != p4.current_pattern) {
            p4.current_pattern = idx;
            udp_send_get_pattern(idx);
        }
    }
}

// =============================================================================
// WiFi MANAGEMENT
// =============================================================================
static void startWiFi(void) {
    P4_LOG_PRINTLN("[WiFi] Connecting to RED808...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    lastWifiAttempt = millis();
}

static void onWiFiConnected(void) {
    wifiConnected = true;
    p4.wifi_connected = true;
    P4_LOG_PRINTF("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Start UDP
    if (udp.begin(UDP_PORT)) {
        udpStarted = true;
        P4_LOG_PRINTF("[UDP] Listening on port %d\n", UDP_PORT);
        udp_request_master_sync();
    } else {
        P4_LOG_PRINTLN("[UDP] Failed to start!");
    }
}

static void onWiFiDisconnected(void) {
    if (wifiConnected) {
        P4_LOG_PRINTLN("[WiFi] Disconnected!");
        wifiConnected = false;
        udpStarted = false;
        masterAlive = false;
        p4.wifi_connected = false;
        p4.master_connected = false;
        // Clear FX latches so they are re-sent after reconnect (LP filter, etc.)
        extern void udp_reset_fx_latch(void);
        udp_reset_fx_latch();
    }
}

// =============================================================================
// INIT
// =============================================================================
void udp_handler_init(void) {
    P4_LOG_PRINTLN("[UDP] Init WiFi/UDP handler");
    startWiFi();
}

// =============================================================================
// PUBLIC GETTERS
// =============================================================================
bool udp_wifi_connected(void)   { return wifiConnected; }
bool udp_master_connected(void) { return masterAlive; }

// =============================================================================
// LOCAL STEP CLOCK (authoritative)
// P4 is the sequencer authority. It advances step locally and mirrors every
// step to S3 over UART (SYS_STEP), independent of WiFi/Master transport.
// =============================================================================
static void run_local_step_clock(unsigned long now) {
    static unsigned long lastStepTime = 0;
    static bool prev_playing = false;

    if (!p4.is_playing) {
        prev_playing = false;
        return;
    }
    // Edge false→true: snap the step clock to now to avoid catch-up bursts.
    if (!prev_playing) {
        lastStepTime = now;
        prev_playing = true;
        p4.current_step = 0;
        uart_send_to_s3(MSG_SYSTEM, SYS_STEP, (uint8_t)p4.current_step);
    }
    float bpm = p4.bpm_int + p4.bpm_frac * 0.1f;
    if (bpm < 40) bpm = 120;
    unsigned long stepInterval = (unsigned long)(60000.0f / bpm / 4.0f); // 16ths
    if (now - lastStepTime >= stepInterval) {
        lastStepTime = now;
        p4.current_step = (p4.current_step + 1) % 16;
        uart_send_to_s3(MSG_SYSTEM, SYS_STEP, (uint8_t)p4.current_step);
    }
}

// =============================================================================
// PROCESS — call from main loop
// =============================================================================
void udp_handler_process(void) {
    unsigned long now = millis();

    // --- WiFi state management ---
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            onWiFiConnected();
        }
    } else {
        if (wifiConnected) {
            onWiFiDisconnected();
        }
        // Retry WiFi
        if (now - lastWifiAttempt > WIFI_RETRY_MS) {
            startWiFi();
        }
        // Keep authoritative clock running even without WiFi.
        run_local_step_clock(now);
        return;
    }

    // --- Master timeout ---
    if (masterAlive && (now - lastMasterPacket > MASTER_TIMEOUT_MS)) {
        masterAlive = false;
        p4.master_connected = false;
        P4_LOG_PRINTLN("[UDP] Master timeout!");
    }

    // --- Re-request sync if no response ---
    if (udpStarted && !masterAlive) {
        if (now - lastSyncRequest > SYNC_REQUEST_MS) {
            udp_request_master_sync();
        }
    }

    // --- Receive UDP packets (bounded) ---
    // Cap packets per loop iteration to avoid starving ui_process_pad_queue()
    // under master-sync floods. Pad taps have priority over sync processing.
    int packetSize = udp.parsePacket();
    int pkt_budget = 4;
    while (packetSize > 0 && pkt_budget-- > 0) {
        int len = udp.read(rxBuf, sizeof(rxBuf) - 1);
        if (len > 0) {
            rxBuf[len] = '\0';

            // Handle JSON array (batch) or single object
            if (rxBuf[0] == '[') {
                // Batch: parse array of commands
                JsonDocument batchDoc;
                DeserializationError err = deserializeJson(batchDoc, rxBuf, len);
                if (!err && batchDoc.is<JsonArray>()) {
                    for (JsonVariant item : batchDoc.as<JsonArray>()) {
                        char singleBuf[512];
                        int sLen = serializeJson(item, singleBuf, sizeof(singleBuf));
                        if (sLen > 0) processJson(singleBuf, sLen);
                    }
                }
            } else {
                processJson(rxBuf, len);
            }
        }

        // Drain pad queue between packets — ensures taps go out even under
        // heavy inbound UDP traffic from the master.
        extern void ui_process_pad_queue(void);
        ui_process_pad_queue();

        packetSize = udp.parsePacket();
    }

    // --- Local step clock (authoritative) ---
    run_local_step_clock(now);
}
