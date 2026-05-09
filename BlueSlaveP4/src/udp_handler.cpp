// =============================================================================
// udp_handler.cpp — WiFi/UDP communication to Master via ESP32-C6
// P4 connects directly to Master AP (RED808) through SDIO ESP-Hosted
// =============================================================================

#include "udp_handler.h"
#include "uart_handler.h"   // for P4State p4
#include "ui/ui_screens.h"
#include "../include/config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
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
static bool sessionCleanSent = false;
// Per-track timestamps of last LOCAL mute/solo change. Used to suppress the
// state_sync broadcast from the master clobbering user input within a short
// window (the broadcast can race with our own UDP commands).
static unsigned long lastLocalMuteMs[16] = {};
static unsigned long lastLocalSoloMs[16] = {};
// Master broadcasts state_sync every 2 s. Pick a window > broadcast period so
// any state_sync arriving right after a P4-initiated SOLO/UNMUTE-ALL is
// suppressed (the master already has the correct state — re-applying it just
// causes UI flicker). Keep it below 2 s to improve responsiveness while still
// filtering near-race packets.
static const unsigned long LOCAL_OWNERSHIP_MS = 1800;
static int pendingPatternRequest = -1;
static unsigned long pendingPatternLastTxMs = 0;
static uint8_t pendingPatternRetries = 0;
static bool patternRowGrid[16][64] = {};
static uint16_t patternRowMask = 0;
static int patternRowPattern = -1;
static int patternRowStepCount = 16;

// JSON parse buffer
static char rxBuf[8192];

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

static bool pattern_grid_has_data(void) {
    for (int t = 0; t < 16; t++) {
        for (int s = 0; s < 16; s++) {
            if (p4.steps[t][s]) return true;
        }
    }
    return false;
}

static int hex_nibble_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void decode_pattern_row_hex(const char* row, bool* dest, int stepCount) {
    if (!row || !dest) return;
    for (int s = 0; s < 64; s++) dest[s] = false;
    int nibbles = (stepCount + 3) / 4;
    if (nibbles > 16) nibbles = 16;
    for (int nib = 0; row[nib] && nib < nibbles; nib++) {
        int value = hex_nibble_value(row[nib]);
        if (value < 0) continue;
        int baseStep = nib * 4;
        for (int bit = 0; bit < 4; bit++) {
            int step = baseStep + bit;
            if (step >= stepCount || step >= 64) break;
            dest[step] = ((value >> bit) & 0x01) != 0;
        }
    }
}

static bool fetch_pattern_http(int pattern) {
    if (!wifiConnected) return false;

    char url[96];
    snprintf(url, sizeof(url), "http://192.168.4.1/api/getPattern?index=%d", pattern);

    HTTPClient http;
    http.setTimeout(900);
    if (!http.begin(url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        P4_LOG_PRINTF("[HTTP][PAT] parse error: %s\n", err.c_str());
        return false;
    }

    bool raw_steps[16][64] = {};
    int raw_len = 16;
    for (int track = 0; track < 16; track++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", track);
        JsonArray row = doc[key];
        if (!row) continue;
        int step = 0;
        for (JsonVariant val : row) {
            if (step >= 64) break;
            raw_steps[track][step] = val.as<bool>();
            step++;
        }
        if (step > raw_len) raw_len = step;
    }

    p4.current_pattern = clamp_int(pattern, 0, 15);
    ui_sequencer_load_external_pattern(raw_steps, clamp_int(raw_len, 16, 64));
    pendingPatternRequest = -1;
    pendingPatternRetries = 0;
    pendingPatternLastTxMs = 0;
    masterAlive = true;
    p4.master_connected = true;
    uart_send_pattern_to_s3(p4.current_pattern, p4.steps);
    P4_LOG_PRINTF("[HTTP][PAT] loaded pattern %d len=%d\n", pattern, raw_len);
    return true;
}

static bool fetch_master_state_http(void) {
    if (!wifiConnected) return false;

    HTTPClient http;
    http.setTimeout(900);
    if (!http.begin("http://192.168.4.1/api/p4State")) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        P4_LOG_PRINTF("[HTTP][STATE] parse error: %s\n", err.c_str());
        return false;
    }

    int pat = clamp_int(doc["pattern"] | p4.current_pattern, 0, 15);
    p4.current_pattern = pat;
    uart_send_to_s3(MSG_SYSTEM, SYS_PATTERN, (uint8_t)pat);

    bool playing = doc["playing"] | p4.is_playing;
    p4.is_playing = playing;
    uart_send_to_s3(MSG_SYSTEM, SYS_PLAY_STATE, playing ? 1 : 0);

    float bpm = clamp_float(doc["tempo"] | (float)(p4.bpm_int + p4.bpm_frac * 0.1f), 40.0f, 240.0f);
    p4.bpm_int = (int)bpm;
    p4.bpm_frac = (int)((bpm - p4.bpm_int) * 10.0f);
    uart_send_to_s3(MSG_SYSTEM, SYS_BPM_INT, (uint8_t)p4.bpm_int);
    uart_send_to_s3(MSG_SYSTEM, SYS_BPM_FRAC, (uint8_t)p4.bpm_frac);

    int masterVol = clamp_int(doc["masterVolume"] | p4.master_volume, 0, 150);
    p4.master_volume = masterVol;
    p4.seq_volume = clamp_int(doc["sequencerVolume"] | p4.seq_volume, 0, 150);
    p4.live_volume = clamp_int(doc["liveVolume"] | p4.live_volume, 0, 150);
    uart_send_to_s3(MSG_SYSTEM, SYS_VOLUME, (uint8_t)masterVol);
    uart_send_to_s3(MSG_SYSTEM, SYS_SEQ_VOL, (uint8_t)p4.seq_volume);
    uart_send_to_s3(MSG_SYSTEM, SYS_LIVE_VOL, (uint8_t)p4.live_volume);

    JsonArray mute = doc["mute"];
    if (mute) {
        unsigned long nowMs = millis();
        int track = 0;
        for (JsonVariant value : mute) {
            if (track >= 16) break;
            if (nowMs - lastLocalMuteMs[track] < LOCAL_OWNERSHIP_MS) {
                bool incoming = value.as<bool>();
                if (incoming != p4.track_muted[track]) {
                    P4_LOG_PRINTF("[SYNC][MUTE] keep-local t=%d incoming=%d local=%d age=%lu\n",
                                  track, (int)incoming, (int)p4.track_muted[track],
                                  (unsigned long)(nowMs - lastLocalMuteMs[track]));
                }
                track++;
                continue;
            }
            bool v = value.as<bool>();
            if (p4.track_muted[track] != v) {
                P4_LOG_PRINTF("[SYNC][MUTE] apply-master t=%d old=%d new=%d age=%lu\n",
                              track, (int)p4.track_muted[track], (int)v,
                              (unsigned long)(nowMs - lastLocalMuteMs[track]));
                p4.track_muted[track] = v;
                uart_send_to_s3(MSG_TRACK, TRK_MUTE_BIT | (track & 0x0F), v ? 1 : 0);
            }
            track++;
        }
    }

    JsonArray solo = doc["solo"];
    if (solo) {
        unsigned long nowMs = millis();
        int track = 0;
        for (JsonVariant value : solo) {
            if (track >= 16) break;
            if (nowMs - lastLocalSoloMs[track] < LOCAL_OWNERSHIP_MS) {
                bool incoming = value.as<bool>();
                if (incoming != p4.track_solo[track]) {
                    P4_LOG_PRINTF("[SYNC][SOLO] keep-local t=%d incoming=%d local=%d age=%lu\n",
                                  track, (int)incoming, (int)p4.track_solo[track],
                                  (unsigned long)(nowMs - lastLocalSoloMs[track]));
                }
                track++;
                continue;
            }
            bool v = value.as<bool>();
            if (p4.track_solo[track] != v) {
                P4_LOG_PRINTF("[SYNC][SOLO] apply-master t=%d old=%d new=%d age=%lu\n",
                              track, (int)p4.track_solo[track], (int)v,
                              (unsigned long)(nowMs - lastLocalSoloMs[track]));
                p4.track_solo[track] = v;
                uart_send_to_s3(MSG_TRACK, TRK_SOLO_BIT | (track & 0x0F), v ? 1 : 0);
            }
            track++;
        }
    }

    JsonArray volumes = doc["trackVolumes"];
    if (volumes) {
        int track = 0;
        for (JsonVariant value : volumes) {
            if (track >= 16) break;
            int vol = clamp_int(value.as<int>(), 0, 150);
            p4.track_volume[track] = vol;
            uart_send_to_s3(MSG_TRACK, TRK_VOLUME | (track & 0x0F), (uint8_t)vol);
            track++;
        }
    }

    JsonObject fx = doc["fx"];
    if (fx) {
        p4.filter_type = clamp_int(fx["filterType"] | p4.filter_type, 0, 4);
        p4.cutoff_hz = clamp_int(fx["filterCutoff"] | p4.cutoff_hz, 20, 20000);
        float resonance = clamp_float(fx["filterResonance"] | ((float)p4.resonance_x10 / 10.0f), 1.0f, 10.0f);
        p4.resonance_x10 = (int)(resonance * 10.0f);
        float distortion = clamp_float(fx["distortion"] | ((float)p4.distortion_pct / 100.0f), 0.0f, 1.0f);
        p4.distortion_pct = (int)(distortion * 100.0f);
        p4.bitcrush_bits = clamp_int(fx["bitCrush"] | p4.bitcrush_bits, 4, 16);
        p4.sample_rate_hz = clamp_int(fx["sampleRate"] | p4.sample_rate_hz, 1000, 48000);
    }

    const char* kit = doc["kit"] | "";
    strncpy(p4.kit_name, kit, sizeof(p4.kit_name) - 1);
    p4.kit_name[sizeof(p4.kit_name) - 1] = '\0';

    masterAlive = true;
    p4.master_connected = true;
    P4_LOG_PRINTF("[HTTP][STATE] loaded pat=%d bpm=%.1f\n", pat, bpm);
    return true;
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

void udp_send_synth_note_on_ex(uint8_t engine, uint8_t note, uint8_t velocity,
                                bool accent, bool slide) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"synthNoteOnEx\",\"engine\":%u,\"note\":%u,\"velocity\":%u,"
             "\"accent\":%s,\"slide\":%s}",
             (unsigned)engine, (unsigned)note, (unsigned)velocity,
             accent ? "true" : "false", slide ? "true" : "false");
    sendJson(buf);
}

void udp_send_synth_note_off(uint8_t engine, uint8_t track) {
    char buf[80];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"synthNoteOff\",\"engine\":%u,\"track\":%u}",
             (unsigned)engine, (unsigned)track);
    sendJson(buf);
}

void udp_send_synth303_note_off(void) {
    sendJson("{\"cmd\":\"synth303NoteOff\"}");
}

void udp_send_synth_param(uint8_t engine, uint8_t instrument, uint8_t paramId, float value) {
    char buf[160];
    if (engine == 3) {
        // TB-303 uses dedicated command
        snprintf(buf, sizeof(buf),
                 "{\"cmd\":\"synth303Param\",\"paramId\":%u,\"value\":%.4f}",
                 (unsigned)paramId, value);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"cmd\":\"synthParam\",\"engine\":%u,\"instrument\":%u,\"paramId\":%u,\"value\":%.4f}",
                 (unsigned)engine, (unsigned)instrument, (unsigned)paramId, value);
    }
    sendJson(buf);
}

void udp_send_synth_preset(uint8_t engine, uint8_t preset) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"synthPreset\",\"engine\":%u,\"preset\":%u}",
             (unsigned)engine, (unsigned)preset);
    sendJson(buf);
}

void udp_send_melody_rec_note(uint8_t engine, uint8_t note) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"melodyRecNote\",\"engine\":%u,\"note\":%u}",
             (unsigned)engine, (unsigned)note);
    sendJson(buf);
}

// v2.9 — Master now keeps melody state. Tell it REC is toggled and which
// engine/octave we're using so it can mirror the change to all slaves.
void udp_send_melody_rec_toggle(bool active, uint8_t engine, uint8_t octave) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"melodyRecToggle\",\"active\":%d,\"engine\":%u,\"octave\":%u}",
             active ? 1 : 0, (unsigned)engine, (unsigned)octave);
    sendJson(buf);
}

void udp_send_melody_set_pad(uint8_t pad) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"melodySetPad\",\"pad\":%u}", (unsigned)pad);
    sendJson(buf);
}

void udp_send_melody_set_engine(uint8_t engine) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"melodySetEngine\",\"engine\":%u}", (unsigned)engine);
    sendJson(buf);
}

void udp_send_melody_set_octave(uint8_t octave) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"melodySetOctave\",\"octave\":%u}", (unsigned)octave);
    sendJson(buf);
}

void udp_send_melody_clear(void) {
    sendJson("{\"cmd\":\"melodyClear\"}");
}

void udp_send_melody_assign_pad(uint8_t pad, uint8_t engine, uint8_t octave) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"melodyAssign\",\"pad\":%u,\"engine\":%u,\"octave\":%u}",
             (unsigned)pad, (unsigned)engine, (unsigned)octave);
    sendJson(buf);
}

void udp_send_melody_assign(uint8_t pad, uint8_t engine, uint8_t octave,
                            const bool grid[16][12]) {
    // Mirror the JSON shape produced by S3's ui_screens.cpp mel_assign_cb:
    //   {"cmd":"melodyAssign","pad":N,"engine":E,"octave":O,
    //    "steps":[[midi,midi,...], ... 16 columns]}
    char buf[768];
    int n = snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"melodyAssign\",\"pad\":%u,\"engine\":%u,"
                     "\"octave\":%u,\"steps\":[",
                     (unsigned)pad, (unsigned)engine, (unsigned)octave);
    if (n < 0 || n >= (int)sizeof(buf)) return;
    // S3 mapping: row r -> pc = (11 - r), midi = (octave + 1) * 12 + pc
    for (int c = 0; c < 16; c++) {
        if (n >= (int)sizeof(buf) - 8) return;
        if (c > 0) buf[n++] = ',';
        buf[n++] = '[';
        bool first = true;
        for (int r = 0; r < 12; r++) {
            if (!grid[c][r]) continue;
            int pc = 11 - r;
            int midi = ((int)octave + 1) * 12 + pc;
            if (midi < 0) midi = 0;
            if (midi > 127) midi = 127;
            int w = snprintf(buf + n, sizeof(buf) - n,
                             "%s%d", first ? "" : ",", midi);
            if (w < 0 || w >= (int)(sizeof(buf) - n)) return;
            n += w;
            first = false;
        }
        if (n >= (int)sizeof(buf) - 4) return;
        buf[n++] = ']';
    }
    if (n >= (int)sizeof(buf) - 3) return;
    buf[n++] = ']';
    buf[n++] = '}';
    buf[n] = 0;
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
    P4_LOG_PRINTF("[UDP][PAT] TX selectPattern idx=%d\n", index);
    sendJson(buf);
}

void udp_send_get_pattern(int pattern) {
    char buf[64];
    if (pendingPatternRequest != pattern) {
        pendingPatternRequest = pattern;
        pendingPatternRetries = 0;
    }
    snprintf(buf, sizeof(buf), "{\"cmd\":\"get_pattern\",\"pattern\":%d}", pattern);
    pendingPatternLastTxMs = millis();
    if (pendingPatternRetries < 255) pendingPatternRetries++;
    P4_LOG_PRINTF("[UDP][PAT] TX get_pattern idx=%d\n", pattern);
    sendJson(buf);
    fetch_pattern_http(pattern);
}

void udp_send_set_step(int track, int step, bool active) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"setStep\",\"track\":%d,\"step\":%d,\"active\":%s}",
             track, step, active ? "true" : "false");
    sendJson(buf);
}

void udp_send_set_step_velocity(int track, int step, int velocity) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"setStepVelocity\",\"track\":%d,\"step\":%d,\"velocity\":%d}",
             track, step, constrain(velocity, 1, 127));
    sendJson(buf);
}

void udp_send_mute(int track, bool muted) {
    if (track >= 0 && track < 16) lastLocalMuteMs[track] = millis();
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"mute\",\"track\":%d,\"value\":%s}",
             track, muted ? "true" : "false");
    sendJson(buf);
}

void udp_send_solo(int track, bool soloed) {
    if (track >= 0 && track < 16) {
        // Solo touches every track's solo + mute state on the master side.
        // Mark all 16 entries to suppress racing state_sync overwrites.
        unsigned long now = millis();
        for (int t = 0; t < 16; t++) {
            lastLocalSoloMs[t] = now;
            lastLocalMuteMs[t] = now;
        }
    }
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"solo\",\"track\":%d,\"value\":%s}",
             track, soloed ? "true" : "false");
    sendJson(buf);
}

void udp_send_mute_mask(uint16_t mask) {
    unsigned long now = millis();
    for (int t = 0; t < 16; t++) lastLocalMuteMs[t] = now;
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"setMuteMask\",\"mask\":%u}", (unsigned)mask);
    sendJson(buf);
}

void udp_send_solo_mask(uint16_t mask) {
    unsigned long now = millis();
    for (int t = 0; t < 16; t++) {
        lastLocalSoloMs[t] = now;
        lastLocalMuteMs[t] = now;
    }
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"setSoloMask\",\"mask\":%u}", (unsigned)mask);
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

void udp_send_set_track_engine(int track, int engine) {
    if (track < 0) track = 0;
    if (track > 15) track = 15;
    if (engine < -1) engine = -1;
    if (engine > 6) engine = 6;
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"setTrackSynthEngine\",\"track\":%d,\"engine\":%d}",
             track, engine);
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
// Ranges are intentionally conservative: audible first, no harsh clipping.
// Params resent periodically to survive UDP packet drops.
// =============================================================================
void udp_send_fx_enc(int enc_id, uint8_t value, bool muted) {
    if (!udpStarted) return;
    if (enc_id < 0 || enc_id > 2) return;

    char buf[96];
    bool active = (!muted && value > 0);
    float norm = (float)value / 127.0f;
    bool fullSend = active;

    P4_LOG_PRINTF("[FX] enc%d val=%d muted=%d active=%d norm=%.2f full=%d\n",
                  enc_id, value, muted, active, norm, fullSend);

    switch (enc_id) {
        case 0: // Flanger — stronger modulation than chorus, with capped feedback/mix.
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setFlangerActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                int rate_pct = clamp_int((int)(8.0f + norm * 44.0f + 0.5f), 8, 52);
                int depth_pct = clamp_int((int)(18.0f + norm * 62.0f + 0.5f), 18, 80);
                int feedback_pct = clamp_int((int)(8.0f + norm * 30.0f + 0.5f), 8, 38);
                int mix_pct = clamp_int((int)(10.0f + norm * 42.0f + 0.5f), 10, 52);
                if (fullSend) {
                    snprintf(buf, sizeof(buf), "{\"cmd\":\"setFlangerRate\",\"value\":%d}", rate_pct);
                    sendJson(buf);
                    snprintf(buf, sizeof(buf), "{\"cmd\":\"setFlangerDepth\",\"value\":%d}", depth_pct);
                    sendJson(buf);
                    snprintf(buf, sizeof(buf), "{\"cmd\":\"setFlangerFeedback\",\"value\":%d}", feedback_pct);
                    sendJson(buf);
                }
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setFlangerMix\",\"value\":%d}", mix_pct);
                sendJson(buf);
            }
            break;
        case 1: // Delay — unmistakable echo effect
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setDelayActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                int delay_ms = clamp_int((int)(90.0f + norm * 650.0f + 0.5f), 60, 900);
                int fb_pct = clamp_int((int)(14.0f + norm * 56.0f + 0.5f), 0, 85);
                int mix_pct = clamp_int((int)(8.0f + norm * 50.0f + 0.5f), 0, 100);
                if (fullSend) {
                    snprintf(buf, sizeof(buf), "{\"cmd\":\"setDelayTime\",\"value\":%d}", delay_ms);
                    sendJson(buf);
                    snprintf(buf, sizeof(buf), "{\"cmd\":\"setDelayFeedback\",\"value\":%d}", fb_pct);
                    sendJson(buf);
                    sendJson("{\"cmd\":\"setDelayStereo\",\"mode\":1}");
                }
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setDelayMix\",\"value\":%d}", mix_pct);
                sendJson(buf);
            }
            break;
        case 2: // Reverb — unmistakable room/hall effect
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setReverbActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                float feedback = 0.28f + norm * 0.48f;
                int lp_hz = clamp_int((int)(2200.0f + norm * 6800.0f + 0.5f), 1800, 12000);
                float early_mix = 0.08f + norm * 0.18f;
                float mix = 0.06f + norm * 0.42f;
                if (fullSend) {
                    snprintf(buf, sizeof(buf), "{\"cmd\":\"setReverbFeedback\",\"value\":%.3f}", feedback);
                    sendJson(buf);
                    snprintf(buf, sizeof(buf), "{\"cmd\":\"setReverbLpFreq\",\"value\":%d}", lp_hz);
                    sendJson(buf);
                    sendJson("{\"cmd\":\"setEarlyRefActive\",\"active\":1}");
                    snprintf(buf, sizeof(buf), "{\"cmd\":\"setEarlyRefMix\",\"mix\":%d}", clamp_int((int)(early_mix * 100.0f + 0.5f), 0, 100));
                    sendJson(buf);
                }
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setReverbMix\",\"value\":%.3f}", mix);
                sendJson(buf);
            }
            break;
    }
}

void udp_send_fx_pot(int pot_id, uint8_t value, bool muted) {
    if (!udpStarted) return;
    char buf[96];
    float norm = (float)value / 127.0f;
    switch (pot_id) {
        case 0: {  // FOLD macro: mild wavefolder drive, reset to 1.0 when muted.
            if (muted) {
                sendJson("{\"cmd\":\"setWavefolderGain\",\"value\":1.0}");
                break;
            }
            float gain = 1.0f + norm * 2.25f;
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setWavefolderGain\",\"value\":%.2f}", gain);
            sendJson(buf);
            break;
        }
        case 1: {  // CRUSH macro: bit depth and sample-rate reduction, no gain boost.
            if (muted || value == 0) {
                sendJson("{\"cmd\":\"setBitCrush\",\"value\":16}");
                sendJson("{\"cmd\":\"setSampleRate\",\"value\":0}");
                sendJson("{\"cmd\":\"setDistortion\",\"value\":0.0}");
                break;
            }
            int bits = clamp_int((int)(16.0f - norm * 8.0f + 0.5f), 8, 16);
            int sr = clamp_int((int)(32000.0f - norm * 22000.0f + 0.5f), 9000, 32000);
            float dist = norm * 0.18f;
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setBitCrush\",\"value\":%d}", bits);
            sendJson(buf);
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setSampleRate\",\"value\":%d}", sr);
            sendJson(buf);
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setDistortion\",\"value\":%.3f}", dist);
            sendJson(buf);
            break;
        }
        case 2: {  // PHASER macro: audible sweep without the tremolo/limiter gain issues.
            bool active = (!muted && value > 0);
            snprintf(buf, sizeof(buf), "{\"cmd\":\"setPhaserActive\",\"value\":%d}", active ? 1 : 0);
            sendJson(buf);
            if (active) {
                int rate_pct = clamp_int((int)(10.0f + norm * 55.0f + 0.5f), 10, 65);
                int depth_pct = clamp_int((int)(28.0f + norm * 60.0f + 0.5f), 28, 88);
                int feedback_pct = clamp_int((int)(8.0f + norm * 34.0f + 0.5f), 8, 42);
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setPhaserRate\",\"value\":%d}", rate_pct);
                sendJson(buf);
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setPhaserDepth\",\"value\":%d}", depth_pct);
                sendJson(buf);
                snprintf(buf, sizeof(buf), "{\"cmd\":\"setPhaserFeedback\",\"value\":%d}", feedback_pct);
                sendJson(buf);
            }
            break;
        }
    }
}

// Kept for the WiFi reconnect path; current FX macros resend their full state directly.
void udp_reset_fx_latch(void) {}

// =============================================================================
// SYNC REQUEST — handshake + request initial data
// =============================================================================
void udp_request_master_sync(void) {
    P4_LOG_PRINTLN("[UDP] Requesting Master sync...");
    sendJson("{\"cmd\":\"hello\",\"device\":\"P4_DISPLAY\"}");

    if (!sessionCleanSent) {
        // One-time boot/session init only. Do NOT repeat this on every
        // reconnect/timeout because that force-clears mute/solo visuals.
        for (int track = 0; track < 16; track++) {
            p4.track_solo[track] = false;
            p4.track_muted[track] = false;
            uart_send_to_s3(MSG_TRACK, TRK_MUTE_BIT | (track & 0x0F), 0);
            uart_send_to_s3(MSG_TRACK, TRK_SOLO_BIT | (track & 0x0F), 0);
        }
        sessionCleanSent = true;
    }

    fetch_master_state_http();
    udp_send_get_pattern(p4.current_pattern);
    sendJson("{\"cmd\":\"getTrackVolumes\"}");

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

    if (strcmp(cmd, "state_sync") == 0) {
        int pat = clamp_int(doc["pattern"] | p4.current_pattern, 0, 15);
        bool patternChanged = (pat != p4.current_pattern);
        p4.current_pattern = pat;
        uart_send_to_s3(MSG_SYSTEM, SYS_PATTERN, (uint8_t)pat);
        // Only re-fetch grid when pattern actually changed. Re-fetching on
        // every state_sync (every 2 s) caused full sequencer repaints and
        // visible flicker that fought against local mute/solo edits.
        if (patternChanged) {
            udp_send_get_pattern(pat);
        }

        bool playing = doc["playing"] | p4.is_playing;
        p4.is_playing = playing;
        uart_send_to_s3(MSG_SYSTEM, SYS_PLAY_STATE, playing ? 1 : 0);

        float bpm = clamp_float(doc["tempo"] | (float)(p4.bpm_int + p4.bpm_frac * 0.1f), 40.0f, 240.0f);
        p4.bpm_int = (int)bpm;
        p4.bpm_frac = (int)((bpm - p4.bpm_int) * 10.0f);
        uart_send_to_s3(MSG_SYSTEM, SYS_BPM_INT, (uint8_t)p4.bpm_int);
        uart_send_to_s3(MSG_SYSTEM, SYS_BPM_FRAC, (uint8_t)p4.bpm_frac);

        int masterVol = clamp_int(doc["masterVolume"] | p4.master_volume, 0, 150);
        p4.master_volume = masterVol;
        p4.seq_volume = clamp_int(doc["sequencerVolume"] | p4.seq_volume, 0, 150);
        p4.live_volume = clamp_int(doc["liveVolume"] | p4.live_volume, 0, 150);
        uart_send_to_s3(MSG_SYSTEM, SYS_VOLUME, (uint8_t)masterVol);
        uart_send_to_s3(MSG_SYSTEM, SYS_SEQ_VOL, (uint8_t)p4.seq_volume);
        uart_send_to_s3(MSG_SYSTEM, SYS_LIVE_VOL, (uint8_t)p4.live_volume);

        // P4 owns mute/solo visuals once the session is running. Applying the
        // periodic 2 s state_sync mix arrays here causes visible flicker when
        // they race with local touch edits or delayed master-side state. The
        // initial HTTP sync still seeds these values on connect/reconnect.

        JsonArray volumes = doc["trackVolumes"];
        if (volumes) {
            int track = 0;
            for (JsonVariant value : volumes) {
                if (track >= 16) break;
                int vol = clamp_int(value.as<int>(), 0, 150);
                if (p4.track_volume[track] != vol) {
                    p4.track_volume[track] = vol;
                    uart_send_to_s3(MSG_TRACK, TRK_VOLUME | (track & 0x0F), (uint8_t)vol);
                }
                track++;
            }
        }

        JsonObject fx = doc["fx"];
        if (fx) {
            p4.filter_type = clamp_int(fx["filterType"] | p4.filter_type, 0, 4);
            p4.cutoff_hz = clamp_int(fx["filterCutoff"] | p4.cutoff_hz, 20, 20000);
            float resonance = clamp_float(fx["filterResonance"] | ((float)p4.resonance_x10 / 10.0f), 1.0f, 10.0f);
            p4.resonance_x10 = (int)(resonance * 10.0f);
            float distortion = clamp_float(fx["distortion"] | ((float)p4.distortion_pct / 100.0f), 0.0f, 1.0f);
            p4.distortion_pct = (int)(distortion * 100.0f);
            p4.bitcrush_bits = clamp_int(fx["bitCrush"] | p4.bitcrush_bits, 4, 16);
            p4.sample_rate_hz = clamp_int(fx["sampleRate"] | p4.sample_rate_hz, 1000, 48000);
        }

        const char* kit = doc["kit"] | "";
        strncpy(p4.kit_name, kit, sizeof(p4.kit_name) - 1);
        p4.kit_name[sizeof(p4.kit_name) - 1] = '\0';
        memset(p4.sample_loaded, 0, sizeof(p4.sample_loaded));
        JsonArray samples = doc["samples"];
        if (samples) {
            for (JsonObject sample : samples) {
                int pad = sample["pad"] | -1;
                if (pad < 0 || pad >= 24) continue;
                p4.sample_loaded[pad] = sample["loaded"] | false;
                const char* name = sample["name"] | "";
                strncpy(p4.sample_name[pad], name, sizeof(p4.sample_name[pad]) - 1);
                p4.sample_name[pad][sizeof(p4.sample_name[pad]) - 1] = '\0';
            }
        }
        return;
    }

    // ----- Pattern sync -----
    if (strcmp(cmd, "pattern_sync") == 0) {
        int pat = clamp_int(doc["pattern"] | p4.current_pattern, 0, 15);
        bool requested_pattern = (pendingPatternRequest == pat);
        bool matches_current   = (pat == p4.current_pattern);
        bool active_hint       = doc["active"] | false;
        // Accept the payload if (a) it matches our pending request, (b) it
        // matches the pattern we are currently displaying, or (c) the master
        // marked the broadcast as the new active pattern. Otherwise ignore.
        if (!requested_pattern && !matches_current && !active_hint) {
            P4_LOG_PRINTF("[UDP][PAT] RX stale pattern_sync pat=%d cur=%d waiting=%d (ignored)\n",
                          pat, p4.current_pattern, pendingPatternRequest);
            return;
        }
        int raw_len = clamp_int(doc["stepCount"] | 16, 16, 64);
        bool raw_steps[16][64] = {};
        P4_LOG_PRINTF("[UDP][PAT] RX pattern_sync pat=%d raw=%d requested=%d\n",
                      pat, raw_len, requested_pattern ? 1 : 0);
        JsonArray rows = doc["rows"];
        JsonArray data = doc["data"];
        if (rows) {
            p4.current_pattern = pat;
            int track = 0;
            for (JsonVariant rowVal : rows) {
                if (track >= 16) break;
                const char* row = rowVal | "";
                for (int nib = 0; row[nib] && nib < 16; nib++) {
                    int value = hex_nibble_value(row[nib]);
                    if (value < 0) continue;
                    int baseStep = nib * 4;
                    for (int bit = 0; bit < 4; bit++) {
                        int step = baseStep + bit;
                        if (step >= 64) break;
                        raw_steps[track][step] = ((value >> bit) & 0x01) != 0;
                    }
                }
                track++;
            }
            ui_sequencer_load_external_pattern(raw_steps, raw_len);
        } else if (data) {
            bool nestedRows = false;
            if (!data.isNull() && data.size() > 0) {
                nestedRows = data[0].is<JsonArray>();
            }

            // Check if incoming payload is all-empty (supports 2D and flat).
            bool incomingEmpty = true;
            if (nestedRows) {
                for (JsonArray row : data) {
                    for (JsonVariant val : row) {
                        if (val.as<int>() != 0) { incomingEmpty = false; break; }
                    }
                    if (!incomingEmpty) break;
                }
            } else {
                for (JsonVariant val : data) {
                    if (val.as<int>() != 0) { incomingEmpty = false; break; }
                }
            }

            if (incomingEmpty) {
                P4_LOG_PRINTF("[UDP][PAT] RX empty payload pat=%d (applied)\n", pat);
            }

            p4.current_pattern = pat;

            if (nestedRows) {
                int track = 0;
                for (JsonArray row : data) {
                    if (track >= 16) break;
                    int step = 0;
                    for (JsonVariant val : row) {
                        if (step >= 64) break;
                        raw_steps[track][step] = (val.as<int>() != 0);
                        step++;
                    }
                    track++;
                }
            } else {
                // Flat payload fallback: track-major [t0s0..t0s63, t1s0..]
                int idx = 0;
                for (JsonVariant val : data) {
                    if (idx >= 1024) break;
                    int track = idx / 64;
                    int step  = idx % 64;
                    if (track >= 16) break;
                    raw_steps[track][step] = (val.as<int>() != 0);
                    idx++;
                }
            }
            ui_sequencer_load_external_pattern(raw_steps, raw_len);
        } else {
            p4.current_pattern = pat;
            ui_sequencer_sync_from_current_pattern();
        }
        pendingPatternRequest = -1;
        pendingPatternRetries = 0;
        pendingPatternLastTxMs = 0;
        // Forward pattern data to S3 so it can sync its sequencer + pad-sync
        uart_send_pattern_to_s3(pat, p4.steps);
    }
    else if (strcmp(cmd, "pattern_row") == 0) {
        int pat = clamp_int(doc["pattern"] | p4.current_pattern, 0, 15);
        int track = clamp_int(doc["track"] | -1, -1, 15);
        int raw_len = clamp_int(doc["stepCount"] | 16, 16, 64);
        const char* row = doc["row"] | "";
        bool requested_pattern = (pendingPatternRequest == pat);
        bool matches_current = (pat == p4.current_pattern);
        if (track < 0 || (!requested_pattern && !matches_current)) {
            return;
        }
        if (patternRowPattern != pat || patternRowStepCount != raw_len) {
            memset(patternRowGrid, 0, sizeof(patternRowGrid));
            patternRowMask = 0;
            patternRowPattern = pat;
            patternRowStepCount = raw_len;
        }
        decode_pattern_row_hex(row, patternRowGrid[track], raw_len);
        patternRowMask |= (uint16_t)(1U << track);
        p4.current_pattern = pat;
        if (patternRowMask == 0xFFFFU) {
            ui_sequencer_load_external_pattern(patternRowGrid, raw_len);
            pendingPatternRequest = -1;
            pendingPatternRetries = 0;
            pendingPatternLastTxMs = 0;
            patternRowMask = 0;
            uart_send_pattern_to_s3(pat, p4.steps);
        }
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
        uart_send_to_s3(MSG_SYSTEM, SYS_VOLUME, (uint8_t)v);
        uart_send_to_s3(MSG_SYSTEM, SYS_SEQ_VOL, (uint8_t)v);
        uart_send_to_s3(MSG_SYSTEM, SYS_LIVE_VOL, (uint8_t)v);
    }
    else if (strcmp(cmd, "volume_seq_sync") == 0 || strcmp(cmd, "setSequencerVolume") == 0) {
        p4.seq_volume = clamp_int(doc["value"] | 75, 0, 150);
        uart_send_to_s3(MSG_SYSTEM, SYS_SEQ_VOL, (uint8_t)p4.seq_volume);
    }
    else if (strcmp(cmd, "volume_live_sync") == 0 || strcmp(cmd, "setLiveVolume") == 0) {
        p4.live_volume = clamp_int(doc["value"] | 75, 0, 150);
        uart_send_to_s3(MSG_SYSTEM, SYS_LIVE_VOL, (uint8_t)p4.live_volume);
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
                uart_send_to_s3(MSG_TRACK, TRK_VOLUME | (i & 0x0F), (uint8_t)p4.track_volume[i]);
                i++;
            }
        }
    }
    else if (strcmp(cmd, "trackVolume") == 0 || strcmp(cmd, "getTrackVolume") == 0) {
        int trk = doc["track"] | 0;
        int vol = clamp_int(doc["volume"] | doc["value"] | 75, 0, 150);
        if (trk >= 0 && trk < 16) {
            p4.track_volume[trk] = vol;
            uart_send_to_s3(MSG_TRACK, TRK_VOLUME | (trk & 0x0F), (uint8_t)vol);
        }
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
            extern void ui_sequencer_sync_from_current_pattern(void);
            ui_sequencer_sync_from_current_pattern();
            udp_send_get_pattern(idx);
        } else if (!pattern_grid_has_data()) {
            // Refresh if we stayed on the same pattern but local grid is empty.
            udp_send_get_pattern(idx);
        }
    }
    // v2.9 — master-authoritative melody state: defer P4 piano UI apply
    // to the LVGL task. UDP/Core1 must never block on the LVGL mutex.
    else if (strcmp(cmd, "melody_sync") == 0) {
        uint8_t eng = (uint8_t)(doc["engine"] | 3);
        uint8_t oct = (uint8_t)(doc["octave"] | 4);
        bool    rec = doc["rec"] | false;
        uint8_t pad = (uint8_t)(doc["pad"]    | 0);
        g_pending_melody_from_s3.engine = eng;
        g_pending_melody_from_s3.octave = oct;
        g_pending_melody_from_s3.rec    = rec ? 1 : 0;
        g_pending_melody_from_s3.pad    = pad;
        g_pending_melody_from_s3.pending = true;
        // v2.9 — forward melody state to S3 so its melody screen stays in sync.
        // S3 has no WiFi; it receives these via UART and applies under LVGL lock.
        uart_send_to_s3(MSG_TOUCH_CMD, TCMD_MELODY_ENGINE, eng);
        uart_send_to_s3(MSG_TOUCH_CMD, TCMD_MELODY_OCTAVE, oct);
        uart_send_to_s3(MSG_TOUCH_CMD, TCMD_MELODY_REC,    rec ? 1 : 0);
        uart_send_to_s3(MSG_TOUCH_CMD, TCMD_MELODY_PAD,    pad);
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

    // --- Periodic heartbeat: keep P4 in master's udpClients while alive ---
    // Without this, P4 is removed after UDP_CLIENT_TIMEOUT (30 s) of inactivity
    // and stops receiving melody_sync broadcasts.
    {
        static unsigned long lastPingMs = 0;
        if (udpStarted && masterAlive && (now - lastPingMs >= 5000)) {
            lastPingMs = now;
            sendCmd("ping");
        }
    }

    // --- Pattern request watchdog ---
    // If the selected pattern wasn't confirmed via pattern_sync, re-request it
    // at a controlled rate so UI/audio don't stay stuck on old steps.
    if (udpStarted && masterAlive && pendingPatternRequest >= 0) {
        if (now - pendingPatternLastTxMs >= 250 && pendingPatternRetries < 12) {
            if ((pendingPatternRetries % 3) == 0) {
                udp_send_select_pattern(pendingPatternRequest);
            }
            udp_send_get_pattern(pendingPatternRequest);
        } else if (pendingPatternRetries >= 12) {
            P4_LOG_PRINTF("[UDP][PAT] timeout waiting pattern_sync idx=%d\n",
                          pendingPatternRequest);
            pendingPatternRequest = -1;
            pendingPatternRetries = 0;
            pendingPatternLastTxMs = 0;
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
