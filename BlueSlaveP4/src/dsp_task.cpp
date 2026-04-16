// =============================================================================
// dsp_task.cpp — DSP processing task for ESP32-P4
// Runs on Core 0, produces spectrum data consumed by UI on Core 1
// =============================================================================

#include "dsp_task.h"
#include "../include/config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// =============================================================================
// CONFIGURATION
// =============================================================================
static constexpr int DSP_TASK_STACK   = 4096;
static constexpr int DSP_TASK_PRIO    = 3;       // below LVGL (5), above idle
static constexpr int DSP_CORE         = 0;       // Core 0 — keep Core 1 for UI
static constexpr int DSP_TICK_MS      = 16;      // ~60 Hz processing
static constexpr int SPECTRUM_BARS    = 16;
static constexpr uint8_t DECAY_RATE   = 6;       // amplitude units per tick

// =============================================================================
// SHARED STATE
// =============================================================================
static SpectrumData s_spectrum = {};
static volatile float s_bpm = 120.0f;

// Per-pad trigger energy (written from any core, read from DSP core)
static volatile uint8_t s_padEnergy[16] = {};

// =============================================================================
// PUBLIC API
// =============================================================================
const SpectrumData& dsp_get_spectrum() {
    return s_spectrum;
}

void dsp_notify_pad(uint8_t pad, uint8_t velocity) {
    if (pad < 16) {
        s_padEnergy[pad] = velocity;
    }
}

void dsp_set_bpm(float bpm) {
    s_bpm = bpm;
}

// =============================================================================
// DSP TASK — runs continuously on Core 0
// =============================================================================
static void dsp_task_func(void* /*arg*/) {
    TickType_t xLastWake = xTaskGetTickCount();

    for (;;) {
        bool changed = false;

        for (int i = 0; i < SPECTRUM_BARS; i++) {
            // Consume pad trigger energy
            uint8_t energy = s_padEnergy[i];
            if (energy > 0) {
                s_padEnergy[i] = 0;
                if (energy > s_spectrum.bars[i]) {
                    s_spectrum.bars[i] = energy;
                    changed = true;
                }
            }

            // Exponential-ish decay
            if (s_spectrum.bars[i] > DECAY_RATE) {
                s_spectrum.bars[i] -= DECAY_RATE;
                changed = true;
            } else if (s_spectrum.bars[i] > 0) {
                s_spectrum.bars[i] = 0;
                changed = true;
            }
        }

        s_spectrum.dirty = changed;

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(DSP_TICK_MS));
    }
}

// =============================================================================
// INIT
// =============================================================================
void dsp_task_init() {
    xTaskCreatePinnedToCore(
        dsp_task_func,
        "dsp",
        DSP_TASK_STACK,
        nullptr,
        DSP_TASK_PRIO,
        nullptr,
        DSP_CORE
    );
    P4_LOG_PRINTLN("[DSP] Task started on Core 0");
}
