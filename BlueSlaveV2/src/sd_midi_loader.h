// =============================================================================
// sd_midi_loader.h — Minimal MIDI file parser for drum patterns
// Reads Standard MIDI Format (.mid) from SD_MMC and extracts a step grid
// of up to MAX_STEPS 16th-note steps (up to 4 bars) per pattern.
//
// Track mapping (GM standard drums):
//   0=Kick  1=Snare  2=Closed HH  3=Pedal HH  4=Open HH  5=Crash
//   6=LowTom  7=HiTom  8=MidTom  9=LMTom  10=HMTom  11=HTom
//  12=SideStick  13=Clap  14=Ride  15=Misc
// =============================================================================
#pragma once
#include <cstdint>
#include "config.h"

// Parse MIDI file at `path` (SD_MMC).
// Fills steps[track][step] collecting all matching MIDI events.
// name_out receives filename without extension (max name_max chars).
// bpm_out  receives the tempo from the first Set Tempo meta-event (0 if none).
// length_out receives the actual pattern length (16/32/48/64 steps = 1-4 bars).
//
// midi_channel:
//   9    (default) — GM drum mode: channel 10 only, uses GM drum note map
//  -1              — all channels: ch.10 uses GM map, others use note%16
//   0-15           — one specific channel only, notes mapped via note%16
//
// Returns true if at least one drum step was found.
bool midi_load_pattern(const char* path,
                       bool        steps[Config::MAX_TRACKS][Config::MAX_STEPS],
                       char*       name_out,
                       int         name_max,
                       int*        steps_found_out = nullptr,
                       float*      bpm_out         = nullptr,
                       int         midi_channel    = 9,
                       int*        length_out      = nullptr);
