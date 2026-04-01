// =============================================================================
// main.cpp - BlueSlaveV2
// RED808 V6 Surface Controller
// Waveshare ESP32-S3-Touch-LCD-7B + M5 8ENCODER + DFRobot SEN0502
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "nvs_flash.h"
#include <M5ROTATE8.h>
#include <DFRobot_VisualRotaryEncoder.h>
#include "lvgl.h"

#include "../include/config.h"
#include "../include/system_state.h"

// Drivers
#include "drivers/i2c_driver.h"
#include "drivers/io_extension.h"
#include "drivers/rgb_lcd.h"
#include "drivers/gt911_touch.h"
#include "drivers/lvgl_port.h"

// UI
#include "ui/ui_theme.h"
#include "ui/ui_screens.h"

// =============================================================================
// GLOBAL STATE DEFINITIONS
// =============================================================================

// Screen
Screen currentScreen = SCREEN_BOOT;
Screen previousScreen = SCREEN_BOOT;
bool needsFullRedraw = true;
volatile bool themeJustChanged = false;

// Sequencer
Pattern patterns[Config::MAX_PATTERNS];
int currentPattern = 0;
int currentStep = 0;
int selectedTrack = 0;
bool isPlaying = false;
int currentBPM = Config::DEFAULT_BPM;
int currentKit = 0;

// Volume
int masterVolume = Config::DEFAULT_VOLUME;
int sequencerVolume = Config::DEFAULT_VOLUME;
int livePadsVolume = 100;
int trackVolumes[Config::MAX_TRACKS];
VolumeMode volumeMode = VOL_SEQUENCER;
bool trackMuted[Config::MAX_TRACKS];
bool trackSolo[Config::MAX_TRACKS];
bool livePadPressed[Config::MAX_SAMPLES];
volatile uint32_t pendingLivePadTriggerMask = 0;

// Filters
TrackFilter trackFilters[Config::MAX_TRACKS];
TrackFilter masterFilter = {false, 0, 0, 0};
int filterSelectedTrack = -1;  // -1 = master
int filterSelectedFX = FILTER_DELAY;
EncoderMode encoderMode = ENC_MODE_VOLUME;
int analogFxPreset = 0;  // 0=OFF, 1..11=FX presets (set by analog rotary)

// I2C Hub
int m5HubChannel[M5_ENCODER_MODULES] = {-1, -1};
int dfRobotHubChannel[DFROBOT_ENCODER_COUNT] = {-1, -1};
int byteButtonHubChannel = -1;
bool hubDetected = false;

// Connection
bool udpConnected = false;
bool wifiConnected = false;
bool wifiReconnecting = false;
bool masterConnected = false;

// Diagnostic
DiagnosticInfo diagInfo = {};

// Track names
const char* trackNames[] = {
    "BD", "SD", "CH", "OH", "CL", "CP", "CB", "TM",
    "CY", "MA", "RS", "LC", "MC", "HC", "LT", "HT"
};
const char* instrumentNames[] = {
    "Bass Drum", "Snare", "Closed HH", "Open HH",
    "Clap", "Clap 2", "Cowbell", "Tom",
    "Cymbal", "Maracas", "Rimshot", "Low Conga",
    "Mid Conga", "Hi Conga", "Low Tom", "Hi Tom"
};
const char* kitNames[] = {"808 CLASSIC", "808 BRIGHT", "808 DRY"};

// =============================================================================
// HARDWARE INSTANCES
// =============================================================================

WiFiUDP udp;
M5ROTATE8 m5encoders[M5_ENCODER_MODULES];
bool m5encoderConnected[M5_ENCODER_MODULES] = {false, false};
DFRobot_VisualRotaryEncoder_I2C* dfEncoders[DFROBOT_ENCODER_COUNT] = {nullptr, nullptr};
bool dfEncoderConnected[DFROBOT_ENCODER_COUNT] = {false, false};
bool byteButtonConnected = false;
esp_lcd_panel_handle_t lcd_panel = NULL;

// Encoder LED colors per track (full rainbow spectrum)
uint8_t encoderLEDColors[Config::MAX_TRACKS][3] = {
    {255, 0,   0},   {255, 80,  0},   {255, 160, 0},   {255, 255, 0},
    {128, 255, 0},   {0,   255, 0},   {0,   255, 128}, {0,   255, 255},
    {0,   128, 255}, {0,   0,   255}, {80,  0,   255}, {160, 0,   255},
    {255, 0,   255}, {255, 0,   160}, {255, 0,   80},  {255, 60,  60}
};

// Live pad touch guard (prevent phantom triggers on screen enter)
unsigned long liveScreenEnteredMs = 0;
static constexpr unsigned long LIVE_TOUCH_GUARD_MS = 500;

// Timing
unsigned long lastEncoderRead = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastUDPCheck = 0;
unsigned long lastMasterPacketMs = 0;
unsigned long lastStepUpdateMs = 0;   // last UDP step_update/step_sync received
unsigned long lastLocalStepMs  = 0;   // last local-clock step advance (independent of UDP)
int32_t prevM5Counter[Config::MAX_TRACKS];
uint16_t prevDFValue[DFROBOT_ENCODER_COUNT];
unsigned long lastLivePadTriggerMs[Config::MAX_SAMPLES];
uint8_t prevByteButtonState = 0;
bool byteButtonLivePressed[BYTEBUTTON_BUTTONS] = {false, false, false, false, false, false, false, false};
uint32_t byteButtonLedCache[BYTEBUTTON_BUTTONS + 1] = {};
bool byteButtonLedInitialized = false;

static constexpr uint8_t BYTEBUTTON_STATUS_REG      = 0x00;  // bitmask — unreliable polarity on some FW
static constexpr uint8_t BYTEBUTTON_STATUS_8BYTE_REG = 0x60;  // per-button byte: 0x01=pressed, 0x00=released
static constexpr uint8_t BYTEBUTTON_LED_BRIGHTNESS_REG = 0x10;
static constexpr uint8_t BYTEBUTTON_LED_SHOW_MODE_REG = 0x19;
static constexpr uint8_t BYTEBUTTON_LED_RGB888_REG = 0x20;
static constexpr uint8_t BYTEBUTTON_LED_COUNT = BYTEBUTTON_BUTTONS + 1;
static constexpr uint8_t BYTEBUTTON_LED_USER_DEFINED = 0;
static constexpr uint8_t BYTEBUTTON_BRIGHTNESS = 180;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

void initState();
void setupWiFi();
void checkWiFiReconnect();
void sendUDPCommand(const char* cmd);
void sendUDPCommand(JsonDocument& doc);
void receiveUDPData();
void requestPatternFromMaster();
void sendPlayStateCommand(bool shouldPlay);
void selectPatternOnMaster(int patternIndex);
void sendFullPatternToMaster(int pat);
void sendLivePadTrigger(int pad, int velocity);
void sendFilterUDP(int track, int fxType);
void scanI2CHub();
void handleM5Encoders();
void handleDFRobotEncoders();
void handleByteButton();
void updateByteButtonLeds();
void updateTrackEncoderLED(int track);
void handleAnalogEncoder();
void updateUI();

static void requestTrackVolumesFromMaster() {
    if (!udpConnected) return;

    JsonDocument doc;
    doc["cmd"] = "getTrackVolumes";
    sendUDPCommand(doc);
}

static void requestMasterSync() {
    if (!udpConnected) return;

    JsonDocument doc;
    doc["cmd"] = "hello";
    doc["device"] = "SURFACE";
    sendUDPCommand(doc);
    requestPatternFromMaster();
    requestTrackVolumesFromMaster();

    // Upload demo pattern 7 (index 6) to master
    sendFullPatternToMaster(6);
}

static int quantizeDFDelta(int index, int16_t delta) {
    static int accumulators[DFROBOT_ENCODER_COUNT] = {0, 0};
    if (index < 0 || index >= DFROBOT_ENCODER_COUNT) return 0;
    if (delta == 0) return 0;

    accumulators[index] += delta;
    int logical_step = 0;
    while (accumulators[index] >= Config::DF_COUNTS_PER_STEP) {
        logical_step++;
        accumulators[index] -= Config::DF_COUNTS_PER_STEP;
    }
    while (accumulators[index] <= -Config::DF_COUNTS_PER_STEP) {
        logical_step--;
        accumulators[index] += Config::DF_COUNTS_PER_STEP;
    }
    return logical_step;
}

static void handleLivePadTouchMatrix(unsigned long now) {
    bool new_state[Config::MAX_SAMPLES] = {};
    if (currentScreen != SCREEN_LIVE || !gt911_is_ready()) {
        memset(livePadPressed, 0, sizeof(bool) * Config::MAX_SAMPLES);
        return;
    }

    // Guard period after entering LIVE screen — ignore stale touches
    if ((now - liveScreenEnteredMs) < LIVE_TOUCH_GUARD_MS) {
        memset(livePadPressed, 0, sizeof(bool) * Config::MAX_SAMPLES);
        return;
    }

    TouchPoint points[Config::TOUCH_MAX_POINTS] = {};
    uint8_t count = gt911_get_points(points, Config::TOUCH_MAX_POINTS);

    for (uint8_t i = 0; i < count; i++) {
        if (!points[i].pressed) continue;
        int pad = ui_live_pad_hit_test(points[i].x, points[i].y);
        if (pad >= 0 && pad < Config::MAX_SAMPLES) {
            new_state[pad] = true;
        }
    }

    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (new_state[pad] && !livePadPressed[pad]) {
            pendingLivePadTriggerMask |= (1UL << pad);
            lastLivePadTriggerMs[pad] = now;
        }
        livePadPressed[pad] = new_state[pad];
    }
}

static bool byteButtonWriteBytes(uint8_t reg, const uint8_t* data, uint8_t length) {
    Wire.beginTransmission(BYTEBUTTON_ADDR);
    Wire.write(reg);
    for (uint8_t i = 0; i < length; i++) {
        Wire.write(data[i]);
    }
    return Wire.endTransmission() == 0;
}

static uint32_t byteButtonColorForPad(int pad) {
    return theme_pad_color(pad);
}

static bool byteButtonApplyLedConfigLocked() {
    uint8_t ledMode = BYTEBUTTON_LED_USER_DEFINED;
    if (!byteButtonWriteBytes(BYTEBUTTON_LED_SHOW_MODE_REG, &ledMode, 1)) {
        return false;
    }
    for (uint8_t led = 0; led < BYTEBUTTON_LED_COUNT; led++) {
        uint8_t brightness = BYTEBUTTON_BRIGHTNESS;
        if (!byteButtonWriteBytes(BYTEBUTTON_LED_BRIGHTNESS_REG + led, &brightness, 1)) {
            return false;
        }
    }
    return true;
}

static void initByteButtonLeds() {
    if (!byteButtonConnected) return;
    if (!i2c_lock(30)) return;

    if (byteButtonHubChannel >= 0) i2c_hub_select_raw(byteButtonHubChannel);
    else if (byteButtonHubChannel == -2) i2c_hub_deselect_raw();
    bool ok = byteButtonApplyLedConfigLocked();
    if (byteButtonHubChannel >= 0) i2c_hub_deselect_raw();
    i2c_unlock();

    if (ok) {
        byteButtonLedInitialized = true;
        memset(byteButtonLedCache, 0xFF, sizeof(byteButtonLedCache));
    }
}

static bool navigateToScreen(Screen screen) {
    if (currentScreen == screen) return true;

    lv_obj_t* target = NULL;
    switch (screen) {
        case SCREEN_BOOT:        target = scr_boot; break;
        case SCREEN_MENU:        target = scr_menu; break;
        case SCREEN_LIVE:        target = scr_live; break;
        case SCREEN_SEQUENCER:   target = scr_sequencer; break;
        case SCREEN_VOLUMES:     target = scr_volumes; break;
        case SCREEN_FILTERS:     target = scr_filters; break;
        case SCREEN_SETTINGS:    target = scr_settings; break;
        case SCREEN_DIAGNOSTICS: target = scr_diagnostics; break;
        case SCREEN_PATTERNS:    target = scr_patterns; break;
        case SCREEN_SDCARD:    target = scr_sdcard; break;
        case SCREEN_PERFORMANCE: target = scr_performance; break;
        case SCREEN_SAMPLES:     target = scr_samples; break;
        case SCREEN_SEQ_CIRCLE:  target = scr_seq_circle; break;
        default: break;
    }

    if (!target) return false;

    // Guard BEFORE screen change: prevent race with touch handler
    if (screen == SCREEN_LIVE) {
        liveScreenEnteredMs = millis();
        memset(livePadPressed, 0, sizeof(livePadPressed));
        pendingLivePadTriggerMask = 0;
        // Sync ByteButton edge detection: assume all buttons were pressed so
        // no phantom edges fire on the first handleByteButton() after entry.
        prevByteButtonState = 0xFF;
        memset(byteButtonLivePressed, 0, sizeof(byteButtonLivePressed));
    }

    if (!lvgl_port_lock(20)) return false;
    currentScreen = screen;
    lv_scr_load(target);
    lvgl_port_unlock();
    return true;
}

// =============================================================================
// STATE INIT
// =============================================================================

void initState() {
    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        trackVolumes[i] = Config::DEFAULT_TRACK_VOLUME;
        trackMuted[i] = false;
        trackSolo[i] = false;
        trackFilters[i] = {false, 0, 0, 0};
    }
    for (int i = 0; i < Config::MAX_SAMPLES; i++) {
        livePadPressed[i] = false;
        lastLivePadTriggerMs[i] = 0;
    }
    for (int p = 0; p < Config::MAX_PATTERNS; p++) {
        memset(patterns[p].steps, 0, sizeof(patterns[p].steps));
        memset(patterns[p].muted, 0, sizeof(patterns[p].muted));
        char _nb[12]; snprintf(_nb, sizeof(_nb), "Pattern %d", p + 1);
        patterns[p].name = _nb;
    }

    // ── DEMO PATTERN 7 (index 6): Full 808 beat with all 16 instruments ──
    // Track layout: BD=0 SD=1 CH=2 OH=3 CL=4 CP=5 CB=6 TM=7
    //               CY=8 MA=9 RS=10 LC=11 MC=12 HC=13 LT=14 HT=15
    // Steps:        0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    {
        const uint16_t demo[16] = {
            0b1000100010001000,  //  0 BD:  1 . . . 5 . . . 9 . . . 13 . . .
            0b0000100000001000,  //  1 SD:  . . . . 5 . . . . . . . 13 . . .
            0b1010101010101010,  //  2 CH:  1 . 3 . 5 . 7 . 9 . 11 . 13 . 15 .
            0b0000000010000000,  //  3 OH:  . . . . . . . . 9 . .  .  .  . .  .
            0b0000100000001000,  //  4 CL:  . . . . 5 . . . . . . . 13 . . .
            0b0000000000001000,  //  5 CP:  . . . . . . . . . . . . 13 . . .
            0b1000001000100010,  //  6 CB:  1 . . . . . 7 . . 9 . . . . 15 .
            0b0010000000100000,  //  7 TM:  . . 3 . . . . . . . 11 .  .  . .  .
            0b1000000000000000,  //  8 CY:  1 . . . . . . . . . .  .  .  . .  .
            0b0101010101010101,  //  9 MA:  . 2 . 4 . 6 . 8 . 10 . 12 . 14 . 16
            0b0000100000000000,  // 10 RS:  . . . . 5 . . . . .  .  .  .  .  .  .
            0b1000000010000000,  // 11 LC:  1 . . . . . . . 9 .  .  .  .  .  .  .
            0b0000100000001000,  // 12 MC:  . . . . 5 . . . . . .  . 13 .  .  .
            0b0010000000100000,  // 13 HC:  . . 3 . . . . . . . 11 .  .  .  .  .
            0b1000000000001000,  // 14 LT:  1 . . . . . . . . .  .  . 13 .  .  .
            0b0000000010000010,  // 15 HT:  . . . . . . . . 9 .  .  .  .  . 15 .
        };
        for (int t = 0; t < 16; t++)
            for (int s = 0; s < 16; s++)
                patterns[6].steps[t][s] = (demo[t] >> (15 - s)) & 1;
        patterns[6].name = "FULL 808";
    }
    for (int i = 0; i < Config::MAX_TRACKS; i++) prevM5Counter[i] = 0;
    for (int i = 0; i < DFROBOT_ENCODER_COUNT; i++) prevDFValue[i] = 0;
    prevByteButtonState = 0;
    pendingLivePadTriggerMask = 0;
    memset(byteButtonLivePressed, 0, sizeof(byteButtonLivePressed));
    memset(byteButtonLedCache, 0xFF, sizeof(byteButtonLedCache));
    byteButtonLedInitialized = false;
}

// =============================================================================
// WiFi / UDP
// =============================================================================

void setupWiFi() {
    Serial.println("[WiFi] Initializing NVS...");
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("[WiFi] NVS corrupted, erasing...");
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    Serial.printf("[WiFi] NVS init: %s\n", esp_err_to_name(nvs_err));

    Serial.println("[WiFi] Connecting...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WiFiConfig::TIMEOUT_MS) {
        delay(250);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        udp.stop();
        udpConnected = udp.begin(WiFiConfig::UDP_PORT);
        Serial.printf("\n[WiFi] Connected! IP: %s  UDP: %s\n",
                      WiFi.localIP().toString().c_str(),
                      udpConnected ? "OK" : "FALLO");

        if (udpConnected) {
            requestMasterSync();
        }
    } else {
        Serial.println("\n[WiFi] Initial connect timeout - will retry in background");
        wifiConnected = false;
    }
    diagInfo.wifiOk = wifiConnected;
    diagInfo.udpConnected = udpConnected;
    lastWiFiCheck = millis();
}

void checkWiFiReconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            wifiConnected = true;
            wifiReconnecting = false;
            udp.stop();  // Asegura socket limpio antes de bind
            udpConnected = udp.begin(WiFiConfig::UDP_PORT);
            diagInfo.wifiOk = true;
            diagInfo.udpConnected = udpConnected;
            Serial.printf("[WiFi] Reconectado! IP: %s  UDP: %s\n",
                          WiFi.localIP().toString().c_str(),
                          udpConnected ? "OK" : "FALLO");
            if (udpConnected) {
                requestMasterSync();
            }
        }
        return;
    }

    wifiConnected = false;
    udpConnected = false;
    masterConnected = false;
    diagInfo.wifiOk = false;
    diagInfo.udpConnected = false;

    unsigned long now_wifi = millis();

    // FIX: wifiReconnecting nunca se reseteaba si WiFi no conectaba → ningún reintento
    // Después del timeout de intento, volver a permitir un nuevo ciclo de reconexión
    if (wifiReconnecting && (now_wifi - lastWiFiCheck > WiFiConfig::RECONNECT_ATTEMPT_TIMEOUT_MS)) {
        wifiReconnecting = false;
        Serial.println("[WiFi] Intento de reconexion agotado, reintentando...");
    }

    if (!wifiReconnecting && (now_wifi - lastWiFiCheck > WiFiConfig::RECONNECT_INTERVAL_MS)) {
        lastWiFiCheck = now_wifi;
        wifiReconnecting = true;
        WiFi.disconnect(false);   // No bloqueante
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);
        Serial.printf("[WiFi] Reconectando a '%s'...\n", WiFiConfig::SSID);
    }
}

void sendUDPCommand(const char* cmd) {
    if (!udpConnected) return;  // silent drop — no Serial in hot path
    udp.beginPacket(WiFiConfig::MASTER_IP, WiFiConfig::UDP_PORT);
    udp.write((const uint8_t*)cmd, strlen(cmd));
    udp.endPacket();
}

void sendUDPCommand(JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    sendUDPCommand(json.c_str());
}

void requestPatternFromMaster() {
    JsonDocument doc;
    doc["cmd"] = "get_pattern";
    doc["pattern"] = currentPattern;
    sendUDPCommand(doc);
}

void sendPlayStateCommand(bool shouldPlay) {
    isPlaying = shouldPlay;
    JsonDocument doc;
    doc["cmd"] = isPlaying ? "start" : "stop";
    sendUDPCommand(doc);
}

void selectPatternOnMaster(int patternIndex) {
    currentPattern = constrain(patternIndex, 0, Config::MAX_PATTERNS - 1);

    JsonDocument doc;
    doc["cmd"] = "selectPattern";
    doc["index"] = currentPattern;
    sendUDPCommand(doc);

    // Request the new pattern data from master
    requestPatternFromMaster();
}

// Send full local pattern to master (select pattern, send active steps, restore)
void sendFullPatternToMaster(int pat) {
    if (pat < 0 || pat >= Config::MAX_PATTERNS || !udpConnected) return;

    int savedPattern = currentPattern;

    // Switch master to target pattern
    {
        JsonDocument doc;
        doc["cmd"] = "selectPattern";
        doc["index"] = pat;
        sendUDPCommand(doc);
    }
    delay(30);

    // Send each active step via setStep (same format the master expects)
    int sent = 0;
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++) {
            if (patterns[pat].steps[t][s]) {
                char buf[80];
                snprintf(buf, sizeof(buf),
                    "{\"cmd\":\"setStep\",\"track\":%d,\"step\":%d,\"active\":true}", t, s);
                sendUDPCommand(buf);
                sent++;
                if (sent % 8 == 0) delay(10);  // throttle every 8 packets
            }
        }
    }

    // Restore master to previous pattern if different
    if (savedPattern != pat) {
        delay(30);
        JsonDocument doc;
        doc["cmd"] = "selectPattern";
        doc["index"] = savedPattern;
        sendUDPCommand(doc);
    }
    Serial.printf("[DEMO] Sent pattern %d to master (%d steps)\n", pat, sent);
}

void sendLivePadTrigger(int pad, int velocity) {
    if (pad < 0 || pad >= Config::MAX_SAMPLES) return;

    JsonDocument doc;
    doc["cmd"] = "trigger";
    doc["pad"] = pad;
    doc["vel"] = constrain(velocity, 1, 127);
    sendUDPCommand(doc);
}

// =============================================================================
// UDP RECEIVE
// =============================================================================

void receiveUDPData() {
    if (!udpConnected) return;

    // Drain ALL pending UDP packets each loop iteration
    for (int pkt = 0; pkt < 8; pkt++) {
    int packetSize = udp.parsePacket();
    if (packetSize == 0) return;

    char buf[1024];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = '\0';

    lastMasterPacketMs = millis();
    masterConnected = true;
    diagInfo.udpConnected = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) continue;

    const char* cmd = doc["cmd"];
    if (!cmd) continue;

    if (strcmp(cmd, "pattern_sync") == 0) {
        int pat = doc["pattern"] | 0;
        if (pat >= 0 && pat < Config::MAX_PATTERNS) {
            JsonArray data = doc["data"];
            if (data) {
                // Check if incoming data is all-empty
                bool incomingEmpty = true;
                for (JsonArray track : data) {
                    for (JsonVariant step : track) {
                        if (step.as<bool>()) { incomingEmpty = false; break; }
                    }
                    if (!incomingEmpty) break;
                }
                // If pattern 6 (demo) has local data and master sends empty, keep local
                if (pat == 6 && incomingEmpty) {
                    bool localHasData = false;
                    for (int t = 0; t < Config::MAX_TRACKS && !localHasData; t++)
                        for (int s = 0; s < Config::MAX_STEPS && !localHasData; s++)
                            if (patterns[6].steps[t][s]) localHasData = true;
                    if (localHasData) {
                        Serial.println("[UDP] Skipping empty pattern_sync for pattern 7 (demo)");
                        continue;  // skip this packet, keep local demo
                    }
                }
                int t = 0;
                for (JsonArray track : data) {
                    if (t >= Config::MAX_TRACKS) break;
                    int s = 0;
                    for (JsonVariant step : track) {
                        if (s >= Config::MAX_STEPS) break;
                        patterns[pat].steps[t][s] = step.as<bool>();
                        s++;
                    }
                    t++;
                }
            }
        }
    }
    else if (strcmp(cmd, "step_update") == 0 || strcmp(cmd, "step_sync") == 0) {
        // Visual step is driven by local clock — ignore master step position
        // (UDP packet loss causes jumps: e.g. 1,3,6,8 instead of smooth 0,1,2,3)
        // Only track that master is alive for connection status
        lastStepUpdateMs = millis();
    }
    else if (strcmp(cmd, "play_state") == 0) {
        bool playing = doc["playing"] | false;
        if (playing && !isPlaying) {
            currentStep = 0;
            lastLocalStepMs = millis();
        }
        isPlaying = playing;
    }
    else if (strcmp(cmd, "start") == 0) {
        if (!isPlaying) { currentStep = 0; lastLocalStepMs = millis(); }
        isPlaying = true;
    }
    else if (strcmp(cmd, "stop") == 0) {
        isPlaying = false;
        currentStep = 0;
    }
    else if (strcmp(cmd, "tempo_sync") == 0 || strcmp(cmd, "tempo") == 0) {
        currentBPM = constrain(doc["value"] | Config::DEFAULT_BPM, Config::MIN_BPM, Config::MAX_BPM);
    }
    else if (strcmp(cmd, "volume_sync") == 0 ||
             strcmp(cmd, "master_volume_sync") == 0 ||
             strcmp(cmd, "volume_master_sync") == 0 ||
             strcmp(cmd, "setVolume") == 0) {
        masterVolume = constrain(doc["value"] | Config::DEFAULT_VOLUME, 0, Config::MAX_VOLUME);
    }
    else if (strcmp(cmd, "volume_seq_sync") == 0 || strcmp(cmd, "setSequencerVolume") == 0) {
        sequencerVolume = constrain(doc["value"] | Config::DEFAULT_VOLUME, 0, Config::MAX_VOLUME);
    }
    else if (strcmp(cmd, "volume_live_sync") == 0 || strcmp(cmd, "setLiveVolume") == 0) {
        livePadsVolume = constrain(doc["value"] | 100, 0, Config::MAX_VOLUME);
    }
    else if (strcmp(cmd, "trackVolumes") == 0 ||
             strcmp(cmd, "track_volumes") == 0 ||
             strcmp(cmd, "track_volume_sync") == 0 ||
             strcmp(cmd, "getTrackVolumes") == 0) {
        JsonArray values = doc["values"].is<JsonArray>() ? doc["values"].as<JsonArray>() : JsonArray();
        if (!values) values = doc["volumes"].as<JsonArray>();
        if (!values) values = doc["data"].as<JsonArray>();

        if (values) {
            int index = 0;
            for (JsonVariant value : values) {
                if (index >= Config::MAX_TRACKS) break;
                trackVolumes[index++] = constrain(value.as<int>(), 0, Config::MAX_VOLUME);
            }
        }
    }
    else if (strcmp(cmd, "trackVolume") == 0 || strcmp(cmd, "getTrackVolume") == 0) {
        int track = doc["track"] | -1;
        if (track >= 0 && track < Config::MAX_TRACKS) {
            int value = doc["volume"] | (doc["value"] | trackVolumes[track]);
            trackVolumes[track] = constrain(value, 0, Config::MAX_VOLUME);
        }
    }
    } // end drain loop
}

// =============================================================================
// FILTER UDP SEND
// =============================================================================

void sendFilterUDP(int track, int fxType) {
    TrackFilter& f = (track == -1) ? masterFilter : trackFilters[track];

    if (track == -1) {
        // Master effects
        JsonDocument doc;
        switch (fxType) {
            case FILTER_DELAY:
                doc["cmd"] = "setDelayActive"; doc["value"] = f.enabled ? 1 : 0; sendUDPCommand(doc); doc.clear();
                doc["cmd"] = "setDelayTime"; doc["value"] = f.delayAmount; sendUDPCommand(doc); doc.clear();
                doc["cmd"] = "setDelayFeedback"; doc["value"] = f.delayAmount / 2; sendUDPCommand(doc); doc.clear();
                doc["cmd"] = "setDelayMix"; doc["value"] = f.delayAmount; sendUDPCommand(doc);
                break;
            case FILTER_FLANGER:
                doc["cmd"] = "setFlangerActive"; doc["value"] = f.enabled ? 1 : 0; sendUDPCommand(doc); doc.clear();
                doc["cmd"] = "setFlangerRate"; doc["value"] = f.flangerAmount; sendUDPCommand(doc); doc.clear();
                doc["cmd"] = "setFlangerDepth"; doc["value"] = f.flangerAmount; sendUDPCommand(doc);
                break;
            case FILTER_COMPRESSOR:
                doc["cmd"] = "setCompActive"; doc["value"] = f.enabled ? 1 : 0; sendUDPCommand(doc); doc.clear();
                doc["cmd"] = "setCompThreshold"; doc["value"] = f.compAmount; sendUDPCommand(doc); doc.clear();
                doc["cmd"] = "setCompRatio"; doc["value"] = f.compAmount / 2; sendUDPCommand(doc);
                break;
        }
    } else {
        // Per-track effects
        JsonDocument doc;
        switch (fxType) {
            case FILTER_DELAY:
                doc["cmd"] = "setTrackEcho";
                doc["track"] = track;
                doc["active"] = f.enabled;
                doc["time"] = f.delayAmount;
                doc["feedback"] = f.delayAmount / 2;
                doc["mix"] = f.delayAmount;
                break;
            case FILTER_FLANGER:
                doc["cmd"] = "setTrackFlanger";
                doc["track"] = track;
                doc["active"] = f.enabled;
                doc["rate"] = f.flangerAmount;
                doc["depth"] = f.flangerAmount;
                doc["feedback"] = f.flangerAmount / 2;
                break;
            case FILTER_COMPRESSOR:
                doc["cmd"] = "setTrackCompressor";
                doc["track"] = track;
                doc["active"] = f.enabled;
                doc["threshold"] = f.compAmount;
                doc["ratio"] = f.compAmount / 2;
                break;
        }
        sendUDPCommand(doc);
    }
}

// =============================================================================
// I2C HUB & DEVICE SCANNING
// =============================================================================

void scanI2CHub() {
    Serial.println("[I2C] Scanning bus...");

    // Check for PCA9548A hub (with mutex protection)
    hubDetected = i2c_device_present(I2C_HUB_ADDR);
    diagInfo.i2cHubOk = hubDetected;

    if (!hubDetected) {
        Serial.println("[I2C] No PCA9548A hub found");
        return;
    }
    Serial.println("[I2C] PCA9548A hub detected");

    int m5Found = 0, dfFound = 0;
    bool byteButtonFound = false;

    for (uint8_t ch = 0; ch < 8 && (m5Found < M5_ENCODER_MODULES || dfFound < DFROBOT_ENCODER_COUNT || !byteButtonFound); ch++) {
        i2c_hub_select(ch);
        delay(5);

        // Probe M5 ROTATE8 (0x41)
        if (m5Found < M5_ENCODER_MODULES && i2c_device_present(M5_ENCODER_ADDR)) {
            m5HubChannel[m5Found] = ch;
            m5encoderConnected[m5Found] = true;
            Serial.printf("[I2C] M5 ROTATE8 #%d found on ch %d\n", m5Found + 1, ch);

            // begin() and resetCounter() do I2C — protect with scoped lock
            if (i2c_lock(50)) {
                i2c_hub_select_raw(ch);
                if (!m5encoders[m5Found].begin()) {
                    for (int e = 0; e < ENCODERS_PER_MODULE; e++) {
                        m5encoders[m5Found].resetCounter(e);
                    }
                }
                i2c_hub_deselect_raw();
                i2c_unlock();
            }
            m5Found++;
        }

        // Probe DFRobot SEN0502 (0x54)
        if (dfFound < DFROBOT_ENCODER_COUNT && i2c_device_present(DFROBOT_ENCODER_ADDR)) {
            dfRobotHubChannel[dfFound] = ch;
            dfEncoders[dfFound] = new DFRobot_VisualRotaryEncoder_I2C(DFROBOT_ENCODER_ADDR, &Wire);
            // begin() and setGainCoefficient() do I2C — protect with scoped lock
            if (i2c_lock(50)) {
                i2c_hub_select_raw(ch);
                if (dfEncoders[dfFound]->begin() == 0) {
                    dfEncoderConnected[dfFound] = true;
                    dfEncoders[dfFound]->setGainCoefficient(12);
                    // Stabilize startup: set a known center and baseline to avoid first-read jumps
                    dfEncoders[dfFound]->setEncoderValue(512);
                    prevDFValue[dfFound] = 512;
                    Serial.printf("[I2C] DFRobot #%d found on ch %d\n", dfFound + 1, ch);
                } else {
                    delete dfEncoders[dfFound];
                    dfEncoders[dfFound] = nullptr;
                }
                i2c_hub_deselect_raw();
                i2c_unlock();
            }
            dfFound++;
        }

        // Re-select hub channel before ByteButton probe
        // (M5/DFRobot init blocks above may have deselected the hub,
        //  causing a false detection on the direct bus)
        if (!byteButtonFound) {
            i2c_hub_select(ch);
            delay(2);
            if (i2c_device_present(BYTEBUTTON_ADDR)) {
                byteButtonHubChannel = ch;
                byteButtonConnected = true;
                byteButtonFound = true;
                prevByteButtonState = 0;
                Serial.printf("[I2C] M5 ByteButton found on hub ch %d\n", ch);

                if (i2c_lock(50)) {
                    i2c_hub_select_raw(ch);
                    byteButtonLedInitialized = byteButtonApplyLedConfigLocked();
                    i2c_hub_deselect_raw();
                    i2c_unlock();
                }
            }
        }

        i2c_hub_deselect();
    }

    // Fallback: try ByteButton directly on bus (not behind hub)
    if (!byteButtonFound) {
        i2c_hub_deselect();  // deselect all hub channels
        delay(5);
        if (i2c_device_present(BYTEBUTTON_ADDR)) {
            byteButtonHubChannel = -2;  // sentinel: direct bus, no hub channel
            byteButtonConnected = true;
            byteButtonFound = true;
            prevByteButtonState = 0;
            Serial.println("[I2C] M5 ByteButton found DIRECT on bus (no hub)");

            if (i2c_lock(50)) {
                byteButtonLedInitialized = byteButtonApplyLedConfigLocked();
                i2c_unlock();
            }
        }
    }

    if (!byteButtonFound) {
        Serial.println("[I2C] WARNING: ByteButton NOT found on any channel or direct bus!");
    }

    diagInfo.m5encoder1Ok = m5encoderConnected[0];
    diagInfo.m5encoder2Ok = m5encoderConnected[1];
    diagInfo.dfrobot1Ok = dfEncoderConnected[0];
    diagInfo.dfrobot2Ok = dfEncoderConnected[1];
    diagInfo.byteButtonOk = byteButtonConnected;

    Serial.printf("[I2C] Found: %d M5 modules, %d DFRobot rotaries, ByteButton: %s\n",
                  m5Found, dfFound, byteButtonConnected ? "YES" : "NO");
}

void updateByteButtonLeds() {
    if (!byteButtonConnected) return;
    if (!byteButtonLedInitialized) {
        initByteButtonLeds();
        if (!byteButtonLedInitialized) return;
    }

    uint32_t desired[BYTEBUTTON_LED_COUNT] = {};
    static const Screen navTargets[7] = {
        SCREEN_LIVE,
        SCREEN_SEQUENCER,
        SCREEN_VOLUMES,
        SCREEN_FILTERS,
        SCREEN_PATTERNS,
        SCREEN_SETTINGS,
        SCREEN_DIAGNOSTICS,
    };
    uint32_t navColors[7];
    for (int nc = 0; nc < 7; nc++) navColors[nc] = theme_nav_color(nc);

    if (currentScreen == SCREEN_LIVE) {
        for (int i = 0; i < BYTEBUTTON_BUTTONS; i++) {
            desired[i] = byteButtonLivePressed[i] ? 0xFFFFFF : byteButtonColorForPad(i);
        }
        desired[8] = isPlaying ? theme_nav_color(2) : theme_nav_color(0);
    } else {
        for (int i = 0; i < 7; i++) {
            desired[i] = (currentScreen == navTargets[i]) ? 0xFFFFFF : navColors[i];
        }
        desired[7] = isPlaying ? theme_nav_color(2) : theme_nav_color(0);
        desired[8] = masterConnected ? theme_nav_color(1) : 0x222222;
    }

    bool hasChanges = false;
    for (int i = 0; i < BYTEBUTTON_LED_COUNT; i++) {
        if (byteButtonLedCache[i] != desired[i]) {
            hasChanges = true;
            break;
        }
    }
    if (!hasChanges) return;
    if (!i2c_lock(25)) return;

    if (byteButtonHubChannel >= 0) i2c_hub_select_raw(byteButtonHubChannel);
    else if (byteButtonHubChannel == -2) i2c_hub_deselect_raw();
    for (int i = 0; i < BYTEBUTTON_LED_COUNT; i++) {
        if (byteButtonLedCache[i] == desired[i]) continue;
        uint32_t color = desired[i];
        if (!byteButtonWriteBytes(BYTEBUTTON_LED_RGB888_REG + i * 4, (uint8_t*)&color, 4)) {
            byteButtonLedInitialized = false;
            break;
        }
        byteButtonLedCache[i] = desired[i];
    }
    if (byteButtonHubChannel >= 0) i2c_hub_deselect_raw();
    i2c_unlock();
}

// =============================================================================
// M5 ROTATE8 HANDLING
// =============================================================================

void updateTrackEncoderLED(int track) {
    int moduleIndex = track / ENCODERS_PER_MODULE;
    int encoderIndex = track % ENCODERS_PER_MODULE;
    if (!m5encoderConnected[moduleIndex]) return;

    int ch = m5HubChannel[moduleIndex];
    if (ch < 0) return;
    if (!i2c_lock(20)) return;
    i2c_hub_select_raw(ch);

    if (trackMuted[track]) {
        m5encoders[moduleIndex].writeRGB(encoderIndex, 0x1E, 0, 0);
    } else {
        uint8_t rgb[3];
        theme_encoder_color(track, rgb);
        uint8_t brightness = map(trackVolumes[track], 0, Config::MAX_VOLUME, 10, 255);
        uint8_t r = (rgb[0] * brightness) / 255;
        uint8_t g = (rgb[1] * brightness) / 255;
        uint8_t b = (rgb[2] * brightness) / 255;
        m5encoders[moduleIndex].writeRGB(encoderIndex, r, g, b);
    }

    i2c_hub_deselect_raw();
    i2c_unlock();
}

// Inline helper: write M5 encoder LED with volume-scaled brightness. Must be called inside i2c_lock.
static inline void m5_write_track_led(M5ROTATE8& mod, int enc, int track) {
    uint8_t rgb[3];
    theme_encoder_color(track, rgb);
    uint8_t br = (uint8_t)map(trackVolumes[track], 0, Config::MAX_VOLUME, 10, 255);
    mod.writeRGB(enc, (rgb[0]*br)/255, (rgb[1]*br)/255, (rgb[2]*br)/255);
}

void handleM5Encoders() {
    static bool prevBtnState[Config::MAX_TRACKS] = {};
    static unsigned long lastBtnToggle[Config::MAX_TRACKS] = {};
    static constexpr unsigned long BTN_DEBOUNCE_MS = 250;

    // Collect changes inside I2C lock, send UDP outside to avoid blocking the bus
    struct PendingMsg { uint8_t type; int track; int value; }; // 0=vol, 1=mute
    PendingMsg pending[Config::MAX_TRACKS * 2];
    int pendingCount = 0;

    for (int mod = 0; mod < M5_ENCODER_MODULES; mod++) {
        if (!m5encoderConnected[mod]) continue;
        int ch = m5HubChannel[mod];
        if (ch < 0) continue;

        if (!i2c_lock(20)) continue;
        i2c_hub_select_raw(ch);

        for (int enc = 0; enc < ENCODERS_PER_MODULE; enc++) {
            int track = mod * ENCODERS_PER_MODULE + enc;

            int32_t counter = m5encoders[mod].getAbsCounter(enc);
            int32_t delta = counter - prevM5Counter[track];
            prevM5Counter[track] = counter;

            if (delta != 0) {
                int newVol = constrain(trackVolumes[track] + delta * 3, 0, Config::MAX_VOLUME);
                if (newVol != trackVolumes[track]) {
                    trackVolumes[track] = newVol;
                    if (!trackMuted[track]) m5_write_track_led(m5encoders[mod], enc, track);
                    pending[pendingCount++] = {0, track, newVol};
                }
            }

            // Button: edge detection + debounce
            bool btnNow = m5encoders[mod].getKeyPressed(enc);
            bool btnPrev = prevBtnState[track];
            prevBtnState[track] = btnNow;

            if (btnNow && !btnPrev) {
                unsigned long now = millis();
                if ((now - lastBtnToggle[track]) >= BTN_DEBOUNCE_MS) {
                    lastBtnToggle[track] = now;
                    trackMuted[track] = !trackMuted[track];
                    if (trackMuted[track]) m5encoders[mod].writeRGB(enc, 0x1E, 0, 0);
                    else                   m5_write_track_led(m5encoders[mod], enc, track);
                    pending[pendingCount++] = {1, track, (int)trackMuted[track]};
                }
            }
        }

        i2c_hub_deselect_raw();
        i2c_unlock();
    }

    // Send all queued UDP messages OUTSIDE i2c lock (WiFi must not hold I2C mutex)
    for (int i = 0; i < pendingCount; i++) {
        JsonDocument doc;
        if (pending[i].type == 0) {
            doc["cmd"] = "setTrackVolume";
            doc["track"] = pending[i].track;
            doc["volume"] = pending[i].value;
        } else {
            doc["cmd"] = "mute";
            doc["track"] = pending[i].track;
            doc["value"] = (bool)pending[i].value;
            Serial.printf("[M5] Track %d %s\n", pending[i].track, pending[i].value ? "MUTED" : "UNMUTED");
        }
        sendUDPCommand(doc);
    }
}

// =============================================================================
// DFROBOT ROTARY HANDLING
// =============================================================================

void handleDFRobotEncoders() {
    static unsigned long lastBtnMs[DFROBOT_ENCODER_COUNT] = {0, 0};

    for (int i = 0; i < DFROBOT_ENCODER_COUNT; i++) {
        if (!dfEncoderConnected[i]) continue;

        int ch = dfRobotHubChannel[i];
        if (ch < 0) continue;
        if (!i2c_lock(20)) continue;
        i2c_hub_select_raw(ch);

        uint16_t val = dfEncoders[i]->getEncoderValue() & 0x03FF; // 10-bit range: 0..1023
        int16_t rawDelta = (int16_t)val - (int16_t)(prevDFValue[i] & 0x03FF);
        if (rawDelta > 512) rawDelta -= 1024;
        if (rawDelta < -512) rawDelta += 1024;

        int16_t delta = rawDelta;
        if (abs((int)rawDelta) > Config::DF_GLITCH_THRESHOLD) {
            delta = 0;
        } else {
            if (delta > Config::DF_DELTA_CLAMP) delta = Config::DF_DELTA_CLAMP;
            if (delta < -Config::DF_DELTA_CLAMP) delta = -Config::DF_DELTA_CLAMP;
            if (abs((int)delta) <= 1) delta = 0;
        }
        prevDFValue[i] = val;

        bool buttonPressed = dfEncoders[i]->detectButtonDown();
        if (buttonPressed) {
            Serial.printf("[DFRobot] Encoder #%d button PRESSED\n", i);
        }

        i2c_hub_deselect_raw();
        i2c_unlock();

        // Process results outside of I2C lock
        if (i == 0) {
            // DFRobot #0: Volume (SEQ or PAD based on volumeMode toggle)
            //   Button = Play/Pause
            int logical_delta = quantizeDFDelta(i, delta);
            if (logical_delta != 0) {
                if (volumeMode == VOL_LIVE_PADS) {
                    // Modify live pads volume
                    int newVol = constrain(livePadsVolume + logical_delta * Config::DF_VOLUME_STEP, 0, Config::MAX_VOLUME);
                    if (newVol != livePadsVolume) {
                        livePadsVolume = newVol;
                        JsonDocument doc;
                        doc["cmd"] = "setLiveVolume";
                        doc["value"] = livePadsVolume;
                        sendUDPCommand(doc);
                    }
                } else {
                    // Modify sequencer volume
                    int newVol = constrain(sequencerVolume + logical_delta * Config::DF_VOLUME_STEP, 0, Config::MAX_VOLUME);
                    if (newVol != sequencerVolume) {
                        sequencerVolume = newVol;
                        JsonDocument doc;
                        doc["cmd"] = "setSequencerVolume";
                        doc["value"] = sequencerVolume;
                        sendUDPCommand(doc);
                    }
                }
            }
            if (buttonPressed && (millis() - lastBtnMs[i]) > Config::DF_BUTTON_GUARD_MS) {
                lastBtnMs[i] = millis();
                sendPlayStateCommand(!isPlaying);
            }
        }
        else if (i == 1) {
            // DFRobot #1: BPM (1 per step) + BACK button
            int logical_delta = quantizeDFDelta(i, delta);
            if (logical_delta != 0) {
                int newBPM = constrain(currentBPM + logical_delta * Config::DF_BPM_STEP, Config::MIN_BPM, Config::MAX_BPM);
                if (newBPM != currentBPM) {
                    currentBPM = newBPM;
                    JsonDocument doc;
                    doc["cmd"] = "tempo";
                    doc["value"] = currentBPM;
                    sendUDPCommand(doc);
                }
            }
            if (buttonPressed && (millis() - lastBtnMs[i]) > Config::DF_BUTTON_GUARD_MS) {
                lastBtnMs[i] = millis();
                navigateToScreen(SCREEN_MENU);
            }
        }
    }
}

// =============================================================================
// ANALOG ROTARY ENCODER (GPIO6 - Pattern Select)
// =============================================================================

// =============================================================================
// ANALOG ROTARY ENCODER (GPIO6) — 12-position FX preset selector
// pos  0 = NO FX (todo off)
// pos  1..11 = 11 presets FX predefinidos
// =============================================================================
struct FxPreset {
    const char* name;
    bool  delayEn;  uint8_t delayAmt;    // delay amount 0-127
    bool  flanEn;   uint8_t flanAmt;     // flanger amount 0-127
    bool  compEn;   uint8_t compAmt;     // compressor amount 0-127
};

static const FxPreset kFxPresets[12] = {
    // pos  0 — OFF
    { "FX OFF",      false,  0, false,  0, false,  0 },
    // pos  1 — Slight delay, dry
    { "ROOM",        true,  32, false,  0, false,  0 },
    // pos  2 — Medium delay
    { "DELAY",       true,  72, false,  0, false,  0 },
    // pos  3 — Heavy slapback
    { "SLAPBACK",    true, 110, false,  0, false,  0 },
    // pos  4 — Soft flanger
    { "FLANGE LO",   false,  0, true,  28, false,  0 },
    // pos  5 — Strong flanger
    { "FLANGE HI",   false,  0, true,  80, false,  0 },
    // pos  6 — Light compression
    { "COMP SOFT",   false,  0, false,  0, true,  30 },
    // pos  7 — Hard compression
    { "COMP HARD",   false,  0, false,  0, true,  90 },
    // pos  8 — Delay + light comp
    { "SPACE",       true,  55, false,  0, true,  40 },
    // pos  9 — Delay + flanger
    { "CHORUS",      true,  40, true,  50, false,  0 },
    // pos 10 — All FX medium
    { "FULL FX",     true,  60, true,  45, true,  50 },
    // pos 11 — All FX heavy
    { "DESTROY",     true, 120, true, 100, true, 100 },
};

static void applyFxPreset(int pos) {
    pos = constrain(pos, 0, 11);
    const FxPreset& p = kFxPresets[pos];

    masterFilter.enabled      = p.delayEn || p.flanEn || p.compEn;
    masterFilter.delayAmount   = p.delayAmt;
    masterFilter.flangerAmount = p.flanAmt;
    masterFilter.compAmount    = p.compAmt;

    sendFilterUDP(-1, FILTER_DELAY);
    sendFilterUDP(-1, FILTER_FLANGER);
    sendFilterUDP(-1, FILTER_COMPRESSOR);
    Serial.printf("[ROTARY FX] pos=%d '%s'\n", pos, p.name);
}

void handleAnalogEncoder() {
    static int  lastPosition = -1;
    static int  stableRaw    = -1;   // last accepted raw after deadband filter
    static unsigned long lastChangeMs = 0;
    // Self-calibrating ADC range: tracks actual min/max seen from the hardware.
    // Many analog rotary encoders only span a fraction of the 0-3.3V range;
    // calibrating automatically avoids hard-coding wrong limits.
    static int  calMin = 4095;  // lowest ADC value ever read
    static int  calMax = 0;     // highest ADC value ever read

    // 16-sample average — reduces ESP32-S3 ADC noise
    long sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(Config::ANALOG_ENC_PIN);
    int raw = (int)(sum / 16);

    // Update calibration bounds continuously
    if (raw < calMin) calMin = raw;
    if (raw > calMax) calMax = raw;

    // Deadband: ignore small fluctuations around the last stable reading.
    if (stableRaw >= 0 && abs(raw - stableRaw) < 60) return;
    stableRaw = raw;

    // Map using calibrated range once we have seen enough spread (>= 200 ADC counts).
    // Until calibrated, fall back to full-range formula.
    int position;
    if ((calMax - calMin) >= 200) {
        position = constrain((int)map(raw, calMin, calMax, 0, 11), 0, 11);
    } else {
        position = constrain((raw * 12 + 2048) / 4096, 0, 11);
    }

    if (position == lastPosition) return;
    if ((millis() - lastChangeMs) < 150) return;  // short debounce after detent cross

    lastPosition = position;
    lastChangeMs = millis();

    analogFxPreset = position;
    applyFxPreset(position);
    Serial.printf("[ROTARY] raw=%d cal=[%d..%d] pos=%d\n", raw, calMin, calMax, position);
}

// =============================================================================
// BYTEBUTTON HANDLING
// =============================================================================

void handleByteButton() {
    if (!byteButtonConnected) return;

    // Read 1-byte bitmask from register 0x00.  Bit N = 1 if button N is pressed.
    // (Reverted from 8-byte reg 0x60: block-read on I2C devices without guaranteed
    //  address auto-increment returned the same byte 8 times, causing ALL pads to
    //  fire simultaneously and "go crazy" in LIVE mode.)
    uint8_t status = 0;
    bool readOk = false;

    if (i2c_lock(20)) {
        if (byteButtonHubChannel >= 0) i2c_hub_select_raw(byteButtonHubChannel);
        else if (byteButtonHubChannel == -2) i2c_hub_deselect_raw();

        Wire.beginTransmission(BYTEBUTTON_ADDR);
        Wire.write(BYTEBUTTON_STATUS_REG);
        if (Wire.endTransmission(false) == 0 &&
            Wire.requestFrom((uint8_t)BYTEBUTTON_ADDR, (uint8_t)1) == 1) {
            status = Wire.read();
            readOk = true;
        }

        if (byteButtonHubChannel >= 0) i2c_hub_deselect_raw();
        i2c_unlock();
    }

    if (!readOk) return;

    uint8_t previousState = prevByteButtonState;
    uint8_t pressedEdges = status & (uint8_t)~previousState;
    prevByteButtonState = status;

    unsigned long now = millis();
    if (currentScreen == SCREEN_LIVE) {
        // Guard: skip ByteButton triggers during guard period (avoids phantom pads on entry)
        extern unsigned long liveScreenEnteredMs;
        if ((now - liveScreenEnteredMs) < LIVE_TOUCH_GUARD_MS) {
            memset(byteButtonLivePressed, 0, sizeof(byteButtonLivePressed));
            return;
        }
        int velocity = map(constrain(livePadsVolume, 0, Config::MAX_VOLUME), 0, Config::MAX_VOLUME, 32, 127);

        // Edge-only: fire once per physical press, no repeat
        for (int button = 0; button < BYTEBUTTON_BUTTONS && button < Config::MAX_SAMPLES; button++) {
            byteButtonLivePressed[button] = false;  // don't feed repeat loop
            if ((pressedEdges & (1U << button)) == 0) continue;
            sendLivePadTrigger(button, velocity);
        }
    } else {
        memset(byteButtonLivePressed, 0, sizeof(byteButtonLivePressed));
        for (int button = 0; button < BYTEBUTTON_BUTTONS; button++) {
            if ((pressedEdges & (1U << button)) == 0) continue;

            switch (button) {
                case 0: // Pattern --
                    if (currentPattern > 0) selectPatternOnMaster(currentPattern - 1);
                    break;
                case 1: // Pattern ++
                    if (currentPattern < Config::MAX_PATTERNS - 1) selectPatternOnMaster(currentPattern + 1);
                    break;
                case 2: // Pattern = 1 (index 0)
                    selectPatternOnMaster(0);
                    break;
                case 3: // Toggle volume mode SEQ <-> PAD
                    volumeMode = (volumeMode == VOL_SEQUENCER) ? VOL_LIVE_PADS : VOL_SEQUENCER;
                    Serial.printf("[BB] Volume mode: %s\n", volumeMode == VOL_SEQUENCER ? "SEQ" : "PAD");
                    break;
                case 4: { // FX Delay toggle
                    masterFilter.enabled = !masterFilter.enabled || (filterSelectedFX != FILTER_DELAY);
                    filterSelectedFX = FILTER_DELAY;
                    if (!masterFilter.enabled) masterFilter.delayAmount = 0;
                    else if (masterFilter.delayAmount == 0) masterFilter.delayAmount = 64;
                    sendFilterUDP(-1, FILTER_DELAY);
                    Serial.printf("[BB] FX DELAY %s\n", masterFilter.enabled ? "ON" : "OFF");
                    break;
                }
                case 5: { // FX Flanger toggle
                    masterFilter.enabled = !masterFilter.enabled || (filterSelectedFX != FILTER_FLANGER);
                    filterSelectedFX = FILTER_FLANGER;
                    if (!masterFilter.enabled) masterFilter.flangerAmount = 0;
                    else if (masterFilter.flangerAmount == 0) masterFilter.flangerAmount = 64;
                    sendFilterUDP(-1, FILTER_FLANGER);
                    Serial.printf("[BB] FX FLANGER %s\n", masterFilter.enabled ? "ON" : "OFF");
                    break;
                }
                case 6: { // FX Compressor toggle
                    masterFilter.enabled = !masterFilter.enabled || (filterSelectedFX != FILTER_COMPRESSOR);
                    filterSelectedFX = FILTER_COMPRESSOR;
                    if (!masterFilter.enabled) masterFilter.compAmount = 0;
                    else if (masterFilter.compAmount == 0) masterFilter.compAmount = 64;
                    sendFilterUDP(-1, FILTER_COMPRESSOR);
                    Serial.printf("[BB] FX COMPRESSOR %s\n", masterFilter.enabled ? "ON" : "OFF");
                    break;
                }
                case 7: { // Clear ALL FX
                    masterFilter.enabled = false;
                    masterFilter.delayAmount = 0;
                    masterFilter.flangerAmount = 0;
                    masterFilter.compAmount = 0;
                    sendFilterUDP(-1, FILTER_DELAY);
                    sendFilterUDP(-1, FILTER_FLANGER);
                    sendFilterUDP(-1, FILTER_COMPRESSOR);
                    Serial.println("[BB] ALL FX CLEARED");
                    break;
                }
            }
        }
    }

    updateByteButtonLeds();
}

// =============================================================================
// UI UPDATE (called from loop on Core 0, LVGL runs on Core 1)
// =============================================================================

void updateUI() {
    // Handle theme change: update currentScreen and refresh M5 LEDs
    if (themeJustChanged) {
        themeJustChanged = false;
        currentScreen = SCREEN_SETTINGS;
        // Refresh all M5 encoder LEDs with new theme colors
        for (int t = 0; t < Config::MAX_TRACKS; t++) {
            updateTrackEncoderLED(t);
        }
    }

    if (!lvgl_port_lock(5)) return;

    ui_update_header();

    switch (currentScreen) {
        case SCREEN_MENU:        ui_update_menu_status(); break;
        case SCREEN_LIVE:        ui_update_live_pads(); break;
        case SCREEN_SEQUENCER:   ui_update_sequencer(); break;
        case SCREEN_SEQ_CIRCLE:  ui_update_seq_circle(); break;
        case SCREEN_VOLUMES:     ui_update_volumes();   break;
        case SCREEN_FILTERS:     ui_update_filters();   break;
        case SCREEN_PATTERNS:    ui_update_patterns();  break;
        case SCREEN_SDCARD:    ui_update_sdcard();    break;
        case SCREEN_DIAGNOSTICS: ui_update_diagnostics(); break;
        default: break;
    }

    lvgl_port_unlock();
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    // Wait up to 3s for USB CDC, skip wait if no monitor connected
    { unsigned long _t = millis(); while (!Serial && (millis()-_t) < 3000) delay(10); }
    delay(50);
    Serial.println("\n====================================");
    Serial.println("  RED808 V6 - BlueSlaveV2");
    Serial.println("  Waveshare ESP32-S3-Touch-LCD-7B");
    Serial.println("====================================\n");

    // Check PSRAM
    if (psramFound()) {
        Serial.printf("[PSRAM] OK - %d bytes free\n", ESP.getFreePsram());
    } else {
        Serial.println("[PSRAM] WARNING: PSRAM not found!");
    }
    Serial.printf("[HEAP] Free: %d bytes\n", ESP.getFreeHeap());

    // Init state
    initState();
    Serial.println("[STATE] Initialized");

    // 1. I2C bus
    i2c_init();
    delay(50);
    Serial.println("[I2C] Bus initialized");

    // 2. IO Extension (CH32V003) - backlight, resets
    io_ext_init();
    io_ext_backlight_on();
    delay(10);
    Serial.println("[IO] CH32V003 initialized, backlight ON");

    // 3. LCD panel
    Serial.println("[LCD] Initializing RGB panel...");
    lcd_panel = rgb_lcd_init();
    if (!lcd_panel) {
        Serial.println("[LCD] FATAL: Panel init failed!");
    } else {
        Serial.println("[LCD] Panel initialized OK");
        diagInfo.lcdOk = true;
    }

    // 4. Touch controller
    Serial.println("[Touch] Initializing GT911...");
    gt911_init();
    diagInfo.touchOk = gt911_is_ready();
    Serial.printf("[Touch] GT911 %s\n", diagInfo.touchOk ? "OK" : "NOT DETECTED");

    // 5. LVGL init (task created but NOT started yet - no I2C contention)
    Serial.println("[LVGL] Initializing port...");
    lvgl_port_init(lcd_panel);
    Serial.println("[LVGL] Port initialized (task paused)");

    // 6. Analog rotary encoder GPIO setup
    pinMode(Config::ANALOG_ENC_PIN, INPUT);
    analogReadResolution(12);  // 12-bit ADC (0-4095)
    Serial.println("[ENC] Analog rotary on GPIO6 initialized");

    // 7. Scan I2C hub for M5 + DFRobot + ByteButton (before LVGL task starts - no I2C race)
    scanI2CHub();

    // 8. WiFi + UDP — init BEFORE LVGL screens to reserve internal SRAM for WiFi DMA buffers
    Serial.printf("[HEAP] Before WiFi: %d bytes\n", ESP.getFreeHeap());
    setupWiFi();
    Serial.printf("[HEAP] After WiFi: %d bytes\n", ESP.getFreeHeap());

    // 9. Now start LVGL task (safe - I2C scanning is done)
    lvgl_port_task_start();
    Serial.println("[LVGL] Task started");

    // 10. Create all UI screens (must be inside LVGL lock)
    //    Widgets now allocate from PSRAM via lv_conf.h, keeping internal heap free
    Serial.println("[UI] Creating screens...");
    if (lvgl_port_lock(1000)) {
        ui_theme_init();

        ui_create_boot_screen();      // boot animation — shown first; auto-navigates to menu
        ui_create_seq_circle_screen();
        ui_create_menu_screen();
        ui_create_live_screen();
        ui_create_sequencer_screen();
        ui_create_volumes_screen();
        ui_create_filters_screen();
        ui_create_settings_screen();
        ui_create_diagnostics_screen();
        ui_create_patterns_screen();
        ui_create_sdcard_screen();
        ui_create_performance_screen();
        ui_create_samples_screen();

        // Start on boot animation; boot_timer_cb() will navigate to SCREEN_MENU when complete
        currentScreen = SCREEN_BOOT;
        lv_scr_load(scr_boot);

        lvgl_port_unlock();
        Serial.println("[UI] All screens created");
    }
    Serial.printf("[HEAP] After UI: %d bytes  PSRAM: %d bytes\n", ESP.getFreeHeap(), ESP.getFreePsram());

    Serial.println("\n[SETUP] Complete! Entering main loop.\n");
}

// =============================================================================
// LOOP (Core 0 - hardware polling + network)
// =============================================================================

void loop() {
    unsigned long now = millis();

    if (masterConnected && (now - lastMasterPacketMs > 3000)) {
        masterConnected = false;
    }

    // WiFi reconnection check
    checkWiFiReconnect();

    // Receive UDP data
    receiveUDPData();

    if (wifiConnected && udpConnected && !masterConnected && (now - lastUDPCheck >= 2000)) {
        lastUDPCheck = now;
        Serial.println("[UDP] Master not responding, resending hello...");
        requestMasterSync();
    }

    handleLivePadTouchMatrix(now);

    // Velocity computed once for all pad triggers this frame
    const int padVelocity = map(constrain(livePadsVolume, 0, Config::MAX_VOLUME), 0, Config::MAX_VOLUME, 32, 127);

    uint32_t pendingMask = pendingLivePadTriggerMask;
    if (pendingMask != 0) {
        pendingLivePadTriggerMask = 0;
        for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
            if ((pendingMask & (1UL << pad)) == 0) continue;
            sendLivePadTrigger(pad, padVelocity);
            lastLivePadTriggerMs[pad] = now;
        }
    }

    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (!livePadPressed[pad]) continue;
        if ((now - lastLivePadTriggerMs[pad]) < Config::LIVE_PAD_REPEAT_MS) continue;
        sendLivePadTrigger(pad, padVelocity);
        lastLivePadTriggerMs[pad] = now;
    }

    // ByteButton: single-shot on press edge only (no repeat — physical buttons sustain too long)

    // Local step clock: PRIMARY driver of currentStep (same approach as old project).
    // Master step_update is ignored for position — UDP packet loss makes it unreliable.
    // BPM is synced via tempo_sync, so local clock stays accurate (<1ms drift/step).
    // SNAP-TO-NOW: never catch up — max 1 step per loop iteration = smooth visual always.
    if (isPlaying && currentBPM > 0) {
        unsigned long step_ms = 60000UL / (unsigned long)currentBPM / 4;
        if (step_ms < 50) step_ms = 50;
        if ((now - lastLocalStepMs) >= step_ms) {
            lastLocalStepMs = now;  // snap to now — NEVER accumulate, NEVER catch up
            currentStep = (currentStep + 1) % Config::MAX_STEPS;
        }
    } else if (!isPlaying) {
        lastLocalStepMs = now;
    }

    // Encoder polling (50ms interval)
    if (now - lastEncoderRead >= Config::ENCODER_READ_MS) {
        lastEncoderRead = now;
        handleM5Encoders();
        handleDFRobotEncoders();
        handleAnalogEncoder();
        handleByteButton();  // calls updateByteButtonLeds() internally
    }

    // UI update (~60fps)
    if (now - lastScreenUpdate >= Config::SCREEN_UPDATE_MS) {
        lastScreenUpdate = now;
        updateUI();
    }

    // Debug heartbeat every 10s
    static unsigned long lastHeartbeat = 0;
    static bool firstDiagDone = false;
    if (!firstDiagDone && now > 5000) {
        firstDiagDone = true;
        Serial.println("\n=== RUNTIME DIAGNOSTICS ===");
        Serial.printf("  GT911 touch: %s\n", gt911_is_ready() ? "OK" : "NOT DETECTED");
        Serial.printf("  I2C Hub:     %s\n", hubDetected ? "OK" : "NOT FOUND");
        Serial.printf("  M5 #1:       %s (ch=%d)\n", m5encoderConnected[0] ? "OK" : "NO", m5HubChannel[0]);
        Serial.printf("  M5 #2:       %s (ch=%d)\n", m5encoderConnected[1] ? "OK" : "NO", m5HubChannel[1]);
        Serial.printf("  DFRobot #1:  %s (ch=%d)\n", dfEncoderConnected[0] ? "OK" : "NO", dfRobotHubChannel[0]);
        Serial.printf("  DFRobot #2:  %s (ch=%d)\n", dfEncoderConnected[1] ? "OK" : "NO", dfRobotHubChannel[1]);
        Serial.printf("  ByteButton:  %s (ch=%d)\n", byteButtonConnected ? "OK" : "NO", byteButtonHubChannel);
        Serial.printf("  LCD panel:   %s\n", lcd_panel ? "OK" : "FAIL");
        Serial.printf("  Heap: %d  PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
        Serial.println("===========================\n");
    }
    if (now - lastHeartbeat >= 10000) {
        lastHeartbeat = now;
        Serial.printf("[ALIVE] %lus Heap:%d PSRAM:%d\n", now/1000, ESP.getFreeHeap(), ESP.getFreePsram());
    }

    delay(1); // Yield to other tasks
}
