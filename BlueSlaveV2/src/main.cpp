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
#include <M5ROTATE8.h>
#include <DFRobot_VisualRotaryEncoder.h>
#include "lvgl.h"

#include "config.h"
#include "system_state.h"

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

// Sequencer
Pattern patterns[Config::MAX_PATTERNS];
int currentPattern = 0;
int currentStep = 0;
int selectedTrack = 0;
bool isPlaying = false;
int currentBPM = Config::DEFAULT_BPM;
int currentKit = 0;

// Volume
int sequencerVolume = Config::DEFAULT_VOLUME;
int livePadsVolume = 100;
int trackVolumes[Config::MAX_TRACKS];
VolumeMode volumeMode = VOL_SEQUENCER;
bool trackMuted[Config::MAX_TRACKS];

// Filters
TrackFilter trackFilters[Config::MAX_TRACKS];
TrackFilter masterFilter = {false, 0, 0, 0};
int filterSelectedTrack = -1;  // -1 = master
int filterSelectedFX = FILTER_DELAY;
EncoderMode encoderMode = ENC_MODE_VOLUME;

// I2C Hub
int m5HubChannel[M5_ENCODER_MODULES] = {-1, -1};
int dfRobotHubChannel[DFROBOT_ENCODER_COUNT] = {-1, -1};
bool hubDetected = false;

// Connection
bool udpConnected = false;
bool wifiConnected = false;
bool wifiReconnecting = false;

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
esp_lcd_panel_handle_t lcd_panel = NULL;

// Encoder LED colors per track
uint8_t encoderLEDColors[Config::MAX_TRACKS][3] = {
    {255, 0,   0},   {255, 128, 0},   {255, 255, 0},   {128, 255, 0},
    {0,   255, 0},   {0,   255, 128}, {0,   255, 255}, {0,   128, 255},
    {0,   0,   255}, {128, 0,   255}, {255, 0,   255}, {255, 0,   128},
    {200, 100, 50},  {100, 200, 100}, {50,  100, 200}, {200, 200, 200}
};

// Timing
unsigned long lastEncoderRead = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastUDPCheck = 0;
int32_t prevM5Counter[Config::MAX_TRACKS];
uint16_t prevDFValue[DFROBOT_ENCODER_COUNT];

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
void sendFilterUDP(int track, int fxType);
void scanI2CHub();
void handleM5Encoders();
void handleDFRobotEncoders();
void updateTrackEncoderLED(int track);
void updateUI();

// =============================================================================
// STATE INIT
// =============================================================================

void initState() {
    for (int i = 0; i < Config::MAX_TRACKS; i++) {
        trackVolumes[i] = Config::DEFAULT_TRACK_VOLUME;
        trackMuted[i] = false;
        trackFilters[i] = {false, 0, 0, 0};
    }
    for (int p = 0; p < Config::MAX_PATTERNS; p++) {
        memset(patterns[p].steps, 0, sizeof(patterns[p].steps));
        memset(patterns[p].muted, 0, sizeof(patterns[p].muted));
        patterns[p].name = "Pattern " + String(p + 1);
    }
    for (int i = 0; i < Config::MAX_TRACKS; i++) prevM5Counter[i] = 0;
    for (int i = 0; i < DFROBOT_ENCODER_COUNT; i++) prevDFValue[i] = 0;
}

// =============================================================================
// WiFi / UDP
// =============================================================================

void setupWiFi() {
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
        udp.begin(WiFiConfig::UDP_PORT);
        udpConnected = true;
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

        // Say hello to master
        JsonDocument doc;
        doc["cmd"] = "hello";
        doc["device"] = "SURFACE_V2";
        sendUDPCommand(doc);

        requestPatternFromMaster();
    } else {
        Serial.println("\n[WiFi] Connection failed");
        wifiConnected = false;
    }
    diagInfo.wifiOk = wifiConnected;
    diagInfo.udpConnected = udpConnected;
}

void checkWiFiReconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            wifiConnected = true;
            udpConnected = true;
            wifiReconnecting = false;
            diagInfo.wifiOk = true;
            diagInfo.udpConnected = true;

            JsonDocument doc;
            doc["cmd"] = "hello";
            doc["device"] = "SURFACE_V2";
            sendUDPCommand(doc);
            requestPatternFromMaster();
        }
        return;
    }

    wifiConnected = false;
    udpConnected = false;
    diagInfo.wifiOk = false;
    diagInfo.udpConnected = false;

    if (!wifiReconnecting && (millis() - lastWiFiCheck > WiFiConfig::RECONNECT_INTERVAL_MS)) {
        lastWiFiCheck = millis();
        wifiReconnecting = true;
        WiFi.disconnect(false);
        delay(50);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);
    }
}

void sendUDPCommand(const char* cmd) {
    if (!udpConnected) return;
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

// =============================================================================
// UDP RECEIVE
// =============================================================================

void receiveUDPData() {
    if (!udpConnected) return;

    int packetSize = udp.parsePacket();
    if (packetSize == 0) return;

    char buf[1024];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = '\0';

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) return;

    const char* cmd = doc["cmd"];
    if (!cmd) return;

    if (strcmp(cmd, "pattern_sync") == 0) {
        int pat = doc["pattern"] | 0;
        if (pat >= 0 && pat < Config::MAX_PATTERNS) {
            JsonArray data = doc["data"];
            if (data) {
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
        currentStep = doc["step"] | 0;
    }
    else if (strcmp(cmd, "play_state") == 0) {
        isPlaying = doc["playing"] | false;
    }
    else if (strcmp(cmd, "tempo_sync") == 0) {
        currentBPM = doc["value"] | Config::DEFAULT_BPM;
    }
    else if (strcmp(cmd, "volume_seq_sync") == 0) {
        sequencerVolume = doc["value"] | Config::DEFAULT_VOLUME;
    }
    else if (strcmp(cmd, "volume_live_sync") == 0) {
        livePadsVolume = doc["value"] | 100;
    }
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

    for (uint8_t ch = 0; ch < 8 && (m5Found < M5_ENCODER_MODULES || dfFound < DFROBOT_ENCODER_COUNT); ch++) {
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
                    dfEncoders[dfFound]->setGainCoefficient(1);
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

        i2c_hub_deselect();
    }

    diagInfo.m5encoder1Ok = m5encoderConnected[0];
    diagInfo.m5encoder2Ok = m5encoderConnected[1];
    diagInfo.dfrobot1Ok = dfEncoderConnected[0];
    diagInfo.dfrobot2Ok = dfEncoderConnected[1];

    Serial.printf("[I2C] Found: %d M5 modules, %d DFRobot rotaries\n", m5Found, dfFound);
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
        uint8_t brightness = map(trackVolumes[track], 0, Config::MAX_VOLUME, 10, 255);
        uint8_t r = (encoderLEDColors[track][0] * brightness) / 255;
        uint8_t g = (encoderLEDColors[track][1] * brightness) / 255;
        uint8_t b = (encoderLEDColors[track][2] * brightness) / 255;
        m5encoders[moduleIndex].writeRGB(encoderIndex, r, g, b);
    }

    i2c_hub_deselect_raw();
    i2c_unlock();
}

void handleM5Encoders() {
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
                int newVol = constrain(trackVolumes[track] + delta * 5, 0, Config::MAX_VOLUME);
                if (newVol != trackVolumes[track]) {
                    trackVolumes[track] = newVol;
                    JsonDocument doc;
                    doc["cmd"] = "setTrackVolume";
                    doc["track"] = track;
                    doc["volume"] = trackVolumes[track];
                    sendUDPCommand(doc);
                    // LED update inline (already holding mutex + hub selected)
                    uint8_t brightness = map(trackVolumes[track], 0, Config::MAX_VOLUME, 10, 255);
                    uint8_t r = (encoderLEDColors[track][0] * brightness) / 255;
                    uint8_t g = (encoderLEDColors[track][1] * brightness) / 255;
                    uint8_t b = (encoderLEDColors[track][2] * brightness) / 255;
                    m5encoders[mod].writeRGB(enc, r, g, b);
                }
            }

            if (m5encoders[mod].getKeyPressed(enc)) {
                trackMuted[track] = !trackMuted[track];
                JsonDocument doc;
                doc["cmd"] = "mute";
                doc["track"] = track;
                doc["value"] = trackMuted[track];
                sendUDPCommand(doc);
                if (trackMuted[track]) {
                    m5encoders[mod].writeRGB(enc, 0x1E, 0, 0);
                } else {
                    uint8_t brightness = map(trackVolumes[track], 0, Config::MAX_VOLUME, 10, 255);
                    uint8_t r = (encoderLEDColors[track][0] * brightness) / 255;
                    uint8_t g = (encoderLEDColors[track][1] * brightness) / 255;
                    uint8_t b = (encoderLEDColors[track][2] * brightness) / 255;
                    m5encoders[mod].writeRGB(enc, r, g, b);
                }
            }
        }

        i2c_hub_deselect_raw();
        i2c_unlock();
    }
}

// =============================================================================
// DFROBOT ROTARY HANDLING
// =============================================================================

void handleDFRobotEncoders() {
    for (int i = 0; i < DFROBOT_ENCODER_COUNT; i++) {
        if (!dfEncoderConnected[i]) continue;

        int ch = dfRobotHubChannel[i];
        if (ch < 0) continue;
        if (!i2c_lock(20)) continue;
        i2c_hub_select_raw(ch);

        uint16_t val = dfEncoders[i]->getEncoderValue();
        int16_t delta = (int16_t)(val - prevDFValue[i]);
        prevDFValue[i] = val;

        bool buttonPressed = dfEncoders[i]->detectButtonDown();

        i2c_hub_deselect_raw();
        i2c_unlock();

        // Process results outside of I2C lock
        if (i == 0) {
            if (delta != 0) {
                TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
                uint8_t* param = nullptr;
                switch (filterSelectedFX) {
                    case FILTER_DELAY:      param = &f.delayAmount;   break;
                    case FILTER_FLANGER:    param = &f.flangerAmount; break;
                    case FILTER_COMPRESSOR: param = &f.compAmount;    break;
                }
                if (param) {
                    int newVal = constrain((int)*param + delta, 0, 127);
                    *param = (uint8_t)newVal;
                    f.enabled = (f.delayAmount > 0 || f.flangerAmount > 0 || f.compAmount > 0);
                    sendFilterUDP(filterSelectedTrack, filterSelectedFX);
                }
            }
            if (buttonPressed) {
                filterSelectedFX = (filterSelectedFX + 1) % FILTER_COUNT;
            }
        }
        else if (i == 1) {
            if (delta != 0) {
                int newPat = currentPattern + (delta > 0 ? 1 : -1);
                newPat = constrain(newPat, 0, Config::MAX_PATTERNS - 1);
                if (newPat != currentPattern) {
                    currentPattern = newPat;
                    requestPatternFromMaster();
                }
            }
            if (buttonPressed) {
                if (lvgl_port_lock(5)) {
                    currentScreen = SCREEN_MENU;
                    lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
                    lvgl_port_unlock();
                }
            }
        }
    }
}

// =============================================================================
// UI UPDATE (called from loop on Core 0, LVGL runs on Core 1)
// =============================================================================

void updateUI() {
    if (!lvgl_port_lock(5)) return;

    ui_update_header();

    switch (currentScreen) {
        case SCREEN_SEQUENCER: ui_update_sequencer(); break;
        case SCREEN_VOLUMES:   ui_update_volumes();   break;
        case SCREEN_FILTERS:   ui_update_filters();   break;
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
    delay(3000);  // Wait for USB CDC to be ready
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

    // 6. Scan I2C hub for M5 + DFRobot (before LVGL task starts - no I2C race)
    scanI2CHub();

    // 7. Now start LVGL task (safe - I2C scanning is done)
    lvgl_port_task_start();
    Serial.println("[LVGL] Task started");

    // 8. Create all UI screens (must be inside LVGL lock)
    Serial.println("[UI] Creating screens...");
    if (lvgl_port_lock(1000)) {
        ui_theme_init();

        ui_create_menu_screen();
        ui_create_live_screen();
        ui_create_sequencer_screen();
        ui_create_volumes_screen();
        ui_create_filters_screen();
        ui_create_settings_screen();
        ui_create_diagnostics_screen();
        ui_create_patterns_screen();

        // Start on menu
        currentScreen = SCREEN_MENU;
        lv_scr_load(scr_menu);

        lvgl_port_unlock();
        Serial.println("[UI] All screens created");
    }

    // 8. WiFi + UDP
    setupWiFi();

    Serial.println("\n[SETUP] Complete! Entering main loop.\n");
}

// =============================================================================
// LOOP (Core 0 - hardware polling + network)
// =============================================================================

void loop() {
    unsigned long now = millis();

    // WiFi reconnection check
    checkWiFiReconnect();

    // Receive UDP data
    receiveUDPData();

    // Encoder polling (50ms interval)
    if (now - lastEncoderRead >= Config::ENCODER_READ_MS) {
        lastEncoderRead = now;
        handleM5Encoders();
        handleDFRobotEncoders();
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
