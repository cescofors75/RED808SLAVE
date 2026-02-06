/**
 * @file system_state.h
 * @brief Estructuras de estado del sistema RED808 V5
 * @author RED808 Team
 * @date 2026-02-06
 * 
 * Organiza todas las variables globales en estructuras lógicas
 * Mejora la mantenibilidad y claridad del código
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>
#include "config.h"

// ============================================
// ENUMS
// ============================================

enum Screen {
    SCREEN_BOOT,
    SCREEN_MENU,
    SCREEN_LIVE,
    SCREEN_SEQUENCER,
    SCREEN_SETTINGS,
    SCREEN_DIAGNOSTICS,
    SCREEN_PATTERNS,
    SCREEN_VOLUMES
};

enum DisplayMode {
    DISPLAY_BPM,
    DISPLAY_VOLUME,
    DISPLAY_PATTERN,
    DISPLAY_KIT,
    DISPLAY_INSTRUMENT,
    DISPLAY_STEP
};

enum SequencerView {
    SEQ_VIEW_GRID,
    SEQ_VIEW_CIRCULAR
};

enum VolumeMode {
    VOL_SEQUENCER,
    VOL_LIVE_PADS
};

enum EncoderMode {
    ENC_MODE_VOLUME,      // Volumen individual del track
    ENC_MODE_PITCH,       // Pitch/velocidad
    ENC_MODE_PAN,         // Paneo (futuro)
    ENC_MODE_EFFECT       // Efecto (futuro)
};

#endif // SYSTEM_STATE_H

