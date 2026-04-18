// =============================================================================
// BlueSlaveV2 - system_state.h
// RED808 V6 Surface Controller - State definitions
// =============================================================================
#pragma once

#include <Arduino.h>
#include "config.h"

// =============================================================================
// ENUMS
// =============================================================================

enum Screen {
    SCREEN_BOOT = 0,
    SCREEN_MENU,
    SCREEN_LIVE,
    SCREEN_SEQUENCER,
    SCREEN_SETTINGS,
    SCREEN_DIAGNOSTICS,
    SCREEN_PATTERNS,
    SCREEN_VOLUMES,
    SCREEN_FILTERS,
    SCREEN_SDCARD,
    SCREEN_PERFORMANCE,
    SCREEN_SAMPLES,
    SCREEN_SEQ_CIRCLE,
    SCREEN_COUNT          // keep last — used for array sizing
};

enum FilterType {
    FILTER_FLANGER = 0,
    FILTER_DELAY,
    FILTER_REVERB,
    FILTER_COUNT = 3
};

enum EncoderMode {
    ENC_MODE_VOLUME,
    ENC_MODE_PITCH,
    ENC_MODE_PAN,
    ENC_MODE_EFFECT
};

enum VolumeMode {
    VOL_SEQUENCER,
    VOL_LIVE_PADS
};

enum FxResponseMode {
    FX_MODE_PRECISION = 0,
    FX_MODE_LIVE
};

enum ByteButtonAction {
    BB_ACTION_MENU = 0,
    BB_ACTION_VOL_MODE,
    BB_ACTION_SCREEN_LIVE,
    BB_ACTION_SCREEN_SEQUENCER,
    BB_ACTION_SCREEN_PERFORMANCE,  // was SCREEN_FILTERS (removed)
    BB_ACTION_SCREEN_VOLUMES,
    BB_ACTION_FX_CLEAN,
    BB_ACTION_FX_SPACE,
    BB_ACTION_FX_ACID,
    BB_ACTION_FX_DESTROY,
    BB_ACTION_FX_TARGET_PREV,
    BB_ACTION_FX_TARGET_NEXT,
    BB_ACTION_PATTERN_PREV,
    BB_ACTION_PATTERN_NEXT,
    BB_ACTION_PLAY_PAUSE,
    BB_ACTION_SCREEN_PATTERNS,     // BB2: go to Patterns screen
    BB_ACTION_SCREEN_SETTINGS,     // BB2: go to Settings screen
    BB_ACTION_SCREEN_SDCARD,       // BB2: go to SD Card screen
    BB_ACTION_BPM_UP,              // BB2: BPM +1
    BB_ACTION_BPM_DOWN,            // BB2: BPM -1
    BB_ACTION_COUNT
};

// =============================================================================
// STRUCTS
// =============================================================================

struct TrackFilter {
    bool enabled;
    uint8_t delayAmount;    // 0-127
    uint8_t flangerAmount;  // 0-127
    uint8_t compAmount;     // 0-127
};

struct Pattern {
    bool steps[Config::MAX_TRACKS][Config::MAX_STEPS];
    bool muted[Config::MAX_TRACKS];
    int  length = Config::STEPS_PER_BANK;  // actual pattern length (16/32/48/64)
    String name;
};

struct DrumKit {
    String name;
    int folder;
};

struct DiagnosticInfo {
    bool wifiOk;
    bool udpConnected;
    bool touchOk;
    bool lcdOk;
    bool sdOk;
    bool i2cHubOk;
    bool m5encoder1Ok;
    bool m5encoder2Ok;
    bool dfrobot1Ok;
    bool dfrobot2Ok;
    bool dfrobot3Ok;
    bool dfrobot4Ok;
    bool dfrobotPotsOk;
    bool byteButton1Ok;
    bool byteButton2Ok;
    String lastError;
};

// =============================================================================
// GLOBAL STATE (extern declarations)
// =============================================================================

// Screen
extern volatile Screen currentScreen;
extern Screen previousScreen;
extern bool needsFullRedraw;

// Sequencer
extern Pattern patterns[];
extern int currentPattern;
extern int currentStep;
extern int selectedTrack;
extern bool isPlaying;
extern int currentBPM;
extern float currentBPMPrecise;
extern int currentKit;

// Volume
extern int masterVolume;
extern int sequencerVolume;
extern int livePadsVolume;
extern volatile int livePadRepeatCount;
extern int trackVolumes[];
extern bool trackMuted[];
extern bool trackSolo[];
extern bool livePadPressed[];
extern unsigned long livePadFlashUntilMs[];
extern bool byteButtonLivePressed[];
extern uint8_t prevByteButtonState[];  // per-module ByteButton bitmask state for edge detection
extern volatile uint32_t pendingLivePadTriggerMask;
extern volatile bool livePadsVisualDirty;
extern VolumeMode volumeMode;

// Filters
extern TrackFilter trackFilters[];
extern TrackFilter masterFilter;
extern int filterSelectedTrack;   // -1 = MASTER
extern int filterSelectedFX;
extern int fxFilterType;
extern int fxFilterCutoffHz;
extern int fxFilterResonanceX10;
extern int fxBitCrushBits;
extern int fxDistortionPercent;
extern int fxSampleRateHz;
extern EncoderMode encoderMode;
extern volatile FxResponseMode fxResponseMode;

// I2C Hub
extern int m5HubChannel[];
extern int dfRobotHubChannel[];
extern int byteButtonHubChannel[];
extern int dfRobotPotHubChannel;
extern uint8_t dfRobotPotAddr;
extern uint8_t dfRobotPotMidi[];
extern uint8_t dfRobotPotPos[];
extern uint8_t dfFxParamMode[];
extern int dfFxParamValue[];
extern bool dfFxMuted[];
extern bool analogFxMuted[];
extern bool byteButtonConnected[];
extern bool hubDetected;
extern uint8_t byteButtonActionMap[];
extern const char* const byteButtonActionNames[];

// Connection
extern bool udpConnected;
extern bool wifiConnected;
#if S3_WIFI_ENABLED
extern bool wifiReconnecting;
#endif
extern bool masterConnected;

// Diagnostic
extern DiagnosticInfo diagInfo;

// Runtime metrics
extern volatile uint32_t uiUpdateCount;
extern volatile uint32_t uiSkippedCount;
extern volatile uint32_t uiLastIntervalMs;
extern volatile uint32_t udpRxCount;
extern volatile uint32_t udpJsonErrorCount;
#if S3_WIFI_ENABLED
extern unsigned long lastWiFiConnectedMs;
#endif
extern unsigned long lastMasterPacketMs;
extern unsigned long lastStepUpdateMs;
extern unsigned long lastLocalStepMs;

// Track names
extern const char* trackNames[];
extern const char* instrumentNames[];
extern const char* kitNames[];
