// =============================================================================
// BlueSlaveV2 - minimal WiFi + text UI firmware
// Waveshare ESP32-S3-LCD-7B (landscape, no touch)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <M5ROTATE8.h>

#include "../include/config.h"
#include "drivers/i2c_driver.h"
#include "drivers/io_extension.h"
#include "drivers/rgb_lcd.h"
#include "drivers/lvgl_port.h"
#include "lvgl.h"

namespace {

static constexpr uint8_t BYTEBUTTON_STATUS_REG = 0x00;
static constexpr uint8_t BYTEBUTTON_STATUS_8BYTE_REG = 0x60;
static constexpr uint8_t BYTEBUTTON_LED_BRIGHTNESS_REG = 0x10;
static constexpr uint8_t BYTEBUTTON_LED_SHOW_MODE_REG = 0x19;
static constexpr uint8_t BYTEBUTTON_LED_RGB888_REG = 0x20;
static constexpr uint8_t BYTEBUTTON_LED_COUNT = BYTEBUTTON_BUTTONS + 1;
static constexpr uint8_t BYTEBUTTON_LED_USER_DEFINED = 0;
static constexpr uint8_t BYTEBUTTON_BRIGHTNESS = 120;
static constexpr unsigned long I2C_BOOT_GUARD_MS = 2000;
static constexpr unsigned long I2C_SCAN_SETTLE_MS = 12;
static constexpr unsigned long I2C_RESCAN_INTERVAL_MS = 8000;
static constexpr int I2C_HUB_DETECT_RETRIES = 6;
static constexpr uint8_t I2C_HUB_ADDR_MIN = 0x70;
static constexpr uint8_t I2C_HUB_ADDR_MAX = 0x77;
static constexpr unsigned long WIFI_STATUS_SEND_MS = 3000;
static constexpr unsigned long UI_REFRESH_MS = 120;
static constexpr unsigned long INPUT_POLL_MS = 15;
static constexpr unsigned long UDP_POLL_MS = 10;
static constexpr unsigned long MASTER_RX_TIMEOUT_MS = 10000;

static constexpr int TRACK_COUNT = M5_ENCODER_MODULES * ENCODERS_PER_MODULE;   // 16
static constexpr int VOL_MIN = 0;
static constexpr int VOL_MAX = 150;
static constexpr int VOL_STEP = 2;            // track volume change per encoder detent (tunable)
static constexpr int TEMPO_MIN = 40;
static constexpr int TEMPO_MAX = 240;
static constexpr int TEMPO_STEP = 1;
static constexpr int MASTER_VOL_STEP = 5;
static constexpr int PATTERN_COUNT = 16;

struct AppState {
    bool hubDetected = false;
    uint8_t hubAddress = I2C_HUB_ADDR;
    bool hubChannelHasEncoder[8] = {};
    bool hubChannelHasByteButton[8] = {};
    bool m5Connected[M5_ENCODER_MODULES] = {false, false};
    int m5Channel[M5_ENCODER_MODULES] = {-1, -1};
    uint8_t m5Version[M5_ENCODER_MODULES] = {0, 0};
    int32_t encoderValue[M5_ENCODER_MODULES][ENCODERS_PER_MODULE] = {};
    bool encoderPressed[M5_ENCODER_MODULES][ENCODERS_PER_MODULE] = {};
    bool byteButtonConnected[BYTEBUTTON_COUNT] = {false, false};
    int byteButtonChannel[BYTEBUTTON_COUNT] = {-1, -1};
    uint8_t byteButtonMask[BYTEBUTTON_COUNT] = {};
    // Shadow of master state (kept in sync from state_sync, mutated optimistically).
    bool playing = false;
    int currentPattern = 0;
    int tempoBpm = 120;
    int masterVolume = 100;
    int trackVolume[TRACK_COUNT] = {};   // initialised to 100 in setup()
    bool trackMuted[TRACK_COUNT] = {};
    char lastAction[40] = "-";
    bool wifiEverConnected = false;
    bool masterSeen = false;
    IPAddress localIp;
    char lastRx[96] = "sin trafico";
    unsigned long lastRxMs = 0;
    unsigned long lastStatusSendMs = 0;
    unsigned long lastUiRefreshMs = 0;
    unsigned long lastInputPollMs = 0;
    unsigned long lastUdpPollMs = 0;
    unsigned long lastI2cScanMs = 0;
    bool rootI2cLogged = false;
    bool udpStarted = false;
} g_state;

M5ROTATE8 g_m5[M5_ENCODER_MODULES];
WiFiUDP g_udp;
esp_lcd_panel_handle_t g_lcdPanel = nullptr;
lv_obj_t* g_screen = nullptr;
lv_obj_t* g_statusLabel = nullptr;
static char g_statusText[1536];
static char g_rxBuf[2048];

int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

bool i2c_device_present_raw(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void clear_i2c_scan_state() {
    for (int ch = 0; ch < 8; ++ch) {
        g_state.hubChannelHasEncoder[ch] = false;
        g_state.hubChannelHasByteButton[ch] = false;
    }
    for (int i = 0; i < M5_ENCODER_MODULES; ++i) {
        g_state.m5Connected[i] = false;
        g_state.m5Channel[i] = -1;
        g_state.m5Version[i] = 0;
    }
    for (int i = 0; i < BYTEBUTTON_COUNT; ++i) {
        g_state.byteButtonConnected[i] = false;
        g_state.byteButtonChannel[i] = -1;
        g_state.byteButtonMask[i] = 0;
    }
}

bool byte_button_write_bytes(uint8_t reg, const uint8_t* data, uint8_t length) {
    Wire.beginTransmission(BYTEBUTTON_ADDR);
    Wire.write(reg);
    for (uint8_t i = 0; i < length; ++i) {
        Wire.write(data[i]);
    }
    return Wire.endTransmission() == 0;
}

void byte_button_set_palette_locked() {
    uint8_t ledMode = BYTEBUTTON_LED_USER_DEFINED;
    byte_button_write_bytes(BYTEBUTTON_LED_SHOW_MODE_REG, &ledMode, 1);
    for (uint8_t led = 0; led < BYTEBUTTON_LED_COUNT; ++led) {
        uint8_t brightness = BYTEBUTTON_BRIGHTNESS;
        byte_button_write_bytes(BYTEBUTTON_LED_BRIGHTNESS_REG + led, &brightness, 1);
        uint8_t rgb4[4] = {0, (uint8_t)(40 + led * 12), (uint8_t)(90 + led * 10), 0};
        byte_button_write_bytes(BYTEBUTTON_LED_RGB888_REG + led * 4, rgb4, 4);
    }
}

void init_bytebutton_leds(int moduleIndex) {
    if (moduleIndex < 0 || moduleIndex >= BYTEBUTTON_COUNT) return;
    if (!g_state.byteButtonConnected[moduleIndex]) return;
    if (!i2c_lock(20)) return;
    i2c_hub_select_raw(g_state.byteButtonChannel[moduleIndex]);
    byte_button_set_palette_locked();
    i2c_hub_deselect_raw();
    i2c_unlock();
}

void set_encoder_leds() {
    for (int moduleIndex = 0; moduleIndex < M5_ENCODER_MODULES; ++moduleIndex) {
        if (!g_state.m5Connected[moduleIndex]) continue;
        if (!i2c_lock(20)) continue;
        i2c_hub_select_raw(g_state.m5Channel[moduleIndex]);
        for (int encoder = 0; encoder < ENCODERS_PER_MODULE; ++encoder) {
            uint8_t r = 0;
            uint8_t g = (uint8_t)(30 + encoder * 18);
            uint8_t b = (uint8_t)(100 + encoder * 14);
            g_m5[moduleIndex].writeRGB(encoder, r, g, b);
        }
        i2c_hub_deselect_raw();
        i2c_unlock();
    }
}

void ensure_boot_guard() {
    unsigned long elapsed = millis();
    if (elapsed < I2C_BOOT_GUARD_MS) {
        delay(I2C_BOOT_GUARD_MS - elapsed);
    }
}

bool all_i2c_modules_detected() {
    for (int i = 0; i < M5_ENCODER_MODULES; ++i) {
        if (!g_state.m5Connected[i]) {
            return false;
        }
    }
    for (int i = 0; i < BYTEBUTTON_COUNT; ++i) {
        if (!g_state.byteButtonConnected[i]) {
            return false;
        }
    }
    return true;
}

void log_root_i2c_scan() {
    if (g_state.rootI2cLogged) {
        return;
    }

    Serial.println("[I2C] Scan bus raiz:");
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
        if (i2c_device_present(addr)) {
            Serial.printf("[I2C] root 0x%02X\n", addr);
        }
    }
    g_state.rootI2cLogged = true;
}

uint8_t detect_i2c_hub_addr() {
    for (int attempt = 0; attempt < I2C_HUB_DETECT_RETRIES; ++attempt) {
        if (i2c_lock(120)) {
            for (uint8_t addr = I2C_HUB_ADDR_MIN; addr <= I2C_HUB_ADDR_MAX; ++addr) {
                Wire.beginTransmission(addr);
                Wire.write(0);
                if (Wire.endTransmission() == 0) {
                    i2c_unlock();
                    delay(5);
                    return addr;
                }
            }
            i2c_unlock();
        }
        delay(40 + attempt * 20);
    }
    return 0;
}

bool encoder_channel_bound(uint8_t channel) {
    for (int m = 0; m < M5_ENCODER_MODULES; ++m) {
        if (g_state.m5Connected[m] && g_state.m5Channel[m] == (int)channel) return true;
    }
    return false;
}

bool bytebutton_channel_bound(uint8_t channel) {
    for (int m = 0; m < BYTEBUTTON_COUNT; ++m) {
        if (g_state.byteButtonConnected[m] && g_state.byteButtonChannel[m] == (int)channel) return true;
    }
    return false;
}

int next_free_encoder_slot() {
    for (int m = 0; m < M5_ENCODER_MODULES; ++m) {
        if (!g_state.m5Connected[m]) return m;
    }
    return -1;
}

int next_free_bytebutton_slot() {
    for (int m = 0; m < BYTEBUTTON_COUNT; ++m) {
        if (!g_state.byteButtonConnected[m]) return m;
    }
    return -1;
}

// Bind each Encoder8 (0x41) and ByteButton (0x47) found on any hub channel to the
// next free module slot, in ascending channel order. No fixed channel map: works
// with whatever wiring is present. Already-connected modules are preserved so a
// re-scan never disrupts a working input.
void discover_i2c_devices() {
    g_state.lastI2cScanMs = millis();
    for (int ch = 0; ch < 8; ++ch) {
        g_state.hubChannelHasEncoder[ch] = false;
        g_state.hubChannelHasByteButton[ch] = false;
    }

    const uint8_t hubAddr = detect_i2c_hub_addr();
    if (hubAddr == 0) {
        g_state.hubDetected = false;
        g_state.hubAddress = I2C_HUB_ADDR;
        i2c_set_hub_addr(I2C_HUB_ADDR);
        log_root_i2c_scan();
        Serial.println("[I2C] Hub no detectado en 0x70-0x77");
        return;
    }

    g_state.hubDetected = true;
    g_state.hubAddress = hubAddr;
    i2c_set_hub_addr(hubAddr);
    Serial.printf("[I2C] Hub 0x%02X detectado\n", hubAddr);

    if (!i2c_lock(300)) {
        Serial.println("[I2C] Lock timeout durante discovery");
        return;
    }

    for (uint8_t ch = 0; ch < 8; ++ch) {
        i2c_hub_select_raw(ch);
        delay(I2C_SCAN_SETTLE_MS);

        const bool encoderHere = i2c_device_present_raw(M5_ENCODER_ADDR);
        const bool byteButtonHere = i2c_device_present_raw(BYTEBUTTON_ADDR);
        g_state.hubChannelHasEncoder[ch] = encoderHere;
        g_state.hubChannelHasByteButton[ch] = byteButtonHere;

        if (encoderHere && !encoder_channel_bound(ch)) {
            const int slot = next_free_encoder_slot();
            if (slot >= 0 && g_m5[slot].begin()) {
                for (int e = 0; e < ENCODERS_PER_MODULE; ++e) {
                    g_m5[slot].resetCounter(e);
                    g_state.encoderValue[slot][e] = 0;
                    g_state.encoderPressed[slot][e] = false;
                }
                g_state.m5Version[slot] = g_m5[slot].getVersion();
                g_state.m5Connected[slot] = true;
                g_state.m5Channel[slot] = ch;
                Serial.printf("[I2C] Encoder8 #%d en ch%u (fw%d)\n", slot + 1, ch, g_state.m5Version[slot]);
            }
        }

        if (byteButtonHere && !bytebutton_channel_bound(ch)) {
            const int slot = next_free_bytebutton_slot();
            if (slot >= 0) {
                g_state.byteButtonConnected[slot] = true;
                g_state.byteButtonChannel[slot] = ch;
                Serial.printf("[I2C] ByteButton #%d en ch%u\n", slot + 1, ch);
            }
        }

        i2c_hub_deselect_raw();
    }

    i2c_unlock();

    set_encoder_leds();
    for (int i = 0; i < BYTEBUTTON_COUNT; ++i) {
        init_bytebutton_leds(i);
    }

    int encCount = 0, bbCount = 0;
    for (int i = 0; i < M5_ENCODER_MODULES; ++i) if (g_state.m5Connected[i]) ++encCount;
    for (int i = 0; i < BYTEBUTTON_COUNT; ++i) if (g_state.byteButtonConnected[i]) ++bbCount;
    Serial.printf("[I2C] Resultado -> encoders:%d/%d bytebuttons:%d/%d\n",
                  encCount, M5_ENCODER_MODULES, bbCount, BYTEBUTTON_COUNT);
}

void rediscover_if_incomplete() {
    if (all_i2c_modules_detected()) {
        return;
    }
    unsigned long now = millis();
    if ((now - g_state.lastI2cScanMs) < I2C_RESCAN_INTERVAL_MS) {
        return;
    }
    discover_i2c_devices();
}

bool read_bytebutton_status(int hubChannel, uint8_t& status) {
    status = 0;
    if (!i2c_lock(20)) {
        return false;
    }

    i2c_hub_select_raw(hubChannel);
    bool ok = false;

    uint8_t statusBytes[BYTEBUTTON_BUTTONS] = {};
    Wire.beginTransmission(BYTEBUTTON_ADDR);
    Wire.write(BYTEBUTTON_STATUS_8BYTE_REG);
    if (Wire.endTransmission(false) == 0 &&
        Wire.requestFrom((uint8_t)BYTEBUTTON_ADDR, (uint8_t)BYTEBUTTON_BUTTONS) == BYTEBUTTON_BUTTONS) {
        for (int i = 0; i < BYTEBUTTON_BUTTONS; ++i) {
            statusBytes[i] = Wire.read();
            if (statusBytes[i]) {
                status |= (uint8_t)(1U << i);
            }
        }
        ok = true;
    } else {
        Wire.beginTransmission(BYTEBUTTON_ADDR);
        Wire.write(BYTEBUTTON_STATUS_REG);
        if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint8_t)BYTEBUTTON_ADDR, (uint8_t)1) == 1) {
            status = Wire.read();
            ok = true;
        }
    }

    i2c_hub_deselect_raw();
    i2c_unlock();
    return ok;
}

void send_udp_json(const JsonDocument& doc) {
    if (WiFi.status() != WL_CONNECTED || !g_state.udpStarted) return;
    char payload[256];
    size_t written = serializeJson(doc, payload, sizeof(payload));
    if (!written) return;
    g_udp.beginPacket(WiFiConfig::MASTER_IP, WiFiConfig::UDP_PORT);
    g_udp.write((const uint8_t*)payload, written);
    g_udp.endPacket();
}

void send_status_packet() {
    JsonDocument doc;
    doc["type"] = "status";
    doc["device"] = "BlueSlaveV2";
    doc["ip"] = WiFi.localIP().toString();
    doc["hub"] = g_state.hubDetected;
    doc["m5_1"] = g_state.m5Connected[0];
    doc["m5_2"] = g_state.m5Connected[1];
    doc["bb_1"] = g_state.byteButtonConnected[0];
    doc["bb_2"] = g_state.byteButtonConnected[1];
    send_udp_json(doc);
    g_state.lastStatusSendMs = millis();
}

// ── Master command senders (RED808 JSON contract) ───────────────────────────
void send_simple_cmd(const char* cmd) {
    JsonDocument doc;
    doc["cmd"] = cmd;
    send_udp_json(doc);
}

void send_track_volume(int track, int volume) {
    JsonDocument doc;
    doc["cmd"] = "setTrackVolume";
    doc["track"] = track;
    doc["volume"] = volume;
    send_udp_json(doc);
}

void send_mute(int track, bool value) {
    JsonDocument doc;
    doc["cmd"] = "mute";
    doc["track"] = track;
    doc["value"] = value;
    send_udp_json(doc);
}

void send_select_pattern(int index) {
    JsonDocument doc;
    doc["cmd"] = "selectPattern";
    doc["index"] = index;
    send_udp_json(doc);
}

void send_tempo(int bpm) {
    JsonDocument doc;
    doc["cmd"] = "tempo";
    doc["value"] = bpm;
    send_udp_json(doc);
}

void send_master_volume(int volume) {
    JsonDocument doc;
    doc["cmd"] = "setVolume";
    doc["value"] = volume;
    send_udp_json(doc);
}

// Button layout (16 buttons = 2x ByteButton). Edit this switch to remap.
//  0 Play/Pause  1 Stop  2 Pattern-  3 Pattern+  4 BPM-  5 BPM+  6 Vol-  7 Vol+
//  8..15 -> select pattern 1..8 directly
void do_button_action(int buttonIndex) {
    switch (buttonIndex) {
        case 0:
            g_state.playing = !g_state.playing;
            send_simple_cmd(g_state.playing ? "start" : "stop");
            snprintf(g_state.lastAction, sizeof(g_state.lastAction), "%s", g_state.playing ? "PLAY" : "PAUSE");
            break;
        case 1:
            g_state.playing = false;
            send_simple_cmd("stop");
            snprintf(g_state.lastAction, sizeof(g_state.lastAction), "STOP");
            break;
        case 2:
            g_state.currentPattern = (g_state.currentPattern + PATTERN_COUNT - 1) % PATTERN_COUNT;
            send_select_pattern(g_state.currentPattern);
            snprintf(g_state.lastAction, sizeof(g_state.lastAction), "PAT %d", g_state.currentPattern + 1);
            break;
        case 3:
            g_state.currentPattern = (g_state.currentPattern + 1) % PATTERN_COUNT;
            send_select_pattern(g_state.currentPattern);
            snprintf(g_state.lastAction, sizeof(g_state.lastAction), "PAT %d", g_state.currentPattern + 1);
            break;
        case 4:
            g_state.tempoBpm = clampi(g_state.tempoBpm - TEMPO_STEP, TEMPO_MIN, TEMPO_MAX);
            send_tempo(g_state.tempoBpm);
            snprintf(g_state.lastAction, sizeof(g_state.lastAction), "BPM %d", g_state.tempoBpm);
            break;
        case 5:
            g_state.tempoBpm = clampi(g_state.tempoBpm + TEMPO_STEP, TEMPO_MIN, TEMPO_MAX);
            send_tempo(g_state.tempoBpm);
            snprintf(g_state.lastAction, sizeof(g_state.lastAction), "BPM %d", g_state.tempoBpm);
            break;
        case 6:
            g_state.masterVolume = clampi(g_state.masterVolume - MASTER_VOL_STEP, VOL_MIN, VOL_MAX);
            send_master_volume(g_state.masterVolume);
            snprintf(g_state.lastAction, sizeof(g_state.lastAction), "VOL %d", g_state.masterVolume);
            break;
        case 7:
            g_state.masterVolume = clampi(g_state.masterVolume + MASTER_VOL_STEP, VOL_MIN, VOL_MAX);
            send_master_volume(g_state.masterVolume);
            snprintf(g_state.lastAction, sizeof(g_state.lastAction), "VOL %d", g_state.masterVolume);
            break;
        default: {
            const int pattern = buttonIndex - 8;
            if (pattern >= 0 && pattern < PATTERN_COUNT) {
                g_state.currentPattern = pattern;
                send_select_pattern(pattern);
                snprintf(g_state.lastAction, sizeof(g_state.lastAction), "PAT %d", pattern + 1);
            }
            break;
        }
    }
}

// Master is authoritative: refresh the shadow from its periodic snapshot.
void apply_state_sync(const JsonDocument& doc) {
    if (!doc["playing"].isNull()) g_state.playing = doc["playing"].as<bool>();
    if (!doc["tempo"].isNull()) g_state.tempoBpm = (int)doc["tempo"].as<float>();
    if (!doc["masterVolume"].isNull()) g_state.masterVolume = doc["masterVolume"].as<int>();
    if (!doc["pattern"].isNull()) g_state.currentPattern = doc["pattern"].as<int>();

    JsonArrayConst vols = doc["trackVolumes"].as<JsonArrayConst>();
    if (!vols.isNull()) {
        int i = 0;
        for (JsonVariantConst v : vols) {
            if (i >= TRACK_COUNT) break;
            g_state.trackVolume[i++] = clampi(v.as<int>(), VOL_MIN, VOL_MAX);
        }
    }
    JsonArrayConst mutes = doc["mute"].as<JsonArrayConst>();
    if (!mutes.isNull()) {
        int i = 0;
        for (JsonVariantConst m : mutes) {
            if (i >= TRACK_COUNT) break;
            g_state.trackMuted[i++] = m.as<bool>();
        }
    }
}

void handle_udp_rx() {
    if (WiFi.status() != WL_CONNECTED || !g_state.udpStarted) {
        return;
    }

    int packetSize = g_udp.parsePacket();
    if (packetSize <= 0) return;

    int len = g_udp.read(g_rxBuf, sizeof(g_rxBuf) - 1);
    if (len < 0) return;
    g_rxBuf[len] = '\0';
    strncpy(g_state.lastRx, g_rxBuf, sizeof(g_state.lastRx) - 1);
    g_state.lastRx[sizeof(g_state.lastRx) - 1] = '\0';
    g_state.lastRxMs = millis();
    g_state.masterSeen = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_rxBuf);
    if (!err) {
        const char* cmd = doc["cmd"] | doc["type"] | "";
        if (strcmp(cmd, "ping") == 0 || strcmp(cmd, "hello") == 0) {
            send_status_packet();
        } else if (strcmp(cmd, "state_sync") == 0) {
            apply_state_sync(doc);
        }
    }
}

void ensure_udp_started() {
    if (g_state.udpStarted || WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (g_udp.begin(WiFiConfig::UDP_PORT) == 1) {
        g_state.udpStarted = true;
    }
}

void ensure_wifi() {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        g_state.wifiEverConnected = true;
        g_state.localIp = WiFi.localIP();
        ensure_udp_started();
        return;
    }

    const bool reconnectAllowed =
        status == WL_NO_SSID_AVAIL ||
        status == WL_CONNECT_FAILED ||
        status == WL_CONNECTION_LOST ||
        status == WL_DISCONNECTED;

    if (!reconnectAllowed) {
        return;
    }

    static unsigned long lastAttemptMs = 0;
    unsigned long now = millis();
    if ((now - lastAttemptMs) < WiFiConfig::RECONNECT_INTERVAL_MS) {
        return;
    }
    lastAttemptMs = now;

    g_state.udpStarted = false;
    if (g_state.wifiEverConnected) {
        if (!WiFi.reconnect()) {
            WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);
        }
        return;
    }

    WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);
}

// Formats 8 tracks "NN:vol" with a red M marker when muted.
void format_track_vol_row(int startTrack, char* out, size_t outSize) {
    if (!out || outSize == 0) {
        return;
    }

    size_t used = 0;
    for (int i = 0; i < 8; ++i) {
        const int t = startTrack + i;
        const bool muted = g_state.trackMuted[t];
        const int written = snprintf(
            out + used,
            outSize - used,
            "%02d:%s%d%s%s",
            t + 1,
            muted ? "#ff4d4d " : "#9fe8ff ",
            g_state.trackVolume[t],
            muted ? "M#" : "#",
            (i < 7) ? "  " : "");
        if (written <= 0) {
            break;
        }
        used += (size_t)written;
        if (used >= outSize) {
            out[outSize - 1] = '\0';
            break;
        }
    }
}

void build_status_text(char* out, size_t outSize) {
    const bool wifiOk = WiFi.status() == WL_CONNECTED;
    const bool hubOk = g_state.hubDetected;
    const bool masterOk =
        g_state.masterSeen &&
        g_state.lastRxMs != 0 &&
        (millis() - g_state.lastRxMs) < MASTER_RX_TIMEOUT_MS;

    const char* wifiState = wifiOk ? "#4cff88 conectado#" : "#ff9f1c buscando#";
    const char* masterState = masterOk ? "#4cff88 ok#" : "#ff4d4d sin respuesta#";
    const char* hubState = hubOk ? "#4cff88 detectado#" : "#ff4d4d no detectado#";
    const char* transport = g_state.playing ? "#4cff88 PLAY#" : "#ff9f1c STOP#";

    int encCount = 0, bbCount = 0;
    for (int i = 0; i < M5_ENCODER_MODULES; ++i) if (g_state.m5Connected[i]) ++encCount;
    for (int i = 0; i < BYTEBUTTON_COUNT; ++i) if (g_state.byteButtonConnected[i]) ++bbCount;

    char volRow1[200] = {};
    char volRow2[200] = {};
    format_track_vol_row(0, volRow1, sizeof(volRow1));
    format_track_vol_row(8, volRow2, sizeof(volRow2));

    snprintf(
        out,
        outSize,
        "#4da6ff RED808 Surface#\n"
        "\n"
        "#4da6ff [RED]#\n"
        "WiFi      : %s    IP: %s\n"
        "Master UDP: %s\n"
        "I2C hub   : %s 0x%02X   Enc %d/2   Btn %d/2\n"
        "\n"
        "#4da6ff [TRANSPORTE]#\n"
        "Estado : %s    Patron: %d/%d    BPM: %d    MasterVol: %d\n"
        "\n"
        "#4da6ff [VOLUMEN TRACKS]  (M = mute, encoder gira=vol / pulsa=mute)#\n"
        "%s\n"
        "%s\n"
        "\n"
        "#4da6ff [BOTONES]  1Play 2Stop 3Pat- 4Pat+ 5BPM- 6BPM+ 7Vol- 8Vol+ 9-16 Patron#\n"
        "Ultima accion: %s\n"
        "\n"
        "#4da6ff [RX]#\n"
        "%s",
        wifiState,
        wifiOk ? WiFi.localIP().toString().c_str() : "#ff4d4d 0.0.0.0#",
        masterState,
        hubState, g_state.hubAddress, encCount, bbCount,
        transport, g_state.currentPattern + 1, PATTERN_COUNT, g_state.tempoBpm, g_state.masterVolume,
        volRow1,
        volRow2,
        g_state.lastAction,
        g_state.lastRx);
}

void init_ui() {
    if (!lvgl_port_lock(500)) {
        return;
    }

    g_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x03111f), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    g_statusLabel = lv_label_create(g_screen);
    lv_obj_set_width(g_statusLabel, SCREEN_WIDTH - 48);
    lv_obj_align(g_statusLabel, LV_ALIGN_TOP_LEFT, 24, 24);
    lv_label_set_long_mode(g_statusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(g_statusLabel, true);
    lv_obj_set_style_text_color(g_statusLabel, lv_color_hex(0xd8f0ff), 0);
    lv_obj_set_style_text_font(g_statusLabel, &lv_font_montserrat_16, 0);

    lv_scr_load(g_screen);
    lvgl_port_unlock();
}

void refresh_ui(bool force = false) {
    unsigned long now = millis();
    if (!force && (now - g_state.lastUiRefreshMs) < UI_REFRESH_MS) {
        return;
    }
    g_state.lastUiRefreshMs = now;

    build_status_text(g_statusText, sizeof(g_statusText));

    if (!lvgl_port_lock(50)) {
        return;
    }
    if (g_statusLabel) {
        lv_label_set_text(g_statusLabel, g_statusText);
    }
    lvgl_port_unlock();
}

// Encoder N -> track N volume (turn) and mute toggle (press).
// Module 0 -> tracks 0..7, module 1 -> tracks 8..15.
void poll_m5_inputs() {
    for (int moduleIndex = 0; moduleIndex < M5_ENCODER_MODULES; ++moduleIndex) {
        if (!g_state.m5Connected[moduleIndex]) continue;
        int32_t counters[ENCODERS_PER_MODULE] = {};
        bool pressedState[ENCODERS_PER_MODULE] = {};

        if (!i2c_lock(15)) continue;
        i2c_hub_select_raw(g_state.m5Channel[moduleIndex]);
        const bool readOk = g_m5[moduleIndex].isConnected();
        if (readOk) {
            for (int encoder = 0; encoder < ENCODERS_PER_MODULE; ++encoder) {
                counters[encoder] = g_m5[moduleIndex].getAbsCounter(encoder);
                pressedState[encoder] = g_m5[moduleIndex].getKeyPressed(encoder);
            }
        }
        i2c_hub_deselect_raw();
        i2c_unlock();

        if (!readOk) {
            continue;
        }

        for (int encoder = 0; encoder < ENCODERS_PER_MODULE; ++encoder) {
            const int track = moduleIndex * ENCODERS_PER_MODULE + encoder;

            const int32_t newAbs = counters[encoder];
            const int32_t delta = newAbs - g_state.encoderValue[moduleIndex][encoder];
            if (delta != 0) {
                g_state.encoderValue[moduleIndex][encoder] = newAbs;
                const int vol = clampi(g_state.trackVolume[track] + (int)delta * VOL_STEP, VOL_MIN, VOL_MAX);
                if (vol != g_state.trackVolume[track]) {
                    g_state.trackVolume[track] = vol;
                    send_track_volume(track, vol);
                }
            }

            const bool pressed = pressedState[encoder];
            if (pressed && !g_state.encoderPressed[moduleIndex][encoder]) {
                g_state.trackMuted[track] = !g_state.trackMuted[track];
                send_mute(track, g_state.trackMuted[track]);
                snprintf(g_state.lastAction, sizeof(g_state.lastAction),
                         "T%d %s", track + 1, g_state.trackMuted[track] ? "MUTE" : "on");
            }
            g_state.encoderPressed[moduleIndex][encoder] = pressed;
        }
    }
}

void poll_bytebuttons() {
    for (int moduleIndex = 0; moduleIndex < BYTEBUTTON_COUNT; ++moduleIndex) {
        if (!g_state.byteButtonConnected[moduleIndex]) continue;
        uint8_t mask = 0;
        if (!read_bytebutton_status(g_state.byteButtonChannel[moduleIndex], mask)) continue;
        const uint8_t prev = g_state.byteButtonMask[moduleIndex];
        if (mask != prev) {
            const uint8_t rising = (uint8_t)(mask & ~prev);  // 0->1 edges only
            for (int b = 0; b < BYTEBUTTON_BUTTONS; ++b) {
                if (rising & (1u << b)) {
                    do_button_action(moduleIndex * BYTEBUTTON_BUTTONS + b);
                }
            }
            g_state.byteButtonMask[moduleIndex] = mask;
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(80);

    for (int t = 0; t < TRACK_COUNT; ++t) {
        g_state.trackVolume[t] = 100;
    }

    i2c_init();
    io_ext_init();
    g_lcdPanel = rgb_lcd_init();
    lvgl_port_init(g_lcdPanel);
    io_ext_backlight_set(90);

    ensure_boot_guard();
    clear_i2c_scan_state();
    for (int attempt = 0; attempt < 5; ++attempt) {
        discover_i2c_devices();
        if (all_i2c_modules_detected()) break;
        delay(150);
    }

    init_ui();
    lvgl_port_task_start();
    refresh_ui(true);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);
}

void loop() {
    unsigned long now = millis();

    ensure_wifi();
    rediscover_if_incomplete();

    if ((now - g_state.lastInputPollMs) >= INPUT_POLL_MS) {
        g_state.lastInputPollMs = now;
        poll_m5_inputs();
        poll_bytebuttons();
    }

    if ((now - g_state.lastUdpPollMs) >= UDP_POLL_MS) {
        g_state.lastUdpPollMs = now;
        handle_udp_rx();
    }

    if (WiFi.status() == WL_CONNECTED && (now - g_state.lastStatusSendMs) >= WIFI_STATUS_SEND_MS) {
        send_status_packet();
    }

    refresh_ui();
    delay(2);
}
