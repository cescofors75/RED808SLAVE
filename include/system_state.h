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
    SCREEN_VOLUMES,
    SCREEN_FILTERS,
    SCREEN_ENCODER_TEST
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

// ============================================
// FILTER FX TYPES
// ============================================
enum FilterType {
    FILTER_DELAY = 0,     // Delay/Echo
    FILTER_FLANGER,       // Flanger
    FILTER_COMPRESSOR,    // Compressor
    FILTER_COUNT          // Total = 3
};

// Estructura para un filtro FX aplicado a un track (1 param "amount" por FX)
struct TrackFilter {
    bool enabled;            // Filtro activo
    uint8_t delayAmount;     // Delay/Echo: cantidad (0-127)
    uint8_t flangerAmount;   // Flanger: cantidad (0-127)
    uint8_t compAmount;      // Compressor: cantidad (0-127)
};

#endif // SYSTEM_STATE_H

