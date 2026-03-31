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
    SCREEN_SPECTRUM,
    SCREEN_PERFORMANCE,
    SCREEN_SAMPLES,
    SCREEN_SEQ_CIRCLE,
    SCREEN_ENCODER_TEST
};

enum FilterType {
    FILTER_DELAY = 0,
    FILTER_FLANGER,
    FILTER_COMPRESSOR,
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
    bool byteButtonOk;
    String lastError;
};

// =============================================================================
// GLOBAL STATE (extern declarations)
// =============================================================================

// Screen
extern Screen currentScreen;
extern Screen previousScreen;
extern bool needsFullRedraw;

// Sequencer
extern Pattern patterns[];
extern int currentPattern;
extern int currentStep;
extern int selectedTrack;
extern bool isPlaying;
extern int currentBPM;
extern int currentKit;

// Volume
extern int masterVolume;
extern int sequencerVolume;
extern int livePadsVolume;
extern int trackVolumes[];
extern bool trackMuted[];
extern bool livePadPressed[];
extern bool byteButtonLivePressed[];
extern uint8_t prevByteButtonState;  // ByteButton bitmask state for edge detection
extern volatile uint32_t pendingLivePadTriggerMask;
extern VolumeMode volumeMode;

// Filters
extern TrackFilter trackFilters[];
extern TrackFilter masterFilter;
extern int filterSelectedTrack;   // -1 = MASTER
extern int filterSelectedFX;
extern EncoderMode encoderMode;
extern int analogFxPreset;        // 0=OFF, 1..11=FX preset (analog rotary)

// I2C Hub
extern int m5HubChannel[];
extern int dfRobotHubChannel[];
extern bool hubDetected;

// Connection
extern bool udpConnected;
extern bool wifiConnected;
extern bool masterConnected;

// Diagnostic
extern DiagnosticInfo diagInfo;

// Track names
extern const char* trackNames[];
extern const char* instrumentNames[];
extern const char* kitNames[];
