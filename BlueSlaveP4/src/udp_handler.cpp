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
static const char*     WIFI_SSID      = "RED808";
static const char*     WIFI_PASS      = "red808esp32";
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
static bool syncRequested      = false;
static unsigned long lastWifiAttempt  = 0;
static unsigned long lastMasterPacket = 0;
static unsigned long lastSyncRequest  = 0;

// JSON parse buffer
static char rxBuf[1024];

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
    snprintf(buf, sizeof(buf), "{\"cmd\":\"setFilter\",\"value\":%d}", type);
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
        case 0: // Flanger
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setFlangerActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                if (fullSend) {
                    sendJson("{\"cmd\":\"setFlangerRate\",\"value\":0.4}");
                    sendJson("{\"cmd\":\"setFlangerDepth\",\"value\":0.8}");
                    sendJson("{\"cmd\":\"setFlangerFeedback\",\"value\":0.7}");
                }
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setFlangerMix\",\"value\":%.3f}", mix);
                sendJson(buf);
            }
            break;
        case 1: // Chorus
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setChorusActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                if (fullSend) {
                    sendJson("{\"cmd\":\"setChorusRate\",\"value\":1.5}");
                    sendJson("{\"cmd\":\"setChorusDepth\",\"value\":0.6}");
                    sendJson("{\"cmd\":\"setChorusStereo\",\"value\":1}");
                }
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setChorusMix\",\"value\":%.3f}", mix);
                sendJson(buf);
            }
            break;
        case 2: // Tremolo
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setTremoloActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setTremoloDepth\",\"value\":%.3f}", mix);
                sendJson(buf);
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setTremoloRate\",\"value\":%.1f}", 3.0f + mix * 7.0f);
                sendJson(buf);
            }
            break;
    }
}

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
        case 2: {  // Resonance (1.0-10.0 Q)
            float q = 1.0f + norm * 9.0f;
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setFilterResonance\",\"value\":%.2f}", q);
            sendJson(buf); break;
        }
    }
}

// =============================================================================
// SYNC REQUEST — handshake + request initial data
// =============================================================================
void udp_request_master_sync(void) {
    P4_LOG_PRINTLN("[UDP] Requesting Master sync...");
    sendJson("{\"cmd\":\"hello\",\"device\":\"P4_DISPLAY\"}");
    udp_send_get_pattern(p4.current_pattern);
    sendJson("{\"cmd\":\"getTrackVolumes\"}");

    // Reset FX to safe defaults
    sendJson("{\"cmd\":\"setFlangerActive\",\"value\":0}");
    sendJson("{\"cmd\":\"setChorusActive\",\"value\":0}");
    sendJson("{\"cmd\":\"setTremoloActive\",\"value\":0}");
    sendJson("{\"cmd\":\"setFilter\",\"value\":0}");
    sendJson("{\"cmd\":\"setDistortion\",\"value\":0.0}");

    syncRequested = true;
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
        int pat = doc["pattern"] | p4.current_pattern;
        p4.current_pattern = pat;
        JsonArray data = doc["data"];
        if (data) {
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
        }
        // Forward pattern data to S3 so it can sync its sequencer + pad-sync
        uart_send_pattern_to_s3(pat, p4.steps);
    }
    // ----- Play state -----
    else if (strcmp(cmd, "play_state") == 0) {
        p4.is_playing = doc["playing"] | false;
        if (p4.is_playing) p4.current_step = 0;
    }
    else if (strcmp(cmd, "start") == 0) {
        p4.is_playing = true;
        p4.current_step = 0;
    }
    else if (strcmp(cmd, "stop") == 0) {
        p4.is_playing = false;
        p4.current_step = 0;
    }
    // ----- Tempo -----
    else if (strcmp(cmd, "tempo_sync") == 0 || strcmp(cmd, "tempo") == 0) {
        float bpm = doc["value"] | 120.0f;
        p4.bpm_int = (int)bpm;
        p4.bpm_frac = (int)((bpm - p4.bpm_int) * 10);
    }
    // ----- Step update (visual tick from Master) -----
    else if (strcmp(cmd, "step_update") == 0 || strcmp(cmd, "step_sync") == 0) {
        // Keep local step clock, just confirm Master is alive
    }
    // ----- Volume -----
    else if (strcmp(cmd, "volume_sync") == 0 || strcmp(cmd, "master_volume_sync") == 0 ||
             strcmp(cmd, "volume_master_sync") == 0 || strcmp(cmd, "setVolume") == 0) {
        int v = doc["value"] | 75;
        p4.master_volume = v;
        p4.seq_volume = v;
        p4.live_volume = v;
    }
    else if (strcmp(cmd, "volume_seq_sync") == 0 || strcmp(cmd, "setSequencerVolume") == 0) {
        p4.seq_volume = doc["value"] | 75;
    }
    else if (strcmp(cmd, "volume_live_sync") == 0 || strcmp(cmd, "setLiveVolume") == 0) {
        p4.live_volume = doc["value"] | 75;
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
                p4.track_volume[i] = v.as<int>();
                i++;
            }
        }
    }
    else if (strcmp(cmd, "trackVolume") == 0 || strcmp(cmd, "getTrackVolume") == 0) {
        int trk = doc["track"] | 0;
        int vol = doc["volume"] | doc["value"] | 75;
        if (trk >= 0 && trk < 16) p4.track_volume[trk] = vol;
    }
    // ----- FX -----
    else if (strcmp(cmd, "setFilter") == 0) {
        p4.filter_type = doc["type"] | doc["value"] | 0;
    }
    else if (strcmp(cmd, "setFilterCutoff") == 0) {
        p4.cutoff_hz = doc["value"] | 20000;
    }
    else if (strcmp(cmd, "setFilterResonance") == 0) {
        float r = doc["value"] | 1.0f;
        p4.resonance_x10 = (int)(r * 10);
    }
    else if (strcmp(cmd, "setBitCrush") == 0) {
        p4.bitcrush_bits = doc["value"] | 16;
    }
    else if (strcmp(cmd, "setDistortion") == 0) {
        float d = doc["value"] | 0.0f;
        p4.distortion_pct = (int)(d * 100);
    }
    else if (strcmp(cmd, "setSampleRate") == 0) {
        p4.sample_rate_hz = doc["value"] | 44100;
    }
    // ----- Pattern selection -----
    else if (strcmp(cmd, "selectPattern") == 0 || strcmp(cmd, "pattern_select") == 0 ||
             strcmp(cmd, "current_pattern") == 0) {
        int idx = doc["index"] | doc["pattern"] | 0;
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
        syncRequested = false;
        p4.wifi_connected = false;
        p4.master_connected = false;
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
        return;
    }

    // --- Master timeout ---
    if (masterAlive && (now - lastMasterPacket > MASTER_TIMEOUT_MS)) {
        masterAlive = false;
        p4.master_connected = false;
        P4_LOG_PRINTLN("[UDP] Master timeout!");
    }

    // --- Re-request sync if no response ---
    if (udpStarted && !syncRequested && !masterAlive) {
        if (now - lastSyncRequest > SYNC_REQUEST_MS) {
            udp_request_master_sync();
        }
    }

    // --- Receive UDP packets ---
    int packetSize = udp.parsePacket();
    while (packetSize > 0) {
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

        packetSize = udp.parsePacket();
    }

    // --- Local step clock (BPM-based, like S3 does) ---
    if (p4.is_playing) {
        static unsigned long lastStepTime = 0;
        float bpm = p4.bpm_int + p4.bpm_frac * 0.1f;
        if (bpm < 40) bpm = 120;
        unsigned long stepInterval = (unsigned long)(60000.0f / bpm / 4.0f); // 16th notes
        if (now - lastStepTime >= stepInterval) {
            lastStepTime = now;
            p4.current_step = (p4.current_step + 1) % 16;
        }
    }
}
