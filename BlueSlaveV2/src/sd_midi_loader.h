// =============================================================================
// sd_midi_loader.h — Minimal MIDI file parser for drum patterns
// Reads Standard MIDI Format (.mid) from SD_MMC and extracts a 16×16 step grid
// based on GM channel 10 (channel index 9) drum note assignments.
//
// Track mapping (GM standard drums):
//   0=Kick  1=Snare  2=Closed HH  3=Pedal HH  4=Open HH  5=Crash
//   6=LowTom  7=HiTom  8=MidTom  9=LMTom  10=HMTom  11=HTom
//  12=SideStick  13=Clap  14=Ride  15=Misc
// =============================================================================
#pragma once
#include <cstdint>

// Parse MIDI file at `path` (SD_MMC).
// Fills steps[track][step] for the first 16 steps (one bar of 16th notes).
// Notes beyond one bar are wrapped modulo 16.
// name_out receives filename without extension (max name_max chars).
// Returns true if at least one drum step was found.
bool midi_load_pattern(const char* path,
                       bool        steps[16][16],
                       char*       name_out,
                       int         name_max,
                       int*        steps_found_out = nullptr);
