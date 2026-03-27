/**
 * @file main.cpp
 * @brief RED808 V6 Surface Controller - Waveshare ESP32-S3-Touch-LCD-7
 * 
 * LVGL-based touch UI + WiFi UDP controller for RED808 drum machine.
 * Hardware: Waveshare ESP32-S3-Touch-LCD-7 (1024x600 RGB LCD + GT911 touch)
 * Peripherals: M5ROTATE8 encoders, DFRobot SEN0502 via PCA9548A I2C hub
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <M5ROTATE8.h>
#include <DFRobot_VisualRotaryEncoder.h>
#include <lvgl.h>

#include "config.h"
#include "system_state.h"
#include "debug_utils.h"
#include "drivers/waveshare_hw.h"

// ============================================
// LEGACY DEFINES
// ============================================
#define MAX_STEPS     Config::MAX_STEPS
#define MAX_TRACKS    Config::MAX_TRACKS
#define MAX_PATTERNS  Config::MAX_PATTERNS
#define MAX_KITS      Config::MAX_KITS
#define MIN_BPM       Config::MIN_BPM
#define MAX_BPM       Config::MAX_BPM
#define DEFAULT_BPM   Config::DEFAULT_BPM
#define DEFAULT_VOLUME Config::DEFAULT_VOLUME
#define MAX_VOLUME    Config::MAX_VOLUME

// ============================================
// INSTRUMENT NAMES & COLORS
// ============================================
const char* instrumentNames[] = {
    "KICK", "SNARE", "CL.HAT", "OP.HAT",
    "CLAP", "TOM LO", "TOM HI", "CYMBAL",
    "PERC 1", "PERC 2", "SHAKER", "COWBELL",
    "RIDE", "CONGA", "BONGO", "EXTRA"
};

const uint16_t instrumentColors[] = {
    0xF800, 0xFD20, 0xFFE0, 0x07FF,
    0xF81F, 0x07E0, 0x3E7F, 0x4A7F,
    0xFC00, 0xAFE0, 0x5BFF, 0xFBE0,
    0x9C1F, 0xFB80, 0x4FE0, 0xBDFF
};

const char* menuItems[] = {
    "LIVE PADS", "SEQUENCER", "VOLUMES", "FILTERS", "SETTINGS", "DIAGNOSTICS"
};

const char* trackNames[] = {
    "KICK    ", "SNARE   ", "CL.HAT  ", "OP.HAT  ",
    "CLAP    ", "TOM LO  ", "TOM HI  ", "CYMBAL  ",
    "PERC 1  ", "PERC 2  ", "SHAKER  ", "COWBELL ",
    "RIDE    ", "CONGA   ", "BONGO   ", "EXTRA   "
};

// ============================================
// STRUCTURES
// ============================================
struct Pattern {
    bool steps[MAX_TRACKS][MAX_STEPS];
    bool muted[MAX_TRACKS];
    String name;
};

struct DrumKit {
    String name;
    int folder;
};

struct DiagnosticInfo {
    bool displayOk;
    bool encoderOk;
    bool m5encoderOk;
    bool udpConnected;
    String lastError;
    DiagnosticInfo() : displayOk(false), encoderOk(false), m5encoderOk(false),
                       udpConnected(false), lastError("") {}
};

// ============================================
// GLOBAL STATE
// ============================================
// Patterns & Sequencer
Pattern patterns[MAX_PATTERNS];
DrumKit kits[MAX_KITS];
DiagnosticInfo diagnostic;

int currentPattern = 0;
int currentKit = 0;
int currentStep = 0;
int tempo = DEFAULT_BPM;
bool isPlaying = false;
unsigned long lastStepTime = 0;
unsigned long stepInterval = 125;

// UI State
Screen currentScreen = SCREEN_MENU;
int menuSelection = 0;
int selectedTrack = 0;
bool needsFullRedraw = true;
int sequencerPage = 0;

// Encoder
int encoderPos = 0;
int lastEncoderPos = 0;
volatile bool encoderChanged = false;
EncoderMode encoderMode = ENC_MODE_VOLUME;

// Audio
int trackVolumes[MAX_TRACKS] = {100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100};
bool trackMuted[MAX_TRACKS] = {false};

// Network
bool udpConnected = false;
unsigned long lastUdpCheck = 0;
const char* ssid = "RED808";
const char* password = "red808esp32";
const char* masterIP = "192.168.4.1";
const int udpPort = 8888;
bool wifiReconnecting = false;
WiFiUDP udp;

// M5 8ENCODER
M5ROTATE8 m5encoders[Config::M5_ENCODER_MODULES];
bool m5encoderConnected = false;
bool m5encoderModuleConnected[Config::M5_ENCODER_MODULES] = {false, false};
bool i2cHubDetected = false;
uint8_t i2cDevicesFound[32];
uint8_t i2cDeviceCount = 0;
uint8_t m5HubChannel[Config::M5_ENCODER_MODULES] = {0xFF, 0xFF};
uint8_t dfRobotHubChannel[Config::DFROBOT_ENCODER_COUNT] = {0xFF, 0xFF};
uint8_t encoderLEDColors[MAX_TRACKS][3] = {{0}};
unsigned long lastEncoderRead = 0;
unsigned long lastM5ButtonTime[MAX_TRACKS] = {0};
bool lastM5SwitchState[Config::M5_ENCODER_MODULES] = {false, false};
unsigned long lastM5SwitchTime[Config::M5_ENCODER_MODULES] = {0};

// DFRobot Encoders
DFRobot_VisualRotaryEncoder_I2C* dfEncoders[Config::DFROBOT_ENCODER_COUNT] = {nullptr};
bool dfEncoderConnected[Config::DFROBOT_ENCODER_COUNT] = {false};
uint16_t dfEncoderValues[Config::DFROBOT_ENCODER_COUNT] = {0};
bool dfEncoderButtons[Config::DFROBOT_ENCODER_COUNT] = {false};
unsigned long lastDFEncoderRead = 0;
int dfEncoderConnectedCount = 0;

// Volume
VolumeMode volumeMode = VOL_SEQUENCER;
int sequencerVolume = DEFAULT_VOLUME;
int livePadsVolume = 100;

// Filters
TrackFilter trackFilters[MAX_TRACKS];
TrackFilter masterFilter;
int filterSelectedTrack = -1;
int filterSelectedFX = FILTER_DELAY;

// Rotary encoder state machine
static uint8_t encoderState = 0;
static const int8_t encoderStates[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

// ============================================
// LVGL UI OBJECTS
// ============================================
static lv_obj_t *scr_menu = NULL;
static lv_obj_t *scr_live = NULL;
static lv_obj_t *scr_sequencer = NULL;
static lv_obj_t *scr_volumes = NULL;
static lv_obj_t *scr_filters = NULL;
static lv_obj_t *scr_settings = NULL;
static lv_obj_t *scr_diagnostics = NULL;

// Status bar labels
static lv_obj_t *lbl_status_bpm = NULL;
static lv_obj_t *lbl_status_pattern = NULL;
static lv_obj_t *lbl_status_wifi = NULL;
static lv_obj_t *lbl_status_play = NULL;

// Sequencer buttons
static lv_obj_t *seq_btns[MAX_TRACKS][MAX_STEPS] = {{NULL}};
static lv_obj_t *seq_track_labels[MAX_TRACKS] = {NULL};

// Live pads
static lv_obj_t *live_pads[MAX_TRACKS] = {NULL};

// Volume sliders
static lv_obj_t *vol_sliders[MAX_TRACKS] = {NULL};
static lv_obj_t *vol_labels[MAX_TRACKS] = {NULL};

// ============================================
// FORWARD DECLARATIONS
// ============================================
// I2C Hub
bool selectI2CHubChannel(uint8_t channel);
void deselectI2CHub();
bool selectM5EncoderModule(int moduleIndex);
bool selectDFRobotEncoder(int index);
void scanI2CBus();

// Encoders
void initDFRobotEncoders();
void handleDFRobotEncoders();
void handleM5Encoders();
bool getM5EncoderRoute(int track, int& moduleIndex, int& encoderIndex);
void writeM5EncoderRGBForTrack(int track, uint8_t r, uint8_t g, uint8_t b);
void updateTrackEncoderLED(int track);
void updateEncoderLEDs();

// WiFi/UDP
bool attemptWiFiConnect(int maxWaitMs = 8000);
void startWiFiReconnect();
void setupWiFiAndUDP();
void checkWiFiReconnect();
void sendUDPCommand(const char* cmd);
void sendUDPCommand(JsonDocument& doc);
void receiveUDPData();
void requestPatternFromMaster();

// Game logic
void calculateStepInterval();
void setupKits();
void setupPatterns();
void updateSequencer();
void triggerDrum(int track);
void toggleStep(int track, int step);
void changeTempo(int delta);
void changePattern(int delta);
void changeKit(int delta);
void sendFilterUDP(int track, int filterType);

// LVGL UI
void ui_create_all_screens();
void ui_create_status_bar(lv_obj_t *parent);
void ui_create_menu_screen();
void ui_create_live_screen();
void ui_create_sequencer_screen();
void ui_create_volumes_screen();
void ui_create_filters_screen();
void ui_create_settings_screen();
void ui_create_diagnostics_screen();
void ui_update_sequencer_grid();
void ui_update_status_bar();
void ui_show_screen(Screen screen);

// ============================================
// I2C HUB / PCA9548A FUNCTIONS
// ============================================

bool getM5EncoderRoute(int track, int& moduleIndex, int& encoderIndex) {
    if (track < 0 || track >= MAX_TRACKS) return false;
    moduleIndex = track / Config::ENCODERS_PER_MODULE;
    encoderIndex = track % Config::ENCODERS_PER_MODULE;
    if (moduleIndex >= Config::M5_ENCODER_MODULES) return false;
    if (!m5encoderModuleConnected[moduleIndex]) return false;
    return true;
}

bool selectI2CHubChannel(uint8_t channel) {
    if (!i2cHubDetected) return true;
    if (channel > 7) return false;
    Wire.beginTransmission(Config::I2C_HUB_ADDR);
    Wire.write(1 << channel);
    return (Wire.endTransmission() == 0);
}

void deselectI2CHub() {
    if (!i2cHubDetected) return;
    Wire.beginTransmission(Config::I2C_HUB_ADDR);
    Wire.write(0);
    Wire.endTransmission();
}

bool selectM5EncoderModule(int moduleIndex) {
    if (moduleIndex < 0 || moduleIndex >= Config::M5_ENCODER_MODULES) return false;
    if (!i2cHubDetected) return true;
    uint8_t ch = m5HubChannel[moduleIndex];
    if (ch == 0xFF) return false;
    return selectI2CHubChannel(ch);
}

bool selectDFRobotEncoder(int index) {
    if (index < 0 || index >= Config::DFROBOT_ENCODER_COUNT) return false;
    if (!i2cHubDetected) return true;
    uint8_t ch = dfRobotHubChannel[index];
    if (ch == 0xFF) return false;
    return selectI2CHubChannel(ch);
}

void scanI2CBus() {
    Serial.println("► I2C scan:");
    i2cDeviceCount = 0;

    for (int i = 0; i < Config::M5_ENCODER_MODULES; i++) m5HubChannel[i] = 0xFF;
    for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) dfRobotHubChannel[i] = 0xFF;

    Wire.flush();
    delay(10);

    // Check for PCA9548A hub
    Wire.beginTransmission(Config::I2C_HUB_ADDR);
    i2cHubDetected = (Wire.endTransmission() == 0);

    if (i2cHubDetected) {
        Serial.printf("   Hub ACTIVO (PCA9548A) @ 0x%02X\n", Config::I2C_HUB_ADDR);
        int m5Found = 0;
        int dfFound = 0;

        for (uint8_t ch = 0; ch < 8; ch++) {
            if (!selectI2CHubChannel(ch)) continue;
            delay(2);

            Wire.beginTransmission(Config::M5_ENCODER_ADDR_1);
            if (Wire.endTransmission() == 0) {
                if (i2cDeviceCount < 32) i2cDevicesFound[i2cDeviceCount++] = Config::M5_ENCODER_ADDR_1;
                if (m5Found < Config::M5_ENCODER_MODULES) {
                    m5HubChannel[m5Found] = ch;
                    m5Found++;
                    Serial.printf("   CH%d: M5 ROTATE8 #%d @ 0x%02X\n", ch, m5Found, Config::M5_ENCODER_ADDR_1);
                }
            }

            for (uint8_t addr = 0x54; addr <= 0x57; addr++) {
                Wire.beginTransmission(addr);
                if (Wire.endTransmission() == 0) {
                    if (i2cDeviceCount < 32) i2cDevicesFound[i2cDeviceCount++] = addr;
                    if (dfFound < Config::DFROBOT_ENCODER_COUNT) {
                        dfRobotHubChannel[dfFound] = ch;
                        dfFound++;
                        Serial.printf("   CH%d: DFRobot #%d @ 0x%02X\n", ch, dfFound, addr);
                    }
                }
            }
            delay(1);
        }
        deselectI2CHub();
    } else {
        Serial.printf("   Hub NOT FOUND @ 0x%02X (direct bus mode)\n", Config::I2C_HUB_ADDR);
        const uint8_t knownAddrs[] = {0x41, 0x42, 0x54, 0x55, 0x56, 0x57};
        for (uint8_t k = 0; k < 6; k++) {
            Wire.beginTransmission(knownAddrs[k]);
            if (Wire.endTransmission() == 0) {
                if (i2cDeviceCount < 32) i2cDevicesFound[i2cDeviceCount++] = knownAddrs[k];
                Serial.printf("   Device found @ 0x%02X\n", knownAddrs[k]);
            }
            delay(2);
        }
    }
    Serial.printf("   Total: %d devices found\n", i2cDeviceCount);
}

// ============================================
// DFRobot ENCODER FUNCTIONS
// ============================================

void initDFRobotEncoders() {
    Serial.println("► DFRobot Visual Rotary Encoder Init...");
    dfEncoderConnectedCount = 0;

    for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) {
        uint8_t addr = Config::DFROBOT_ENCODER_ADDRS[i];

        if (!i2cHubDetected) {
            bool addrAlreadyUsed = false;
            for (int j = 0; j < i; j++) {
                if (dfEncoderConnected[j] && Config::DFROBOT_ENCODER_ADDRS[j] == addr) {
                    addrAlreadyUsed = true;
                    break;
                }
            }
            if (addrAlreadyUsed) { dfEncoderConnected[i] = false; continue; }
        }

        if (!selectDFRobotEncoder(i)) { dfEncoderConnected[i] = false; continue; }

        dfEncoders[i] = new DFRobot_VisualRotaryEncoder_I2C(addr, &Wire);
        delay(5);
        if (dfEncoders[i]->begin() == 0) {
            dfEncoderConnected[i] = true;
            dfEncoderConnectedCount++;
            dfEncoders[i]->setGainCoefficient(51);
            dfEncoderValues[i] = dfEncoders[i]->getEncoderValue();
            Serial.printf("   DFRobot #%d OK @ 0x%02X\n", i + 1, addr);
        } else {
            dfEncoderConnected[i] = false;
            delete dfEncoders[i];
            dfEncoders[i] = nullptr;
            Serial.printf("   DFRobot #%d FAILED @ 0x%02X\n", i + 1, addr);
        }
        deselectI2CHub();
    }
    Serial.printf("   %d DFRobot encoders connected\n", dfEncoderConnectedCount);
}

void handleDFRobotEncoders() {
    if (dfEncoderConnectedCount == 0) return;
    unsigned long now = millis();
    if (now - lastDFEncoderRead < Config::ENCODER_READ_INTERVAL) return;
    lastDFEncoderRead = now;

    for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) {
        if (!dfEncoderConnected[i] || !dfEncoders[i]) continue;
        if (!selectDFRobotEncoder(i)) continue;

        uint16_t newVal = dfEncoders[i]->getEncoderValue();
        bool btn = dfEncoders[i]->detectButtonDown();
        deselectI2CHub();

        if (newVal != dfEncoderValues[i]) {
            int16_t delta = (int16_t)newVal - (int16_t)dfEncoderValues[i];
            dfEncoderValues[i] = newVal;

            if (i == 0) {
                // #1: FX amount
                TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
                uint8_t* param;
                switch (filterSelectedFX) {
                    case FILTER_DELAY:   param = &f.delayAmount;   break;
                    case FILTER_FLANGER: param = &f.flangerAmount; break;
                    default:             param = &f.compAmount;    break;
                }
                *param = constrain((int)*param + delta * 2, 0, 127);
                sendFilterUDP(filterSelectedTrack, filterSelectedFX);
            } else {
                // #2: Pattern select
                changePattern(delta > 0 ? 1 : -1);
            }
        }

        if (btn != dfEncoderButtons[i]) {
            dfEncoderButtons[i] = btn;
            if (btn) {
                if (i == 0) {
                    filterSelectedFX = (filterSelectedFX + 1) % FILTER_COUNT;
                } else {
                    changePattern(-currentPattern);
                }
            }
        }
    }
}

// ============================================
// M5 ROTATE8 ENCODER FUNCTIONS
// ============================================

void writeM5EncoderRGBForTrack(int track, uint8_t r, uint8_t g, uint8_t b) {
    int moduleIndex, encoderIndex;
    if (!getM5EncoderRoute(track, moduleIndex, encoderIndex)) return;
    if (!selectM5EncoderModule(moduleIndex)) return;
    m5encoders[moduleIndex].writeRGB(encoderIndex, r, g, b);
}

void updateTrackEncoderLED(int track) {
    if (track < 0 || track >= MAX_TRACKS) return;
    int moduleIndex, encoderIndex;
    if (!getM5EncoderRoute(track, moduleIndex, encoderIndex)) return;
    if (!selectM5EncoderModule(moduleIndex)) return;

    if (trackMuted[track]) {
        m5encoders[moduleIndex].writeRGB(encoderIndex, 30, 0, 0);
        return;
    }

    uint8_t brightness = map(trackVolumes[track], 0, MAX_VOLUME, 10, 255);
    uint8_t r = (encoderLEDColors[track][0] * brightness) / 255;
    uint8_t g = (encoderLEDColors[track][1] * brightness) / 255;
    uint8_t b = (encoderLEDColors[track][2] * brightness) / 255;
    m5encoders[moduleIndex].writeRGB(encoderIndex, r, g, b);
}

void updateEncoderLEDs() {
    if (!m5encoderConnected) return;
    for (int i = 0; i < MAX_TRACKS; i++) updateTrackEncoderLED(i);
}

void handleM5Encoders() {
    if (!m5encoderConnected) return;
    unsigned long now = millis();
    if (now - lastEncoderRead < Config::ENCODER_READ_INTERVAL) return;
    lastEncoderRead = now;

    for (int track = 0; track < MAX_TRACKS; track++) {
        int moduleIndex, encoderIndex;
        if (!getM5EncoderRoute(track, moduleIndex, encoderIndex)) continue;
        if (!selectM5EncoderModule(moduleIndex)) continue;

        int32_t delta = m5encoders[moduleIndex].getRelCounter(encoderIndex);
        if (delta != 0) {
            trackVolumes[track] = constrain(trackVolumes[track] + delta * 5, 0, MAX_VOLUME);
            JsonDocument doc;
            doc["cmd"] = "setTrackVolume";
            doc["track"] = track;
            doc["volume"] = trackVolumes[track];
            sendUDPCommand(doc);
            updateTrackEncoderLED(track);
        }

        if (m5encoders[moduleIndex].getKeyPressed(encoderIndex)) {
            if (now - lastM5ButtonTime[track] < 120) continue;
            lastM5ButtonTime[track] = now;
            trackMuted[track] = !trackMuted[track];
            patterns[currentPattern].muted[track] = trackMuted[track];
            JsonDocument doc;
            doc["cmd"] = "mute";
            doc["track"] = track;
            doc["value"] = trackMuted[track];
            sendUDPCommand(doc);
            updateTrackEncoderLED(track);
        }
    }
    deselectI2CHub();
}

// ============================================
// WiFi / UDP FUNCTIONS
// ============================================

bool attemptWiFiConnect(int maxWaitMs) {
    Serial.println("[WiFi] Attempting connection (blocking)...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < (unsigned long)maxWaitMs) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    return (WiFi.status() == WL_CONNECTED);
}

void startWiFiReconnect() {
    WiFi.disconnect(false);
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    wifiReconnecting = true;
}

void setupWiFiAndUDP() {
    Serial.println("\n[WiFi] Connecting to MASTER...");
    Serial.printf("  SSID: %s | Target: %s:%d\n", ssid, masterIP, udpPort);

    bool connected = (WiFi.status() == WL_CONNECTED);
    if (!connected) connected = attemptWiFiConnect(8000);

    if (connected) {
        Serial.printf("[WiFi] Connected! IP: %s RSSI: %d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        udp.begin(udpPort);
        udpConnected = true;
        diagnostic.udpConnected = true;

        JsonDocument doc;
        doc["cmd"] = "hello";
        doc["device"] = "SURFACE";
        sendUDPCommand(doc);

        delay(50);
        requestPatternFromMaster();
    } else {
        Serial.println("[WiFi] Connection FAILED - will retry in background");
        int n = WiFi.scanNetworks(false, false, false, 300);
        for (int i = 0; i < n; i++) {
            Serial.printf("  %d: %s (RSSI: %d)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
        WiFi.scanDelete();
        udpConnected = false;
        diagnostic.udpConnected = false;
        lastUdpCheck = millis();
    }
}

void checkWiFiReconnect() {
    unsigned long now = millis();

    if (udpConnected) {
        if (WiFi.status() != WL_CONNECTED) {
            udpConnected = false;
            diagnostic.udpConnected = false;
            lastUdpCheck = now;
            wifiReconnecting = false;
        }
        return;
    }

    if (wifiReconnecting) {
        if (WiFi.status() == WL_CONNECTED) {
            udp.begin(udpPort);
            udpConnected = true;
            diagnostic.udpConnected = true;
            wifiReconnecting = false;
            JsonDocument doc;
            doc["cmd"] = "hello";
            doc["device"] = "SURFACE";
            sendUDPCommand(doc);
            delay(50);
            requestPatternFromMaster();
        } else if (now - lastUdpCheck > 10000) {
            wifiReconnecting = false;
            lastUdpCheck = now;
        }
        return;
    }

    if (now - lastUdpCheck < 8000) return;
    lastUdpCheck = now;
    startWiFiReconnect();
}

void sendUDPCommand(const char* cmd) {
    if (!udpConnected) return;
    udp.beginPacket(masterIP, udpPort);
    udp.write((uint8_t*)cmd, strlen(cmd));
    udp.endPacket();
    Serial.printf("► UDP: %s\n", cmd);
}

void sendUDPCommand(JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    sendUDPCommand(json.c_str());
}

void receiveUDPData() {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) return;

    static char buf[512];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = 0;

    JsonDocument doc;
    if (deserializeJson(doc, buf)) return;

    const char* cmd = doc["cmd"];
    if (!cmd) return;

    if (strcmp(cmd, "pattern_sync") == 0) {
        int pat = doc["pattern"] | currentPattern;
        if (pat < 0 || pat >= MAX_PATTERNS) return;
        JsonArray data = doc["data"];
        if (!data) return;

        for (int t = 0; t < MAX_TRACKS; t++)
            for (int s = 0; s < MAX_STEPS; s++)
                patterns[pat].steps[t][s] = false;

        int tracks = min((int)data.size(), (int)MAX_TRACKS);
        for (int t = 0; t < tracks; t++) {
            JsonArray td = data[t];
            if (!td) continue;
            int steps = min((int)td.size(), (int)MAX_STEPS);
            for (int s = 0; s < steps; s++)
                patterns[pat].steps[t][s] = td[s];
        }

        if (pat == currentPattern) {
            needsFullRedraw = true;
            ui_update_sequencer_grid();
        }
    }
    else if (strcmp(cmd, "step_update") == 0 || strcmp(cmd, "step_sync") == 0) {
        int newStep = doc["step"] | 0;
        if (newStep >= 0 && newStep < MAX_STEPS) currentStep = newStep;
    }
    else if (strcmp(cmd, "play_state") == 0) {
        bool playing = doc["playing"] | false;
        if (playing != isPlaying) {
            isPlaying = playing;
            if (isPlaying) { lastStepTime = millis(); currentStep = 0; }
        }
    }
    else if (strcmp(cmd, "tempo_sync") == 0) {
        int t = doc["value"];
        if (t >= MIN_BPM && t <= MAX_BPM) { tempo = t; calculateStepInterval(); }
    }
    else if (strcmp(cmd, "volume_seq_sync") == 0) {
        int v = doc["value"];
        if (v >= 0 && v <= MAX_VOLUME) sequencerVolume = v;
    }
    else if (strcmp(cmd, "volume_live_sync") == 0) {
        int v = doc["value"];
        if (v >= 0 && v <= MAX_VOLUME) livePadsVolume = v;
    }
}

void requestPatternFromMaster() {
    if (!udpConnected || currentPattern < 0 || currentPattern >= MAX_PATTERNS) return;
    JsonDocument doc;
    doc["cmd"] = "get_pattern";
    doc["pattern"] = currentPattern;
    sendUDPCommand(doc);
}

// ============================================
// GAME LOGIC
// ============================================

void calculateStepInterval() {
    stepInterval = 60000UL / tempo / 4;
}

void setupKits() {
    kits[0] = {"TR-808", 0};
    kits[1] = {"TR-909", 1};
    kits[2] = {"LinnDrum", 2};
}

void setupPatterns() {
    for (int p = 0; p < MAX_PATTERNS; p++) {
        patterns[p].name = "Pattern " + String(p + 1);
        for (int t = 0; t < MAX_TRACKS; t++) {
            patterns[p].muted[t] = false;
            for (int s = 0; s < MAX_STEPS; s++)
                patterns[p].steps[t][s] = false;
        }
    }
}

void updateSequencer() {
    if (!isPlaying) return;
    unsigned long now = millis();
    if (now - lastStepTime >= stepInterval) {
        lastStepTime = now;
        currentStep = (currentStep + 1) % MAX_STEPS;

        for (int t = 0; t < MAX_TRACKS; t++) {
            if (!trackMuted[t] && patterns[currentPattern].steps[t][currentStep]) {
                triggerDrum(t);
            }
        }
    }
}

void triggerDrum(int track) {
    if (track < 0 || track >= MAX_TRACKS) return;
    JsonDocument doc;
    doc["cmd"] = "trigger";
    doc["track"] = track;
    sendUDPCommand(doc);
}

void toggleStep(int track, int step) {
    if (currentPattern < 0 || currentPattern >= MAX_PATTERNS) return;
    if (track < 0 || track >= MAX_TRACKS) return;
    if (step < 0 || step >= MAX_STEPS) return;

    patterns[currentPattern].steps[track][step] = !patterns[currentPattern].steps[track][step];

    if (udpConnected) {
        JsonDocument doc;
        doc["cmd"] = "setStep";
        doc["track"] = track;
        doc["step"] = step;
        doc["active"] = patterns[currentPattern].steps[track][step];
        sendUDPCommand(doc);
    }

    ui_update_sequencer_grid();
}

void changeTempo(int delta) {
    tempo = constrain(tempo + delta, MIN_BPM, MAX_BPM);
    calculateStepInterval();
    JsonDocument doc;
    doc["cmd"] = "tempo";
    doc["value"] = tempo;
    sendUDPCommand(doc);
    ui_update_status_bar();
}

void changePattern(int delta) {
    currentPattern = (currentPattern + delta + MAX_PATTERNS) % MAX_PATTERNS;
    currentStep = 0;
    if (udpConnected) {
        JsonDocument doc;
        doc["cmd"] = "selectPattern";
        doc["index"] = currentPattern;
        sendUDPCommand(doc);
        requestPatternFromMaster();
    }
    ui_update_status_bar();
    ui_update_sequencer_grid();
}

void changeKit(int delta) {
    currentKit = (currentKit + delta + MAX_KITS) % MAX_KITS;
    JsonDocument doc;
    doc["cmd"] = "kit";
    doc["value"] = currentKit;
    sendUDPCommand(doc);
}

void sendFilterUDP(int track, int filterType) {
    if (!udpConnected) return;
    TrackFilter& f = (track == -1) ? masterFilter : trackFilters[track];

    JsonDocument doc;
    if (track >= 0) {
        switch (filterType) {
            case FILTER_DELAY:
                doc["cmd"] = "setTrackEcho"; doc["track"] = track;
                doc["active"] = f.enabled; doc["time"] = f.delayAmount / 2;
                doc["feedback"] = f.delayAmount / 3; doc["mix"] = f.delayAmount;
                break;
            case FILTER_FLANGER:
                doc["cmd"] = "setTrackFlanger"; doc["track"] = track;
                doc["active"] = f.enabled; doc["rate"] = f.flangerAmount;
                doc["depth"] = f.flangerAmount * 3 / 4; doc["feedback"] = f.flangerAmount / 2;
                break;
            case FILTER_COMPRESSOR:
                doc["cmd"] = "setTrackCompressor"; doc["track"] = track;
                doc["active"] = f.enabled; doc["threshold"] = 127 - f.compAmount;
                doc["ratio"] = f.compAmount / 2;
                break;
        }
        sendUDPCommand(doc);
    } else {
        const char* cmds[4]; uint8_t vals[4];
        switch (filterType) {
            case FILTER_DELAY:
                cmds[0]="setDelayActive"; vals[0]=f.enabled;
                cmds[1]="setDelayTime"; vals[1]=f.delayAmount/2;
                cmds[2]="setDelayFeedback"; vals[2]=f.delayAmount/3;
                cmds[3]="setDelayMix"; vals[3]=f.delayAmount;
                break;
            case FILTER_FLANGER:
                cmds[0]="setFlangerActive"; vals[0]=f.enabled;
                cmds[1]="setFlangerRate"; vals[1]=f.flangerAmount;
                cmds[2]="setFlangerDepth"; vals[2]=f.flangerAmount*3/4;
                cmds[3]="setFlangerMix"; vals[3]=f.flangerAmount/2;
                break;
            default:
                cmds[0]="setCompressorActive"; vals[0]=f.enabled;
                cmds[1]="setCompressorThreshold"; vals[1]=127-f.compAmount;
                cmds[2]="setCompressorRatio"; vals[2]=f.compAmount/2;
                cmds[3]="setCompressorMakeupGain"; vals[3]=f.compAmount/4;
                break;
        }
        for (int i = 0; i < 4; i++) {
            doc.clear();
            doc["cmd"] = cmds[i]; doc["value"] = vals[i];
            sendUDPCommand(doc);
        }
    }
}

// ============================================
// LVGL UI CREATION
// ============================================

// Color helper: RGB565 to lv_color_t
static lv_color_t c565(uint16_t c) {
    uint8_t r = ((c >> 11) & 0x1F) << 3;
    uint8_t g = ((c >> 5) & 0x3F) << 2;
    uint8_t b = (c & 0x1F) << 3;
    return lv_color_make(r, g, b);
}

static lv_style_t style_bg;
static lv_style_t style_status_bar;
static lv_style_t style_step_off;
static lv_style_t style_step_on;
static lv_style_t style_pad;

static void init_styles() {
    // Dark background
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_hex(0x1A0000));
    lv_style_set_text_color(&style_bg, lv_color_white());
    lv_style_set_pad_all(&style_bg, 0);
    lv_style_set_border_width(&style_bg, 0);
    lv_style_set_radius(&style_bg, 0);

    // Status bar
    lv_style_init(&style_status_bar);
    lv_style_set_bg_color(&style_status_bar, lv_color_hex(0x300000));
    lv_style_set_pad_all(&style_status_bar, 4);
    lv_style_set_radius(&style_status_bar, 0);

    // Step OFF
    lv_style_init(&style_step_off);
    lv_style_set_bg_color(&style_step_off, lv_color_hex(0x2A2A2A));
    lv_style_set_border_color(&style_step_off, lv_color_hex(0x444444));
    lv_style_set_border_width(&style_step_off, 1);
    lv_style_set_radius(&style_step_off, 4);

    // Step ON
    lv_style_init(&style_step_on);
    lv_style_set_bg_color(&style_step_on, lv_color_hex(0xFF3333));
    lv_style_set_border_color(&style_step_on, lv_color_hex(0xFF6666));
    lv_style_set_border_width(&style_step_on, 1);
    lv_style_set_radius(&style_step_on, 4);

    // Live pad
    lv_style_init(&style_pad);
    lv_style_set_radius(&style_pad, 8);
    lv_style_set_border_width(&style_pad, 2);
    lv_style_set_shadow_width(&style_pad, 10);
    lv_style_set_shadow_opa(&style_pad, LV_OPA_30);
}

void ui_create_status_bar(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, WS_LCD_H_RES, 36);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_add_style(bar, &style_status_bar, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "RED808");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFF4444), 0);

    lbl_status_bpm = lv_label_create(bar);
    lv_label_set_text_fmt(lbl_status_bpm, "BPM:%d", tempo);
    lv_obj_set_style_text_color(lbl_status_bpm, lv_color_white(), 0);

    lbl_status_pattern = lv_label_create(bar);
    lv_label_set_text_fmt(lbl_status_pattern, "PAT:%d", currentPattern + 1);
    lv_obj_set_style_text_color(lbl_status_pattern, lv_color_hex(0x44FF44), 0);

    lbl_status_play = lv_label_create(bar);
    lv_label_set_text(lbl_status_play, isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(lbl_status_play, lv_color_hex(0xFFFF00), 0);

    lbl_status_wifi = lv_label_create(bar);
    lv_label_set_text(lbl_status_wifi, udpConnected ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(lbl_status_wifi, udpConnected ? lv_color_hex(0x44FF44) : lv_color_hex(0xFF4444), 0);
}

void ui_update_status_bar() {
    if (lbl_status_bpm) lv_label_set_text_fmt(lbl_status_bpm, "BPM:%d", tempo);
    if (lbl_status_pattern) lv_label_set_text_fmt(lbl_status_pattern, "PAT:%d", currentPattern + 1);
    if (lbl_status_play) lv_label_set_text(lbl_status_play, isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (lbl_status_wifi) {
        lv_label_set_text(lbl_status_wifi, udpConnected ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(lbl_status_wifi, udpConnected ? lv_color_hex(0x44FF44) : lv_color_hex(0xFF4444), 0);
    }
}

// ============================================
// MENU SCREEN
// ============================================

static void menu_btn_event_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0: ui_show_screen(SCREEN_LIVE); break;
        case 1: ui_show_screen(SCREEN_SEQUENCER); break;
        case 2: ui_show_screen(SCREEN_VOLUMES); break;
        case 3: ui_show_screen(SCREEN_FILTERS); break;
        case 4: ui_show_screen(SCREEN_SETTINGS); break;
        case 5: ui_show_screen(SCREEN_DIAGNOSTICS); break;
    }
}

void ui_create_menu_screen() {
    scr_menu = lv_obj_create(NULL);
    lv_obj_add_style(scr_menu, &style_bg, 0);
    lv_obj_clear_flag(scr_menu, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(scr_menu);
    lv_label_set_text(title, "RED808 V6");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF4444), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *subtitle = lv_label_create(scr_menu);
    lv_label_set_text(subtitle, "DRUM MACHINE SURFACE CONTROLLER");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 60);

    // Menu buttons (3x2 grid)
    const uint32_t btnColors[] = {0xF800, 0xFF8800, 0x44FF44, 0x4488FF, 0xAA44FF, 0x888888};
    const char* icons[] = {LV_SYMBOL_AUDIO, LV_SYMBOL_LIST, LV_SYMBOL_VOLUME_MAX,
                           LV_SYMBOL_SETTINGS, LV_SYMBOL_SETTINGS, LV_SYMBOL_EYE_OPEN};

    int btnW = 300, btnH = 100;
    int startX = (WS_LCD_H_RES - btnW * 3 - 20 * 2) / 2;
    int startY = 100;

    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;

        lv_obj_t *btn = lv_btn_create(scr_menu);
        lv_obj_set_size(btn, btnW, btnH);
        lv_obj_set_pos(btn, startX + col * (btnW + 20), startY + row * (btnH + 20));
        lv_obj_set_style_bg_color(btn, lv_color_hex(btnColors[i]), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(btnColors[i]), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_100, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_add_event_cb(btn, menu_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, icons[i]);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menuItems[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 15, 0);
    }

    // WiFi status at bottom
    lv_obj_t *wifi_lbl = lv_label_create(scr_menu);
    lv_label_set_text_fmt(wifi_lbl, "%s  MASTER: %s:%d  %s",
                          udpConnected ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING,
                          masterIP, udpPort,
                          udpConnected ? "CONNECTED" : "OFFLINE");
    lv_obj_set_style_text_color(wifi_lbl, udpConnected ? lv_color_hex(0x44FF44) : lv_color_hex(0xFF4444), 0);
    lv_obj_align(wifi_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ============================================
// LIVE PADS SCREEN
// ============================================

static void live_pad_event_cb(lv_event_t *e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    triggerDrum(track);

    // Visual flash
    lv_obj_t *pad = lv_event_get_target(e);
    lv_obj_set_style_bg_opa(pad, LV_OPA_100, 0);
    // Will decay back to normal opacity via timer
}

static void back_to_menu_cb(lv_event_t *e) {
    ui_show_screen(SCREEN_MENU);
}

void ui_create_live_screen() {
    scr_live = lv_obj_create(NULL);
    lv_obj_add_style(scr_live, &style_bg, 0);
    lv_obj_clear_flag(scr_live, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_status_bar(scr_live);

    // Back button
    lv_obj_t *back = lv_btn_create(scr_live);
    lv_obj_set_size(back, 80, 36);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " MENU");
    lv_obj_center(back_lbl);

    // 4x4 pad grid
    int padSize = 120;
    int gap = 8;
    int startX = (WS_LCD_H_RES - 4 * padSize - 3 * gap) / 2;
    int startY = 50;

    for (int i = 0; i < 16; i++) {
        int col = i % 4;
        int row = i / 4;

        lv_obj_t *pad = lv_btn_create(scr_live);
        lv_obj_set_size(pad, padSize, padSize);
        lv_obj_set_pos(pad, startX + col * (padSize + gap), startY + row * (padSize + gap));
        lv_obj_set_style_bg_color(pad, c565(instrumentColors[i]), 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_60, 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_100, LV_STATE_PRESSED);
        lv_obj_set_style_radius(pad, 10, 0);
        lv_obj_set_style_shadow_width(pad, 15, 0);
        lv_obj_set_style_shadow_color(pad, c565(instrumentColors[i]), 0);
        lv_obj_set_style_shadow_opa(pad, LV_OPA_40, 0);
        lv_obj_add_event_cb(pad, live_pad_event_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(pad);
        lv_label_set_text(lbl, instrumentNames[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        live_pads[i] = pad;
    }
}

// ============================================
// SEQUENCER SCREEN
// ============================================

static void seq_step_event_cb(lv_event_t *e) {
    uint32_t id = (uint32_t)(intptr_t)lv_event_get_user_data(e);
    int track = id >> 8;
    int step = id & 0xFF;
    toggleStep(track, step);
}

static void seq_track_event_cb(lv_event_t *e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    selectedTrack = track;
}

void ui_create_sequencer_screen() {
    scr_sequencer = lv_obj_create(NULL);
    lv_obj_add_style(scr_sequencer, &style_bg, 0);
    lv_obj_clear_flag(scr_sequencer, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_status_bar(scr_sequencer);

    // Back button
    lv_obj_t *back = lv_btn_create(scr_sequencer);
    lv_obj_set_size(back, 80, 36);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    // Play/Stop button
    lv_obj_t *play_btn = lv_btn_create(scr_sequencer);
    lv_obj_set_size(play_btn, 80, 36);
    lv_obj_set_pos(play_btn, WS_LCD_H_RES - 90, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x44AA44), 0);
    lv_obj_add_event_cb(play_btn, [](lv_event_t *e) {
        isPlaying = !isPlaying;
        if (isPlaying) { lastStepTime = millis(); currentStep = 0; }
        JsonDocument doc;
        doc["cmd"] = isPlaying ? "play" : "stop";
        sendUDPCommand(doc);
        ui_update_status_bar();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *play_lbl = lv_label_create(play_btn);
    lv_label_set_text(play_lbl, LV_SYMBOL_PLAY);
    lv_obj_center(play_lbl);

    // 8-track x 16-step grid (show 8 tracks at a time)
    int stepW = 52;
    int stepH = 56;
    int gapX = 4;
    int gapY = 4;
    int labelW = 80;
    int startX = labelW + 10;
    int startY = 42;

    for (int t = 0; t < 8; t++) {
        // Track label
        lv_obj_t *tlbl = lv_btn_create(scr_sequencer);
        lv_obj_set_size(tlbl, labelW, stepH);
        lv_obj_set_pos(tlbl, 4, startY + t * (stepH + gapY));
        lv_obj_set_style_bg_color(tlbl, c565(instrumentColors[t]), 0);
        lv_obj_set_style_bg_opa(tlbl, LV_OPA_50, 0);
        lv_obj_set_style_radius(tlbl, 4, 0);
        lv_obj_add_event_cb(tlbl, seq_track_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t);

        lv_obj_t *tn = lv_label_create(tlbl);
        lv_label_set_text(tn, instrumentNames[t]);
        lv_obj_set_style_text_font(tn, &lv_font_montserrat_12, 0);
        lv_obj_center(tn);
        seq_track_labels[t] = tlbl;

        // 16 step buttons
        for (int s = 0; s < MAX_STEPS; s++) {
            lv_obj_t *btn = lv_btn_create(scr_sequencer);
            lv_obj_set_size(btn, stepW, stepH);
            lv_obj_set_pos(btn, startX + s * (stepW + gapX), startY + t * (stepH + gapY));

            bool active = patterns[currentPattern].steps[t][s];
            if (active) {
                lv_obj_set_style_bg_color(btn, c565(instrumentColors[t]), 0);
                lv_obj_set_style_bg_opa(btn, LV_OPA_90, 0);
            } else {
                lv_obj_set_style_bg_color(btn, lv_color_hex(s % 4 == 0 ? 0x333333 : 0x222222), 0);
                lv_obj_set_style_bg_opa(btn, LV_OPA_100, 0);
            }
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
            lv_obj_set_style_pad_all(btn, 0, 0);

            uint32_t id = (t << 8) | s;
            lv_obj_add_event_cb(btn, seq_step_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)id);

            seq_btns[t][s] = btn;
        }
    }
}

void ui_update_sequencer_grid() {
    for (int t = 0; t < 8; t++) {
        int track = t + sequencerPage * 8;
        if (track >= MAX_TRACKS) break;
        for (int s = 0; s < MAX_STEPS; s++) {
            if (!seq_btns[t][s]) continue;
            bool active = patterns[currentPattern].steps[track][s];
            if (active) {
                lv_obj_set_style_bg_color(seq_btns[t][s], c565(instrumentColors[track]), 0);
                lv_obj_set_style_bg_opa(seq_btns[t][s], LV_OPA_90, 0);
            } else {
                lv_obj_set_style_bg_color(seq_btns[t][s], lv_color_hex(s % 4 == 0 ? 0x333333 : 0x222222), 0);
                lv_obj_set_style_bg_opa(seq_btns[t][s], LV_OPA_100, 0);
            }
        }
    }
}

// ============================================
// VOLUMES SCREEN
// ============================================

static void vol_slider_event_cb(lv_event_t *e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    trackVolumes[track] = lv_slider_get_value(slider);

    if (vol_labels[track]) {
        lv_label_set_text_fmt(vol_labels[track], "%d", trackVolumes[track]);
    }

    JsonDocument doc;
    doc["cmd"] = "setTrackVolume";
    doc["track"] = track;
    doc["volume"] = trackVolumes[track];
    sendUDPCommand(doc);
    updateTrackEncoderLED(track);
}

void ui_create_volumes_screen() {
    scr_volumes = lv_obj_create(NULL);
    lv_obj_add_style(scr_volumes, &style_bg, 0);
    lv_obj_clear_flag(scr_volumes, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_status_bar(scr_volumes);

    lv_obj_t *back = lv_btn_create(scr_volumes);
    lv_obj_set_size(back, 80, 36);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);

    // 16 vertical sliders with labels
    int sliderW = 50;
    int sliderH = 450;
    int gap = 6;
    int startX = (WS_LCD_H_RES - 16 * (sliderW + gap)) / 2;
    int startY = 50;

    for (int i = 0; i < MAX_TRACKS; i++) {
        // Track name label
        lv_obj_t *name = lv_label_create(scr_volumes);
        lv_label_set_text(name, instrumentNames[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, c565(instrumentColors[i]), 0);
        lv_obj_set_pos(name, startX + i * (sliderW + gap), startY);

        // Slider
        lv_obj_t *slider = lv_slider_create(scr_volumes);
        lv_obj_set_size(slider, sliderW - 10, sliderH);
        lv_obj_set_pos(slider, startX + i * (sliderW + gap) + 5, startY + 20);
        lv_slider_set_range(slider, 0, MAX_VOLUME);
        lv_slider_set_value(slider, trackVolumes[i], LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, c565(instrumentColors[i]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
        lv_obj_add_event_cb(slider, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        vol_sliders[i] = slider;

        // Value label
        lv_obj_t *val = lv_label_create(scr_volumes);
        lv_label_set_text_fmt(val, "%d", trackVolumes[i]);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(val, startX + i * (sliderW + gap) + 10, startY + sliderH + 25);
        vol_labels[i] = val;
    }
}

// ============================================
// FILTERS SCREEN (placeholder)
// ============================================

void ui_create_filters_screen() {
    scr_filters = lv_obj_create(NULL);
    lv_obj_add_style(scr_filters, &style_bg, 0);
    lv_obj_clear_flag(scr_filters, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_status_bar(scr_filters);

    lv_obj_t *back = lv_btn_create(scr_filters);
    lv_obj_set_size(back, 80, 36);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(scr_filters);
    lv_label_set_text(title, "FILTERS / FX");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4488FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *info = lv_label_create(scr_filters);
    lv_label_set_text(info, "Use DFRobot encoder #1 to adjust FX amount\nPress button to cycle: DELAY > FLANGER > COMPRESSOR");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 0);
}

// ============================================
// SETTINGS SCREEN
// ============================================

void ui_create_settings_screen() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_add_style(scr_settings, &style_bg, 0);
    lv_obj_clear_flag(scr_settings, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_status_bar(scr_settings);

    lv_obj_t *back = lv_btn_create(scr_settings);
    lv_obj_set_size(back, 80, 36);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(scr_settings);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    // BPM control
    int cy = 120;
    lv_obj_t *bpm_lbl = lv_label_create(scr_settings);
    lv_label_set_text(bpm_lbl, "BPM");
    lv_obj_set_style_text_font(bpm_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(bpm_lbl, 100, cy);

    lv_obj_t *bpm_slider = lv_slider_create(scr_settings);
    lv_obj_set_size(bpm_slider, 600, 40);
    lv_obj_set_pos(bpm_slider, 200, cy);
    lv_slider_set_range(bpm_slider, MIN_BPM, MAX_BPM);
    lv_slider_set_value(bpm_slider, tempo, LV_ANIM_OFF);
    lv_obj_add_event_cb(bpm_slider, [](lv_event_t *e) {
        lv_obj_t *s = lv_event_get_target(e);
        int newBpm = lv_slider_get_value(s);
        tempo = newBpm;
        calculateStepInterval();
        JsonDocument doc;
        doc["cmd"] = "tempo";
        doc["value"] = tempo;
        sendUDPCommand(doc);
        ui_update_status_bar();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Kit selection
    cy += 80;
    lv_obj_t *kit_lbl = lv_label_create(scr_settings);
    lv_label_set_text(kit_lbl, "KIT");
    lv_obj_set_style_text_font(kit_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(kit_lbl, 100, cy);

    const char* kit_items = "TR-808\nTR-909\nLinnDrum";
    lv_obj_t *dd = lv_dropdown_create(scr_settings);
    lv_dropdown_set_options(dd, kit_items);
    lv_dropdown_set_selected(dd, currentKit);
    lv_obj_set_size(dd, 300, 40);
    lv_obj_set_pos(dd, 200, cy);
    lv_obj_add_event_cb(dd, [](lv_event_t *e) {
        lv_obj_t *d = lv_event_get_target(e);
        currentKit = lv_dropdown_get_selected(d);
        JsonDocument doc;
        doc["cmd"] = "kit";
        doc["value"] = currentKit;
        sendUDPCommand(doc);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // WiFi reconnect button
    cy += 100;
    lv_obj_t *wifi_btn = lv_btn_create(scr_settings);
    lv_obj_set_size(wifi_btn, 300, 50);
    lv_obj_set_pos(wifi_btn, 200, cy);
    lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(0x4488FF), 0);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t *e) {
        setupWiFiAndUDP();
        ui_update_status_bar();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wlbl = lv_label_create(wifi_btn);
    lv_label_set_text(wlbl, LV_SYMBOL_REFRESH "  RECONNECT WiFi");
    lv_obj_center(wlbl);
}

// ============================================
// DIAGNOSTICS SCREEN
// ============================================

void ui_create_diagnostics_screen() {
    scr_diagnostics = lv_obj_create(NULL);
    lv_obj_add_style(scr_diagnostics, &style_bg, 0);
    lv_obj_clear_flag(scr_diagnostics, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_status_bar(scr_diagnostics);

    lv_obj_t *back = lv_btn_create(scr_diagnostics);
    lv_obj_set_size(back, 80, 36);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, back_to_menu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(scr_diagnostics);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN "  DIAGNOSTICS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    // Info strings
    char info[512];
    snprintf(info, sizeof(info),
        "Display: %s\n"
        "WiFi: %s (SSID: %s)\n"
        "UDP: %s (Master: %s:%d)\n"
        "I2C Hub: %s (0x%02X)\n"
        "M5 ROTATE8: %s (%d modules)\n"
        "DFRobot ENC: %s (%d units)\n"
        "\n"
        "Free Heap: %d bytes\n"
        "PSRAM: %d bytes\n"
        "CPU Freq: %d MHz",
        diagnostic.displayOk ? "OK (1024x600)" : "FAIL",
        WiFi.isConnected() ? "Connected" : "Disconnected", ssid,
        udpConnected ? "Active" : "Inactive", masterIP, udpPort,
        i2cHubDetected ? "Detected" : "Not found", Config::I2C_HUB_ADDR,
        m5encoderConnected ? "OK" : "Not found",
        (int)(m5encoderModuleConnected[0]) + (int)(m5encoderModuleConnected[1]),
        dfEncoderConnectedCount > 0 ? "OK" : "Not found", dfEncoderConnectedCount,
        ESP.getFreeHeap(),
        ESP.getFreePsram(),
        ESP.getCpuFreqMHz()
    );

    lv_obj_t *info_lbl = lv_label_create(scr_diagnostics);
    lv_label_set_text(info_lbl, info);
    lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(info_lbl, 50, 100);
}

// ============================================
// SCREEN MANAGEMENT
// ============================================

void ui_create_all_screens() {
    init_styles();
    ui_create_menu_screen();
    ui_create_live_screen();
    ui_create_sequencer_screen();
    ui_create_volumes_screen();
    ui_create_filters_screen();
    ui_create_settings_screen();
    ui_create_diagnostics_screen();
}

void ui_show_screen(Screen screen) {
    currentScreen = screen;
    lv_obj_t *target = NULL;

    switch (screen) {
        case SCREEN_MENU:        target = scr_menu; break;
        case SCREEN_LIVE:        target = scr_live; break;
        case SCREEN_SEQUENCER:   target = scr_sequencer; ui_update_sequencer_grid(); break;
        case SCREEN_VOLUMES:     target = scr_volumes; break;
        case SCREEN_FILTERS:     target = scr_filters; break;
        case SCREEN_SETTINGS:    target = scr_settings; break;
        case SCREEN_DIAGNOSTICS: target = scr_diagnostics; break;
        default:                 target = scr_menu; break;
    }

    if (target) {
        lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
    }
}

// ============================================
// SETUP
// ============================================

void setup() {
    Serial.begin(115200);
    delay(3000);  // Wait for USB CDC to connect so we capture init messages

    Serial.println("\n\n╔════════════════════════════════════╗");
    Serial.println("║   RED808 V6 - SURFACE (SLAVE)     ║");
    Serial.println("║   Waveshare ESP32-S3-Touch-LCD-7   ║");
    Serial.println("║   LVGL Touch UI + WiFi UDP         ║");
    Serial.println("╚════════════════════════════════════╝\n");

    Serial.printf("  Free Heap: %d | PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());

    // WiFi Early Start (non-blocking)
    Serial.println("► WiFi Early Start...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    Serial.printf("  SSID: %s | Target: %s:%d\n", ssid, masterIP, udpPort);

    // Initialize Waveshare display hardware (I2C, CH422G, LCD, Touch, LVGL)
    if (!waveshare_init()) {
        Serial.println("FATAL: Display init failed!");
        while (1) { delay(1000); Serial.println("Display init failed - halted"); }
    }
    diagnostic.displayOk = true;

    // POST-INIT DIAGNOSTIC: Verify IO expander and I2C bus state
    Serial.println("\n=== POST-INIT DIAGNOSTIC ===");
    Wire.beginTransmission(0x24);
    uint8_t tca_err = Wire.endTransmission();
    Serial.printf("[DIAG] TCA9554 (0x24) probe: %s (err=%d)\n", tca_err == 0 ? "OK" : "FAIL", tca_err);
    Wire.beginTransmission(0x5D);
    uint8_t gt_err = Wire.endTransmission();
    Serial.printf("[DIAG] GT911 (0x5D) probe: %s (err=%d)\n", gt_err == 0 ? "OK" : "FAIL", gt_err);
    Serial.printf("[DIAG] Display ptr: %s\n", waveshare_get_display() ? "valid" : "NULL");
    Serial.printf("[DIAG] Heap: %d  PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.println("=== END DIAGNOSTIC ===\n");

    // Scan I2C bus for external devices (PCA9548A, M5, DFRobot)
    // NOTE: waveshare_init() already called Wire.begin(), so we just scan
    scanI2CBus();

    // M5 8ENCODER Init
    Serial.println("► M5 8ENCODER Init...");
    int connectedModules = 0;
    for (int module = 0; module < Config::M5_ENCODER_MODULES; module++) {
        if (!selectM5EncoderModule(module)) continue;
        if (m5encoders[module].begin()) {
            m5encoderModuleConnected[module] = true;
            connectedModules++;
            Serial.printf("   Module %d OK (FW: %d)\n", module + 1, m5encoders[module].getVersion());

            // Set instrument colors on encoder LEDs
            int trackStart = module * Config::ENCODERS_PER_MODULE;
            for (int t = trackStart; t < min(trackStart + (int)Config::ENCODERS_PER_MODULE, (int)MAX_TRACKS); t++) {
                int enc = t - trackStart;
                uint16_t color = instrumentColors[t];
                uint8_t r = ((color >> 11) & 0x1F) << 3;
                uint8_t g = ((color >> 5) & 0x3F) << 2;
                uint8_t b = (color & 0x1F) << 3;
                m5encoders[module].writeRGB(enc, r, g, b);
                encoderLEDColors[t][0] = r;
                encoderLEDColors[t][1] = g;
                encoderLEDColors[t][2] = b;
                m5encoders[module].setAbsCounter(enc, 100);
            }
        }
    }
    m5encoderConnected = (connectedModules > 0);
    diagnostic.m5encoderOk = m5encoderConnected;
    Serial.printf("   %d M5 modules connected\n", connectedModules);
    deselectI2CHub();

    // DFRobot encoders
    initDFRobotEncoders();

    // WiFi/UDP finalize
    setupWiFiAndUDP();

    // Initialize data
    setupKits();
    setupPatterns();
    calculateStepInterval();

    // Create LVGL UI
    Serial.println("► Creating LVGL UI screens...");
    ui_create_all_screens();

    // Show menu screen
    lv_scr_load(scr_menu);

    // Start LVGL rendering task (must be after UI creation)
    waveshare_lvgl_task_start();
    Serial.println("\n[SETUP] Complete! Entering main loop.\n");
}

// ============================================
// LOOP
// ============================================

void loop() {
    // I2C encoders
    handleM5Encoders();
    handleDFRobotEncoders();

    // Sequencer playback
    if (isPlaying) {
        updateSequencer();
    }

    // WiFi/UDP
    if (udpConnected) {
        receiveUDPData();
    }
    checkWiFiReconnect();

    // Periodic status update (must lock LVGL mutex)
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 1000) {
        lastStatusUpdate = millis();
        if (waveshare_lvgl_lock(10)) {
            ui_update_status_bar();
            waveshare_lvgl_unlock();
        }
    }

    // Heartbeat
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 10000) {
        lastHeartbeat = millis();
        Serial.printf("[ALIVE] %lus Heap:%d PSRAM:%d WiFi:%s\n",
                      millis() / 1000, ESP.getFreeHeap(), ESP.getFreePsram(),
                      udpConnected ? "OK" : "OFF");
    }

    delay(5);  // Small yield
}
