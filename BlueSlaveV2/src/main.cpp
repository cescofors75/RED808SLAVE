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
#include <math.h>
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

// UART bridge to P4 Visual Beast
#include "uart_bridge.h"
#include "../include/uart_protocol.h"

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
ArduinoJson::Allocator* sramAllocatorPtr = &sramAllocator;

// =============================================================================
// GLOBAL STATE DEFINITIONS
// =============================================================================

// Screen
volatile Screen currentScreen = SCREEN_BOOT;
Screen previousScreen = SCREEN_BOOT;
bool needsFullRedraw = true;
volatile bool themeJustChanged = false;
volatile int  pendingThemeIdx  = -1;   // Set by encoder_task, consumed by updateUI under LVGL lock

// Sequencer
Pattern patterns[Config::MAX_PATTERNS];
int currentPattern = 0;
int currentStep = 0;
int selectedTrack = 0;
bool isPlaying = false;
int currentBPM = Config::DEFAULT_BPM;
float currentBPMPrecise = (float)Config::DEFAULT_BPM;
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
int filterSelectedFX = FILTER_FLANGER;
int fxFilterType = 0;
int fxFilterCutoffHz = 20000;    // max (fully open) — pot at zero = no filtering
int fxFilterResonanceX10 = 10;   // 1.0 Q — min
int fxBitCrushBits = 16;
int fxDistortionPercent = 0;
int fxSampleRateHz = 44100;
EncoderMode encoderMode = ENC_MODE_VOLUME;

// I2C Hub
int m5HubChannel[M5_ENCODER_MODULES] = {-1, -1};
int dfRobotHubChannel[DFROBOT_ENCODER_COUNT] = {-1, -1, -1, -1};
int byteButtonHubChannel[BYTEBUTTON_COUNT] = {-1, -1};
int dfRobotPotHubChannel = -1;
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
uint8_t m5FirmwareVersion[M5_ENCODER_MODULES] = {0, 0};  // stored at init for I2C health check
DFRobot_VisualRotaryEncoder_I2C* dfEncoders[DFROBOT_ENCODER_COUNT] = {};
bool dfEncoderConnected[DFROBOT_ENCODER_COUNT] = {};
bool byteButtonConnected[BYTEBUTTON_COUNT] = {false, false};
bool dfRobotPotConnected = false;
uint8_t dfRobotPotAddr = DFROBOT_POT_ADC_ADDR;
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
static constexpr unsigned long LIVE_TOUCH_GUARD_MS = 150;
static constexpr unsigned long TOUCH_RELEASE_DEBOUNCE_MS = 16;  // avoid false releases on fast GT911 cache gaps
static constexpr unsigned long LIVE_PAD_REPEAT_INTERVAL_MS = 75;
static constexpr unsigned long LIVE_PAD_FLASH_MS = 25;
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
uint32_t lastLocalStepUs = 0;         // microsecond local clock anchor for decimal BPM precision
int32_t prevM5Counter[Config::MAX_TRACKS];
uint16_t prevDFValue[DFROBOT_ENCODER_COUNT];
unsigned long lastLivePadTriggerMs[Config::MAX_SAMPLES];
unsigned long livePadFlashUntilMs[Config::MAX_SAMPLES];
unsigned long livePadHoldStartMs[Config::MAX_SAMPLES];
static uint8_t livePadRemainingRepeats[Config::MAX_SAMPLES] = {};
static unsigned long livePadNextRepeatMs[Config::MAX_SAMPLES] = {};
uint8_t prevByteButtonState[BYTEBUTTON_COUNT] = {0, 0};
bool byteButtonLivePressed[BYTEBUTTON_TOTAL_BUTTONS] = {};
uint32_t byteButtonLedCache[BYTEBUTTON_COUNT][BYTEBUTTON_BUTTONS + 1] = {};
bool byteButtonLedInitialized[BYTEBUTTON_COUNT] = {false, false};
volatile FxResponseMode fxResponseMode = FX_MODE_LIVE;
const char* const byteButtonActionNames[] = {
    "Back/Menu",
    "Master Link Sync",
    "Go Live Pads",
    "Go Sequencer",
    "Go FX",
    "Go Volumes",
    "FX Mute P2 Cutoff",
    "FX Mute P3 Resonance",
    "FX Mute P4 Drive",
    "FX Mute Analog ALL",
    "FX Target --",
    "FX Target ++",
    "Pattern --",
    "Pattern ++",
    "Play/Pause"
};
uint8_t byteButtonActionMap[BYTEBUTTON_TOTAL_BUTTONS] = {
    BB_ACTION_MENU,
    BB_ACTION_FX_CLEAN,
    BB_ACTION_FX_SPACE,
    BB_ACTION_FX_ACID,
    BB_ACTION_SCREEN_FILTERS,
    BB_ACTION_PATTERN_PREV,
    BB_ACTION_PATTERN_NEXT,
    BB_ACTION_PLAY_PAUSE,
    BB_ACTION_SCREEN_LIVE,
    BB_ACTION_SCREEN_SEQUENCER,
    BB_ACTION_SCREEN_VOLUMES,
    BB_ACTION_FX_DESTROY,
    BB_ACTION_FX_TARGET_PREV,
    BB_ACTION_FX_TARGET_NEXT,
    BB_ACTION_VOL_MODE,
    BB_ACTION_FX_DESTROY
};
uint8_t dfFxParamMode[3] = {0, 0, 0};
int dfFxParamValue[3] = {0, 0, 0};  // boot gate requires zero start
bool dfFxMuted[3] = {false, false, false};
bool analogFxMuted[3] = {false, false, false};
uint16_t dfRobotPotRaw[DFROBOT_POT_COUNT] = {};
uint8_t dfRobotPotMidi[DFROBOT_POT_COUNT] = {};
uint8_t dfRobotPotPos[DFROBOT_POT_COUNT] = {};
uint8_t dfRobotPotLastSent[DFROBOT_POT_COUNT] = {255, 255, 255, 255};
uint8_t dfRobotPotPosLastSent[DFROBOT_POT_COUNT] = {255, 255, 255, 255};
uint16_t dfRobotPotCalMin[DFROBOT_POT_COUNT] = {65535, 65535, 65535, 65535};
uint16_t dfRobotPotCalMax[DFROBOT_POT_COUNT] = {0, 0, 0, 0};

static constexpr uint8_t BYTEBUTTON_STATUS_REG      = 0x00;  // bitmask — unreliable polarity on some FW
static constexpr uint8_t BYTEBUTTON_STATUS_8BYTE_REG = 0x60;  // per-button byte: 0x01=pressed, 0x00=released
static constexpr uint8_t BYTEBUTTON_LED_BRIGHTNESS_REG = 0x10;
static constexpr uint8_t BYTEBUTTON_LED_SHOW_MODE_REG = 0x19;
static constexpr uint8_t BYTEBUTTON_LED_RGB888_REG = 0x20;
static constexpr uint8_t BYTEBUTTON_LED_COUNT = BYTEBUTTON_BUTTONS + 1;
static constexpr uint8_t BYTEBUTTON_LED_USER_DEFINED = 0;
static constexpr uint8_t BYTEBUTTON_BRIGHTNESS = 180;
static constexpr int kByteButtonExpectedChannels[BYTEBUTTON_COUNT] = {BYTEBUTTON1_HUB_CH, BYTEBUTTON2_HUB_CH};

// FX response profile — replaces 6 individual switch-case functions
struct FxProfile { int df_step; int accel_threshold; int accel_mult; uint32_t pot_read_ms; uint8_t pot_midi_db; uint8_t pot_stable; };
static constexpr FxProfile kFxProfiles[] = {
    /* PRECISION */ {1, 99, 1, 16, 2, 2},
    /* LIVE      */ {2,  8, 2, 8,  1, 1},
};
static inline const FxProfile& fxp() { return kFxProfiles[fxResponseMode]; }

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
            return 16;
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
static void applyUnifiedMasterVolume(int value, bool sendToMaster);
static void applyBPMPrecise(float bpm, bool sendToMaster);
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
void handleDFRobotPots();
void handleByteButton();
void updateByteButtonLeds();
void updateTrackEncoderLED(int track);
void handleUnitFader();
void updateUI();
static void encoder_task(void* arg);
static void touch_task(void* arg);
static void pad_trigger_task(void* arg);

// =============================================================================
// P4 TOUCH COMMAND HANDLER (called from uart_bridge_receive)
// =============================================================================
static bool navigateToScreen(Screen screen);  // forward decl

void handleP4TouchCommand(uint8_t cmdId, uint8_t value) {
    switch (cmdId) {
        case TCMD_PAD_TAP:
            if (value < Config::MAX_SAMPLES) {
                sendLivePadTrigger(value, 100);
            }
            break;
        case TCMD_PLAY_TOGGLE:
            sendPlayStateCommand(!isPlaying);
            break;
        case TCMD_PATTERN_SEL:
            selectPatternOnMaster(value);
            break;
        case TCMD_SCREEN_NAV:
            navigateToScreen((Screen)value);
            break;
        case TCMD_THEME_NEXT:
            pendingThemeIdx = (currentTheme + 1) % THEME_COUNT;
            break;
        default:
            break;
    }
}

// =============================================================================
// NVS PERSISTENCE — save/load user settings across reboots
// =============================================================================
#include "nvs.h"

static constexpr const char* NVS_NAMESPACE = "red808";
static unsigned long nvs_last_save_ms = 0;
static bool nvs_dirty = false;

static void applyBPMPrecise(float bpm, bool sendToMaster) {
    float clamped = constrain(bpm, (float)Config::MIN_BPM, (float)Config::MAX_BPM);
    currentBPMPrecise = clamped;
    currentBPM = (int)lroundf(clamped);
    nvs_dirty = true;

    // Forward BPM to P4
    uart_bridge_send_bpm(clamped);

    if (!sendToMaster || !udpConnected) return;

    JsonDocument doc(&sramAllocator);
    doc["cmd"] = "tempo";
    doc["value"] = currentBPMPrecise;
    sendUDPCommand(doc);
}

static void nvs_load_settings() {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        RED808_LOG_PRINTLN("[NVS] No saved settings found");
        return;
    }
    uint8_t val8;
    int32_t val32;
    bool hasMasterVol = false;
    bool hasSeqVol = false;
    bool hasLiveVol = false;

    if (nvs_get_u8(h, "theme", &val8) == ESP_OK && val8 < THEME_COUNT)
        currentTheme = (VisualTheme)val8;
    if (nvs_get_i32(h, "masterVol", &val32) == ESP_OK) {
        masterVolume = constrain((int)val32, 0, Config::MAX_VOLUME);
        hasMasterVol = true;
    }
    if (nvs_get_i32(h, "seqVol", &val32) == ESP_OK) {
        sequencerVolume = constrain((int)val32, 0, Config::MAX_VOLUME);
        hasSeqVol = true;
    }
    if (nvs_get_i32(h, "liveVol", &val32) == ESP_OK) {
        livePadsVolume = constrain((int)val32, 0, Config::MAX_VOLUME);
        hasLiveVol = true;
    }
    if (nvs_get_i32(h, "bpm", &val32) == ESP_OK) {
        currentBPM = constrain((int)val32, Config::MIN_BPM, Config::MAX_BPM);
        currentBPMPrecise = (float)currentBPM;
    }
    if (nvs_get_i32(h, "bpm10", &val32) == ESP_OK) {
        currentBPMPrecise = constrain((float)val32 / 10.0f, (float)Config::MIN_BPM, (float)Config::MAX_BPM);
        currentBPM = (int)lroundf(currentBPMPrecise);
    }
    if (nvs_get_u8(h, "volMode", &val8) == ESP_OK)
        volumeMode = (val8 == 1) ? VOL_LIVE_PADS : VOL_SEQUENCER;

    // Unified master volume mode: keep all lanes locked to one value.
    int unified = masterVolume;
    if (!hasMasterVol) {
        if (hasSeqVol) unified = sequencerVolume;
        else if (hasLiveVol) unified = livePadsVolume;
    }
    masterVolume = unified;
    sequencerVolume = unified;
    livePadsVolume = unified;
    volumeMode = VOL_SEQUENCER;

    nvs_close(h);
    RED808_LOG_PRINTF("[NVS] Loaded: theme=%d bpm=%d vol=%d/%d/%d\n",
                  currentTheme, currentBPM, masterVolume, sequencerVolume, livePadsVolume);
}

void nvs_save_settings() {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_u8(h, "theme", (uint8_t)currentTheme);
    nvs_set_i32(h, "masterVol", masterVolume);
    nvs_set_i32(h, "seqVol", masterVolume);
    nvs_set_i32(h, "liveVol", masterVolume);
    nvs_set_i32(h, "bpm", currentBPM);
    nvs_set_i32(h, "bpm10", (int32_t)lroundf(currentBPMPrecise * 10.0f));
    nvs_set_u8(h, "volMode", 0);

    nvs_commit(h);
    nvs_close(h);
    nvs_dirty = false;
    nvs_last_save_ms = millis();
}

// Mark settings dirty — will be flushed on next periodic check
void nvs_mark_dirty() { nvs_dirty = true; }

static void applyUnifiedMasterVolume(int value, bool sendToMaster) {
    int v = constrain(value, 0, Config::MAX_VOLUME);
    masterVolume = v;
    sequencerVolume = v;
    livePadsVolume = v;
    volumeMode = VOL_SEQUENCER;
    nvs_mark_dirty();

    // Forward volume to P4
    uart_bridge_send_volume(v, v, v);

    if (!sendToMaster || !udpConnected) return;

    JsonDocument doc(&sramAllocator);
    doc["cmd"] = "setVolume";
    doc["value"] = v;
    sendUDPCommand(doc);

    doc.clear();
    doc["cmd"] = "setSequencerVolume";
    doc["value"] = v;
    sendUDPCommand(doc);

    doc.clear();
    doc["cmd"] = "setLiveVolume";
    doc["value"] = v;
    sendUDPCommand(doc);
}

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

        // Reset all encoder FX to OFF on connection (clean slate)
        { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerActive"; d["value"] = 0; sendUDPCommand(d); }
        { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbActive";  d["value"] = 0; sendUDPCommand(d); }
        { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserActive";  d["value"] = 0; sendUDPCommand(d); }
        { JsonDocument d(&sramAllocator); d["cmd"] = "setDelayActive";   d["value"] = 0; sendUDPCommand(d); }
        { JsonDocument d(&sramAllocator); d["cmd"] = "setChorusActive";  d["value"] = 0; sendUDPCommand(d); }
        { JsonDocument d(&sramAllocator); d["cmd"] = "setFilter";        d["value"] = 0; sendUDPCommand(d); }
        { JsonDocument d(&sramAllocator); d["cmd"] = "setDistortion";    d["value"] = 0.0f; sendUDPCommand(d); }
        RED808_LOG_PRINTLN("[FX] All effects reset to OFF on connection");
    }
}

static int quantizeDFDelta(int index, int16_t delta) {
    static int accumulators[DFROBOT_ENCODER_COUNT] = {};
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

static int16_t filterDFDeltaJitter(int index, int16_t delta) {
    static int8_t lastSign[DFROBOT_ENCODER_COUNT] = {};
    static uint8_t confirmCount[DFROBOT_ENCODER_COUNT] = {};

    if (index < 0 || index >= DFROBOT_ENCODER_COUNT) return 0;
    if (delta == 0) return 0;

    int mag = abs((int)delta);
    if (mag <= Config::DF_IDLE_DELTA_DB) return 0;

    int8_t sign = (delta > 0) ? 1 : -1;
    if (mag <= Config::DF_NEAR_ZERO_REPEAT) {
        if (lastSign[index] == sign) {
            if (confirmCount[index] < 255) confirmCount[index]++;
        } else {
            lastSign[index] = sign;
            confirmCount[index] = 1;
        }
        if (confirmCount[index] < 2) return 0;
    } else {
        lastSign[index] = sign;
        confirmCount[index] = 0;
    }

    return delta;
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

static void initByteButtonLeds(int moduleIdx) {
    if (moduleIdx < 0 || moduleIdx >= BYTEBUTTON_COUNT) return;
    if (!byteButtonConnected[moduleIdx]) return;
    if (!i2c_lock(30)) return;

    if (byteButtonHubChannel[moduleIdx] >= 0) i2c_hub_select_raw(byteButtonHubChannel[moduleIdx]);
    else if (byteButtonHubChannel[moduleIdx] == -2) i2c_hub_deselect_raw();
    bool ok = byteButtonApplyLedConfigLocked();
    if (byteButtonHubChannel[moduleIdx] >= 0) i2c_hub_deselect_raw();
    i2c_unlock();

    if (ok) {
        byteButtonLedInitialized[moduleIdx] = true;
        memset(byteButtonLedCache[moduleIdx], 0xFF, sizeof(byteButtonLedCache[moduleIdx]));
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
        for (int m = 0; m < BYTEBUTTON_COUNT; m++) prevByteButtonState[m] = 0xFF;
        memset(byteButtonLivePressed, 0, sizeof(byteButtonLivePressed));
    }

    if (!lvgl_port_lock(20)) return false;
    currentScreen = screen;
    lv_scr_load(target);
    lvgl_port_unlock();

    // Forward screen navigation to P4
    uart_bridge_send_screen((int)screen);

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
    for (int i = 0; i < DFROBOT_POT_COUNT; i++) {
        dfRobotPotRaw[i] = 0;
        dfRobotPotMidi[i] = 0;
        dfRobotPotPos[i] = 0;
        dfRobotPotLastSent[i] = 255;
        dfRobotPotPosLastSent[i] = 255;
        dfRobotPotCalMin[i] = 65535;
        dfRobotPotCalMax[i] = 0;
    }
    for (int m = 0; m < BYTEBUTTON_COUNT; m++) prevByteButtonState[m] = 0;
    pendingLivePadTriggerMask = 0;
    memset(byteButtonLivePressed, 0, sizeof(byteButtonLivePressed));
    memset(byteButtonLedCache, 0xFF, sizeof(byteButtonLedCache));
    memset(byteButtonLedInitialized, 0, sizeof(byteButtonLedInitialized));
}

// =============================================================================
// WiFi / UDP
// =============================================================================

static void finalizeWiFiConnection() {
    wifiConnected = true;
    wifiReconnecting = false;
    lastWiFiConnectedMs = millis();
    lastWiFiCheck = lastWiFiConnectedMs;
    lastUDPCheck = 0;  // force immediate hello on next loop tick
    uart_bridge_send_wifi_state(true, masterConnected);

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
    uart_bridge_send_play_state(isPlaying);
    JsonDocument doc(&sramAllocator);
    doc["cmd"] = isPlaying ? "start" : "stop";
    sendUDPCommand(doc);
}

void selectPatternOnMaster(int patternIndex) {
    currentPattern = constrain(patternIndex, 0, Config::MAX_PATTERNS - 1);

    // Forward pattern to P4
    uart_bridge_send_pattern(currentPattern);

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
    if (!masterConnected) {
        masterConnected = true;
        uart_bridge_send_wifi_state(wifiConnected, true);
    }
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
            // Forward pattern data to P4
            uart_bridge_send_pattern_data(pat, patterns[pat].steps, Config::MAX_TRACKS);
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
            lastLocalStepUs = micros();
        }
        isPlaying = playing;
        uart_bridge_send_play_state(isPlaying);
    }
    else if (strcmp(cmd, "start") == 0) {
        if (!isPlaying) {
            currentStep = 0;
            lastLocalStepMs = millis();
            lastLocalStepUs = micros();
        }
        isPlaying = true;
        uart_bridge_send_play_state(true);
    }
    else if (strcmp(cmd, "stop") == 0) {
        isPlaying = false;
        currentStep = 0;
        uart_bridge_send_play_state(false);
    }
    else if (strcmp(cmd, "tempo_sync") == 0 || strcmp(cmd, "tempo") == 0) {
        float bpm = doc["value"].is<float>() ? doc["value"].as<float>() : (float)(doc["value"] | Config::DEFAULT_BPM);
        applyBPMPrecise(bpm, false);
    }
    else if (strcmp(cmd, "volume_sync") == 0 ||
             strcmp(cmd, "master_volume_sync") == 0 ||
             strcmp(cmd, "volume_master_sync") == 0 ||
             strcmp(cmd, "setVolume") == 0) {
        applyUnifiedMasterVolume(doc["value"] | Config::DEFAULT_VOLUME, false);
    }
    else if (strcmp(cmd, "volume_seq_sync") == 0 || strcmp(cmd, "setSequencerVolume") == 0) {
        applyUnifiedMasterVolume(doc["value"] | Config::DEFAULT_VOLUME, false);
    }
    else if (strcmp(cmd, "volume_live_sync") == 0 || strcmp(cmd, "setLiveVolume") == 0) {
        applyUnifiedMasterVolume(doc["value"] | Config::DEFAULT_VOLUME, false);
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
            uart_bridge_send_pattern(currentPattern);
            requestPatternFromMaster();
        }
    }
    // end receiveUDPData
}

// =============================================================================
// FILTER UDP SEND
// =============================================================================

void sendFilterUDP(int track, int fxType) {
    // Per-track FX not supported by master protocol — ignore.
    if (track != -1) return;

    TrackFilter& f = masterFilter;
    float mix;
    bool activating;

    switch (fxType) {
        case FILTER_FLANGER: {
            mix = (float)f.delayAmount / 127.0f;
            activating = (f.delayAmount > 0);
            { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerActive"; d["value"] = activating ? 1 : 0; sendUDPCommand(d); }
            if (activating) {
                { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerRate";     d["value"] = 0.3f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerDepth";    d["value"] = 0.7f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerFeedback"; d["value"] = 0.5f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerMix";      d["value"] = mix;  sendUDPCommand(d); }
            }
            break;
        }
        case FILTER_REVERB: {
            mix = (float)f.flangerAmount / 127.0f;
            activating = (f.flangerAmount > 0);
            { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbActive"; d["value"] = activating ? 1 : 0; sendUDPCommand(d); }
            if (activating) {
                { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbFeedback"; d["value"] = 0.6f;  sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbLpFreq";   d["value"] = 5000;  sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbMix";      d["value"] = mix;   sendUDPCommand(d); }
            }
            break;
        }
        case FILTER_PHASER: {
            mix = (float)f.compAmount / 127.0f;
            activating = (f.compAmount > 0);
            { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserActive"; d["value"] = activating ? 1 : 0; sendUDPCommand(d); }
            if (activating) {
                { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserRate";     d["value"] = 0.8f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserDepth";    d["value"] = 0.6f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserFeedback"; d["value"] = 0.5f; sendUDPCommand(d); }
            }
            break;
        }
    }
}

// =============================================================================
// I2C HUB & DEVICE SCANNING
// =============================================================================

static bool ads1115WriteReg(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(dfRobotPotAddr);
    Wire.write(reg);
    Wire.write((uint8_t)(value >> 8));
    Wire.write((uint8_t)(value & 0xFF));
    return Wire.endTransmission() == 0;
}

static bool ads1115ReadReg(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(dfRobotPotAddr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)dfRobotPotAddr, (uint8_t)2) != 2) return false;
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    value = (uint16_t)((msb << 8) | lsb);
    return true;
}

static bool ads1115ReadChannel(uint8_t channel, uint16_t& raw) {
    if (channel >= DFROBOT_POT_COUNT) return false;

    // Single-shot conversion, AINx vs GND, PGA +-4.096V, 860SPS.
    const uint16_t muxByChannel[DFROBOT_POT_COUNT] = {
        0x4000, 0x5000, 0x6000, 0x7000
    };
    uint16_t config = 0x8000 | muxByChannel[channel] | 0x0200 | 0x0100 | 0x00E0 | 0x0003;
    if (!ads1115WriteReg(0x01, config)) return false;

    delay(2);

    uint16_t conv = 0;
    if (!ads1115ReadReg(0x00, conv)) return false;

    int16_t signedValue = (int16_t)conv;
    if (signedValue < 0) signedValue = 0;
    raw = (uint16_t)signedValue;
    return true;
}

void handleDFRobotPots() {
    if (!dfRobotPotConnected) return;

    static unsigned long lastReadMs = 0;
    unsigned long now = millis();
    if ((now - lastReadMs) < fxp().pot_read_ms) return;
    lastReadMs = now;

    bool readOk = false;
    uint16_t sampleRaw[DFROBOT_POT_COUNT] = {};
    uint8_t sampleMidi[DFROBOT_POT_COUNT] = {};

    if (i2c_lock(18)) {
        if (dfRobotPotHubChannel >= 0) i2c_hub_select_raw(dfRobotPotHubChannel);
        else if (dfRobotPotHubChannel == -2) i2c_hub_deselect_raw();

        readOk = true;
        for (uint8_t ch = 0; ch < DFROBOT_POT_COUNT; ch++) {
            uint16_t raw = 0;
            if (!ads1115ReadChannel(ch, raw)) {
                readOk = false;
                break;
            }
            sampleRaw[ch] = raw;
            sampleMidi[ch] = (uint8_t)map((long)raw, 0L, 32767L, 0L, 127L);
        }

        if (dfRobotPotHubChannel >= 0) i2c_hub_deselect_raw();
        i2c_unlock();
    }

    if (!readOk) return;

    static bool potInit[DFROBOT_POT_COUNT] = {false, false, false, false};
    static uint16_t potFilteredRaw[DFROBOT_POT_COUNT] = {0, 0, 0, 0};
    static uint8_t potLastMidi[DFROBOT_POT_COUNT] = {255, 255, 255, 255};

    for (int pot = 0; pot < DFROBOT_POT_COUNT; pot++) {
        uint16_t raw = sampleRaw[pot];
        if (!potInit[pot]) {
            potFilteredRaw[pot] = raw;
            potInit[pot] = true;
            // First read = physical zero reference. Min is fixed here.
            // Max starts slightly above so range expands only upward as user turns pot.
            dfRobotPotCalMin[pot] = raw;
            dfRobotPotCalMax[pot] = raw;  // no span yet → holds at MIDI 0 until pot moves
        } else {
            // Light low-pass: 50/50 — tracks pot movement quickly, still smooths single-sample spikes.
            potFilteredRaw[pot] = (uint16_t)((potFilteredRaw[pot] + (uint32_t)raw) / 2U);
        }
        raw = potFilteredRaw[pot];
        dfRobotPotRaw[pot] = raw;

        // Adaptive calibration — expands range as pot is moved.
        if (raw < dfRobotPotCalMin[pot]) dfRobotPotCalMin[pot] = raw;
        if (raw > dfRobotPotCalMax[pot]) dfRobotPotCalMax[pot] = raw;

        uint16_t minV = dfRobotPotCalMin[pot];
        uint16_t maxV = dfRobotPotCalMax[pot];

        // Map directly to MIDI 0..127 using calibrated range.
        uint8_t midi;
        if (maxV > minV + Config::DF_POT_MIN_SPAN) {
            long mapped = map((long)raw, (long)minV, (long)maxV, 0L, 127L);
            midi = (uint8_t)constrain((int)mapped, 0, 127);
        } else {
            // Not enough travel yet — hold previous value.
            midi = (potLastMidi[pot] != 255) ? potLastMidi[pot] : 0;
        }

        // Also update detent position (12 steps) for diagnostics display.
        dfRobotPotPos[pot] = (uint8_t)constrain((int)((midi * 11U) / 127U), 0, 11);
        dfRobotPotMidi[pot] = midi;

        // Hysteresis: only send when change exceeds deadband.
        if (potLastMidi[pot] != 255) {
            int delta = abs((int)midi - (int)potLastMidi[pot]);
            if (delta < fxp().pot_midi_db) continue;
        }
        potLastMidi[pot] = midi;
        dfRobotPotLastSent[pot] = midi;
        dfRobotPotPosLastSent[pot] = dfRobotPotPos[pot];

        // Forward pot MIDI to P4
        uart_bridge_send_pot(pot, midi);

        JsonDocument doc(&sramAllocator);
        if (pot == 0) {
            // P1: Master volume — continuous 0..150 from MIDI 0..127.
            int newVol = (int)lroundf(((float)midi / 127.0f) * (float)Config::MAX_VOLUME);
            applyUnifiedMasterVolume(newVol, true);
        } else {
            // P2/P3/P4: direct analog FX controls (no scene macros).
            float t = (float)midi / 127.0f;
            if (pot == 1) {
                // P2: disabled (cutoff removed)
                (void)t;
            } else if (pot == 2) {
                // 1.0 .. 10.0 Q
                fxFilterResonanceX10 = constrain((int)lroundf(10.0f + t * 90.0f), 10, 100);
                if (!analogFxMuted[1]) {
                    doc["cmd"] = "setFilterResonance";
                    doc["value"] = (float)fxFilterResonanceX10 / 10.0f;
                    sendUDPCommand(doc);
                }
            } else {
                // P4: Distortion drive (0.0 – 1.0)
                fxDistortionPercent = constrain((int)lroundf(t * 100.0f), 0, 100);
                if (!analogFxMuted[2]) {
                    doc["cmd"] = "setDistortion";
                    doc["value"] = (float)fxDistortionPercent / 100.0f;
                    sendUDPCommand(doc);
                }
            }
        }
    }
}

static void sendAnalogFxParamNow(int lane) {
    JsonDocument doc(&sramAllocator);
    if (lane == 0) {
        // P2: cutoff removed — do nothing
        return;
    } else if (lane == 1) {
        doc["cmd"] = "setFilterResonance";
        doc["value"] = analogFxMuted[1] ? 1.0f : ((float)fxFilterResonanceX10 / 10.0f);
    } else {
        doc["cmd"] = "setDistortion";
        doc["value"] = analogFxMuted[2] ? 0.0f : ((float)fxDistortionPercent / 100.0f);
    }
    sendUDPCommand(doc);
}

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
    int byteButtonsFound = 0;
    bool potHubFound = false;

    for (uint8_t ch = 0; ch < 8 && (m5Found < M5_ENCODER_MODULES || dfFound < DFROBOT_ENCODER_COUNT || byteButtonsFound < BYTEBUTTON_COUNT || !potHubFound); ch++) {
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
                if (m5encoders[m5Found].begin()) {
                    for (int e = 0; e < ENCODERS_PER_MODULE; e++) {
                        m5encoders[m5Found].resetCounter(e);
                    }
                }
                m5FirmwareVersion[m5Found] = m5encoders[m5Found].getVersion();
                RED808_LOG_PRINTF("[I2C] M5 #%d firmware v%d\n", m5Found + 1, m5FirmwareVersion[m5Found]);
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
                    dfEncoders[dfFound]->setGainCoefficient(10);
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
        if (byteButtonsFound < BYTEBUTTON_COUNT) {
            i2c_hub_select(ch);
            delay(2);
            if (i2c_device_present(BYTEBUTTON_ADDR)) {
                bool alreadyAssigned = false;
                for (int m = 0; m < BYTEBUTTON_COUNT; m++) {
                    if (byteButtonConnected[m] && byteButtonHubChannel[m] == ch) {
                        alreadyAssigned = true;
                        break;
                    }
                }
                if (!alreadyAssigned) {
                    int moduleIdx = -1;
                    for (int m = 0; m < BYTEBUTTON_COUNT; m++) {
                        if (!byteButtonConnected[m] && ch == kByteButtonExpectedChannels[m]) {
                            moduleIdx = m;
                            break;
                        }
                    }
                    if (moduleIdx < 0) {
                        for (int m = 0; m < BYTEBUTTON_COUNT; m++) {
                            if (!byteButtonConnected[m]) {
                                moduleIdx = m;
                                break;
                            }
                        }
                    }

                    if (moduleIdx >= 0) {
                        byteButtonHubChannel[moduleIdx] = ch;
                        byteButtonConnected[moduleIdx] = true;
                        prevByteButtonState[moduleIdx] = 0;
                        byteButtonsFound++;

                        if (ch == kByteButtonExpectedChannels[moduleIdx]) {
                            RED808_LOG_PRINTF("[I2C] M5 ByteButton #%d found on hub ch %d\n", moduleIdx + 1, ch);
                        } else {
                            RED808_LOG_PRINTF("[I2C] WARNING: ByteButton #%d found on ch %d (expected %d), using detected channel\n",
                                              moduleIdx + 1, ch, kByteButtonExpectedChannels[moduleIdx]);
                        }

                        if (i2c_lock(50)) {
                            i2c_hub_select_raw(ch);
                            byteButtonLedInitialized[moduleIdx] = byteButtonApplyLedConfigLocked();
                            i2c_hub_deselect_raw();
                            i2c_unlock();
                        }
                    }
                }
            }
        }

        // Probe DFRobot 4-pot ADC hub converter (ADS1115-compatible, 0x48)
        if (!potHubFound) {
            i2c_hub_select(ch);
            delay(2);
            if (i2c_device_present(DFROBOT_POT_ADC_ADDR) || i2c_device_present(DFROBOT_POT_ADC_ADDR_ALT)) {
                dfRobotPotAddr = i2c_device_present(DFROBOT_POT_ADC_ADDR) ? DFROBOT_POT_ADC_ADDR : DFROBOT_POT_ADC_ADDR_ALT;
                dfRobotPotHubChannel = ch;
                dfRobotPotConnected = true;
                potHubFound = true;
                RED808_LOG_PRINTF("[I2C] DFRobot 4-pot ADC found on hub ch %d (0x%02X)\n", ch, dfRobotPotAddr);
            }
        }

        i2c_hub_deselect();
    }

    int byteButtonFoundCount = byteButtonsFound;

    for (int m = 0; m < BYTEBUTTON_COUNT; m++) {
        if (!byteButtonConnected[m]) {
            RED808_LOG_PRINTF("[I2C] WARNING: ByteButton #%d missing on expected ch%d\n", m + 1, kByteButtonExpectedChannels[m]);
        }
    }
    if (byteButtonFoundCount < BYTEBUTTON_COUNT) {
        RED808_LOG_PRINTLN("[I2C] NOTE: If two ByteButton units are on a passive hub/same bus, both use addr 0x47 and appear as ONE device.");
        RED808_LOG_PRINTLN("[I2C]       Put each ByteButton on a different PCA9548A channel (or different I2C bus) to detect both independently.");
    }

    // Fallback: try 4-pot ADC directly on bus (not behind hub)
    if (!potHubFound) {
        i2c_hub_deselect();
        delay(5);
        if (i2c_device_present(DFROBOT_POT_ADC_ADDR) || i2c_device_present(DFROBOT_POT_ADC_ADDR_ALT)) {
            dfRobotPotAddr = i2c_device_present(DFROBOT_POT_ADC_ADDR) ? DFROBOT_POT_ADC_ADDR : DFROBOT_POT_ADC_ADDR_ALT;
            dfRobotPotHubChannel = -2;
            dfRobotPotConnected = true;
            potHubFound = true;
            RED808_LOG_PRINTF("[I2C] DFRobot 4-pot ADC found DIRECT on bus (0x%02X, no hub)\n", dfRobotPotAddr);
        }
    }

    if (!potHubFound) {
        RED808_LOG_PRINTLN("[I2C] WARNING: DFRobot 4-pot ADC NOT found on any channel or direct bus!");
    }

    diagInfo.m5encoder1Ok = m5encoderConnected[0];
    diagInfo.m5encoder2Ok = m5encoderConnected[1];
    diagInfo.dfrobot1Ok = dfEncoderConnected[0];
    diagInfo.dfrobot2Ok = dfEncoderConnected[1];
    diagInfo.dfrobot3Ok = dfEncoderConnected[2];
    diagInfo.dfrobot4Ok = dfEncoderConnected[3];
    diagInfo.dfrobotPotsOk = dfRobotPotConnected;
    diagInfo.byteButton1Ok = byteButtonConnected[0];
    diagInfo.byteButton2Ok = (BYTEBUTTON_COUNT > 1) ? byteButtonConnected[1] : false;

    RED808_LOG_PRINTF("[I2C] Found: %d M5 modules, %d DFRobot rotaries, ByteButtons: %d, Pot ADC: %s\n",
                  m5Found,
                  dfFound,
                  byteButtonFoundCount,
                  dfRobotPotConnected ? "YES" : "NO");
}

void updateByteButtonLeds() {
    auto ledColorForAction = [&](uint8_t action) -> uint32_t {
        switch ((ByteButtonAction)action) {
            case BB_ACTION_MENU:
                return theme_nav_color(6);
            case BB_ACTION_VOL_MODE:
                return 0x0055CC;
            case BB_ACTION_SCREEN_LIVE:
                return 0x22AAFF;
            case BB_ACTION_SCREEN_SEQUENCER:
                return 0x55CC55;
            case BB_ACTION_SCREEN_FILTERS:
                return 0xCC66FF;
            case BB_ACTION_SCREEN_VOLUMES:
                return 0xFFAA33;
            case BB_ACTION_FX_CLEAN:
                return analogFxMuted[0] ? 0xAA2222 : 0x225522;
            case BB_ACTION_FX_SPACE:
                return analogFxMuted[1] ? 0xAA2222 : 0x336655;
            case BB_ACTION_FX_ACID:
                return analogFxMuted[2] ? 0xAA2222 : 0x557733;
            case BB_ACTION_FX_DESTROY:
                return (analogFxMuted[0] && analogFxMuted[1] && analogFxMuted[2]) ? 0xCC2222 : 0x553333;
            case BB_ACTION_FX_TARGET_PREV:
            case BB_ACTION_FX_TARGET_NEXT:
                return (filterSelectedTrack == -1) ? 0x7711AA : 0xAA11AA;
            case BB_ACTION_PATTERN_PREV:
                return (currentPattern > 0) ? 0xAA6600 : 0x221100;
            case BB_ACTION_PATTERN_NEXT:
                return (currentPattern < Config::MAX_PATTERNS - 1) ? 0xAA6600 : 0x221100;
            case BB_ACTION_PLAY_PAUSE:
                return isPlaying ? 0x00AA44 : 0x224422;
            default:
                return 0x202020;
        }
    };

    for (int moduleIdx = 0; moduleIdx < BYTEBUTTON_COUNT; moduleIdx++) {
        if (!byteButtonConnected[moduleIdx]) continue;
        if (!byteButtonLedInitialized[moduleIdx]) {
            initByteButtonLeds(moduleIdx);
            if (!byteButtonLedInitialized[moduleIdx]) continue;
        }

        uint32_t desired[BYTEBUTTON_LED_COUNT] = {};
        int actionOffset = moduleIdx * BYTEBUTTON_BUTTONS;

        // Button LED colors (mirrored: hw bit N = physical btn 7-N)
        for (int btn = 0; btn < BYTEBUTTON_BUTTONS; btn++) {
            int ledIndex = (BYTEBUTTON_BUTTONS - 1) - btn;
            desired[ledIndex] = ledColorForAction(byteButtonActionMap[actionOffset + btn]);
        }

        // LED 8 (status ring): master connection
        desired[8] = masterConnected ? theme_nav_color(1) : 0x222222;

        bool hasChanges = false;
        for (int i = 0; i < BYTEBUTTON_LED_COUNT; i++) {
            if (byteButtonLedCache[moduleIdx][i] != desired[i]) { hasChanges = true; break; }
        }
        if (!hasChanges) continue;
        if (!i2c_lock(10)) continue;

        if (byteButtonHubChannel[moduleIdx] >= 0) i2c_hub_select_raw(byteButtonHubChannel[moduleIdx]);
        else if (byteButtonHubChannel[moduleIdx] == -2) i2c_hub_deselect_raw();
        for (int i = 0; i < BYTEBUTTON_LED_COUNT; i++) {
            if (byteButtonLedCache[moduleIdx][i] == desired[i]) continue;
            uint32_t color = desired[i];
            if (!byteButtonWriteBytes(BYTEBUTTON_LED_RGB888_REG + i * 4, (uint8_t*)&color, 4)) {
                byteButtonLedInitialized[moduleIdx] = false;
                break;
            }
            byteButtonLedCache[moduleIdx][i] = desired[i];
        }
        if (byteButtonHubChannel[moduleIdx] >= 0) i2c_hub_deselect_raw();
        i2c_unlock();
    }
}

// =============================================================================
// M5 ROTATE8 HANDLING
// =============================================================================

void updateTrackEncoderLED(int track) {
    int moduleIndex = track / ENCODERS_PER_MODULE;
    int encoderIndex = (ENCODERS_PER_MODULE - 1) - (track % ENCODERS_PER_MODULE);  // mirror
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

static void setByteButtonLedDirect(int moduleIdx, int index, uint32_t color) {
    if (moduleIdx < 0 || moduleIdx >= BYTEBUTTON_COUNT) return;
    if (!byteButtonConnected[moduleIdx] || !byteButtonLedInitialized[moduleIdx]) return;
    if (index < 0 || index >= BYTEBUTTON_LED_COUNT) return;
    if (!i2c_lock(10)) return;
    if (byteButtonHubChannel[moduleIdx] >= 0) i2c_hub_select_raw(byteButtonHubChannel[moduleIdx]);
    else if (byteButtonHubChannel[moduleIdx] == -2) i2c_hub_deselect_raw();
    byteButtonWriteBytes(BYTEBUTTON_LED_RGB888_REG + index * 4, (uint8_t*)&color, 4);
    byteButtonLedCache[moduleIdx][index] = color;
    if (byteButtonHubChannel[moduleIdx] >= 0) i2c_hub_deselect_raw();
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

    const int totalByteButtonLeds = BYTEBUTTON_COUNT * BYTEBUTTON_LED_COUNT;
    const int totalLeds = 16 + totalByteButtonLeds;

    // Total LEDs: M5 module 0 (8) + M5 module 1 (8) + ByteButtons (9 each)
    // Phase 0-7:   M5 #0 encoders 0-7 (rainbow sweep)
    // Phase 8-15:  M5 #1 encoders 0-7 (rainbow sweep)
    // Phase 16-..: ByteButton LEDs (ocean blue per module)

    if (bootLedPhase < 8) {
        uint8_t r, g, b;
        hue_to_rgb(bootLedPhase * 32, r, g, b);
        setM5EncoderLedDirect(0, bootLedPhase, r, g, b);
    } else if (bootLedPhase < 16) {
        int enc = bootLedPhase - 8;
        uint8_t r, g, b;
        hue_to_rgb(enc * 32, r, g, b);
        setM5EncoderLedDirect(1, enc, r, g, b);
    } else if (bootLedPhase < totalLeds) {
        int bbLed = bootLedPhase - 16;
        int moduleIdx = bbLed / BYTEBUTTON_LED_COUNT;
        int idx = bbLed % BYTEBUTTON_LED_COUNT;
        setByteButtonLedDirect(moduleIdx, idx, oceanBlues[idx]);
    }

    bootLedPhase++;
    if (bootLedPhase >= totalLeds) {
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
    // ByteButtons — ocean blue palette with high contrast
    for (int moduleIdx = 0; moduleIdx < BYTEBUTTON_COUNT; moduleIdx++) {
        for (int i = 0; i < BYTEBUTTON_LED_COUNT; i++) {
            setByteButtonLedDirect(moduleIdx, i, oceanBlues[i]);
        }
    }
    RED808_LOG_PRINTF("[LED] Ocean blue theme applied (M5 #1=%s ch=%d, M5 #2=%s ch=%d)\n",
                  m5encoderConnected[0] ? "OK" : "NO",
                  m5HubChannel[0],
                  m5encoderConnected[1] ? "OK" : "NO",
                  m5HubChannel[1]);
}

void handleM5Encoders() {
    static unsigned long lastBtnToggle[Config::MAX_TRACKS] = {};
    static constexpr unsigned long BTN_DEBOUNCE_MS = 300;
    static uint8_t btnConfirmCount[Config::MAX_TRACKS] = {};
    static bool btnArmed[Config::MAX_TRACKS] = {};
    static constexpr int DELTA_CLAMP = 16;
    static constexpr uint8_t BTN_CONFIRM_READS = 4;
    static int healthCycle[M5_ENCODER_MODULES] = {};

    struct PendingMsg { uint8_t type; int track; int value; };
    PendingMsg pending[Config::MAX_TRACKS * 2];
    int pendingCount = 0;

    // Deferred LED writes (outside I2C lock to reduce hold time)
    struct LedCmd { int mod; int enc; uint8_t r, g, b; };
    LedCmd ledQueue[Config::MAX_TRACKS];
    int ledCount = 0;

    for (int mod = 0; mod < M5_ENCODER_MODULES; mod++) {
        if (!m5encoderConnected[mod]) continue;
        int ch = m5HubChannel[mod];
        if (ch < 0) continue;

        if (!i2c_lock(12)) continue;
        i2c_hub_select_raw(ch);

        for (int enc = 0; enc < ENCODERS_PER_MODULE; enc++) {
            int track = mod * ENCODERS_PER_MODULE + (ENCODERS_PER_MODULE - 1 - enc);

            // Always read the absolute counter — the V2 change mask is unreliable
            // across firmware versions and I2C hubs.
            {
                int32_t counter = m5encoders[mod].getAbsCounter(enc);
                int32_t delta = counter - prevM5Counter[track];

                if (delta != 0 && abs((int)delta) <= DELTA_CLAMP) {
                    prevM5Counter[track] = counter;
                    delta = -delta;
                    int newVol = constrain(trackVolumes[track] + delta * 5, 0, Config::MAX_VOLUME);
                    if (newVol != trackVolumes[track]) {
                        trackVolumes[track] = newVol;
                        if (!trackMuted[track] && ledCount < Config::MAX_TRACKS) {
                            uint8_t rgb[3]; theme_encoder_color(track, rgb);
                            uint8_t br = (uint8_t)map(newVol, 0, Config::MAX_VOLUME, 10, 255);
                            ledQueue[ledCount++] = {mod, enc, (uint8_t)((rgb[0]*br)/255), (uint8_t)((rgb[1]*br)/255), (uint8_t)((rgb[2]*br)/255)};
                        }
                        pending[pendingCount++] = {0, track, newVol};
                    }
                } else if (delta != 0) {
                    // Resync counter on large delta (reboot / I2C glitch)
                    prevM5Counter[track] = counter;
                }
            }

            // Buttons: individual reads (safer than mask on I2C error)
            bool btnNow = m5encoders[mod].getKeyPressed(enc);
            if (btnNow) {
                if (btnConfirmCount[track] < 255) btnConfirmCount[track]++;
            } else {
                btnConfirmCount[track] = 0;
                btnArmed[track] = true;
            }

            if (btnConfirmCount[track] == BTN_CONFIRM_READS && btnArmed[track]) {
                unsigned long now = millis();
                if ((now - lastBtnToggle[track]) >= BTN_DEBOUNCE_MS) {
                    lastBtnToggle[track] = now;
                    btnArmed[track] = false;
                    trackMuted[track] = !trackMuted[track];
                    if (trackMuted[track]) {
                        ledQueue[ledCount++] = {mod, enc, 0x1E, 0, 0};
                    } else {
                        uint8_t rgb[3]; theme_encoder_color(track, rgb);
                        uint8_t br = (uint8_t)map(trackVolumes[track], 0, Config::MAX_VOLUME, 10, 255);
                        ledQueue[ledCount++] = {mod, enc, (uint8_t)((rgb[0]*br)/255), (uint8_t)((rgb[1]*br)/255), (uint8_t)((rgb[2]*br)/255)};
                    }
                    pending[pendingCount++] = {1, track, (int)trackMuted[track]};
                }
            }
        }

        // Health check every 50 cycles instead of every cycle (saves ~200µs/cycle)
        if (++healthCycle[mod] >= 50) {
            healthCycle[mod] = 0;
            uint8_t ver = m5encoders[mod].getVersion();
            if (ver == 0 || ver != m5FirmwareVersion[mod]) {
                int base = mod * ENCODERS_PER_MODULE;
                for (int e = 0; e < ENCODERS_PER_MODULE; e++) {
                    btnConfirmCount[base + e] = 0;
                }
            }
        }

        // Write queued LEDs for this module while hub is still selected
        for (int l = 0; l < ledCount; l++) {
            if (ledQueue[l].mod == mod) {
                m5encoders[mod].writeRGB(ledQueue[l].enc, ledQueue[l].r, ledQueue[l].g, ledQueue[l].b);
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

    // Forward track changes to P4
    for (int i = 0; i < pendingCount; i++) {
        if (pending[i].type == 0) {
            uart_bridge_send_track_volume(pending[i].track, pending[i].value);
        } else {
            uart_bridge_send_track_mute(pending[i].track, (bool)pending[i].value);
        }
    }
}

// =============================================================================
// DFROBOT ROTARY HANDLING
// =============================================================================

void handleDFRobotEncoders() {
    static unsigned long lastBtnMs[DFROBOT_ENCODER_COUNT] = {};
    static int laneStoredValue[3] = {0, 0, 0};

    auto syncMasterEnabled = [&]() {
        masterFilter.enabled = (masterFilter.delayAmount > 0 ||
                                masterFilter.flangerAmount > 0 ||
                                masterFilter.compAmount > 0);
    };

    auto sendFxLane = [&](int lane, int value, bool muted) {
        int effective = muted ? 0 : constrain(value, 0, 127);
        float mix = (float)effective / 127.0f;
        bool activating = (effective > 0);

        if (lane == 0) {
            // Encoder 0 → Flanger
            { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerActive"; d["value"] = activating ? 1 : 0; sendUDPCommand(d); }
            if (activating) {
                { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerRate";     d["value"] = 0.3f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerDepth";    d["value"] = 0.7f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerFeedback"; d["value"] = 0.5f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setFlangerMix";      d["value"] = mix;  sendUDPCommand(d); }
            }
            masterFilter.delayAmount = (uint8_t)effective;
        } else if (lane == 1) {
            // Encoder 1 → Reverb
            { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbActive"; d["value"] = activating ? 1 : 0; sendUDPCommand(d); }
            if (activating) {
                { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbFeedback"; d["value"] = 0.6f;  sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbLpFreq";   d["value"] = 5000;  sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setReverbMix";      d["value"] = mix;   sendUDPCommand(d); }
            }
            masterFilter.flangerAmount = (uint8_t)effective;
        } else {
            // Encoder 2 → Phaser
            { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserActive"; d["value"] = activating ? 1 : 0; sendUDPCommand(d); }
            if (activating) {
                { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserRate";     d["value"] = 0.8f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserDepth";    d["value"] = 0.6f; sendUDPCommand(d); }
                { JsonDocument d(&sramAllocator); d["cmd"] = "setPhaserFeedback"; d["value"] = 0.5f; sendUDPCommand(d); }
            }
            masterFilter.compAmount = (uint8_t)effective;
        }

        syncMasterEnabled();
        dfFxParamMode[lane] = 0;
        dfFxParamValue[lane] = laneStoredValue[lane];
    };

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
            delta = filterDFDeltaJitter(i, delta);
        }
        prevDFValue[i] = val;

        bool buttonPressed = dfEncoders[i]->detectButtonDown();
        if (buttonPressed) {
            RED808_LOG_PRINTF("[DFRobot] Encoder #%d button PRESSED\n", i);
        }

        i2c_hub_deselect_raw();
        i2c_unlock();

        // Process results outside of I2C lock
        if (i < 3) {
            // DFRobot #0/#1/#2: FX lanes (Flanger/Reverb/Phaser). Button toggles mute.
            static const char* laneNames[] = {"Flanger", "Reverb", "Phaser"};
            int lane = i;
            int logical_delta = quantizeDFDelta(i, delta);
            if (logical_delta != 0) {
                int accel = (abs(logical_delta) >= fxp().accel_threshold) ? fxp().accel_mult : 1;
                int newVal = constrain(laneStoredValue[lane] + logical_delta * fxp().df_step * accel, 0, 127);
                if (newVal != laneStoredValue[lane]) {
                    laneStoredValue[lane] = newVal;
                    if (!dfFxMuted[lane]) sendFxLane(lane, laneStoredValue[lane], false);
                    dfFxParamValue[lane] = laneStoredValue[lane];
                    uart_bridge_send_encoder(lane, (uint8_t)laneStoredValue[lane]);
                }
            }
            if (buttonPressed && (millis() - lastBtnMs[i]) > Config::DF_BUTTON_GUARD_MS) {
                lastBtnMs[i] = millis();
                dfFxMuted[lane] = !dfFxMuted[lane];
                sendFxLane(lane, laneStoredValue[lane], dfFxMuted[lane]);
                uart_bridge_send_encoder_mute(lane, dfFxMuted[lane]);
                RED808_LOG_PRINTF("[DFRobot] %s lane %s\n", laneNames[lane], dfFxMuted[lane] ? "MUTED" : "UNMUTED");
            }
        }
        else if (i == 3) {
            // DFRobot #3: BPM control (coarse). Button resets to default BPM.
            // Unit Fader handles fine 0.1 tuning; rotary uses 1 BPM steps + acceleration.
            int logical_delta = quantizeDFDelta(i, delta);
            if (logical_delta != 0) {
                // Each logical step = 1 BPM (gain=10, COUNTS_PER_STEP=2 → ~5 BPM/click).
                float newBpm = currentBPMPrecise + (float)logical_delta;
                applyBPMPrecise(newBpm, true);
            }
            if (buttonPressed && (millis() - lastBtnMs[i]) > Config::DF_BUTTON_GUARD_MS) {
                lastBtnMs[i] = millis();
                applyBPMPrecise((float)Config::DEFAULT_BPM, true);
                RED808_LOG_PRINTF("[DFRobot] BPM reset to %.1f\n", currentBPMPrecise);
            }
        }
    }
}

// =============================================================================
// M5 UNIT FADER (GPIO6) - analog control
// =============================================================================

void handleUnitFader() {
    static unsigned long lastReadMs = 0;
    static int filtered = -1;
    static int lastRaw = -1;
    static int accum = 0;

    unsigned long now = millis();
    if ((now - lastReadMs) < Config::UNIT_FADER_READ_MS) return;
    lastReadMs = now;

    long sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(Config::UNIT_FADER_PIN);
    int raw = (int)(sum / 8);

    if (filtered < 0) filtered = raw;
    filtered = (filtered * 3 + raw) / 4;

    if (lastRaw < 0) {
        lastRaw = filtered;
        return;
    }

    int delta = filtered - lastRaw;
    lastRaw = filtered;
    if (abs(delta) < Config::UNIT_FADER_DEADBAND) return;

    accum += delta;
    const int countsPerTenth = Config::UNIT_FADER_COUNTS_PER_TENTH;
    int steps = 0;
    // Apply at most one 0.1 step per cycle for predictable fine tuning.
    if (accum >= countsPerTenth) {
        steps = 1;
        accum -= countsPerTenth;
    } else if (accum <= -countsPerTenth) {
        steps = -1;
        accum += countsPerTenth;
    }
    if (steps == 0) return;

    float bpmFine = currentBPMPrecise + (float)steps * 0.1f;
    applyBPMPrecise(bpmFine, true);
}

// =============================================================================
// BYTEBUTTON HANDLING
// =============================================================================

static bool readByteButtonStatus(int hubChannel, uint8_t& status) {
    status = 0;
    bool readOk = false;

    if (i2c_lock(20)) {
        if (hubChannel >= 0) i2c_hub_select_raw(hubChannel);
        else if (hubChannel == -2) i2c_hub_deselect_raw();

        // Preferred path: register 0x60 returns one byte per button (0x01 pressed, 0x00 released).
        uint8_t statusBytes[BYTEBUTTON_BUTTONS] = {};
        Wire.beginTransmission(BYTEBUTTON_ADDR);
        Wire.write(BYTEBUTTON_STATUS_8BYTE_REG);
        if (Wire.endTransmission(false) == 0 &&
            Wire.requestFrom((uint8_t)BYTEBUTTON_ADDR, (uint8_t)BYTEBUTTON_BUTTONS) == BYTEBUTTON_BUTTONS) {
            for (int i = 0; i < BYTEBUTTON_BUTTONS; i++) {
                statusBytes[i] = Wire.read();
                if (statusBytes[i]) status |= (uint8_t)(1U << i);
            }
            readOk = true;
        } else {
            // Fallback for older/alternate firmware exposing only bitmask register.
            Wire.beginTransmission(BYTEBUTTON_ADDR);
            Wire.write(BYTEBUTTON_STATUS_REG);
            if (Wire.endTransmission(false) == 0 &&
                Wire.requestFrom((uint8_t)BYTEBUTTON_ADDR, (uint8_t)1) == 1) {
                status = Wire.read();
                readOk = true;
            }
        }

        if (hubChannel >= 0) i2c_hub_deselect_raw();
        i2c_unlock();
    }
    return readOk;
}

static void runByteButtonAction(uint8_t action, int moduleIdx) {
    switch ((ByteButtonAction)action) {
        case BB_ACTION_MENU:
            navigateToScreen(SCREEN_MENU);
            break;
        case BB_ACTION_VOL_MODE:
            applyUnifiedMasterVolume(masterVolume, true);
            RED808_LOG_PRINTF("[BB%d] Master volume resync: %d\n", moduleIdx + 1, masterVolume);
            break;
        case BB_ACTION_SCREEN_LIVE:
            navigateToScreen(SCREEN_LIVE);
            break;
        case BB_ACTION_SCREEN_SEQUENCER:
            navigateToScreen(SCREEN_SEQUENCER);
            break;
        case BB_ACTION_SCREEN_FILTERS:
            navigateToScreen(SCREEN_FILTERS);
            break;
        case BB_ACTION_SCREEN_VOLUMES:
            navigateToScreen(SCREEN_VOLUMES);
            break;
        case BB_ACTION_FX_CLEAN:
            analogFxMuted[0] = !analogFxMuted[0];
            sendAnalogFxParamNow(0);
            RED808_LOG_PRINTF("[BB%d] P2 CUTOFF mute: %s\n", moduleIdx + 1, analogFxMuted[0] ? "ON" : "OFF");
            break;
        case BB_ACTION_FX_SPACE:
            analogFxMuted[1] = !analogFxMuted[1];
            sendAnalogFxParamNow(1);
            RED808_LOG_PRINTF("[BB%d] P3 RESONANCE mute: %s\n", moduleIdx + 1, analogFxMuted[1] ? "ON" : "OFF");
            break;
        case BB_ACTION_FX_ACID:
            analogFxMuted[2] = !analogFxMuted[2];
            sendAnalogFxParamNow(2);
            RED808_LOG_PRINTF("[BB%d] P4 DRIVE mute: %s\n", moduleIdx + 1, analogFxMuted[2] ? "ON" : "OFF");
            break;
        case BB_ACTION_FX_DESTROY:
            {
                bool allMuted = analogFxMuted[0] && analogFxMuted[1] && analogFxMuted[2];
                bool newState = !allMuted;
                analogFxMuted[0] = newState;
                analogFxMuted[1] = newState;
                analogFxMuted[2] = newState;
                sendAnalogFxParamNow(0);
                sendAnalogFxParamNow(1);
                sendAnalogFxParamNow(2);
                RED808_LOG_PRINTF("[BB%d] Analog FX mute ALL: %s\n", moduleIdx + 1, newState ? "ON" : "OFF");
            }
            break;
        case BB_ACTION_FX_TARGET_PREV:
            if (filterSelectedTrack == -1) filterSelectedTrack = Config::MAX_TRACKS - 1;
            else filterSelectedTrack--;
            RED808_LOG_PRINTF("[BB%d] FX target: %s\n", moduleIdx + 1, filterSelectedTrack == -1 ? "MASTER" : trackNames[filterSelectedTrack]);
            break;
        case BB_ACTION_FX_TARGET_NEXT:
            if (filterSelectedTrack >= Config::MAX_TRACKS - 1) filterSelectedTrack = -1;
            else filterSelectedTrack++;
            RED808_LOG_PRINTF("[BB%d] FX target: %s\n", moduleIdx + 1, filterSelectedTrack == -1 ? "MASTER" : trackNames[filterSelectedTrack]);
            break;
        case BB_ACTION_PATTERN_PREV:
            if (currentPattern > 0) selectPatternOnMaster(currentPattern - 1);
            break;
        case BB_ACTION_PATTERN_NEXT:
            if (currentPattern < Config::MAX_PATTERNS - 1) selectPatternOnMaster(currentPattern + 1);
            break;
        case BB_ACTION_PLAY_PAUSE:
            sendPlayStateCommand(!isPlaying);
            break;
        default:
            break;
    }
}

static void handleByteButtonPrimaryEdges(int moduleIdx, uint8_t pressedEdges) {
    // Only clear this module's buttons — preserve other module's state
    int actionOffset = moduleIdx * BYTEBUTTON_BUTTONS;
    for (int b = 0; b < BYTEBUTTON_BUTTONS; b++) byteButtonLivePressed[actionOffset + b] = false;
    for (int button = 0; button < BYTEBUTTON_BUTTONS; button++) {
        if ((pressedEdges & (1U << button)) == 0) continue;
        int btn = (BYTEBUTTON_BUTTONS - 1) - button;
        int globalBtn = actionOffset + btn;
        if (globalBtn >= 0 && globalBtn < BYTEBUTTON_TOTAL_BUTTONS) {
            byteButtonLivePressed[globalBtn] = true;
            runByteButtonAction(byteButtonActionMap[globalBtn], moduleIdx);
        }
    }
    updateByteButtonLeds();
}

void handleByteButton() {
    for (int moduleIdx = 0; moduleIdx < BYTEBUTTON_COUNT; moduleIdx++) {
        if (!byteButtonConnected[moduleIdx]) continue;
        uint8_t status = 0;
        if (readByteButtonStatus(byteButtonHubChannel[moduleIdx], status)) {
            uint8_t pressedEdges = status & (uint8_t)~prevByteButtonState[moduleIdx];
            prevByteButtonState[moduleIdx] = status;
            if (pressedEdges) handleByteButtonPrimaryEdges(moduleIdx, pressedEdges);
        }
    }
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

    // Apply pending theme change from analog encoder — INSIDE LVGL lock
    int pt = pendingThemeIdx;
    if (pt >= 0 && pt < (int)THEME_COUNT) {
        pendingThemeIdx = -1;
        ui_theme_apply((VisualTheme)pt);
        uart_bridge_send_theme(pt);
    }

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

    // 2. IO Extension (CH32V003) - resets, backlight stays OFF until first frame
    io_ext_init();
    delay(10);
    RED808_LOG_PRINTLN("[IO] CH32V003 initialized, backlight OFF (deferred)");

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

    // 6. M5 Unit Fader GPIO setup
    pinMode(Config::UNIT_FADER_PIN, INPUT);
    analogReadResolution(12);  // 12-bit ADC (0-4095)
    RED808_LOG_PRINTLN("[FADER] Unit Fader on GPIO6 initialized");

    // 7. Scan I2C hub for M5 + DFRobot + ByteButton (before LVGL task starts - no I2C race)
    scanI2CHub();

    // 8. WiFi + UDP — only when S3 connects to Master directly
#if S3_WIFI_ENABLED
    RED808_LOG_PRINTF("[HEAP] Before WiFi: %d bytes\n", ESP.getFreeHeap());
    setupWiFi();
    RED808_LOG_PRINTF("[HEAP] After WiFi: %d bytes\n", ESP.getFreeHeap());
#else
    RED808_LOG_PRINTLN("[WiFi] DISABLED — S3 is UART-only slave to P4");
    wifiConnected = false;
    udpConnected = false;
    masterConnected = false;
#endif

    // 8b. UART bridge to ESP32-P4 Visual Beast
    uart_bridge_init();

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

        // Force first frame render so framebuffers contain the boot screen
        lv_timer_handler();
        lv_timer_handler();  // two passes: layout + flush

        lvgl_port_unlock();
        RED808_LOG_PRINTLN("[UI] All screens created");
    }

    // Backlight ON only after boot screen is rendered — eliminates PSRAM garbage flicker
    io_ext_backlight_on();
    RED808_LOG_PRINTLN("[BL] Backlight ON (boot screen visible)");
    RED808_LOG_PRINTF("[HEAP] After UI: %d bytes  PSRAM: %d bytes\n", ESP.getFreeHeap(), ESP.getFreePsram());

    // 11. Encoder task — pri 1 = same as loop → round-robin time-slicing
    //     loop() no longer starved; gets CPU every 1ms tick for touch triggers
    xTaskCreatePinnedToCore(encoder_task, "enc", 6144, NULL, 1, NULL, 0);
    RED808_LOG_PRINTLN("[ENC] Encoder task started (Core 0, pri 1)");

    // 12. Touch task — highest I2C priority, preempts encoder via priority inheritance
    xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 3, NULL, 0);
    RED808_LOG_PRINTLN("[TOUCH] Touch task started (Core 0, pri 3)");

    // 13. Pad trigger task — prio 2, preempts loop/enc but yields to touch
    xTaskCreatePinnedToCore(pad_trigger_task, "pads", 4096, NULL, 2, NULL, 0);
    RED808_LOG_PRINTLN("[PADS] Pad trigger task started (Core 0, pri 2)");

    // 14. Microtiming engine — seed PRNG from hardware RNG
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
    while (true) {
        // Poll unconditionally — GT911 status register handles "no new data"
        // internally (returns immediately when !bufferReady).
        // INT pin polling was unreliable (pulse mode, missed >99% of events).
        gt911_poll();
        vTaskDelay(pdMS_TO_TICKS(2));  // ~500Hz poll, GT911 updates at ~100Hz
    }
}

// =============================================================================
// PAD TRIGGER TASK (Core 0, priority 2 — preempts loop/enc, yields to touch)
// Reads GT911 cache (lock-free), does hit-test, sends UDP pad triggers.
// Decoupled from loop() so updateUI/WiFi never delays pad response.
// =============================================================================

static void pad_trigger_task(void* arg) {
    (void)arg;
    while (true) {
        unsigned long now = millis();

        handleLivePadTouchMatrix(now);

        const int padVelocity = map(constrain(livePadsVolume, 0, Config::MAX_VOLUME), 0, Config::MAX_VOLUME, 32, 127);

        uint32_t mask = pendingLivePadTriggerMask;
        if (mask != 0) {
            pendingLivePadTriggerMask = 0;
            for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
                if ((mask & (1UL << pad)) == 0) continue;
                sendLivePadTrigger(pad, padVelocity);
                uart_bridge_send_pad_trigger(pad, (uint8_t)padVelocity);
                lastLivePadTriggerMs[pad] = now;
                livePadFlashUntilMs[pad] = now + LIVE_PAD_FLASH_MS;
            }
            if (currentScreen == SCREEN_LIVE) {
                livePadsVisualDirty = true;
            }
        }

        // Repeat triggers (hold-to-repeat)
        bool repeated = false;
        for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
            if (livePadRemainingRepeats[pad] == 0) continue;
            if (now < livePadNextRepeatMs[pad]) continue;
            sendLivePadTrigger(pad, padVelocity);
            lastLivePadTriggerMs[pad] = now;
            livePadFlashUntilMs[pad] = now + LIVE_PAD_FLASH_MS;
            livePadRemainingRepeats[pad]--;
            livePadNextRepeatMs[pad] = now + LIVE_PAD_REPEAT_INTERVAL_MS;
            repeated = true;
        }
        if (repeated && currentScreen == SCREEN_LIVE) {
            livePadsVisualDirty = true;
        }

        vTaskDelay(pdMS_TO_TICKS(1));  // ~1kHz — matches GT911 500Hz update rate
    }
}

// =============================================================================
// ENCODER TASK (Core 0, priority 1 — same as loop, round-robin)
// taskYIELD() between modules gives loop() immediate CPU for touch triggers.
// =============================================================================

static void encoder_task(void* arg) {
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        handleM5Encoders();
        taskYIELD();  // let loop() process touch triggers between I2C batches
        handleDFRobotEncoders();
        taskYIELD();
        handleDFRobotPots();
        taskYIELD();
        handleUnitFader();
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

    // === Pad triggers now handled by pad_trigger_task (prio 2) ===

    // === NETWORK + STATUS ===
#if S3_WIFI_ENABLED
    if (masterConnected && (now - lastMasterPacketMs > 3000)) {
        masterConnected = false;
        uart_bridge_send_wifi_state(wifiConnected, false);
    }

    checkWiFiReconnect();
#endif

    // === UART BRIDGE (P4 receive + heartbeat) ===
    uart_bridge_receive();
    {
        static unsigned long lastHB = 0;
        if (now - lastHB >= 500) {
            lastHB = now;
            uart_bridge_heartbeat();
        }
    }

#if S3_WIFI_ENABLED
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
#endif

    // Boot LED animation
    if (currentScreen == SCREEN_BOOT && !bootLedDone) {
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
    // Uses microsecond period so decimal BPM (e.g. 120.5 vs 120.7) has real timing effect.
    // SNAP-TO-NOW: never catch up — max 1 step per loop iteration = smooth visual always.
    if (isPlaying && currentBPMPrecise > 0.0f) {
        uint32_t nowUs = micros();
        uint32_t step_us = (uint32_t)(60000000.0f / currentBPMPrecise / 4.0f);
        if (step_us < 50000U) step_us = 50000U;
        if (lastLocalStepUs == 0) lastLocalStepUs = nowUs;
        if ((uint32_t)(nowUs - lastLocalStepUs) >= step_us) {
            lastLocalStepUs = nowUs;  // snap to now — NEVER accumulate, NEVER catch up
            lastLocalStepMs = now;
            currentStep = (currentStep + 1) % Config::MAX_STEPS;
            uart_bridge_send_step(currentStep);

            // ── Sequencer triggers: send active pads on this step to Master ──
            if (udpConnected) {
                const int seqVel = map(constrain(sequencerVolume, 0, Config::MAX_VOLUME),
                                       0, Config::MAX_VOLUME, 32, 127);
                for (int t = 0; t < Config::MAX_TRACKS; t++) {
                    if (patterns[currentPattern].steps[t][currentStep] &&
                        !patterns[currentPattern].muted[t]) {
                        sendLivePadTrigger(t, seqVel);
                    }
                }
            }
        }
    } else if (!isPlaying) {
        lastLocalStepMs = now;
        lastLocalStepUs = micros();
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
        RED808_LOG_PRINTF("  DFRobot #3:  %s (ch=%d)\n", dfEncoderConnected[2] ? "OK" : "NO", dfRobotHubChannel[2]);
        RED808_LOG_PRINTF("  DFRobot #4:  %s (ch=%d)\n", dfEncoderConnected[3] ? "OK" : "NO", dfRobotHubChannel[3]);
        RED808_LOG_PRINTF("  DFRobot 4P:  %s (ch=%d, addr=0x%02X)\n", dfRobotPotConnected ? "OK" : "NO", dfRobotPotHubChannel, dfRobotPotAddr);
        for (int m = 0; m < BYTEBUTTON_COUNT; m++) {
            RED808_LOG_PRINTF("  ByteButton #%d: %s (ch=%d)\n", m + 1,
                              byteButtonConnected[m] ? "OK" : "NO", byteButtonHubChannel[m]);
        }
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
