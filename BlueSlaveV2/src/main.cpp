// =============================================================================
// main.cpp - BlueSlaveV2
// RED808 V6 Surface Controller
// Waveshare ESP32-S3-Touch-LCD-7B + M5 8ENCODER + DFRobot SEN0502
// =============================================================================

#include <Arduino.h>
#include "../include/config.h"
#include <ArduinoJson.h>
#include <Wire.h>
#include <math.h>
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include <M5ROTATE8.h>
#include <DFRobot_VisualRotaryEncoder.h>
#include <SD_MMC.h>
#include "lvgl.h"
#include <atomic>
#include <freertos/portmacro.h>

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
#include "sd_midi_loader.h"

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
volatile uint32_t patternUiRevision = 1;
volatile int currentStep = 0;
int selectedTrack = 0;
volatile bool isPlaying = false;
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
std::atomic<uint32_t> pendingLivePadTriggerMask{0};
std::atomic<bool> livePadsVisualDirty{false};

// Filters
TrackFilter trackFilters[Config::MAX_TRACKS];
TrackFilter masterFilter = {false, 0, 0, 0};
int filterSelectedTrack = -1;  // -1 = master
int filterSelectedFX = FILTER_CHORUS;
int fxFilterType = 0;
int fxFilterCutoffHz = 20000;    // max (fully open) — pot at zero = no filtering
int fxFilterResonanceX10 = 10;   // 1.0 Q — min
int fxBitCrushBits = 16;
int fxDistortionPercent = 0;
int fxSampleRateHz = 44100;
char masterKitName[32] = "";
bool masterSampleLoaded[Config::MAX_SAMPLES] = {};
char masterSampleName[Config::MAX_SAMPLES][32] = {};
EncoderMode encoderMode = ENC_MODE_VOLUME;

// I2C Hub
int m5HubChannel[M5_ENCODER_MODULES] = {-1, -1};
int dfRobotHubChannel[DFROBOT_ENCODER_COUNT] = {-1, -1, -1, -1};
int byteButtonHubChannel[BYTEBUTTON_COUNT] = {-1, -1};
uint8_t i2cHubKnownMask[8] = {};
int dfRobotPotHubChannel = -1;
bool hubDetected = false;

// Connection — S3 is UART-only slave; no WiFi/UDP
bool masterConnected = false;
unsigned long lastMasterPacketMs = 0;  // updated by uart_bridge on P4 heartbeat

// Diagnostic
DiagnosticInfo diagInfo = {};
volatile uint32_t uiUpdateCount = 0;
volatile uint32_t uiSkippedCount = 0;
volatile uint32_t uiLastIntervalMs = Config::SCREEN_UPDATE_MS;
volatile uint32_t udpRxCount = 0;
volatile uint32_t udpJsonErrorCount = 0;

// Track names
const char* trackNames[] = {
    "BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL",
    "MA", "CY", "HT", "LT", "MC", "MT", "HC", "LC"
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
static constexpr unsigned long TOUCH_RELEASE_DEBOUNCE_MS = 8;   // avoid false releases on fast GT911 cache gaps
static constexpr unsigned long LIVE_PAD_REPEAT_INTERVAL_MS = 75;
static constexpr unsigned long LIVE_PAD_FLASH_MS = 120;
static unsigned long livePadReleaseMs[Config::MAX_SAMPLES] = {}; // when each pad last went untouched

// Timing
unsigned long lastEncoderRead = 0;
unsigned long lastScreenUpdate = 0;
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
static portMUX_TYPE s_live_pad_state_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t prevByteButtonState[BYTEBUTTON_COUNT] = {0, 0};
bool byteButtonLivePressed[BYTEBUTTON_TOTAL_BUTTONS] = {};
uint32_t byteButtonLedCache[BYTEBUTTON_COUNT][BYTEBUTTON_BUTTONS + 1] = {};
bool byteButtonLedInitialized[BYTEBUTTON_COUNT] = {false, false};
volatile FxResponseMode fxResponseMode = FX_MODE_LIVE;
volatile bool g_i2c_scan_in_progress = false;
const char* const byteButtonActionNames[] = {
    "Menu",              // BB_ACTION_MENU
    "Vol Resync",        // BB_ACTION_VOL_MODE
    "Go Live",           // BB_ACTION_SCREEN_LIVE
    "Go Sequencer",      // BB_ACTION_SCREEN_SEQUENCER
    "Go BUTTONS",        // BB_ACTION_SCREEN_PERFORMANCE
    "Go Volumes",        // BB_ACTION_SCREEN_VOLUMES
    "FX Mute CHORUS",    // BB_ACTION_FX_CLEAN
    "FX Mute DELAY",     // BB_ACTION_FX_SPACE
    "FX Mute REVERB",    // BB_ACTION_FX_ACID
    "FX Mute ALL",       // BB_ACTION_FX_DESTROY
    "FX Target --",      // BB_ACTION_FX_TARGET_PREV
    "FX Target ++",      // BB_ACTION_FX_TARGET_NEXT
    "Pattern --",        // BB_ACTION_PATTERN_PREV
    "Pattern ++",        // BB_ACTION_PATTERN_NEXT
    "Play/Pause",        // BB_ACTION_PLAY_PAUSE
    "Go Patterns",       // BB_ACTION_SCREEN_PATTERNS
    "Go Settings",       // BB_ACTION_SCREEN_SETTINGS
    "Go SD Card",        // BB_ACTION_SCREEN_SDCARD
    "BPM +1",            // BB_ACTION_BPM_UP
    "BPM -1",            // BB_ACTION_BPM_DOWN
};

void resetLivePadRuntimeState() {
    taskENTER_CRITICAL(&s_live_pad_state_mux);
    memset(livePadPressed, 0, sizeof(bool) * Config::MAX_SAMPLES);
    memset(livePadReleaseMs, 0, sizeof(livePadReleaseMs));
    memset(livePadFlashUntilMs, 0, sizeof(unsigned long) * Config::MAX_SAMPLES);
    memset(livePadHoldStartMs, 0, sizeof(unsigned long) * Config::MAX_SAMPLES);
    memset(livePadRemainingRepeats, 0, sizeof(livePadRemainingRepeats));
    memset(livePadNextRepeatMs, 0, sizeof(livePadNextRepeatMs));
    taskEXIT_CRITICAL(&s_live_pad_state_mux);
    pendingLivePadTriggerMask.store(0, std::memory_order_release);
}
// ─── BB MODULE 1 (I2C Hub CH4): Transport + FX mute controls ───────────────
// ─── BB MODULE 2 (I2C Hub CH5): Navigation + BPM control ────────────────────
uint8_t byteButtonActionMap[BYTEBUTTON_TOTAL_BUTTONS] = {
    // BB1 buttons 1-8 (transport + FX)
    BB_ACTION_MENU,
    BB_ACTION_PLAY_PAUSE,
    BB_ACTION_PATTERN_PREV,
    BB_ACTION_PATTERN_NEXT,
    BB_ACTION_FX_CLEAN,       // 1-5: FX Mute FLANGER
    BB_ACTION_FX_SPACE,       // 1-6: FX Mute DELAY
    BB_ACTION_FX_ACID,        // 1-7: FX Mute REVERB
    BB_ACTION_FX_DESTROY,     // 1-8: FX Mute ALL
    // BB2 buttons 1-8 (navigation + BPM)
    BB_ACTION_SCREEN_LIVE,
    BB_ACTION_SCREEN_SEQUENCER,
    BB_ACTION_SCREEN_VOLUMES,
    BB_ACTION_SCREEN_PATTERNS,
    BB_ACTION_SCREEN_SDCARD,
    BB_ACTION_BPM_DOWN,
    BB_ACTION_BPM_UP,
    BB_ACTION_MENU,            // 2-8: Go MENU PRINCIPAL
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
            return 200;    // static screen, no animation
        case SCREEN_LIVE:
            return 16;     // fast pad flash response
        case SCREEN_SEQUENCER:
        case SCREEN_SEQ_CIRCLE:
            return playing_now ? 16 : 80;
        case SCREEN_VOLUMES:
            return 50;     // event-driven sliders, no need to dirty LVGL at 30Hz
        case SCREEN_FILTERS:
            return 80;
        case SCREEN_PATTERNS:
            return 150;
        case SCREEN_PIANO:
            return 50;     // key events are callbacks; periodic refresh is light
        case SCREEN_DIAGNOSTICS:
        case SCREEN_PERFORMANCE:
            return 300;    // near-static text
        case SCREEN_SETTINGS:
        case SCREEN_SDCARD:
        case SCREEN_SAMPLES:
            return 300;
        default:
            return 150;
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
static void requestMasterSync(bool requestState);
void sendUDPCommand(const char* cmd);
void sendUDPCommand(JsonDocument& doc);
static void applyUnifiedMasterVolume(int value, bool sendToMaster);
static void set_master_drive_percent(int percent, bool sendToMaster);
void nvs_mark_dirty();
void applyBPMPrecise(float bpm, bool sendToMaster);
void requestPatternFromMaster();
void sendPlayStateCommand(bool shouldPlay);
void selectPatternOnMaster(int patternIndex);
void sendFullPatternToMaster(int pat);
void sendLivePadTrigger(int pad, int velocity);
void sendFilterUDP(int track, int fxType);
int get_master_drive_percent();
void apply_mpc_preset(bool saveToNvs);
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
// P4 REMOTE SD BROWSE (S3 mounts/reads SD, sends entries to P4 via UART)
// =============================================================================
extern bool sd_mounted;                // defined in ui_screens.cpp
extern bool sd_try_mount();            // defined in ui_screens.cpp

static char p4sd_dir[128] = "/";
static char p4sd_selected[64] = "";
static char p4sd_family[32] = "";
struct P4SdDirEntry { char name[48]; bool is_dir; bool is_midi; };
static P4SdDirEntry p4sd_entries[64];
static int p4sd_count = 0;

static void p4sd_send_listing() {
    // Status
    uint8_t status = sd_mounted ? 1 : 0;
    uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_STATUS, &status, 1);
    // Path
    uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_PATH,
                              (const uint8_t*)p4sd_dir, strlen(p4sd_dir));
    if (!sd_mounted) {
        uint8_t zero = 0;
        uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_LIST_END, &zero, 1);
        return;
    }
    // Read directory
    p4sd_count = 0;
    File dir = SD_MMC.open(p4sd_dir);
    if (!dir || !dir.isDirectory()) {
        uint8_t zero = 0;
        uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_LIST_END, &zero, 1);
        return;
    }
    File entry = dir.openNextFile();
    while (entry && p4sd_count < 64) {
        const char* name = entry.name();
        bool isDir = entry.isDirectory();
        if (name[0] == '.') { entry.close(); entry = dir.openNextFile(); continue; }
        if (!isDir) {
            size_t nlen = strlen(name);
            bool isWav  = (nlen > 4 && strcasecmp(name + nlen - 4, ".wav") == 0);
            bool isMidi = (nlen > 4 && strcasecmp(name + nlen - 4, ".mid") == 0);
            if (!isWav && !isMidi) {
                entry.close(); entry = dir.openNextFile(); continue;
            }
        }
        strncpy(p4sd_entries[p4sd_count].name, name, 47);
        p4sd_entries[p4sd_count].name[47] = '\0';
        p4sd_entries[p4sd_count].is_dir = isDir;
        p4sd_entries[p4sd_count].is_midi = !isDir && [&](){
            size_t nlen = strlen(name);
            return nlen > 4 && strcasecmp(name + nlen - 4, ".mid") == 0;
        }();
        // Send entry: [index, type, name...]
        uint8_t buf[50];
        buf[0] = (uint8_t)p4sd_count;
        buf[1] = isDir ? 'D' : (p4sd_entries[p4sd_count].is_midi ? 'M' : 'F');
        int nameLen = strlen(name);
        if (nameLen > 47) nameLen = 47;
        memcpy(&buf[2], name, nameLen);
        uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_ENTRY, buf, 2 + nameLen);
        p4sd_count++;
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    uint8_t total = (uint8_t)p4sd_count;
    uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_LIST_END, &total, 1);
}

static void handleP4SdMount() {
    sd_try_mount();
    strcpy(p4sd_dir, "/");
    p4sd_selected[0] = '\0';
    p4sd_family[0] = '\0';
    p4sd_send_listing();
}

static void handleP4SdSelect(uint8_t idx) {
    if (idx >= p4sd_count) return;
    if (p4sd_entries[idx].is_dir) {
        if (strcmp(p4sd_dir, "/") == 0)
            snprintf(p4sd_dir, sizeof(p4sd_dir), "/%s", p4sd_entries[idx].name);
        else {
            char tmp[128];
            snprintf(tmp, sizeof(tmp), "%s/%s", p4sd_dir, p4sd_entries[idx].name);
            strncpy(p4sd_dir, tmp, sizeof(p4sd_dir) - 1);
        }
        strncpy(p4sd_family, p4sd_entries[idx].name, sizeof(p4sd_family) - 1);
        p4sd_selected[0] = '\0';
        p4sd_send_listing();
    } else {
        strncpy(p4sd_selected, p4sd_entries[idx].name, sizeof(p4sd_selected) - 1);
        uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_SELECTED,
                                  (const uint8_t*)p4sd_selected, strlen(p4sd_selected));
    }
}

static void handleP4SdBack() {
    char* last = strrchr(p4sd_dir, '/');
    if (last && last != p4sd_dir) *last = '\0';
    else strcpy(p4sd_dir, "/");
    p4sd_selected[0] = '\0';
    p4sd_send_listing();
}

static void handleP4SdLoad(uint8_t pad) {
    if (pad >= Config::MAX_TRACKS || p4sd_selected[0] == '\0') return;
    ui_sdcard_send_load_sample(pad, p4sd_family, p4sd_selected);
    uint8_t ok = pad;
    uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_LOAD_OK, &ok, 1);
}

static void handleP4SdLoadMidi(uint8_t slot) {
    if (slot >= Config::MAX_PATTERNS || p4sd_selected[0] == '\0') return;
    // Build full path
    char path[192];
    if (strcmp(p4sd_dir, "/") == 0)
        snprintf(path, sizeof(path), "/%s", p4sd_selected);
    else
        snprintf(path, sizeof(path), "%s/%s", p4sd_dir, p4sd_selected);
    // Parse MIDI
    bool steps[Config::MAX_TRACKS][Config::MAX_STEPS] = {};
    char name[12] = "";
    int   found = 0;
    float midi_bpm = 0.0f;
    int   plen = Config::STEPS_PER_BANK;
    bool ok = midi_load_pattern(path, steps, name, sizeof(name), &found, &midi_bpm, 9, &plen);
    uint8_t resp = ok ? slot : 0xFF;
    uart_bridge_send_extended(MSG_SD_DATA, SD_RESP_LOAD_OK, &resp, 1);
    if (ok) {
        if (midi_last_load_truncated()) {
            diagInfo.lastError = "MIDI pattern truncated";
            RED808_LOG_PRINTF("[MIDI] WARNING: %s truncated to internal event buffer\n", path);
        } else {
            diagInfo.lastError = "";
        }
        if (midi_bpm > 0.0f) {
            RED808_LOG_PRINTF("[MIDI] Tempo from file: %.1f BPM\n", midi_bpm);
            applyBPMPrecise(midi_bpm, true);
        } else {
            // MIDI file had no tempo meta: re-broadcast current BPM so the
            // P4 sequencer header doesn't sit at 0.0 after loading.
            RED808_LOG_PRINTLN("[MIDI] No tempo meta, re-sending current BPM");
            applyBPMPrecise(currentBPMPrecise, true);
        }
        handleMidiPatternLoaded(slot, steps, name, plen);
    } else {
        diagInfo.lastError = "MIDI load failed";
    }
}

// =============================================================================
// P4 TOUCH COMMAND HANDLER (called from uart_bridge_receive)
// =============================================================================
static bool navigateToScreen(Screen screen);  // forward decl

void handleP4TouchCommand(uint8_t cmdId, uint8_t value) {
    switch (cmdId) {
        case TCMD_PAD_TAP:
            if (value < Config::MAX_SAMPLES) {
                livePadFlashUntilMs[value] = millis() + LIVE_PAD_FLASH_MS;
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
            if (value < SCREEN_COUNT) navigateToScreen((Screen)value);
            break;
        case TCMD_THEME_NEXT:
            pendingThemeIdx = (value < THEME_COUNT) ? value : ((currentTheme + 1) % THEME_COUNT);
            break;
        case TCMD_SD_MOUNT:
            handleP4SdMount();
            break;
        case TCMD_SD_SELECT:
            handleP4SdSelect(value);
            break;
        case TCMD_SD_BACK:
            handleP4SdBack();
            break;
        case TCMD_SD_LOAD:
            handleP4SdLoad(value);
            break;
        case TCMD_SD_LOAD_MIDI:
            handleP4SdLoadMidi(value);
            break;
        case TCMD_SYNC_PADS:
            ui_live_set_sync(value != 0);
            break;
        case TCMD_DRIVE_UP:
            set_master_drive_percent(fxDistortionPercent + 2, true);
            break;
        case TCMD_DRIVE_DOWN:
            set_master_drive_percent(fxDistortionPercent - 2, true);
            break;
        default:
            break;
    }
}

// =============================================================================
// PATTERN DATA HANDLER — called when P4 pushes pattern data via UART
// =============================================================================
void handleP4PatternData(int pat, const bool steps[16][16]) {
    if (pat < 0 || pat >= Config::MAX_PATTERNS) return;
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++)
            patterns[pat].steps[t][s] = (s < 16) ? steps[t][s] : false;
    }
    patterns[pat].length = Config::STEPS_PER_BANK;  // P4 always sends 16 steps
    patternUiRevision += 1;
    // Update current pattern number
    if (currentPattern != pat) {
        currentPattern = pat;
        uart_bridge_send_pattern(currentPattern);
    }
    // Mark UI for full redraw (sequencer grid needs refresh)
    needsFullRedraw = true;
}

// =============================================================================
// MIDI PATTERN LOADER — called from SD screen MIDI load button callback
// Fills patterns[slot], sends full pattern to Master via UDP (fire-and-forget),
// sends pattern data + selection to P4 via UART, then navigates to sequencer.
// NOTE: called from within LVGL callback — no delay() allowed, no lvgl_port_lock.
// =============================================================================
void handleMidiPatternLoaded(int slot, const bool steps[16][64], const char* name, int patternLength) {
    if (slot < 0 || slot >= Config::MAX_PATTERNS) return;

    // The Master and P4 sequencer views are both fixed at 16 steps. Any
    // multi-bar MIDI (up to 64 steps = 4 bars) is folded onto a single bar
    // by OR-ing bar 2/3/4 hits onto bar 1. We MUST apply the same fold to
    // the S3-local pattern so its on-screen grid matches what the master
    // actually plays and what the P4 shows. Previous code stored the raw
    // 64-step grid here, which made the S3 display disagree with audio.
    (void)patternLength;  // ignored — we always store folded 16 steps
    for (int t = 0; t < Config::MAX_TRACKS; t++) {
        for (int s = 0; s < Config::MAX_STEPS; s++) patterns[slot].steps[t][s] = false;
        for (int s = 0; s < 16; s++) {
            bool active = false;
            for (int bar = 0; bar < 4; bar++) {
                int idx = s + bar * 16;
                if (idx >= 64) break;
                if (steps[t][idx]) { active = true; break; }
            }
            patterns[slot].steps[t][s] = active;
        }
    }
    patterns[slot].name = name;
    patterns[slot].length = 16;   // folded view

    // Select this pattern locally
    currentPattern = slot;
    needsFullRedraw = true;
    patternUiRevision += 1;

    // --- Hand over to P4 ----------------------------------------------------
    // Design: the P4 is the sole owner of the Master UDP channel for
    // freshly-loaded MIDI patterns. S3 parses the SMF (it has the SD card
    // and the parser) and sends the packed pattern to P4 via MSG_PATTERN_PUSH.
    // P4 then broadcasts it to Master over UDP (non-blocking drain from loop).
    // The packed-push helper also folds bars 2-4 onto bar 1, so both sides
    // see the same folded 16-step pattern.
    uart_bridge_send_pattern(slot);                                    // SYS_PATTERN
    uart_bridge_send_pattern_push(slot, patterns[slot].steps,          // MSG_PATTERN_PUSH
                                   Config::MAX_TRACKS);

    RED808_LOG_PRINTF("[MIDI] Loaded pattern %d '%s' (folded to 16 steps) \u2192 P4\n",
                      slot + 1, name);
}

// =============================================================================
// NVS PERSISTENCE — save/load user settings across reboots
// =============================================================================
#include "nvs.h"

static constexpr const char* NVS_NAMESPACE = "red808";
static unsigned long nvs_last_save_ms = 0;
static bool nvs_dirty = false;

static float master_drive_amount_from_percent(int percent) {
    // Soft drive taper: keeps low values subtle while preserving punch range.
    float t = (float)constrain(percent, 0, 100) / 100.0f;
    return 0.45f * t + 0.25f * t * t;
}

static int master_drive_percent_from_amount(float amount) {
    // Inverse of: amount = 0.45*t + 0.25*t^2
    float a = constrain(amount, 0.0f, 1.0f);
    float disc = 0.45f * 0.45f + a;
    float t = (-0.45f + sqrtf(disc)) / 0.5f;
    return constrain((int)lroundf(t * 100.0f), 0, 100);
}

static void set_master_drive_percent(int percent, bool sendToMaster) {
    (void)sendToMaster;
    fxDistortionPercent = constrain(percent, 0, 100);
    nvs_dirty = true;

    // Mirror drive through the POT protocol. Encoder lane 2 is Reverb on P4.
    uint8_t driveMidi = (uint8_t)constrain((int)lroundf((fxDistortionPercent / 100.0f) * 127.0f), 0, 127);
    uart_bridge_send_pot(POT_DRIVE, driveMidi);
}

int get_master_drive_percent() {
    return fxDistortionPercent;
}

void applyBPMPrecise(float bpm, bool sendToMaster) {
    (void)sendToMaster;
    float clamped = constrain(bpm, (float)Config::MIN_BPM, (float)Config::MAX_BPM);
    currentBPMPrecise = clamped;
    currentBPM = (int)lroundf(clamped);
    nvs_dirty = true;

    // Forward BPM to P4
    uart_bridge_send_bpm(clamped);
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
    if (currentTheme == THEME_OCEAN) {
        currentTheme = THEME_RED808;
    }
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
    if (nvs_get_i32(h, "drvPct", &val32) == ESP_OK) {
        fxDistortionPercent = constrain((int)val32, 0, 100);
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
    nvs_set_i32(h, "seqVol", sequencerVolume);
    nvs_set_i32(h, "liveVol", livePadsVolume);
    nvs_set_i32(h, "bpm", currentBPM);
    nvs_set_i32(h, "bpm10", (int32_t)lroundf(currentBPMPrecise * 10.0f));
    nvs_set_i32(h, "drvPct", fxDistortionPercent);
    nvs_set_u8(h, "volMode", 0);

    nvs_commit(h);
    nvs_close(h);
    nvs_dirty = false;
    nvs_last_save_ms = millis();
}

// Mark settings dirty — will be flushed on next periodic check
void nvs_mark_dirty() { nvs_dirty = true; }

static void applyUnifiedMasterVolume(int value, bool sendToMaster) {
    (void)sendToMaster;
    int v = constrain(value, 0, Config::MAX_VOLUME);
    masterVolume = v;
    sequencerVolume = v;
    livePadsVolume = v;
    volumeMode = VOL_SEQUENCER;
    nvs_mark_dirty();

    // Forward volume to P4
    uart_bridge_send_volume(v, v, v);
}

// Call from loop() — debounced save every 5 seconds when dirty
static void nvs_periodic_save() {
    if (!nvs_dirty) return;
    if ((millis() - nvs_last_save_ms) < 5000) return;
    nvs_save_settings();
    RED808_LOG_PRINTLN("[NVS] Settings auto-saved");
}

static void requestTrackVolumesFromMaster() {
    // S3 is UART-only — no WiFi/UDP, nothing to request
}

static void requestMasterSync(bool requestState) {
    (void)requestState;
    // S3 is UART-only — no UDP master sync
    // Reset UART bridge encoder/FX state
    for (int lane = 0; lane < 3; lane++) {
        uart_bridge_send_encoder(lane, 0);
        uart_bridge_send_encoder_mute(lane, false);
    }
    if (fxDistortionPercent > 0) {
        set_master_drive_percent(fxDistortionPercent, false);
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
        resetLivePadRuntimeState();
        return;
    }

    // Guard period after entering LIVE screen — ignore stale touches
    if ((now - liveScreenEnteredMs) < LIVE_TOUCH_GUARD_MS) {
        resetLivePadRuntimeState();
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
    taskENTER_CRITICAL(&s_live_pad_state_mux);
    for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
        if (new_state[pad]) {
            // Finger is on pad — reset release timer
            livePadReleaseMs[pad] = 0;
            if (!livePadPressed[pad]) {
                // Rising edge — new press
                pendingLivePadTriggerMask.fetch_or((1UL << pad), std::memory_order_release);
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
    taskEXIT_CRITICAL(&s_live_pad_state_mux);

    // Mark visual dirty — LVGL task (Core 1) will pick it up on next cycle (~10ms)
    if (anyChanged) {
        livePadsVisualDirty.store(true, std::memory_order_release);
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

    if (!lvgl_port_lock(200)) return false;
    bool ready = ui_ensure_screen_created(screen);
    lvgl_port_unlock();
    if (!ready) return false;

    lv_obj_t* target = NULL;
    switch (screen) {
        case SCREEN_BOOT:        target = scr_boot; break;
        case SCREEN_MENU:        target = scr_menu; break;
        case SCREEN_LIVE:        target = scr_live; break;
        case SCREEN_SEQUENCER:   target = scr_sequencer; break;
        case SCREEN_VOLUMES:     target = scr_volumes; break;
        case SCREEN_FILTERS:     target = scr_menu; break; // FX removed: fallback to menu
        case SCREEN_SETTINGS:    target = scr_settings; break;
        case SCREEN_DIAGNOSTICS: target = scr_diagnostics; break;
        case SCREEN_PATTERNS:    target = scr_patterns; break;
        case SCREEN_SDCARD:    target = scr_sdcard; break;
        case SCREEN_PERFORMANCE: target = scr_performance; break;
        case SCREEN_SAMPLES:     target = scr_menu; break;
        case SCREEN_SEQ_CIRCLE:  target = scr_seq_circle; break;
        case SCREEN_MELODY:      target = scr_melody; break;
        case SCREEN_PIANO_PARAMS:target = scr_piano_params; break;
        case SCREEN_PIANO:       target = scr_piano; break;
        default: break;
    }

    if (!target) return false;

    // Guard BEFORE screen change: prevent race with touch handler
    if (screen == SCREEN_LIVE) {
        liveScreenEnteredMs = millis();
        resetLivePadRuntimeState();
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
    resetLivePadRuntimeState();
    memset(byteButtonLivePressed, 0, sizeof(byteButtonLivePressed));
    memset(byteButtonLedCache, 0xFF, sizeof(byteButtonLedCache));
    memset(byteButtonLedInitialized, 0, sizeof(byteButtonLedInitialized));
}

void requestPatternFromMaster() {
    // S3 is UART-only — no UDP
}

void sendPlayStateCommand(bool shouldPlay) {
    isPlaying = shouldPlay;
    uart_bridge_send_play_state(isPlaying);
}

static bool pattern_has_data(int patternIndex) {
    if (patternIndex < 0 || patternIndex >= Config::MAX_PATTERNS) return false;
    for (int t = 0; t < Config::MAX_TRACKS; t++)
        for (int s = 0; s < Config::MAX_STEPS; s++)
            if (patterns[patternIndex].steps[t][s]) return true;
    return false;
}

void selectPatternOnMaster(int patternIndex) {
    currentPattern = constrain(patternIndex, 0, Config::MAX_PATTERNS - 1);
    needsFullRedraw = true;
    patternUiRevision += 1;

    bool localHasData = pattern_has_data(currentPattern);

    // Forward pattern selection to P4.
    uart_bridge_send_pattern(currentPattern);
    if (localHasData) {
        uart_bridge_send_pattern_push(currentPattern, patterns[currentPattern].steps,
                                      Config::MAX_TRACKS);
    }
}

// Send full local pattern to master — no-op: S3 is UART-only
void sendFullPatternToMaster(int pat) { (void)pat; }

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

// MPC-style velocity shaping for sequencer hits.
// Adds groove accents without requiring per-step velocity storage.
static int sequencer_groove_velocity(int track, int step, int baseVel) {
    int v = constrain(baseVel, 1, 127);

    // Stronger pulse on quarter notes (steps 0/4/8/12 in a 16-step bar).
    if ((step & 0x03) == 0) v += 7;

    // Track profile: kick/snare punch, hats softer with off-beat push.
    switch (track) {
        case 0: v += 12; break; // Kick
        case 1: v += 9;  break; // Snare
        case 2: v -= 12; if ((step & 0x01) == 1) v += 6; break; // Closed HH
        case 3: v -= 8;  if ((step & 0x03) == 2) v += 7; break; // Open HH
        case 4: v += 4;  break; // Clap
        default: break;
    }

    // Tiny deterministic movement to avoid machine-gun flatness.
    v += ((step * 37 + track * 13) % 5) - 2; // [-2..+2]

    return constrain(v, 16, 127);
}

static uint32_t sequencer_step_interval_us(int step, float bpmPrecise) {
    (void)step;
    float bpm = constrain(bpmPrecise, (float)Config::MIN_BPM, (float)Config::MAX_BPM);
    float base16 = 60000000.0f / bpm / 4.0f;
    uint32_t us = (uint32_t)lroundf(base16);
    if (us < 20000U) us = 20000U;
    return us;
}

void apply_mpc_preset(bool saveToNvs) {
    set_master_drive_percent(18, true);

    if (saveToNvs) {
        nvs_save_settings();
    } else {
        nvs_dirty = true;
    }

    RED808_LOG_PRINTF("[MPC] drive applied: drive=%d%%\n", fxDistortionPercent);
}

void sendLivePadTrigger(int pad, int velocity) {
    (void)pad; (void)velocity;
    // S3 is UART-only — pad triggers go via uart_bridge_send_pad_trigger in pad_trigger_task
}

// =============================================================================
// UDP STUBS — S3 is UART-only; these are kept for link compatibility with
// ui_screens.cpp externs but do nothing.
// =============================================================================

void sendUDPCommand(const char* cmd) { (void)cmd; }
void sendUDPCommand(JsonDocument& doc) { (void)doc; }

// =============================================================================
// FILTER UDP SEND
// =============================================================================

void sendFilterUDP(int track, int fxType) {
    (void)track; (void)fxType;
    // S3 is UART-only — no UDP FX commands
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

static bool i2c_device_present_raw(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
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
                    // Auto-activate LowPass filter so resonance is audible.
                    if (fxFilterType == 0) {
                        fxFilterType = 1;  // LP
                        JsonDocument fdoc(&sramAllocator);
                        fdoc["cmd"] = "setFilter";
                        fdoc["type"] = 1;
                        sendUDPCommand(fdoc);
                    }
                    doc["cmd"] = "setFilterResonance";
                    doc["value"] = (float)fxFilterResonanceX10 / 10.0f;
                    sendUDPCommand(doc);
                }
            } else {
                // P4: Distortion drive (0.0 – 1.0)
                fxDistortionPercent = constrain((int)lroundf(t * 100.0f), 0, 100);
                if (!analogFxMuted[2]) {
                    doc["cmd"] = "setDistortion";
                    doc["value"] = master_drive_amount_from_percent(fxDistortionPercent);
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
        doc["value"] = analogFxMuted[2] ? 0.0f : master_drive_amount_from_percent(fxDistortionPercent);
    }
    sendUDPCommand(doc);
}

void scanI2CHub() {
    RED808_LOG_PRINTLN("[I2C] Scanning bus...");

    g_i2c_scan_in_progress = true;

    // Check for PCA9548A hub (with mutex protection)
    hubDetected = i2c_device_present(I2C_HUB_ADDR);
    diagInfo.i2cHubOk = hubDetected;

    if (!hubDetected) {
        RED808_LOG_PRINTLN("[I2C] No PCA9548A hub found");
        g_i2c_scan_in_progress = false;
        return;
    }
    RED808_LOG_PRINTLN("[I2C] PCA9548A hub detected");

    // Full rescan reset: clear stale channel/connection state before remapping.
    for (int i = 0; i < M5_ENCODER_MODULES; i++) {
        m5encoderConnected[i] = false;
        m5HubChannel[i] = -1;
        m5FirmwareVersion[i] = 0;
    }
    for (int i = 0; i < DFROBOT_ENCODER_COUNT; i++) {
        dfEncoderConnected[i] = false;
        dfRobotHubChannel[i] = -1;
        if (dfEncoders[i]) {
            delete dfEncoders[i];
            dfEncoders[i] = nullptr;
        }
    }
    for (int i = 0; i < BYTEBUTTON_COUNT; i++) {
        byteButtonConnected[i] = false;
        byteButtonHubChannel[i] = -1;
        byteButtonLedInitialized[i] = false;
        prevByteButtonState[i] = 0;
    }
    dfRobotPotConnected = false;
    dfRobotPotHubChannel = -1;

    int m5Found = 0, dfFound = 0;
    int byteButtonsFound = 0;
    bool potHubFound = false;
    memset(i2cHubKnownMask, 0, sizeof(i2cHubKnownMask));

    // Atomic scan: hold I2C mutex for complete hub scan to avoid channel races
    // with touch/encoder tasks while selecting PCA9548A channels.
    if (i2c_lock(300)) {
        for (uint8_t ch = 0; ch < 8 && (m5Found < M5_ENCODER_MODULES || dfFound < DFROBOT_ENCODER_COUNT || byteButtonsFound < BYTEBUTTON_COUNT || !potHubFound); ch++) {
            i2c_hub_select_raw(ch);
            delay(5);

            // Probe M5 ROTATE8 (0x41)
            if (m5Found < M5_ENCODER_MODULES && i2c_device_present_raw(M5_ENCODER_ADDR)) {
                m5HubChannel[m5Found] = ch;
                m5encoderConnected[m5Found] = true;
                i2cHubKnownMask[ch] |= I2C_DEVBIT_M5_ROTATE8;
                RED808_LOG_PRINTF("[I2C] M5 ROTATE8 #%d found on ch %d\n", m5Found + 1, ch);

                if (m5encoders[m5Found].begin()) {
                    for (int e = 0; e < ENCODERS_PER_MODULE; e++) {
                        m5encoders[m5Found].resetCounter(e);
                    }
                }
                m5FirmwareVersion[m5Found] = m5encoders[m5Found].getVersion();
                RED808_LOG_PRINTF("[I2C] M5 #%d firmware v%d\n", m5Found + 1, m5FirmwareVersion[m5Found]);
                i2c_hub_deselect_raw();
                m5Found++;
            }

            // Probe DFRobot SEN0502 (0x54)
            if (dfFound < DFROBOT_ENCODER_COUNT && i2c_device_present_raw(DFROBOT_ENCODER_ADDR)) {
                i2cHubKnownMask[ch] |= I2C_DEVBIT_DFROBOT_ENC;
                dfRobotHubChannel[dfFound] = ch;
                dfEncoders[dfFound] = new DFRobot_VisualRotaryEncoder_I2C(DFROBOT_ENCODER_ADDR, &Wire);
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
                dfFound++;
            }

            // Re-select hub channel before ByteButton probe
            if (byteButtonsFound < BYTEBUTTON_COUNT) {
                i2c_hub_select_raw(ch);
                delay(2);
                if (i2c_device_present_raw(BYTEBUTTON_ADDR)) {
                    i2cHubKnownMask[ch] |= I2C_DEVBIT_BYTEBUTTON;
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

                            i2c_hub_select_raw(ch);
                            byteButtonLedInitialized[moduleIdx] = byteButtonApplyLedConfigLocked();
                            i2c_hub_deselect_raw();
                        }
                    }
                }
            }

            // Probe DFRobot 4-pot ADC hub converter (ADS1115-compatible, 0x48)
            if (!potHubFound) {
                i2c_hub_select_raw(ch);
                delay(2);
                if (i2c_device_present_raw(DFROBOT_POT_ADC_ADDR) || i2c_device_present_raw(DFROBOT_POT_ADC_ADDR_ALT)) {
                    i2cHubKnownMask[ch] |= I2C_DEVBIT_POT_ADC;
                    dfRobotPotAddr = i2c_device_present_raw(DFROBOT_POT_ADC_ADDR) ? DFROBOT_POT_ADC_ADDR : DFROBOT_POT_ADC_ADDR_ALT;
                    dfRobotPotHubChannel = ch;
                    dfRobotPotConnected = true;
                    potHubFound = true;
                    RED808_LOG_PRINTF("[I2C] DFRobot 4-pot ADC found on hub ch %d (0x%02X)\n", ch, dfRobotPotAddr);
                }
            }

            i2c_hub_deselect_raw();
        }

        i2c_hub_deselect_raw();
        i2c_unlock();
    } else {
        RED808_LOG_PRINTLN("[I2C] WARNING: could not acquire I2C lock for atomic hub scan");
    }

    int byteButtonFoundCount = byteButtonsFound;

    // ── ByteButton direct-bus fallback ────────────────────────────────────
    // If not all ByteButtons were found on hub channels, try the direct bus
    // (hub fully deselected). Useful when a unit is connected upstream of
    // the hub or behind a passive splitter.
    if (byteButtonsFound < BYTEBUTTON_COUNT) {
        i2c_hub_deselect();
        delay(5);
        if (i2c_device_present(BYTEBUTTON_ADDR)) {
            // Find next unassigned slot
            int moduleIdx = -1;
            for (int m = 0; m < BYTEBUTTON_COUNT; m++) {
                if (!byteButtonConnected[m]) { moduleIdx = m; break; }
            }
            if (moduleIdx >= 0) {
                byteButtonHubChannel[moduleIdx] = -2;  // -2 = direct bus (no hub)
                byteButtonConnected[moduleIdx] = true;
                prevByteButtonState[moduleIdx] = 0;
                byteButtonsFound++;
                byteButtonFoundCount = byteButtonsFound;
                RED808_LOG_PRINTF("[I2C] ByteButton #%d found on DIRECT bus (no hub)\n", moduleIdx + 1);
                if (i2c_lock(50)) {
                    // hub is already deselected
                    byteButtonLedInitialized[moduleIdx] = byteButtonApplyLedConfigLocked();
                    i2c_unlock();
                }
            }
        }
    }
    // ─────────────────────────────────────────────────────────────────────

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

    for (int ch = 0; ch < 8; ch++) {
        if (!i2cHubKnownMask[ch]) continue;
        RED808_LOG_PRINTF("[I2C] CH%d mask=0x%02X (M5=%d BB=%d DF=%d POT=%d)\n", ch, i2cHubKnownMask[ch],
            (i2cHubKnownMask[ch] & I2C_DEVBIT_M5_ROTATE8) != 0,
            (i2cHubKnownMask[ch] & I2C_DEVBIT_BYTEBUTTON) != 0,
            (i2cHubKnownMask[ch] & I2C_DEVBIT_DFROBOT_ENC) != 0,
            (i2cHubKnownMask[ch] & I2C_DEVBIT_POT_ADC) != 0);
    }

    g_i2c_scan_in_progress = false;
}

static bool controls_i2c_ready() {
    for (int i = 0; i < M5_ENCODER_MODULES; i++) {
        if (!m5encoderConnected[i]) return false;
    }
    for (int i = 0; i < DFROBOT_ENCODER_COUNT; i++) {
        if (!dfEncoderConnected[i]) return false;
    }
    for (int i = 0; i < BYTEBUTTON_COUNT; i++) {
        if (!byteButtonConnected[i]) return false;
    }
    if (!dfRobotPotConnected) return false;
    return true;
}

void updateByteButtonLeds() {
    auto ledColorForAction = [&](uint8_t action) -> uint32_t {
        // Helper: dim a raw 0xRRGGBB color to ~30% brightness
        auto dim = [](uint32_t c, int shift) -> uint32_t {
            return (((c >> 16) & 0xFF) >> shift) << 16 |
                   (((c >>  8) & 0xFF) >> shift) <<  8 |
                   (((c      ) & 0xFF) >> shift);
        };
        uint32_t c0 = theme_nav_color(0), c1 = theme_nav_color(1);
        uint32_t c2 = theme_nav_color(2), c3 = theme_nav_color(3);
        uint32_t c4 = theme_nav_color(4), c5 = theme_nav_color(5);
        uint32_t c6 = theme_nav_color(6);
        switch ((ByteButtonAction)action) {
            case BB_ACTION_MENU:           return c6;
            case BB_ACTION_VOL_MODE:       return dim(c0, 1);
            case BB_ACTION_SCREEN_LIVE:    return c2;
            case BB_ACTION_SCREEN_SEQUENCER: return c3;
            case BB_ACTION_SCREEN_PERFORMANCE: return c4;
            case BB_ACTION_SCREEN_VOLUMES: return c5;
            case BB_ACTION_FX_CLEAN:
                return analogFxMuted[0] ? dim(0xFF0000, 2) : c1;
            case BB_ACTION_FX_SPACE:
                return analogFxMuted[1] ? dim(0xFF0000, 2) : c2;
            case BB_ACTION_FX_ACID:
                return analogFxMuted[2] ? dim(0xFF0000, 2) : c3;
            case BB_ACTION_FX_DESTROY:
                return (analogFxMuted[0] && analogFxMuted[1] && analogFxMuted[2]) ? dim(0xFF0000, 1) : dim(c0, 1);
            case BB_ACTION_FX_TARGET_PREV:
            case BB_ACTION_FX_TARGET_NEXT:
                return (filterSelectedTrack == -1) ? dim(c4, 2) : c4;
            case BB_ACTION_PATTERN_PREV:
                return (currentPattern > 0) ? c0 : dim(c0, 3);
            case BB_ACTION_PATTERN_NEXT:
                return (currentPattern < Config::MAX_PATTERNS - 1) ? c0 : dim(c0, 3);
            case BB_ACTION_PLAY_PAUSE:
                return isPlaying ? c1 : dim(c1, 2);
            case BB_ACTION_SCREEN_PATTERNS: return c1;
            case BB_ACTION_SCREEN_SETTINGS: return c5;
            case BB_ACTION_BPM_UP:          return dim(c0, 1);
            case BB_ACTION_BPM_DOWN:        return dim(c0, 2);
            default:                        return 0x202020;
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
            uint8_t rgb4[4] = {(uint8_t)(desired[i] >> 16), (uint8_t)(desired[i] >> 8), (uint8_t)desired[i], 0};
            if (!byteButtonWriteBytes(BYTEBUTTON_LED_RGB888_REG + i * 4, rgb4, 4)) {
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

static void setByteButtonLedDirect(int moduleIdx, int index, uint32_t color) {
    if (moduleIdx < 0 || moduleIdx >= BYTEBUTTON_COUNT) return;
    if (!byteButtonConnected[moduleIdx] || !byteButtonLedInitialized[moduleIdx]) return;
    if (index < 0 || index >= BYTEBUTTON_LED_COUNT) return;
    if (!i2c_lock(10)) return;
    if (byteButtonHubChannel[moduleIdx] >= 0) i2c_hub_select_raw(byteButtonHubChannel[moduleIdx]);
    else if (byteButtonHubChannel[moduleIdx] == -2) i2c_hub_deselect_raw();
    uint8_t rgb4[4] = {(uint8_t)(color >> 16), (uint8_t)(color >> 8), (uint8_t)color, 0};
    byteButtonWriteBytes(BYTEBUTTON_LED_RGB888_REG + index * 4, rgb4, 4);
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
            int track = mod * ENCODERS_PER_MODULE + enc;

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
                        if (ledCount < Config::MAX_TRACKS) ledQueue[ledCount++] = {mod, enc, 0x1E, 0, 0};
                    } else {
                        uint8_t rgb[3]; theme_encoder_color(track, rgb);
                        uint8_t br = (uint8_t)map(trackVolumes[track], 0, Config::MAX_VOLUME, 10, 255);
                        if (ledCount < Config::MAX_TRACKS) ledQueue[ledCount++] = {mod, enc, (uint8_t)((rgb[0]*br)/255), (uint8_t)((rgb[1]*br)/255), (uint8_t)((rgb[2]*br)/255)};
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

    // UDP batch send removed — S3 is UART-only

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
// FX ENCODER LED SYNC — write clamped value back to DFRobot hardware ring
// =============================================================================
static int fxLaneToDfEncoder(int lane) {
    if (lane == 0) return 3;  // Chorus
    if (lane == 1) return 1;  // Delay
    if (lane == 2) return 2;  // Reverb
    return -1;
}

static int dfEncoderToFxLane(int enc) {
    if (enc == 1) return 1;  // Delay
    if (enc == 2) return 2;  // Reverb
    if (enc == 3) return 0;  // Chorus
    return -1;
}

static void syncFxEncoderLED(int lane) {
    int enc = fxLaneToDfEncoder(lane);
    if (enc >= DFROBOT_ENCODER_COUNT || !dfEncoderConnected[enc]) return;
    int ch = dfRobotHubChannel[enc];
    if (ch < 0) return;
    int ledVal = dfFxMuted[lane] ? 0 : constrain(dfFxParamValue[lane], 0, 127);
    uint16_t mapped = (uint16_t)(ledVal * 1023 / 127);
    if (i2c_lock(8)) {
        i2c_hub_select_raw(ch);
        dfEncoders[enc]->setEncoderValue(mapped);
        prevDFValue[enc] = mapped;
        i2c_hub_deselect_raw();
        i2c_unlock();
    }
}

// =============================================================================
// FX LANE MUTE TOGGLE (used by ByteButton + can be called from anywhere)
// lane: 0=Chorus, 1=Delay, 2=Reverb
// =============================================================================
static void toggleDfFxMute(int lane) {
    if (lane < 0 || lane > 2) return;
    dfFxMuted[lane] = !dfFxMuted[lane];
    bool muted = dfFxMuted[lane];
    int storedVal = dfFxParamValue[lane];
    int effective = muted ? 0 : constrain(storedVal, 0, 127);

    // Update local state
    if (lane == 0)      masterFilter.chorusAmount  = (uint8_t)effective;
    else if (lane == 1) masterFilter.delayAmount   = (uint8_t)effective;
    else                masterFilter.compAmount    = (uint8_t)effective;
    masterFilter.enabled = (masterFilter.chorusAmount > 0 ||
                            masterFilter.delayAmount > 0 ||
                            masterFilter.compAmount > 0);

    // Send via UART → P4 → Master
    uart_bridge_send_encoder(lane, (uint8_t)effective);
    uart_bridge_send_encoder_mute(lane, muted);
    syncFxEncoderLED(lane);
    RED808_LOG_PRINTF("[FX] Lane %d (%s) %s\n", lane,
        lane==0?"Chorus":lane==1?"Delay":"Reverb", muted?"MUTED":"UNMUTED");
}

// =============================================================================
// DFROBOT ROTARY HANDLING
// =============================================================================

void handleDFRobotEncoders() {
    static unsigned long lastBtnMs[DFROBOT_ENCODER_COUNT] = {};
    static int laneStoredValue[3] = {0, 0, 0};

    auto syncMasterEnabled = [&]() {
        masterFilter.enabled = (masterFilter.chorusAmount > 0 ||
                                masterFilter.delayAmount > 0 ||
                                masterFilter.compAmount > 0);
    };

    auto sendFxLane = [&](int lane, int value, bool muted) {
        int effective = muted ? 0 : constrain(value, 0, 127);

        // Update local state: lane 0=Chorus, 1=Delay, 2=Reverb
        if (lane == 0)      masterFilter.chorusAmount  = (uint8_t)effective;
        else if (lane == 1) masterFilter.delayAmount   = (uint8_t)effective;
        else                masterFilter.compAmount    = (uint8_t)effective;  // reuse compAmount for Reverb

        syncMasterEnabled();
        dfFxParamMode[lane] = 0;
        dfFxParamValue[lane] = laneStoredValue[lane];

        // Send via UART → P4 → Master (primary path when S3_WIFI_ENABLED=0)
        uart_bridge_send_encoder(lane, (uint8_t)effective);
        uart_bridge_send_encoder_mute(lane, muted);  // always sync mute state to P4
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

        // ── Process results outside of I2C lock ──
        if (i == 0) {
            // DFRobot #0 (CH2): BPM coarse control. Button resets to default BPM.
            int logical_delta = quantizeDFDelta(i, delta);
            if (logical_delta != 0) {
                float newBpm = currentBPMPrecise + (float)logical_delta;
                applyBPMPrecise(newBpm, true);
                RED808_LOG_PRINTF("[DFRobot] BPM %.1f (delta=%d)\n", currentBPMPrecise, logical_delta);
            }
            if (buttonPressed && (millis() - lastBtnMs[i]) > Config::DF_BUTTON_GUARD_MS) {
                lastBtnMs[i] = millis();
                applyBPMPrecise((float)Config::DEFAULT_BPM, true);
                RED808_LOG_PRINTF("[DFRobot] BPM reset to %.1f\n", currentBPMPrecise);
            }
        }
        else {
            // DFRobot #1/#2/#3: FX lanes (Flanger / Delay / Reverb)
            int lane = dfEncoderToFxLane(i);
            if (lane < 0) continue;

            int logical_delta = quantizeDFDelta(i, delta);
            if (logical_delta != 0) {
                int step = Config::DF_FX_STEP;
                int change = logical_delta * step;
                if (laneStoredValue[lane] == 0 && change < 0) change = -change;
                else if (laneStoredValue[lane] == 127 && change > 0) change = -change;
                laneStoredValue[lane] = constrain(laneStoredValue[lane] + change, 0, 127);
                sendFxLane(lane, laneStoredValue[lane], dfFxMuted[lane]);

                // Sync LED ring to clamped value (write back to DFRobot hardware)
                int ledVal = dfFxMuted[lane] ? 0 : laneStoredValue[lane];
                uint16_t mapped = (uint16_t)(ledVal * 1023 / 127);
                if (i2c_lock(8)) {
                    i2c_hub_select_raw(ch);
                    dfEncoders[i]->setEncoderValue(mapped);
                    prevDFValue[i] = mapped;
                    i2c_hub_deselect_raw();
                    i2c_unlock();
                }

                RED808_LOG_PRINTF("[DFRobot] FX enc %d -> lane %d val=%d (delta=%d change=%d)\n", i, lane, laneStoredValue[lane], logical_delta, change);
            }
            if (buttonPressed && (millis() - lastBtnMs[i]) > Config::DF_BUTTON_GUARD_MS) {
                lastBtnMs[i] = millis();
                toggleDfFxMute(lane);
                RED808_LOG_PRINTF("[DFRobot] FX lane %d mute toggle → %s\n", lane, dfFxMuted[lane] ? "MUTED" : "UNMUTED");
            }
        }
    }
}

// =============================================================================
// M5 UNIT FADER (GPIO6) - analog control
// =============================================================================

void handleUnitFader() {
    // Absolute positional mapping: ADC position → Master Volume 0..150
    // Uses heavy oversampling + EMA filter for smooth, jitter-free response.
    static unsigned long lastReadMs = 0;
    static float filtered = -1.0f;  // -1 = uninitialized
    static int lastMappedVol = -1;

    unsigned long now = millis();
    if ((now - lastReadMs) < Config::UNIT_FADER_READ_MS) return;
    lastReadMs = now;

    // 16-sample oversampling for noise reduction
    long sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(Config::UNIT_FADER_PIN);
    float raw = (float)(sum / 16);

    // First call: initialize filter to real position and apply immediately
    if (filtered < 0.0f) {
        filtered = raw;
        int initVol = (int)((filtered / 4095.0f) * (float)Config::MAX_VOLUME + 0.5f);
        initVol = constrain(initVol, 0, Config::MAX_VOLUME);
        lastMappedVol = initVol;
        applyUnifiedMasterVolume(initVol, true);
        return;
    }
    filtered = filtered * 0.9f + raw * 0.1f;

    // Map ADC 0..4095 → Volume 0..150
    int newVol = (int)((filtered / 4095.0f) * (float)Config::MAX_VOLUME + 0.5f);
    newVol = constrain(newVol, 0, Config::MAX_VOLUME);

    // Deadband: only apply if value changed by ≥1 unit
    if (newVol == lastMappedVol) return;
    lastMappedVol = newVol;

    applyUnifiedMasterVolume(newVol, true);
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
        case BB_ACTION_SCREEN_PERFORMANCE:
            navigateToScreen(SCREEN_PERFORMANCE);
            break;
        case BB_ACTION_SCREEN_VOLUMES:
            navigateToScreen(SCREEN_VOLUMES);
            break;
        case BB_ACTION_FX_CLEAN:
            toggleDfFxMute(0);  // Chorus
            break;
        case BB_ACTION_FX_SPACE:
            toggleDfFxMute(1);  // Delay
            break;
        case BB_ACTION_FX_ACID:
            toggleDfFxMute(2);  // Reverb
            break;
        case BB_ACTION_FX_DESTROY:
            {
                bool allMuted = dfFxMuted[0] && dfFxMuted[1] && dfFxMuted[2];
                if (!allMuted) { // mute all
                    if (!dfFxMuted[0]) toggleDfFxMute(0);
                    if (!dfFxMuted[1]) toggleDfFxMute(1);
                    if (!dfFxMuted[2]) toggleDfFxMute(2);
                } else { // unmute all
                    toggleDfFxMute(0);
                    toggleDfFxMute(1);
                    toggleDfFxMute(2);
                }
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
        case BB_ACTION_SCREEN_PATTERNS:
            navigateToScreen(SCREEN_PATTERNS);
            break;
        case BB_ACTION_SCREEN_SETTINGS:
            navigateToScreen(SCREEN_SETTINGS);
            break;
        case BB_ACTION_SCREEN_SDCARD:
            navigateToScreen(SCREEN_SDCARD);
            break;
        case BB_ACTION_BPM_UP:
            applyBPMPrecise(currentBPMPrecise + 1.0f, true);
            RED808_LOG_PRINTF("[BB%d] BPM +1 -> %.1f\n", moduleIdx + 1, currentBPMPrecise);
            break;
        case BB_ACTION_BPM_DOWN:
            applyBPMPrecise(currentBPMPrecise - 1.0f, true);
            RED808_LOG_PRINTF("[BB%d] BPM -1 -> %.1f\n", moduleIdx + 1, currentBPMPrecise);
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
        // Invalidate ByteButton LED cache and immediately push new colors
        memset(byteButtonLedCache, 0xFF, sizeof(byteButtonLedCache));
        updateByteButtonLeds();

        // Update active indicator on settings theme buttons (inside LVGL lock below)
        if (lvgl_port_lock(15)) {
            ui_refresh_theme_buttons();
            lvgl_port_unlock();
        }
    }

    if (!lvgl_port_lock(15)) return;  // 15ms: give LVGL task time to finish

    // Apply pending theme change from analog encoder — INSIDE LVGL lock
    int pt = pendingThemeIdx;
    if (pt >= 0 && pt < (int)THEME_COUNT) {
        pendingThemeIdx = -1;
        ui_theme_apply((VisualTheme)pt);
        uart_bridge_send_theme(pt);
        themeJustChanged = true;  // trigger BB LED cache flush on next updateUI()
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
    { unsigned long _t = millis(); while (!Serial && (millis()-_t) < 50) delay(5); }
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

    // 7b. UART bridge to ESP32-P4 — init EARLY so P4 handshake is not missed during I2C delay.
    uart_bridge_init();

    // 7. Scan I2C hub for M5 + DFRobot + ByteButton (before LVGL task starts - no I2C race)
    // Guard: CH32V003 inside M5 ROTATE8 needs ~4-5s from cold-power-on before it responds to I2C.
    { unsigned long ms = millis(); if (ms < 5000) delay(5000 - ms); }
    scanI2CHub();

    setOceanBlueLeds();

    // 8. WiFi DISABLED — S3 communicates via UART/USB-C only
    RED808_LOG_PRINTLN("[WiFi] DISABLED — S3 is UART-only slave to P4");

    // 9. Create initial UI screens (must be inside LVGL lock)
    //    Widgets now allocate from PSRAM via lv_conf.h, keeping internal heap free
    RED808_LOG_PRINTLN("[UI] Creating initial screens...");
    if (lvgl_port_lock(1000)) {
        ui_theme_init();

        ui_create_boot_screen();      // boot animation — shown first; auto-navigates to menu
        ui_create_menu_screen();
        ui_create_live_screen();
        ui_create_sequencer_screen();

        // Start on boot animation; boot_timer_cb() will navigate to SCREEN_MENU when complete
        currentScreen = SCREEN_BOOT;
        lv_scr_load(scr_boot);

        // Force first frame render so framebuffers contain the boot screen
        lv_timer_handler();
        lv_timer_handler();  // two passes: layout + flush

        lvgl_port_unlock();
        RED808_LOG_PRINTLN("[UI] Initial screens created; secondary screens lazy-load on first use");
    }

    // 10. Start LVGL task after initial screen creation to avoid setup-time mutex contention.
    lvgl_port_task_start();
    RED808_LOG_PRINTLN("[LVGL] Task started");

    // Backlight ON only after boot screen is rendered — eliminates PSRAM garbage flicker
    io_ext_backlight_on();
    RED808_LOG_PRINTLN("[BL] Backlight ON (boot screen visible)");
    RED808_LOG_PRINTF("[HEAP] After UI: %d bytes  PSRAM: %d bytes\n", ESP.getFreeHeap(), ESP.getFreePsram());

    // 11. Encoder task — pri 1 = same as loop → round-robin time-slicing
    //     loop() no longer starved; gets CPU every 1ms tick for touch triggers
    BaseType_t encTaskOk = xTaskCreatePinnedToCore(encoder_task, "enc", 6144, NULL, 1, NULL, 0);
    RED808_LOG_PRINTF("[ENC] Encoder task %s (Core 0, pri 1)\n", encTaskOk == pdPASS ? "started" : "FAILED");

    // 12. Touch task — highest I2C priority, preempts encoder via priority inheritance
    BaseType_t touchTaskOk = xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 3, NULL, 0);
    RED808_LOG_PRINTF("[TOUCH] Touch task %s (Core 0, pri 3)\n", touchTaskOk == pdPASS ? "started" : "FAILED");

    // 13. Pad trigger task — prio 2, preempts loop/enc but yields to touch
    BaseType_t padsTaskOk = xTaskCreatePinnedToCore(pad_trigger_task, "pads", 4096, NULL, 2, NULL, 0);
    RED808_LOG_PRINTF("[PADS] Pad trigger task %s (Core 0, pri 2)\n", padsTaskOk == pdPASS ? "started" : "FAILED");

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
        if (g_i2c_scan_in_progress) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        // Poll unconditionally — GT911 status register handles "no new data"
        // internally (returns immediately when !bufferReady).
        // INT pin polling was unreliable (pulse mode, missed >99% of events).
        gt911_poll();
        vTaskDelay(pdMS_TO_TICKS(5));  // 200Hz poll; GT911 itself updates around 100Hz
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

        uint32_t mask = pendingLivePadTriggerMask.exchange(0, std::memory_order_acq_rel);
        if (mask != 0) {
            for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
                if ((mask & (1UL << pad)) == 0) continue;
                sendLivePadTrigger(pad, padVelocity);
                uart_bridge_send_pad_trigger(pad, (uint8_t)padVelocity);
                taskENTER_CRITICAL(&s_live_pad_state_mux);
                lastLivePadTriggerMs[pad] = now;
                livePadFlashUntilMs[pad] = now + LIVE_PAD_FLASH_MS;
                taskEXIT_CRITICAL(&s_live_pad_state_mux);
            }
            if (currentScreen == SCREEN_LIVE) {
                livePadsVisualDirty.store(true, std::memory_order_release);
            }
        }

        // Repeat triggers (hold-to-repeat)
        uint32_t repeatMask = 0;
        taskENTER_CRITICAL(&s_live_pad_state_mux);
        for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
            if (livePadRemainingRepeats[pad] == 0) continue;
            if (now < livePadNextRepeatMs[pad]) continue;
            lastLivePadTriggerMs[pad] = now;
            livePadFlashUntilMs[pad] = now + LIVE_PAD_FLASH_MS;
            livePadRemainingRepeats[pad]--;
            livePadNextRepeatMs[pad] = now + LIVE_PAD_REPEAT_INTERVAL_MS;
            repeatMask |= (1UL << pad);
        }
        taskEXIT_CRITICAL(&s_live_pad_state_mux);

        if (repeatMask != 0) {
            for (int pad = 0; pad < Config::MAX_SAMPLES; pad++) {
                if ((repeatMask & (1UL << pad)) == 0) continue;
                sendLivePadTrigger(pad, padVelocity);
                uart_bridge_send_pad_trigger(pad, (uint8_t)padVelocity);
            }
        }
        if (repeatMask != 0 && currentScreen == SCREEN_LIVE) {
            livePadsVisualDirty.store(true, std::memory_order_release);
        }

        vTaskDelay(pdMS_TO_TICKS(2));  // 500Hz edge processing without burning a full core
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
        if (g_i2c_scan_in_progress) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        // Periodic alive check
        {
            static uint32_t encLoops = 0;
            encLoops++;
            if (encLoops % 500 == 0) {
                RED808_LOG_PRINTF("[ENC_TASK] alive loops=%u stack=%u\n",
                    encLoops, (unsigned)uxTaskGetStackHighWaterMark(NULL));
            }
        }
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

    // Runtime I2C rescan disabled for stability. Only boot-time scan is used.

    // === Pad triggers now handled by pad_trigger_task (prio 2) ===

    // === UART BRIDGE (P4 receive + heartbeat) ===
    uart_bridge_receive();
    // v2.9 — apply melody state forwarded from master via P4 UART
    {
        extern PendingMelodyFromP4 g_pending_melody_from_p4;
        if (g_pending_melody_from_p4.pending) {
            g_pending_melody_from_p4.pending = false;
            if (lvgl_port_lock(200)) {
                melody_apply_basic_sync(
                    g_pending_melody_from_p4.engine,
                    g_pending_melody_from_p4.octave,
                    g_pending_melody_from_p4.rec,
                    g_pending_melody_from_p4.pad);
                lvgl_port_unlock();
            }
        }
    }
    // On SCREEN_MELODY entry, push S3's current melody state to P4 so PIANO syncs.
    if (g_mel_screen_entered) {
        g_mel_screen_entered = false;
        melody_uart_broadcast_state();
    }
    {
        static unsigned long lastHB = 0;
        if (now - lastHB >= 500) {
            lastHB = now;
            uart_bridge_heartbeat();
        }
    }


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

    // P4-authoritative clock mode:
    // S3 no longer advances currentStep locally. It executes sequencer hits
    // only when a new step value arrives from P4 over UART (SYS_STEP).
    {
        static int prev_exec_step = -1;

        if (isPlaying) {
            int patLen = patterns[currentPattern].length;
            if (patLen < 1) patLen = 16;

            int safeStep = currentStep % patLen;
            if (safeStep < 0) safeStep += patLen;

            if (safeStep != prev_exec_step) {
                prev_exec_step = safeStep;
                lastLocalStepMs = now;

                const int seqVel = map(constrain(sequencerVolume, 0, Config::MAX_VOLUME),
                                       0, Config::MAX_VOLUME, 32, 127);
                bool anySeqPadFlash = false;
                for (int t = 0; t < Config::MAX_TRACKS; t++) {
                    if (patterns[currentPattern].steps[t][safeStep] &&
                        !patterns[currentPattern].muted[t]) {
                        const int hitVel = sequencer_groove_velocity(t, safeStep, seqVel);
                        livePadFlashUntilMs[t] = now + LIVE_PAD_FLASH_MS;
                        anySeqPadFlash = true;
                    }
                }
                if (anySeqPadFlash && currentScreen == SCREEN_LIVE) {
                    livePadsVisualDirty.store(true, std::memory_order_release);
                }
            }
        } else {
            prev_exec_step = -1;
            lastLocalStepMs = now;
            lastLocalStepUs = micros();
        }
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
