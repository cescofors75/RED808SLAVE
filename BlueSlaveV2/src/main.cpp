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
#include "esp_heap_caps.h"
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
// SRAM ALLOCATOR — forces ArduinoJson to use internal SRAM (not PSRAM)
// Avoids PSRAM bus contention with LCD bounce-buffer DMA.
// =============================================================================
struct SramAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override {
        return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    void deallocate(void* ptr) override {
        heap_caps_free(ptr);
    }
    void* reallocate(void* ptr, size_t new_size) override {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
};
static SramAllocator sramAllocator;

// =============================================================================
// GLOBAL STATE DEFINITIONS
// =============================================================================

// Screen
volatile Screen currentScreen = SCREEN_BOOT;
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
volatile int livePadRepeatCount = 1;  // 1-16: how many triggers per single tap (volatile: written by LVGL Core 1, read by loop Core 0)
int trackVolumes[Config::MAX_TRACKS];
VolumeMode volumeMode = VOL_SEQUENCER;
bool trackMuted[Config::MAX_TRACKS];
bool trackSolo[Config::MAX_TRACKS];
bool livePadPressed[Config::MAX_SAMPLES];
volatile uint32_t pendingLivePadTriggerMask = 0;
volatile bool livePadsVisualDirty = false;

// Filters
TrackFilter trackFilters[Config::MAX_TRACKS];
TrackFilter masterFilter = {false, 0, 0, 0};
int filterSelectedTrack = -1;  // -1 = master
int filterSelectedFX = FILTER_DELAY;
int fxFilterType = 0;
int fxFilterCutoffHz = 2000;
int fxFilterResonanceX10 = 30;
int fxBitCrushBits = 16;
int fxDistortionPercent = 0;
int fxSampleRateHz = 44100;
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
volatile uint32_t uiUpdateCount = 0;
volatile uint32_t uiSkippedCount = 0;
volatile uint32_t uiLastIntervalMs = Config::SCREEN_UPDATE_MS;
volatile uint32_t udpRxCount = 0;
volatile uint32_t udpJsonErrorCount = 0;

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
static constexpr unsigned long TOUCH_RELEASE_DEBOUNCE_MS = 40;  // ignore brief touch gaps (GT911 bounce)
static constexpr unsigned long LIVE_PAD_REPEAT_INTERVAL_MS = 75;
static constexpr unsigned long LIVE_PAD_FLASH_MS = 90;
static unsigned long livePadReleaseMs[Config::MAX_SAMPLES] = {}; // when each pad last went untouched

// Timing
unsigned long lastEncoderRead = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastWiFiConnectedMs = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastUDPCheck = 0;
unsigned long lastMasterPacketMs = 0;
unsigned long lastStepUpdateMs = 0;   // last UDP step_update/step_sync received
unsigned long lastLocalStepMs  = 0;   // last local-clock step advance (independent of UDP)
int32_t prevM5Counter[Config::MAX_TRACKS];
uint16_t prevDFValue[DFROBOT_ENCODER_COUNT];
unsigned long lastLivePadTriggerMs[Config::MAX_SAMPLES];
unsigned long livePadFlashUntilMs[Config::MAX_SAMPLES];
unsigned long livePadHoldStartMs[Config::MAX_SAMPLES];
static uint8_t livePadRemainingRepeats[Config::MAX_SAMPLES] = {};
static unsigned long livePadNextRepeatMs[Config::MAX_SAMPLES] = {};
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

static uint32_t ui_refresh_interval_ms(Screen screen, bool playing_now) {
    switch (screen) {
        case SCREEN_BOOT:
            return 33;
        case SCREEN_MENU:
            return 120;
        case SCREEN_LIVE:
            return 50;
        case SCREEN_SEQUENCER:
        case SCREEN_SEQ_CIRCLE:
            return playing_now ? 16 : 50;
        case SCREEN_VOLUMES:
            return 40;
        case SCREEN_FILTERS:
            return 50;
        case SCREEN_PATTERNS:
            return 100;
        case SCREEN_DIAGNOSTICS:
        case SCREEN_PERFORMANCE:
            return 200;
        case SCREEN_SETTINGS:
        case SCREEN_SDCARD:
        case SCREEN_SAMPLES:
            return 250;
        default:
            return 100;
    }
}

static uint32_t encoder_poll_interval_ms(Screen screen) {
    switch (screen) {
        case SCREEN_LIVE:
        case SCREEN_SEQUENCER:
        case SCREEN_SEQ_CIRCLE:
            // Touch-intensive screens: free some I2C bandwidth for GT911 polling.
            return Config::TOUCH_ENCODER_READ_MS;
        default:
            return Config::ENCODER_READ_MS;
    }
}

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

void initState();
void setupWiFi();
void checkWiFiReconnect();
static void finalizeWiFiConnection();
static void startWiFiReconnectAttempt();
static void requestMasterSync(bool requestState);
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
static void encoder_task(void* arg);
static void touch_task(void* arg);

// =============================================================================
// NVS PERSISTENCE — save/load user settings across reboots
// =============================================================================
#include "nvs.h"

static constexpr const char* NVS_NAMESPACE = "red808";
static unsigned long nvs_last_save_ms = 0;
static bool nvs_dirty = false;

static void nvs_load_settings() {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        RED808_LOG_PRINTLN("[NVS] No saved settings found");
        return;
    }
    uint8_t val8;
    int32_t val32;

    if (nvs_get_u8(h, "theme", &val8) == ESP_OK && val8 < THEME_COUNT)
        currentTheme = (VisualTheme)val8;
    if (nvs_get_i32(h, "masterVol", &val32) == ESP_OK)
        masterVolume = constrain((int)val32, 0, Config::MAX_VOLUME);
    if (nvs_get_i32(h, "seqVol", &val32) == ESP_OK)
        sequencerVolume = constrain((int)val32, 0, Config::MAX_VOLUME);
    if (nvs_get_i32(h, "liveVol", &val32) == ESP_OK)
        livePadsVolume = constrain((int)val32, 0, Config::MAX_VOLUME);
    if (nvs_get_i32(h, "bpm", &val32) == ESP_OK)
        currentBPM = constrain((int)val32, Config::MIN_BPM, Config::MAX_BPM);
    if (nvs_get_u8(h, "volMode", &val8) == ESP_OK)
        volumeMode = (val8 == 1) ? VOL_LIVE_PADS : VOL_SEQUENCER;

    nvs_close(h);
    RED808_LOG_PRINTF("[NVS] Loaded: theme=%d bpm=%d vol=%d/%d/%d\n",
                  currentTheme, currentBPM, masterVolume, sequencerVolume, livePadsVolume);
}

void nvs_save_settings() {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_u8(h, "theme", (uint8_t)currentTheme);
    nvs_set_i32(h, "masterVol", masterVolume);
    nvs_set_i32(h, "seqVol", sequencerVolume);
    nvs_set_i32(h, "liveVol", livePadsVolume);
    nvs_set_i32(h, "bpm", currentBPM);
    nvs_set_u8(h, "volMode", volumeMode == VOL_LIVE_PADS ? 1 : 0);

    nvs_commit(h);
    nvs_close(h);
    nvs_dirty = false;
    nvs_last_save_ms = millis();
}

// Mark settings dirty — will be flushed on next periodic check
void nvs_mark_dirty() { nvs_dirty = true; }

// Call from loop() — debounced save every 5 seconds when dirty
static void nvs_periodic_save() {
    if (!nvs_dirty) return;
    if ((millis() - nvs_last_save_ms) < 5000) return;
    nvs_save_settings();
    RED808_LOG_PRINTLN("[NVS] Settings auto-saved");
}

static void requestTrackVolumesFromMaster() {
    if (!udpConnected) return;

    JsonDocument doc(&sramAllocator);
    doc["cmd"] = "getTrackVolumes";
    sendUDPCommand(doc);
}

static void requestMasterSync(bool requestState) {
    if (!udpConnected) return;

    JsonDocument doc(&sramAllocator);
    doc["cmd"] = "hello";
    doc["device"] = "SURFACE";
    sendUDPCommand(doc);

    if (requestState) {
        requestPatternFromMaster();
        requestTrackVolumesFromMaster();
    }
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

    // Read multi-touch from cache (updated by LVGL task every 10ms on Core 1).
    // Cache read is lock-free (~0us) so loop() runs at full speed for rapid tapping.
    TouchPoint points[Config::TOUCH_MAX_POINTS] = {};
    uint8_t count = gt911_get_points(points, Config::TOUCH_MAX_POINTS);

    for (uint8_t i = 0; i < count; i++) {
        if (!points[i].pressed) continue;
        int pad = ui_live_pad_hit_test(points[i].x, points[i].y);
        if (pad >= 0 && pad < Config::MAX_SAMPLES) {
            new_state[pad] = true;
        }
    }

    bool anyChanged = false;
    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (new_state[pad]) {
            // Finger is on pad — reset release timer
            livePadReleaseMs[pad] = 0;
            if (!livePadPressed[pad]) {
                // Rising edge — new press
                pendingLivePadTriggerMask |= (1UL << pad);
                lastLivePadTriggerMs[pad] = now;
                livePadFlashUntilMs[pad] = now + LIVE_PAD_FLASH_MS;
                livePadHoldStartMs[pad] = now;
                int repeatCount = constrain((int)livePadRepeatCount, 1, 16);
                livePadRemainingRepeats[pad] = (uint8_t)(repeatCount - 1);
                livePadNextRepeatMs[pad] = now + LIVE_PAD_REPEAT_INTERVAL_MS;
                livePadPressed[pad] = true;
                anyChanged = true;
            }
        } else if (livePadPressed[pad]) {
            // Finger lifted — start/check release debounce
            if (livePadReleaseMs[pad] == 0) {
                livePadReleaseMs[pad] = now;  // start debounce timer
            } else if ((now - livePadReleaseMs[pad]) >= TOUCH_RELEASE_DEBOUNCE_MS) {
                // Confirmed release after debounce period
                livePadPressed[pad] = false;
                livePadReleaseMs[pad] = 0;
                anyChanged = true;
            }
            // else: still within debounce window — keep pad pressed (ignore brief gap)
        }
    }

    // Mark visual dirty — LVGL task (Core 1) will pick it up on next cycle (~10ms)
    if (anyChanged) {
        livePadsVisualDirty = true;
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
        livePadFlashUntilMs[i] = 0;
        livePadHoldStartMs[i] = 0;
        livePadRemainingRepeats[i] = 0;
        livePadNextRepeatMs[i] = 0;
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

static void finalizeWiFiConnection() {
    wifiConnected = true;
    wifiReconnecting = false;
    lastWiFiConnectedMs = millis();
    lastWiFiCheck = lastWiFiConnectedMs;

    udp.stop();
    udpConnected = udp.begin(WiFiConfig::UDP_PORT);

    diagInfo.wifiOk = true;
    diagInfo.udpConnected = udpConnected;
}

static void startWiFiReconnectAttempt() {
    lastWiFiCheck = millis();
    wifiReconnecting = true;

    if (!WiFi.reconnect()) {
        WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);
    }
}

void setupWiFi() {
    RED808_LOG_PRINTLN("[WiFi] Connecting...");
    WiFi.disconnect(true);
    delay(20);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WiFiConfig::TIMEOUT_MS) {
        delay(250);
        RED808_LOG_PRINT(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        finalizeWiFiConnection();
        RED808_LOG_PRINTF("\n[WiFi] Connected! IP: %s  UDP: %s\n",
                      WiFi.localIP().toString().c_str(),
                      udpConnected ? "OK" : "FALLO");

        if (udpConnected) {
            requestMasterSync(true);
        }
    } else {
        RED808_LOG_PRINTLN("\n[WiFi] Initial connect timeout - will retry in background");
        wifiConnected = false;
        wifiReconnecting = false;
        udpConnected = false;
        diagInfo.wifiOk = false;
        diagInfo.udpConnected = false;
    }
    lastWiFiCheck = millis();
}

void checkWiFiReconnect() {
    unsigned long now_wifi = millis();
    wl_status_t wifiStatus = WiFi.status();

    if (wifiStatus == WL_CONNECTED) {
        lastWiFiConnectedMs = now_wifi;
        if (!wifiConnected) {
            finalizeWiFiConnection();
            RED808_LOG_PRINTF("[WiFi] Reconectado! IP: %s  UDP: %s\n",
                          WiFi.localIP().toString().c_str(),
                          udpConnected ? "OK" : "FALLO");
            if (udpConnected) {
                requestMasterSync(true);
            }
        }
        return;
    }

    if (wifiConnected && (now_wifi - lastWiFiConnectedMs) < WiFiConfig::DISCONNECT_GRACE_MS) {
        return;
    }

    if (wifiConnected || udpConnected || masterConnected) {
        wifiConnected = false;
        masterConnected = false;
        if (udpConnected) {
            udp.stop();
        }
        udpConnected = false;
        diagInfo.wifiOk = false;
        diagInfo.udpConnected = false;
        RED808_LOG_PRINTLN("[WiFi] Link lost, waiting for background reconnect");
    }

    if (wifiReconnecting && (now_wifi - lastWiFiCheck > WiFiConfig::RECONNECT_ATTEMPT_TIMEOUT_MS)) {
        wifiReconnecting = false;
    }

    if (!wifiReconnecting && (now_wifi - lastWiFiCheck > WiFiConfig::RECONNECT_INTERVAL_MS)) {
        startWiFiReconnectAttempt();
    }
}

void sendUDPCommand(const char* cmd) {
    if (!udpConnected) return;  // silent drop — no Serial in hot path
    udp.beginPacket(WiFiConfig::MASTER_IP, WiFiConfig::UDP_PORT);
    udp.write((const uint8_t*)cmd, strlen(cmd));
    udp.endPacket();
}

void sendUDPCommand(JsonDocument& doc) {
    if (!udpConnected) return;
    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        udp.beginPacket(WiFiConfig::MASTER_IP, WiFiConfig::UDP_PORT);
        udp.write((const uint8_t*)buf, len);
        udp.endPacket();
    }
}

void requestPatternFromMaster() {
    JsonDocument doc(&sramAllocator);
    doc["cmd"] = "get_pattern";
    doc["pattern"] = currentPattern;
    sendUDPCommand(doc);
}

void sendPlayStateCommand(bool shouldPlay) {
    isPlaying = shouldPlay;
    JsonDocument doc(&sramAllocator);
    doc["cmd"] = isPlaying ? "start" : "stop";
    sendUDPCommand(doc);
}

void selectPatternOnMaster(int patternIndex) {
    currentPattern = constrain(patternIndex, 0, Config::MAX_PATTERNS - 1);

    JsonDocument doc(&sramAllocator);
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
        JsonDocument doc(&sramAllocator);
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
        JsonDocument doc(&sramAllocator);
        doc["cmd"] = "selectPattern";
        doc["index"] = savedPattern;
        sendUDPCommand(doc);
    }
    RED808_LOG_PRINTF("[DEMO] Sent pattern %d to master (%d steps)\n", pat, sent);
}

// =============================================================================
// MICROTIMING ENGINE — humanizes pad triggers with subtle timing offsets
// =============================================================================
// Adds ±0-4ms random jitter to trigger timing, simulating human feel.
// Uses a fast xorshift32 PRNG seeded from esp_random() at boot.
static uint32_t mtRngState = 0;

static void microtiming_init() {
    mtRngState = esp_random();  // hardware RNG seed
    if (mtRngState == 0) mtRngState = 0xDEADBEEF;
}

static inline uint32_t xorshift32() {
    mtRngState ^= mtRngState << 13;
    mtRngState ^= mtRngState >> 17;
    mtRngState ^= mtRngState << 5;
    return mtRngState;
}

// Returns a humanization offset in ms: range [-maxJitterMs, +maxJitterMs]
static int microtiming_jitter(int maxJitterMs) {
    if (!Config::ENABLE_MICROTIMING) return 0;
    if (maxJitterMs <= 0) return 0;
    return (int)(xorshift32() % (2 * maxJitterMs + 1)) - maxJitterMs;
}

// Velocity humanization: ±5% random variation
static int microtiming_velocity(int velocity) {
    if (!Config::ENABLE_MICROTIMING) return constrain(velocity, 1, 127);
    int jitter = (int)(xorshift32() % 13) - 6;  // -6 to +6
    return constrain(velocity + jitter, 1, 127);
}

void sendLivePadTrigger(int pad, int velocity) {
    if (pad < 0 || pad >= Config::MAX_SAMPLES) return;
    if (!udpConnected) return;

    // Humanize velocity with subtle random variation
    int humanVel = microtiming_velocity(velocity);
    int jitterMs = microtiming_jitter(4);  // ±4ms

    // Pre-formatted UDP — NO ArduinoJson overhead (~10x faster)
    char buf[80];
    int len;
    if (jitterMs != 0) {
        len = snprintf(buf, sizeof(buf), "{\"cmd\":\"trigger\",\"pad\":%d,\"vel\":%d,\"mt\":%d}", pad, humanVel, jitterMs);
    } else {
        len = snprintf(buf, sizeof(buf), "{\"cmd\":\"trigger\",\"pad\":%d,\"vel\":%d}", pad, humanVel);
    }
    udp.beginPacket(WiFiConfig::MASTER_IP, WiFiConfig::UDP_PORT);
    udp.write((const uint8_t*)buf, len);
    udp.endPacket();
}

// =============================================================================
// UDP RECEIVE
// =============================================================================

void receiveUDPData() {
    if (!udpConnected) return;

    // Process only 1 UDP packet per call (called every 30ms).
    // Minimises PSRAM bus contention between JSON parsing and LCD DMA.
    int packetSize = udp.parsePacket();
    if (packetSize == 0) return;

    char buf[512];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = '\0';
    udpRxCount = udpRxCount + 1;

    lastMasterPacketMs = millis();
    masterConnected = true;
    diagInfo.udpConnected = true;

    // Parse JSON using internal SRAM only — no PSRAM access
    JsonDocument doc(&sramAllocator);
    DeserializationError err = deserializeJson(doc, buf);
    if (err) {
        udpJsonErrorCount = udpJsonErrorCount + 1;
        return;
    }

    const char* cmd = doc["cmd"];
    if (!cmd) return;

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
                        RED808_LOG_PRINTLN("[UDP] Skipping empty pattern_sync for pattern 7 (demo)");
                        return;  // skip this packet, keep local demo
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
        nvs_mark_dirty();
    }
    else if (strcmp(cmd, "volume_sync") == 0 ||
             strcmp(cmd, "master_volume_sync") == 0 ||
             strcmp(cmd, "volume_master_sync") == 0 ||
             strcmp(cmd, "setVolume") == 0) {
        masterVolume = constrain(doc["value"] | Config::DEFAULT_VOLUME, 0, Config::MAX_VOLUME);
        nvs_mark_dirty();
    }
    else if (strcmp(cmd, "volume_seq_sync") == 0 || strcmp(cmd, "setSequencerVolume") == 0) {
        sequencerVolume = constrain(doc["value"] | Config::DEFAULT_VOLUME, 0, Config::MAX_VOLUME);
        nvs_mark_dirty();
    }
    else if (strcmp(cmd, "volume_live_sync") == 0 || strcmp(cmd, "setLiveVolume") == 0) {
        livePadsVolume = constrain(doc["value"] | 100, 0, Config::MAX_VOLUME);
        nvs_mark_dirty();
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
    else if (strcmp(cmd, "setFilter") == 0) {
        fxFilterType = constrain(doc["type"] | fxFilterType, 0, 4);
    }
    else if (strcmp(cmd, "setFilterCutoff") == 0) {
        fxFilterCutoffHz = constrain(doc["value"] | fxFilterCutoffHz, 20, 20000);
    }
    else if (strcmp(cmd, "setFilterResonance") == 0) {
        float resonance = doc["value"].is<float>() ? doc["value"].as<float>() : ((float)(doc["value"] | fxFilterResonanceX10) / 10.0f);
        fxFilterResonanceX10 = constrain((int)lroundf(resonance * 10.0f), 1, 100);
    }
    else if (strcmp(cmd, "setBitCrush") == 0) {
        fxBitCrushBits = constrain(doc["value"] | fxBitCrushBits, 1, 16);
    }
    else if (strcmp(cmd, "setDistortion") == 0) {
        float distortion = doc["value"].is<float>() ? doc["value"].as<float>() : ((float)(doc["value"] | fxDistortionPercent) / 100.0f);
        fxDistortionPercent = constrain((int)lroundf(distortion * 100.0f), 0, 100);
    }
    else if (strcmp(cmd, "setSampleRate") == 0) {
        fxSampleRateHz = constrain(doc["value"] | fxSampleRateHz, 1000, 44100);
    }
    else if (strcmp(cmd, "selectPattern") == 0 || strcmp(cmd, "pattern_select") == 0 ||
             strcmp(cmd, "current_pattern") == 0) {
        int pat = doc["index"] | (doc["pattern"] | -1);
        if (pat >= 0 && pat < Config::MAX_PATTERNS) {
            currentPattern = pat;
            requestPatternFromMaster();
        }
    }
    // end receiveUDPData
}

// =============================================================================
// FILTER UDP SEND
// =============================================================================

void sendFilterUDP(int track, int fxType) {
    TrackFilter& f = (track == -1) ? masterFilter : trackFilters[track];

    if (track == -1) {
        // Master effects
        JsonDocument doc(&sramAllocator);
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
        JsonDocument doc(&sramAllocator);
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
    RED808_LOG_PRINTLN("[I2C] Scanning bus...");

    // Check for PCA9548A hub (with mutex protection)
    hubDetected = i2c_device_present(I2C_HUB_ADDR);
    diagInfo.i2cHubOk = hubDetected;

    if (!hubDetected) {
        RED808_LOG_PRINTLN("[I2C] No PCA9548A hub found");
        return;
    }
    RED808_LOG_PRINTLN("[I2C] PCA9548A hub detected");

    int m5Found = 0, dfFound = 0;
    bool byteButtonFound = false;

    for (uint8_t ch = 0; ch < 8 && (m5Found < M5_ENCODER_MODULES || dfFound < DFROBOT_ENCODER_COUNT || !byteButtonFound); ch++) {
        i2c_hub_select(ch);
        delay(5);

        // Probe M5 ROTATE8 (0x41)
        if (m5Found < M5_ENCODER_MODULES && i2c_device_present(M5_ENCODER_ADDR)) {
            m5HubChannel[m5Found] = ch;
            m5encoderConnected[m5Found] = true;
            RED808_LOG_PRINTF("[I2C] M5 ROTATE8 #%d found on ch %d\n", m5Found + 1, ch);

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
                    RED808_LOG_PRINTF("[I2C] DFRobot #%d found on ch %d\n", dfFound + 1, ch);
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
                RED808_LOG_PRINTF("[I2C] M5 ByteButton found on hub ch %d\n", ch);

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
            RED808_LOG_PRINTLN("[I2C] M5 ByteButton found DIRECT on bus (no hub)");

            if (i2c_lock(50)) {
                byteButtonLedInitialized = byteButtonApplyLedConfigLocked();
                i2c_unlock();
            }
        }
    }

    if (!byteButtonFound) {
        RED808_LOG_PRINTLN("[I2C] WARNING: ByteButton NOT found on any channel or direct bus!");
    }

    diagInfo.m5encoder1Ok = m5encoderConnected[0];
    diagInfo.m5encoder2Ok = m5encoderConnected[1];
    diagInfo.dfrobot1Ok = dfEncoderConnected[0];
    diagInfo.dfrobot2Ok = dfEncoderConnected[1];
    diagInfo.byteButtonOk = byteButtonConnected;

    RED808_LOG_PRINTF("[I2C] Found: %d M5 modules, %d DFRobot rotaries, ByteButton: %s\n",
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
    if (!i2c_lock(10)) return;

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
    if (!i2c_lock(10)) return;
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

// =============================================================================
// BOOT LED ANIMATION — sweeps all hardware LEDs as a tester
// =============================================================================
static int  bootLedPhase = 0;        // which LED we're lighting (0..24)
static unsigned long bootLedLastMs = 0;
static bool bootLedDone = false;

// Ocean blue palette — 9 tones for ByteButton, cycling for M5
static const uint32_t oceanBlues[] = {
    0x001F5C, 0x003380, 0x0050A0, 0x0070C0, 0x0090E0,
    0x00A8FF, 0x00C0FF, 0x00D4FF, 0x33E0FF,
};

static void setByteButtonLedDirect(int index, uint32_t color) {
    if (!byteButtonConnected || !byteButtonLedInitialized) return;
    if (index < 0 || index >= BYTEBUTTON_LED_COUNT) return;
    if (!i2c_lock(10)) return;
    if (byteButtonHubChannel >= 0) i2c_hub_select_raw(byteButtonHubChannel);
    else if (byteButtonHubChannel == -2) i2c_hub_deselect_raw();
    byteButtonWriteBytes(BYTEBUTTON_LED_RGB888_REG + index * 4, (uint8_t*)&color, 4);
    byteButtonLedCache[index] = color;
    if (byteButtonHubChannel >= 0) i2c_hub_deselect_raw();
    i2c_unlock();
}

static void setM5EncoderLedDirect(int module, int enc, uint8_t r, uint8_t g, uint8_t b) {
    if (module < 0 || module >= M5_ENCODER_MODULES) return;
    if (!m5encoderConnected[module]) return;
    int ch = m5HubChannel[module];
    if (ch < 0) return;
    if (!i2c_lock(10)) return;
    i2c_hub_select_raw(ch);
    m5encoders[module].writeRGB(enc, r, g, b);
    i2c_hub_deselect_raw();
    i2c_unlock();
}

// Called from loop() while SCREEN_BOOT — lights LEDs one by one in a rainbow sweep
// HSV-like hue to RGB (s=1, v=200)
static void hue_to_rgb(uint8_t hue, uint8_t &r, uint8_t &g, uint8_t &b) {
    uint8_t region = hue / 43;
    uint8_t remainder = (hue - (region * 43)) * 6;
    switch (region) {
        case 0: r = 200; g = remainder * 200 / 255; b = 0; break;
        case 1: r = 200 - remainder * 200 / 255; g = 200; b = 0; break;
        case 2: r = 0; g = 200; b = remainder * 200 / 255; break;
        case 3: r = 0; g = 200 - remainder * 200 / 255; b = 200; break;
        case 4: r = remainder * 200 / 255; g = 0; b = 200; break;
        default: r = 200; g = 0; b = 200 - remainder * 200 / 255; break;
    }
}

void bootLedAnimation() {
    if (bootLedDone) return;
    unsigned long now = millis();
    if (now - bootLedLastMs < 120) return;  // 120ms between LED activations
    bootLedLastMs = now;

    // Total LEDs: M5 module 0 (8) + M5 module 1 (8) + ByteButton (9) = 25
    // Phase 0-7:   M5 #0 encoders 0-7 (rainbow sweep)
    // Phase 8-15:  M5 #1 encoders 0-7 (rainbow sweep)
    // Phase 16-24: ByteButton LEDs 0-8 (ocean blue)

    if (bootLedPhase < 8) {
        uint8_t r, g, b;
        hue_to_rgb(bootLedPhase * 32, r, g, b);
        setM5EncoderLedDirect(0, bootLedPhase, r, g, b);
    } else if (bootLedPhase < 16) {
        int enc = bootLedPhase - 8;
        uint8_t r, g, b;
        hue_to_rgb(enc * 32, r, g, b);
        setM5EncoderLedDirect(1, enc, r, g, b);
    } else if (bootLedPhase < 25) {
        // ByteButton — ocean blue tones
        int idx = bootLedPhase - 16;
        setByteButtonLedDirect(idx, oceanBlues[idx]);
    }

    bootLedPhase++;
    if (bootLedPhase >= 25) {
        bootLedDone = true;
        RED808_LOG_PRINTLN("[BOOT] LED test sequence complete");
    }
}

// Set all LEDs to ocean blue palette (called when entering menu from boot)
void setOceanBlueLeds() {
    // M5 encoders — deep to bright blue gradient across 16 encoders
    for (int mod = 0; mod < M5_ENCODER_MODULES; mod++) {
        if (!m5encoderConnected[mod]) continue;
        for (int enc = 0; enc < ENCODERS_PER_MODULE; enc++) {
            int idx = mod * ENCODERS_PER_MODULE + enc;
            // Gradient from dark navy (0,20,90) to bright cyan (0,180,255)
            uint8_t r = 0;
            uint8_t g = 20 + idx * 11;   // 20..195
            uint8_t b = 90 + idx * 11;   // 90..265 → clamped
            if (b > 255) b = 255;
            setM5EncoderLedDirect(mod, enc, r, g, b);
        }
    }
    // ByteButton — ocean blue palette with high contrast
    for (int i = 0; i < BYTEBUTTON_LED_COUNT; i++) {
        setByteButtonLedDirect(i, oceanBlues[i]);
    }
    RED808_LOG_PRINTF("[LED] Ocean blue theme applied (M5 #1=%s ch=%d, M5 #2=%s ch=%d)\n",
                  m5encoderConnected[0] ? "OK" : "NO",
                  m5HubChannel[0],
                  m5encoderConnected[1] ? "OK" : "NO",
                  m5HubChannel[1]);
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

        if (!i2c_lock(8)) continue;
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

    // Send all queued UDP messages as a batch OUTSIDE i2c lock
    if (pendingCount > 0 && udpConnected) {
        if (pendingCount == 1) {
            // Single message — send directly (no array overhead)
            JsonDocument doc(&sramAllocator);
            if (pending[0].type == 0) {
                doc["cmd"] = "setTrackVolume";
                doc["track"] = pending[0].track;
                doc["volume"] = pending[0].value;
            } else {
                doc["cmd"] = "mute";
                doc["track"] = pending[0].track;
                doc["value"] = (bool)pending[0].value;
                RED808_LOG_PRINTF("[M5] Track %d %s\n", pending[0].track, pending[0].value ? "MUTED" : "UNMUTED");
            }
            sendUDPCommand(doc);
        } else {
            // Multiple messages — batch into JSON array, single UDP packet
            JsonDocument doc(&sramAllocator);
            JsonArray arr = doc.to<JsonArray>();
            for (int i = 0; i < pendingCount; i++) {
                JsonObject obj = arr.add<JsonObject>();
                if (pending[i].type == 0) {
                    obj["cmd"] = "setTrackVolume";
                    obj["track"] = pending[i].track;
                    obj["volume"] = pending[i].value;
                } else {
                    obj["cmd"] = "mute";
                    obj["track"] = pending[i].track;
                    obj["value"] = (bool)pending[i].value;
                    RED808_LOG_PRINTF("[M5] Track %d %s\n", pending[i].track, pending[i].value ? "MUTED" : "UNMUTED");
                }
            }
            sendUDPCommand(doc);
        }
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
        if (!i2c_lock(8)) continue;
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
            RED808_LOG_PRINTF("[DFRobot] Encoder #%d button PRESSED\n", i);
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
                        nvs_mark_dirty();
                        JsonDocument doc(&sramAllocator);
                        doc["cmd"] = "setLiveVolume";
                        doc["value"] = livePadsVolume;
                        sendUDPCommand(doc);
                    }
                } else {
                    // Modify sequencer volume
                    int newVol = constrain(sequencerVolume + logical_delta * Config::DF_VOLUME_STEP, 0, Config::MAX_VOLUME);
                    if (newVol != sequencerVolume) {
                        sequencerVolume = newVol;
                        nvs_mark_dirty();
                        JsonDocument doc(&sramAllocator);
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
                    nvs_mark_dirty();
                    JsonDocument doc(&sramAllocator);
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
    RED808_LOG_PRINTF("[ROTARY FX] pos=%d '%s'\n", pos, p.name);
}

void handleAnalogEncoder() {
    static int  lastPosition = -1;
    static unsigned long lastChangeMs = 0;
    // Ring buffer for robust median filtering
    static constexpr int RING_SIZE = 8;
    static int ring[RING_SIZE] = {};
    static int ringIdx = 0;
    static bool ringFull = false;
    // Self-calibrating ADC range with noise margin
    static int  calMin = 4095;
    static int  calMax = 0;
    static int  calSamples = 0;

    // 16-sample average per call — sufficient for ESP32-S3 ADC noise
    long sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(Config::ANALOG_ENC_PIN);
    int raw = (int)(sum / 8);

    // Store in ring buffer
    ring[ringIdx] = raw;
    ringIdx = (ringIdx + 1) % RING_SIZE;
    if (!ringFull && ringIdx == 0) ringFull = true;

    // Need at least 4 samples before processing
    int count = ringFull ? RING_SIZE : ringIdx;
    if (count < 4) return;

    // Compute median of ring buffer (sort copy)
    int sorted[RING_SIZE];
    for (int i = 0; i < count; i++) sorted[i] = ring[i];
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (sorted[j] < sorted[i]) { int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }
    int median = sorted[count / 2];

    // Update calibration with median (rejects noise spikes)
    calSamples++;
    if (calSamples > 10) {
        if (median < calMin) calMin = median;
        if (median > calMax) calMax = median;
    }

    // Map using calibrated range
    int position;
    if ((calMax - calMin) >= 250) {
        int margin = (calMax - calMin) / 20;  // 5% margin on edges
        position = constrain((int)map(median, calMin + margin, calMax - margin, 0, 11), 0, 11);
    } else {
        position = constrain((median * 12 + 2048) / 4096, 0, 11);
    }

    // Hysteresis: require 3 consecutive agreeing reads at new position
    static int candidatePos = -1;
    static int candidateCount = 0;
    if (position != lastPosition) {
        if (position == candidatePos) {
            candidateCount++;
        } else {
            candidatePos = position;
            candidateCount = 1;
        }
        if (candidateCount < 3) return;
        if ((millis() - lastChangeMs) < 200) return;
    } else {
        candidatePos = -1;
        candidateCount = 0;
        return;
    }

    lastPosition = position;
    lastChangeMs = millis();
    candidatePos = -1;
    candidateCount = 0;

    analogFxPreset = position;
    applyFxPreset(position);
    RED808_LOG_PRINTF("[ROTARY] median=%d cal=[%d..%d] pos=%d\n", median, calMin, calMax, position);
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

    if (i2c_lock(8)) {
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
    // ByteButton always uses navigation functions (all screens including LIVE)
    {
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
                    nvs_mark_dirty();
                    RED808_LOG_PRINTF("[BB] Volume mode: %s\n", volumeMode == VOL_SEQUENCER ? "SEQ" : "PAD");
                    break;
                case 4: { // FX Delay toggle
                    masterFilter.enabled = !masterFilter.enabled || (filterSelectedFX != FILTER_DELAY);
                    filterSelectedFX = FILTER_DELAY;
                    if (!masterFilter.enabled) masterFilter.delayAmount = 0;
                    else if (masterFilter.delayAmount == 0) masterFilter.delayAmount = 64;
                    sendFilterUDP(-1, FILTER_DELAY);
                    RED808_LOG_PRINTF("[BB] FX DELAY %s\n", masterFilter.enabled ? "ON" : "OFF");
                    break;
                }
                case 5: { // FX Flanger toggle
                    masterFilter.enabled = !masterFilter.enabled || (filterSelectedFX != FILTER_FLANGER);
                    filterSelectedFX = FILTER_FLANGER;
                    if (!masterFilter.enabled) masterFilter.flangerAmount = 0;
                    else if (masterFilter.flangerAmount == 0) masterFilter.flangerAmount = 64;
                    sendFilterUDP(-1, FILTER_FLANGER);
                    RED808_LOG_PRINTF("[BB] FX FLANGER %s\n", masterFilter.enabled ? "ON" : "OFF");
                    break;
                }
                case 6: { // FX Compressor toggle
                    masterFilter.enabled = !masterFilter.enabled || (filterSelectedFX != FILTER_COMPRESSOR);
                    filterSelectedFX = FILTER_COMPRESSOR;
                    if (!masterFilter.enabled) masterFilter.compAmount = 0;
                    else if (masterFilter.compAmount == 0) masterFilter.compAmount = 64;
                    sendFilterUDP(-1, FILTER_COMPRESSOR);
                    RED808_LOG_PRINTF("[BB] FX COMPRESSOR %s\n", masterFilter.enabled ? "ON" : "OFF");
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
                    RED808_LOG_PRINTLN("[BB] ALL FX CLEARED");
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

    if (!lvgl_port_lock(15)) return;  // 15ms: give LVGL task time to finish

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
        case SCREEN_PERFORMANCE: ui_update_performance(); break;
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
    // Wait up to 500ms for USB CDC, skip wait if no monitor connected
    { unsigned long _t = millis(); while (!Serial && (millis()-_t) < 500) delay(10); }
    delay(5);
    RED808_LOG_PRINTLN("\n====================================");
    RED808_LOG_PRINTLN("  Blue808Slave V6");
    RED808_LOG_PRINTLN("  Waveshare ESP32-S3-Touch-LCD-7B");
    RED808_LOG_PRINTLN("====================================\n");

    // Check PSRAM
    if (psramFound()) {
        RED808_LOG_PRINTF("[PSRAM] OK - %d bytes free\n", ESP.getFreePsram());
    } else {
        RED808_LOG_PRINTLN("[PSRAM] WARNING: PSRAM not found!");
    }
    RED808_LOG_PRINTF("[HEAP] Free: %d bytes\n", ESP.getFreeHeap());

    // Init state
    initState();
    RED808_LOG_PRINTLN("[STATE] Initialized");

    // Init NVS flash (must be before nvs_load and setupWiFi)
    {
        esp_err_t nvs_err = nvs_flash_init();
        if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            RED808_LOG_PRINTLN("[NVS] Flash corrupted, erasing...");
            nvs_flash_erase();
            nvs_err = nvs_flash_init();
        }
        RED808_LOG_PRINTF("[NVS] Flash init: %s\n", esp_err_to_name(nvs_err));
    }

    // Load saved settings from NVS (theme, volumes, BPM)
    nvs_load_settings();
    RED808_LOG_PRINTLN("[NVS] Settings loaded");

    // 1. I2C bus
    i2c_init();
    delay(10);
    RED808_LOG_PRINTLN("[I2C] Bus initialized");

    // 2. IO Extension (CH32V003) - backlight, resets
    io_ext_init();
    io_ext_backlight_on();
    delay(10);
    RED808_LOG_PRINTLN("[IO] CH32V003 initialized, backlight ON");

    // 3. LCD panel
    RED808_LOG_PRINTLN("[LCD] Initializing RGB panel...");
    lcd_panel = rgb_lcd_init();
    if (!lcd_panel) {
        RED808_LOG_PRINTLN("[LCD] FATAL: Panel init failed!");
    } else {
        RED808_LOG_PRINTLN("[LCD] Panel initialized OK");
        diagInfo.lcdOk = true;
    }

    // 4. Touch controller
    RED808_LOG_PRINTLN("[Touch] Initializing GT911...");
    gt911_init();
    diagInfo.touchOk = gt911_is_ready();
    RED808_LOG_PRINTF("[Touch] GT911 %s\n", diagInfo.touchOk ? "OK" : "NOT DETECTED");

    // 5. LVGL init (task created but NOT started yet - no I2C contention)
    RED808_LOG_PRINTLN("[LVGL] Initializing port...");
    lvgl_port_init(lcd_panel);
    RED808_LOG_PRINTLN("[LVGL] Port initialized (task paused)");

    // 6. Analog rotary encoder GPIO setup
    pinMode(Config::ANALOG_ENC_PIN, INPUT);
    analogReadResolution(12);  // 12-bit ADC (0-4095)
    RED808_LOG_PRINTLN("[ENC] Analog rotary on GPIO6 initialized");

    // 7. Scan I2C hub for M5 + DFRobot + ByteButton (before LVGL task starts - no I2C race)
    scanI2CHub();

    // 8. WiFi + UDP — init BEFORE LVGL screens to reserve internal SRAM for WiFi DMA buffers
    RED808_LOG_PRINTF("[HEAP] Before WiFi: %d bytes\n", ESP.getFreeHeap());
    setupWiFi();
    RED808_LOG_PRINTF("[HEAP] After WiFi: %d bytes\n", ESP.getFreeHeap());

    // 9. Now start LVGL task (safe - I2C scanning is done)
    lvgl_port_task_start();
    RED808_LOG_PRINTLN("[LVGL] Task started");

    // 10. Create all UI screens (must be inside LVGL lock)
    //    Widgets now allocate from PSRAM via lv_conf.h, keeping internal heap free
    RED808_LOG_PRINTLN("[UI] Creating screens...");
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
        RED808_LOG_PRINTLN("[UI] All screens created");
    }
    RED808_LOG_PRINTF("[HEAP] After UI: %d bytes  PSRAM: %d bytes\n", ESP.getFreeHeap(), ESP.getFreePsram());

    // 11. Encoder task — dedicated FreeRTOS task for I2C encoder polling
    xTaskCreatePinnedToCore(encoder_task, "enc", 6144, NULL, 2, NULL, 0);
    RED808_LOG_PRINTLN("[ENC] Encoder task started (Core 0, pri 2)");

    // 12. Touch task — highest I2C priority, preempts encoder via priority inheritance
    xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 3, NULL, 0);
    RED808_LOG_PRINTLN("[TOUCH] Touch task started (Core 0, pri 3)");

    // 13. Microtiming engine — seed PRNG from hardware RNG
    microtiming_init();

    RED808_LOG_PRINTLN("\n[SETUP] Complete! Entering main loop.\n");
}

// =============================================================================
// TOUCH TASK (Core 0, priority 3 — highest I2C priority)
// Polls GT911 INT pin every 2ms. When INT=LOW (data ready), acquires i2c_lock
// and reads touch data. Priority inheritance ensures encoder_task (pri 2)
// finishes its current I2C transaction quickly then yields the bus.
// =============================================================================

static void touch_task(void* arg) {
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        // Only read when GT911 signals new data (INT pin = LOW)
        if (digitalRead(GT911_INT_PIN) == LOW) {
            gt911_poll();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(2));
    }
}

// =============================================================================
// ENCODER TASK (Core 0, priority 2 — dedicated I2C polling)
// Runs independently of loop(), so I2C reads never block network/UI/touch.
// =============================================================================

static void encoder_task(void* arg) {
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        handleM5Encoders();
        handleDFRobotEncoders();
        handleAnalogEncoder();
        handleByteButton();
        uint32_t period = encoder_poll_interval_ms(currentScreen);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(period));
    }
}

// =============================================================================
// LOOP (Core 0 - network + touch + UI)
// =============================================================================

void loop() {
    unsigned long now = millis();
    static Screen lastUiScreen = SCREEN_BOOT;

    // === TOUCH + TRIGGER: highest priority — process FIRST for lowest latency ===
    handleLivePadTouchMatrix(now);

    const int padVelocity = map(constrain(livePadsVolume, 0, Config::MAX_VOLUME), 0, Config::MAX_VOLUME, 32, 127);

    uint32_t pendingMask = pendingLivePadTriggerMask;
    if (pendingMask != 0) {
        pendingLivePadTriggerMask = 0;
        // Send UDP triggers FIRST (lowest latency to audio)
        for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
            if ((pendingMask & (1UL << pad)) == 0) continue;
            sendLivePadTrigger(pad, padVelocity);
            lastLivePadTriggerMs[pad] = now;
            livePadFlashUntilMs[pad] = now + LIVE_PAD_FLASH_MS;
        }
        // Mark visual dirty — LVGL task picks it up within ~10ms (no lock contention)
        if (currentScreen == SCREEN_LIVE) {
            livePadsVisualDirty = true;
        }
    }

    bool repeatedPadTrigger = false;
    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (livePadRemainingRepeats[pad] == 0) continue;
        if (now < livePadNextRepeatMs[pad]) continue;

        sendLivePadTrigger(pad, padVelocity);
        lastLivePadTriggerMs[pad] = now;
        livePadFlashUntilMs[pad] = now + LIVE_PAD_FLASH_MS;
        livePadRemainingRepeats[pad]--;
        livePadNextRepeatMs[pad] = now + LIVE_PAD_REPEAT_INTERVAL_MS;
        repeatedPadTrigger = true;
    }
    if (repeatedPadTrigger && currentScreen == SCREEN_LIVE) {
        livePadsVisualDirty = true;
    }

    // === NETWORK + STATUS ===
    if (masterConnected && (now - lastMasterPacketMs > 3000)) {
        masterConnected = false;
    }

    checkWiFiReconnect();

    static unsigned long lastUDPReceiveMs = 0;
    if (now - lastUDPReceiveMs >= WiFiConfig::UDP_RECEIVE_MS) {
        lastUDPReceiveMs = now;
        receiveUDPData();
    }

    if (wifiConnected && udpConnected && !masterConnected && (now - lastUDPCheck >= WiFiConfig::MASTER_HELLO_RETRY_MS)) {
        lastUDPCheck = now;
        RED808_LOG_PRINTLN("[UDP] Master not responding, resending hello...");
        requestMasterSync(false);
    }

    // Boot LED animation
    if (currentScreen == SCREEN_BOOT) {
        bootLedAnimation();
    }

    // Transition from boot → runtime UI: always apply the stable LED theme once.
    // Do not wait for bootLedDone; if the user dismisses BOOT early, M5 #2 could
    // otherwise remain dark because the boot LED sweep never reached module 2.
    {
        static bool bootLedTransitionDone = false;
        if (!bootLedTransitionDone && currentScreen != SCREEN_BOOT) {
            bootLedTransitionDone = true;
            setOceanBlueLeds();
        }
    }

    // (Hold-repeat removed — repeat count is controlled by RPT buttons)

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

    // Encoders run in dedicated encoder_task — no polling here

    // UI update (~60fps)
    bool forceUiRefresh = false;
    if (currentScreen != lastUiScreen) {
        previousScreen = lastUiScreen;
        lastUiScreen = currentScreen;
        forceUiRefresh = true;
    }

    uint32_t uiInterval = ui_refresh_interval_ms(currentScreen, isPlaying);
    uiLastIntervalMs = uiInterval;
    if (forceUiRefresh || (now - lastScreenUpdate >= uiInterval)) {
        lastScreenUpdate = now;
        updateUI();
        uiUpdateCount = uiUpdateCount + 1;
    } else {
        uiSkippedCount = uiSkippedCount + 1;
    }

    // Debug heartbeat every 10s
    static unsigned long lastHeartbeat = 0;

    // NVS periodic save (debounced every 5s when dirty)
    nvs_periodic_save();

    static bool firstDiagDone = false;
    if (!firstDiagDone && now > 5000) {
        firstDiagDone = true;
        RED808_LOG_PRINTLN("\n=== RUNTIME DIAGNOSTICS ===");
        RED808_LOG_PRINTF("  GT911 touch: %s\n", gt911_is_ready() ? "OK" : "NOT DETECTED");
        RED808_LOG_PRINTF("  I2C Hub:     %s\n", hubDetected ? "OK" : "NOT FOUND");
        RED808_LOG_PRINTF("  M5 #1:       %s (ch=%d)\n", m5encoderConnected[0] ? "OK" : "NO", m5HubChannel[0]);
        RED808_LOG_PRINTF("  M5 #2:       %s (ch=%d)\n", m5encoderConnected[1] ? "OK" : "NO", m5HubChannel[1]);
        RED808_LOG_PRINTF("  DFRobot #1:  %s (ch=%d)\n", dfEncoderConnected[0] ? "OK" : "NO", dfRobotHubChannel[0]);
        RED808_LOG_PRINTF("  DFRobot #2:  %s (ch=%d)\n", dfEncoderConnected[1] ? "OK" : "NO", dfRobotHubChannel[1]);
        RED808_LOG_PRINTF("  ByteButton:  %s (ch=%d)\n", byteButtonConnected ? "OK" : "NO", byteButtonHubChannel);
        RED808_LOG_PRINTF("  LCD panel:   %s\n", lcd_panel ? "OK" : "FAIL");
        RED808_LOG_PRINTF("  Heap: %d  PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
        RED808_LOG_PRINTLN("===========================\n");
    }
    if (now - lastHeartbeat >= 10000) {
        lastHeartbeat = now;
        RED808_LOG_PRINTF("[ALIVE] %lus Heap:%d PSRAM:%d\n", now/1000, ESP.getFreeHeap(), ESP.getFreePsram());
    }

    taskYIELD(); // Yield to encoder_task and other Core 0 tasks
}
