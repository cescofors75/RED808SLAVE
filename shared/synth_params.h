// =============================================================================
// shared/synth_params.h
// Read-only parameter & preset definitions for PIANO synth engines.
// Used by both BlueSlaveV2 (ESP32-S3) and BlueSlaveP4 to render
// the PIANO PARAMS LVGL screen and emit UDP {synth303Param|synthParam|synthPreset}.
//
// Mirrors RedMaster_ESP32S3/data/web/synth-editor.js (engines 3-6 only).
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

// Engine indices (compatible with Daisy synthNoteOnEx / synthParam)
//   0 = 808 sampler   1 = 909 sampler   2 = 505 sampler   (NOT exposed here)
//   3 = TB-303         4 = Wavetable    5 = SH-101         6 = FM 2-Op

#define SP_ENGINE_303    3
#define SP_ENGINE_WT     4
#define SP_ENGINE_SH101  5
#define SP_ENGINE_FM2OP  6

#define SP_ENGINE_COUNT  4   // engines 3..6

typedef struct {
    uint8_t      param_id;
    const char*  name;
    float        vmin;
    float        vmax;
    float        vdef;
    uint8_t      step_int;   // 1 if integer-stepped (waveform select etc.)
    const char*  unit;
} SynthParamDef;

typedef struct {
    uint8_t  param_id;
    float    value;
} SynthPresetVal;

typedef struct {
    const char*           name;
    const SynthPresetVal* values;
    uint8_t               count;
} SynthPreset;

typedef struct {
    uint8_t              engine;        // SP_ENGINE_*
    const char*          label;         // "303" / "WT" / "SH101" / "FM2"
    const char*          long_name;     // "TB-303 Bass" etc
    const SynthParamDef* params;
    uint8_t              param_count;
    const SynthPreset*   presets;
    uint8_t              preset_count;
} SynthEngineDef;

// ---------------------------------------------------------------------------
// 303 — 15 params
// ---------------------------------------------------------------------------
static const SynthParamDef SP_PARAMS_303[] = {
    { 0,  "Cutoff",     20.f,    5000.f, 800.f,   0, "Hz" },
    { 1,  "Resonance",  0.f,     0.97f,  0.5f,    0, "" },
    { 2,  "Env Mod",    0.f,     1.f,    0.5f,    0, "" },
    { 3,  "Decay",      0.02f,   3.f,    0.3f,    0, "s" },
    { 8,  "Attack",     0.001f,  2.f,    0.001f,  0, "s" },
    { 9,  "Sustain",    0.f,     1.f,    0.f,     0, "" },
    { 10, "Release",    0.005f,  2.f,    0.05f,   0, "s" },
    { 4,  "Accent",     0.f,     1.f,    0.5f,    0, "" },
    { 5,  "Slide",      0.01f,   0.5f,   0.06f,   0, "s" },
    { 11, "Overdrive",  0.f,     1.f,    0.f,     0, "" },
    { 12, "Sub Osc",    0.f,     1.f,    0.f,     0, "" },
    { 13, "Drift",      0.f,     1.f,    0.f,     0, "" },
    { 14, "PitchBend", -12.f,    12.f,   0.f,     0, "st" },
    { 6,  "Wave",       0.f,     1.f,    0.f,     1, "" },   // 0=SAW 1=SQR
    { 7,  "Volume",     0.f,     1.f,    0.7f,    0, "" },
};

static const SynthPresetVal SP_PRES_303_0[] = {
    {0,1200.f},{1,0.72f},{2,0.65f},{3,0.35f},{4,0.60f},{5,0.09f},{6,0},{7,0.80f},
    {8,0.001f},{9,0},{10,0.15f},{11,0.12f},{12,0.08f},{13,0.04f},{14,0}};
static const SynthPresetVal SP_PRES_303_1[] = {
    {0,900.f},{1,0.92f},{2,0.95f},{3,0.45f},{4,0.85f},{5,0.12f},{6,0},{7,0.85f},
    {8,0.001f},{9,0},{10,0.18f},{11,0.28f},{12,0.06f},{13,0.08f},{14,0}};
static const SynthPresetVal SP_PRES_303_2[] = {
    {0,240.f},{1,0.45f},{2,0.25f},{3,0.60f},{4,0.25f},{5,0.06f},{6,1.f},{7,0.90f},
    {8,0.004f},{9,0.45f},{10,0.35f},{11,0.18f},{12,0.45f},{13,0.02f},{14,0}};
static const SynthPresetVal SP_PRES_303_3[] = {
    {0,2200.f},{1,0.58f},{2,0.40f},{3,0.80f},{4,0.35f},{5,0.15f},{6,1.f},{7,0.75f},
    {8,0.010f},{9,0.35f},{10,0.40f},{11,0.08f},{12,0.18f},{13,0.12f},{14,0}};

static const SynthPreset SP_PRESETS_303[] = {
    { "Acid",      SP_PRES_303_0, 15 },
    { "Squelch",   SP_PRES_303_1, 15 },
    { "Sub Bass",  SP_PRES_303_2, 15 },
    { "Soft Lead", SP_PRES_303_3, 15 },
};

// ---------------------------------------------------------------------------
// WT — 8 params
// ---------------------------------------------------------------------------
static const SynthParamDef SP_PARAMS_WT[] = {
    { 0, "Wave Pos",   0.f,    7.f,    0.f,     0, ""   },
    { 1, "Attack",     0.f,    2000.f, 5.f,     1, "ms" },
    { 2, "Decay",      1.f,    4000.f, 300.f,   1, "ms" },
    { 3, "Volume",     0.f,    1.f,    0.75f,   0, ""   },
    { 4, "Filter Cut", 0.f,    18000.f,8000.f,  1, "Hz" },
    { 5, "LFO Rate",   0.01f,  20.f,   2.f,     0, "Hz" },
    { 6, "LFO Depth",  0.f,    1.f,    0.f,     0, ""   },
    { 7, "LFO Target", 0.f,    2.f,    0.f,     1, ""   }, // 0=Wave 1=Pitch 2=Vol
};

static const SynthPresetVal SP_PRES_WT_0[] = { {0,1.2f},{1,30},{2,900},{3,0.75f},{4,6500},{5,0.20f},{6,0.15f},{7,2} };
static const SynthPresetVal SP_PRES_WT_1[] = { {0,2.7f},{1,0},{2,260},{3,0.82f},{4,4200},{5,5.20f},{6,0.08f},{7,1} };
static const SynthPresetVal SP_PRES_WT_2[] = { {0,6.0f},{1,8},{2,1200},{3,0.78f},{4,9000},{5,0.90f},{6,0.30f},{7,2} };
static const SynthPresetVal SP_PRES_WT_3[] = { {0,4.0f},{1,0},{2,320},{3,0.85f},{4,2400},{5,3.50f},{6,0.12f},{7,0} };
static const SynthPreset SP_PRESETS_WT[] = {
    { "Pad",      SP_PRES_WT_0, 8 },
    { "Pluck",    SP_PRES_WT_1, 8 },
    { "Organ",    SP_PRES_WT_2, 8 },
    { "PWM Bass", SP_PRES_WT_3, 8 },
};

// ---------------------------------------------------------------------------
// SH101 — 19 params (skips paramId 3, that's why they go up to 19)
// ---------------------------------------------------------------------------
static const SynthParamDef SP_PARAMS_SH101[] = {
    { 0,  "Wave",       0.f,    2.f,    0.f,     1, "" },     // SAW/SQR/PUL
    { 1,  "PWM Width",  0.1f,   0.9f,   0.5f,    0, "" },
    { 2,  "Sub Lvl",    0.f,    1.f,    0.3f,    0, "" },
    { 4,  "VCF Cut",    20.f,   18000.f,2000.f,  1, "Hz" },
    { 5,  "VCF Res",    0.f,    1.f,    0.4f,    0, "" },
    { 6,  "Env→VCF",    0.f,    1.f,    0.5f,    0, "" },
    { 7,  "VCA Atk",    0.001f, 2.f,    0.005f,  0, "s" },
    { 8,  "VCA Dec",    0.01f,  3.f,    0.3f,    0, "s" },
    { 9,  "VCA Sus",    0.f,    1.f,    0.6f,    0, "" },
    { 10, "VCA Rel",    0.005f, 3.f,    0.15f,   0, "s" },
    { 11, "VCF Atk",    0.001f, 2.f,    0.005f,  0, "s" },
    { 12, "VCF Dec",    0.01f,  3.f,    0.2f,    0, "s" },
    { 13, "LFO Rate",   0.1f,   20.f,   4.f,     0, "Hz" },
    { 14, "LFO Depth",  0.f,    1.f,    0.f,     0, "" },
    { 15, "LFO Tgt",    0.f,    2.f,    0.f,     1, "" },     // Pitch/Cut/PWM
    { 16, "LFO Wave",   0.f,    3.f,    0.f,     1, "" },     // SIN/TRI/SQR/SAW
    { 17, "Portament",  0.f,    1.f,    0.f,     0, "" },
    { 18, "A-Drift",    0.f,    1.f,    0.1f,    0, "" },
    { 19, "Volume",     0.f,    1.f,    0.75f,   0, "" },
};

static const SynthPresetVal SP_PRES_SH_0[] = {
    {0,0},{1,0.50f},{2,0.72f},{4,650.f},{5,0.25f},{6,0.55f},{7,0.001f},{8,0.18f},
    {9,0},{10,0.08f},{11,0.001f},{12,0.14f},{13,0.10f},{14,0},{15,0},{16,0},
    {17,0.05f},{18,0.04f},{19,0.85f}};
static const SynthPresetVal SP_PRES_SH_1[] = {
    {0,0},{1,0.42f},{2,0.20f},{4,1800.f},{5,0.70f},{6,0.75f},{7,0.001f},{8,0.35f},
    {9,0.25f},{10,0.18f},{11,0.001f},{12,0.25f},{13,5.50f},{14,0.18f},{15,1.f},
    {16,0},{17,0.12f},{18,0.07f},{19,0.80f}};
static const SynthPresetVal SP_PRES_SH_2[] = {
    {0,1.f},{1,0.28f},{2,0.15f},{4,2600.f},{5,0.35f},{6,0.45f},{7,0.010f},{8,0.40f},
    {9,0.55f},{10,0.28f},{11,0.010f},{12,0.45f},{13,3.20f},{14,0.32f},{15,0},
    {16,1.f},{17,0},{18,0.03f},{19,0.78f}};
static const SynthPresetVal SP_PRES_SH_3[] = {
    {0,2.f},{1,0.50f},{2,0.35f},{4,1200.f},{5,0.82f},{6,0.60f},{7,0.120f},{8,1.20f},
    {9,0.75f},{10,1.00f},{11,0.080f},{12,1.60f},{13,0.35f},{14,0.40f},{15,1.f},
    {16,0},{17,0.18f},{18,0.15f},{19,0.72f}};
static const SynthPreset SP_PRESETS_SH101[] = {
    { "Bass",   SP_PRES_SH_0, 19 },
    { "Acid",   SP_PRES_SH_1, 19 },
    { "Keys",   SP_PRES_SH_2, 19 },
    { "Drone",  SP_PRES_SH_3, 19 },
};

// ---------------------------------------------------------------------------
// FM 2-Op — 15 params
// ---------------------------------------------------------------------------
static const SynthParamDef SP_PARAMS_FM2OP[] = {
    { 0,  "C Atk",      0.001f, 2.f,    0.005f,  0, "s" },
    { 1,  "C Dec",      0.01f,  3.f,    0.4f,    0, "s" },
    { 2,  "C Sus",      0.f,    1.f,    0.3f,    0, "" },
    { 3,  "C Rel",      0.005f, 3.f,    0.2f,    0, "s" },
    { 4,  "M Atk",      0.001f, 2.f,    0.001f,  0, "s" },
    { 5,  "M Dec",      0.01f,  3.f,    0.25f,   0, "s" },
    { 6,  "M Sus",      0.f,    1.f,    0.f,     0, "" },
    { 7,  "M Rel",      0.005f, 3.f,    0.1f,    0, "s" },
    { 8,  "M/C Ratio",  0.5f,   8.f,    2.f,     0, "x" },
    { 9,  "FM Index",   0.f,    12.f,   3.f,     0, "" },
    { 10, "Feedback",   0.f,    1.f,    0.f,     0, "" },
    { 11, "Algorithm",  0.f,    2.f,    0.f,     1, "" }, // FM/ADD/RING
    { 12, "Detune",    -1.f,    1.f,    0.f,     0, "st" },
    { 13, "VelSens",    0.f,    1.f,    0.7f,    0, "" },
    { 14, "Volume",     0.f,    1.f,    0.75f,   0, "" },
};

static const SynthPresetVal SP_PRES_FM_0[] = {
    {0,0.001f},{1,0.30f},{2,0},{3,0.12f},{4,0.001f},{5,0.22f},{6,0},{7,0.15f},
    {8,1.00f},{9,5.50f},{10,0.08f},{11,0},{12,0},{13,0.40f},{14,0.85f}};
static const SynthPresetVal SP_PRES_FM_1[] = {
    {0,0.001f},{1,1.40f},{2,0.15f},{3,1.10f},{4,0.001f},{5,0.90f},{6,0},{7,0.60f},
    {8,2.00f},{9,3.20f},{10,0.05f},{11,1.f},{12,0.8f},{13,0.75f},{14,0.80f}};
static const SynthPresetVal SP_PRES_FM_2[] = {
    {0,0.001f},{1,2.60f},{2,0},{3,1.80f},{4,0.001f},{5,1.40f},{6,0},{7,1.00f},
    {8,3.00f},{9,8.50f},{10,0.12f},{11,0},{12,1.5f},{13,0.85f},{14,0.75f}};
static const SynthPresetVal SP_PRES_FM_3[] = {
    {0,0.005f},{1,0.50f},{2,0.35f},{3,0.25f},{4,0.001f},{5,0.40f},{6,0.20f},{7,0.30f},
    {8,1.50f},{9,10.50f},{10,0.50f},{11,2.f},{12,7.f},{13,0.60f},{14,0.82f}};
static const SynthPreset SP_PRESETS_FM2OP[] = {
    { "FM Bass",  SP_PRES_FM_0, 15 },
    { "EPiano",   SP_PRES_FM_1, 15 },
    { "Bell",     SP_PRES_FM_2, 15 },
    { "Growl",    SP_PRES_FM_3, 15 },
};

// ---------------------------------------------------------------------------
// Engine table
// ---------------------------------------------------------------------------
static const SynthEngineDef SP_ENGINES[SP_ENGINE_COUNT] = {
    { SP_ENGINE_303,   "303",   "TB-303 Bass",
      SP_PARAMS_303,   (uint8_t)(sizeof(SP_PARAMS_303)/sizeof(SP_PARAMS_303[0])),
      SP_PRESETS_303,  4 },
    { SP_ENGINE_WT,    "WT",    "Wavetable OSC",
      SP_PARAMS_WT,    (uint8_t)(sizeof(SP_PARAMS_WT)/sizeof(SP_PARAMS_WT[0])),
      SP_PRESETS_WT,   4 },
    { SP_ENGINE_SH101, "SH101", "SH-101 Lead",
      SP_PARAMS_SH101, (uint8_t)(sizeof(SP_PARAMS_SH101)/sizeof(SP_PARAMS_SH101[0])),
      SP_PRESETS_SH101,4 },
    { SP_ENGINE_FM2OP, "FM2",   "FM 2-Op",
      SP_PARAMS_FM2OP, (uint8_t)(sizeof(SP_PARAMS_FM2OP)/sizeof(SP_PARAMS_FM2OP[0])),
      SP_PRESETS_FM2OP,4 },
};
