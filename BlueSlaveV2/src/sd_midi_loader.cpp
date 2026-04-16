// =============================================================================
// sd_midi_loader.cpp — Minimal SMF drum parser (GM channel 10)
// =============================================================================
#include "sd_midi_loader.h"
#include <SD_MMC.h>
#include <cstring>
#include <Arduino.h>

// ---------------------------------------------------------------------------
// GM drum note → our track index (offset table for MIDI notes 35-59)
// 0xFF = note ignored
// ---------------------------------------------------------------------------
static const uint8_t NOTE_MAP[25] = {
    //35  36   37  38   39  40  41   42  43   44  45   46  47   48  49   50  51   52  53   54  55   56  57    58   59
      0,   0,  12,  1,  13,  1,  6,   2,  7,   3,  8,   4,  9,  10,  5,  11, 14,   5, 14,  15,  5,  15,  5, 0xFF, 14
};

static inline uint8_t gm_note_to_track(uint8_t note) {
    if (note >= 35 && note <= 59) return NOTE_MAP[note - 35];
    if (note >= 60 && note <= 81) return 15;  // congas, bongos, etc. → misc
    return 0xFF;
}

// ---------------------------------------------------------------------------
// File-scope reader state (single-threaded, user-triggered only)
// ---------------------------------------------------------------------------
static File   s_f;
static bool   s_err = false;

static uint8_t  readU8()  { uint8_t b = 0; if (s_f.read(&b, 1) != 1) s_err = true; return b; }
static uint16_t readU16() { uint8_t a = readU8(), b2 = readU8(); return (uint16_t)((a << 8) | b2); }
static uint32_t readU32() { uint16_t a = readU16(), b2 = readU16(); return ((uint32_t)a << 16) | b2; }

static uint32_t readVLQ() {
    uint32_t v = 0;
    for (int i = 0; i < 4 && !s_err; i++) {
        uint8_t b = readU8();
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

static void skipN(uint32_t n) {
    if (n == 0) return;
    s_f.seek(s_f.position() + n);
}

// ---------------------------------------------------------------------------
// Parse one MTrk chunk
// ---------------------------------------------------------------------------
static void parseTrack(uint32_t len, int tpq, bool steps[16][16], int* found) {
    uint32_t end = (uint32_t)s_f.position() + len;
    uint32_t tick = 0;
    uint8_t  status = 0;

    while (!s_err && (uint32_t)s_f.position() < end) {
        tick += readVLQ();

        uint8_t b = readU8();

        // Meta event
        if (b == 0xFF) {
            readU8();               // meta type
            uint32_t ml = readVLQ();
            skipN(ml);
            continue;
        }
        // Sysex
        if (b == 0xF0 || b == 0xF7) {
            uint32_t sl = readVLQ();
            skipN(sl);
            continue;
        }

        // Regular MIDI event — handle running status
        uint8_t d1;
        if (b & 0x80) {
            status = b;
            d1 = readU8();
        } else {
            d1 = b;  // running status; b IS data byte 1
        }

        uint8_t ev = status & 0xF0;
        uint8_t ch = status & 0x0F;

        if (ev == 0x90 || ev == 0x80) {
            uint8_t vel = readU8();
            bool is_on = (ev == 0x90 && vel > 0);
            if (is_on && ch == 9) {  // GM drums = channel 10 (0-indexed = 9)
                uint8_t trk = gm_note_to_track(d1);
                if (trk != 0xFF && tpq > 0) {
                    // ticks_per_16th = tpq / 4
                    // step = tick / ticks_per_16th = tick * 4 / tpq
                    uint32_t step = (tick * 4u) / (uint32_t)tpq;
                    steps[trk][step % 16] = true;
                    if (found) (*found)++;
                }
            }
        } else if (ev == 0xA0 || ev == 0xB0 || ev == 0xE0) {
            readU8();  // two-byte messages: consume 2nd data byte
        }
        // 0xC0, 0xD0: single data byte (d1 already consumed)
    }

    s_f.seek(end);  // ensure exact position at track end
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool midi_load_pattern(const char* path, bool steps[16][16],
                       char* name_out, int name_max, int* steps_found_out) {
    memset(steps, 0, 16 * 16 * sizeof(bool));

    // Extract filename (without extension) as pattern name
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (name_out && name_max > 0) {
        strncpy(name_out, base, name_max - 1);
        name_out[name_max - 1] = '\0';
        char* dot = strrchr(name_out, '.');
        if (dot) *dot = '\0';
        if ((int)strlen(name_out) > 8) name_out[8] = '\0';  // max 8 chars for pattern name
    }

    s_f = SD_MMC.open(path);
    s_err = false;
    if (!s_f) return false;

    // MThd header
    char magic[4];
    if (s_f.read((uint8_t*)magic, 4) != 4 || strncmp(magic, "MThd", 4) != 0) {
        s_f.close(); return false;
    }
    uint32_t hdr_len  = readU32();
    uint16_t fmt      = readU16();   (void)fmt;
    uint16_t ntracks  = readU16();
    uint16_t tpq      = readU16();
    if (tpq & 0x8000) { s_f.close(); return false; }  // SMPTE timecode not supported
    if (tpq == 0) tpq = 96;
    if (hdr_len > 6) skipN(hdr_len - 6);

    int total = 0;
    for (int t = 0; t < ntracks && !s_err; t++) {
        char tmagic[4];
        if (s_f.read((uint8_t*)tmagic, 4) != 4) break;
        if (strncmp(tmagic, "MTrk", 4) != 0) break;
        uint32_t tlen = readU32();
        parseTrack(tlen, (int)tpq, steps, &total);
    }

    s_f.close();
    if (steps_found_out) *steps_found_out = total;
    return total > 0;
}
