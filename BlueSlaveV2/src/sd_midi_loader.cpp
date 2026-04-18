// =============================================================================
// sd_midi_loader.cpp — Minimal SMF drum parser (GM channel 10)
// =============================================================================
#include "sd_midi_loader.h"
#include <SD_MMC.h>
#include <cstring>
#include <Arduino.h>
#include <esp_heap_caps.h>

// ---------------------------------------------------------------------------
// GM drum note → our track index
// Track layout (matches trackNames[] in main.cpp):
//   0:BD  1:SD  2:CH  3:OH  4:CP  5:CB  6:RS  7:CL
//   8:MA  9:CY  10:HT 11:LT 12:MC 13:MT 14:HC 15:LC
// 0xFF = note ignored
// ---------------------------------------------------------------------------
static const uint8_t NOTE_MAP[47] = {
    // 35 Acoustic Bass Drum  -> BD
    /*35*/ 0,
    // 36 Bass Drum 1         -> BD
    /*36*/ 0,
    // 37 Side Stick          -> RS
    /*37*/ 6,
    // 38 Acoustic Snare      -> SD
    /*38*/ 1,
    // 39 Hand Clap           -> CP
    /*39*/ 4,
    // 40 Electric Snare      -> SD
    /*40*/ 1,
    // 41 Low Floor Tom       -> LT
    /*41*/ 11,
    // 42 Closed Hi-Hat       -> CH
    /*42*/ 2,
    // 43 High Floor Tom      -> LT
    /*43*/ 11,
    // 44 Pedal Hi-Hat        -> CH (no pedal track, fold to closed)
    /*44*/ 2,
    // 45 Low Tom             -> LT
    /*45*/ 11,
    // 46 Open Hi-Hat         -> OH
    /*46*/ 3,
    // 47 Low-Mid Tom         -> MT
    /*47*/ 13,
    // 48 Hi-Mid Tom          -> HT
    /*48*/ 10,
    // 49 Crash Cymbal 1      -> CY
    /*49*/ 9,
    // 50 High Tom            -> HT
    /*50*/ 10,
    // 51 Ride Cymbal 1       -> CY
    /*51*/ 9,
    // 52 Chinese Cymbal      -> CY
    /*52*/ 9,
    // 53 Ride Bell           -> CY
    /*53*/ 9,
    // 54 Tambourine          -> MA (use shaker slot)
    /*54*/ 8,
    // 55 Splash Cymbal       -> CY
    /*55*/ 9,
    // 56 Cowbell             -> CB
    /*56*/ 5,
    // 57 Crash Cymbal 2      -> CY
    /*57*/ 9,
    // 58 Vibraslap           -> CL
    /*58*/ 7,
    // 59 Ride Cymbal 2       -> CY
    /*59*/ 9,
    // 60 Hi Bongo            -> HC (high conga slot)
    /*60*/ 14,
    // 61 Low Bongo           -> LC
    /*61*/ 15,
    // 62 Mute Hi Conga       -> HC
    /*62*/ 14,
    // 63 Open Hi Conga       -> HC
    /*63*/ 14,
    // 64 Low Conga           -> LC
    /*64*/ 15,
    // 65 High Timbale        -> HT
    /*65*/ 10,
    // 66 Low Timbale         -> LT
    /*66*/ 11,
    // 67 High Agogo          -> CL
    /*67*/ 7,
    // 68 Low Agogo           -> CL
    /*68*/ 7,
    // 69 Cabasa              -> MA
    /*69*/ 8,
    // 70 Maracas             -> MA
    /*70*/ 8,
    // 71 Short Whistle       -> CL
    /*71*/ 7,
    // 72 Long Whistle        -> CL
    /*72*/ 7,
    // 73 Short Guiro         -> MA
    /*73*/ 8,
    // 74 Long Guiro          -> MA
    /*74*/ 8,
    // 75 Claves              -> CL
    /*75*/ 7,
    // 76 Hi Wood Block       -> CL
    /*76*/ 7,
    // 77 Lo Wood Block       -> CL
    /*77*/ 7,
    // 78 Mute Cuica          -> MC (mid conga slot)
    /*78*/ 12,
    // 79 Open Cuica          -> MC
    /*79*/ 12,
    // 80 Mute Triangle       -> CL
    /*80*/ 7,
    // 81 Open Triangle       -> CL
    /*81*/ 7,
};

static inline uint8_t gm_note_to_track(uint8_t note) {
    if (note >= 35 && note <= 81) return NOTE_MAP[note - 35];
    return 0xFF;
}

// ---------------------------------------------------------------------------
// File-scope reader state (single-threaded, user-triggered only)
// ---------------------------------------------------------------------------
static File          s_f;
static bool          s_err = false;
static unsigned long s_parse_start_ms = 0;
static constexpr unsigned long MIDI_PARSE_TIMEOUT_MS = 5000;  // hard ceiling

static inline bool parse_timed_out() {
    return (millis() - s_parse_start_ms) > MIDI_PARSE_TIMEOUT_MS;
}

static uint8_t  readU8()  { uint8_t b = 0; if (s_f.read(&b, 1) != 1) s_err = true; return b; }
static uint16_t readU16() { uint8_t a = readU8(), b2 = readU8(); return (uint16_t)((a << 8) | b2); }
static uint32_t readU32() { uint16_t a = readU16(), b2 = readU16(); return ((uint32_t)a << 16) | b2; }

// VLQ: at most 4 bytes per SMF spec. A 5th byte with continuation bit set is
// malformed; flag as error so the caller can stop parsing instead of
// corrupting the read position.
static uint32_t readVLQ() {
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4 && !s_err; i++) {
        uint8_t b = readU8();
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
    // Either we consumed 4 bytes whose 4th still had MSB set (malformed),
    // or we hit a read error. Either way: bail out.
    s_err = true;
    return v;
}

static void skipN(uint32_t n) {
    if (n == 0) return;
    s_f.seek(s_f.position() + n);
}

// ---------------------------------------------------------------------------
// Raw event buffer — filled during parseTrack, post-processed in midi_load_pattern
// Allocated in PSRAM (~32KB) to avoid reducing internal SRAM available for
// SD_MMC/LVGL/WiFi drivers. Previously 512 truncated medium/large MIDI files
// mid-parse; now 4096 covers full song themes.
// ---------------------------------------------------------------------------
#define MIDI_EVENT_BUF 4096
struct RawEvt { uint32_t tick; uint8_t trk; };
static RawEvt* s_evbuf = nullptr;
static int    s_evcount = 0;
static bool   s_evbuf_overflow = false;

static bool ensure_evbuf() {
    if (s_evbuf) return true;
    s_evbuf = (RawEvt*)heap_caps_malloc(sizeof(RawEvt) * MIDI_EVENT_BUF,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_evbuf) {
        // Fallback: try internal RAM (smaller, but still works)
        s_evbuf = (RawEvt*)malloc(sizeof(RawEvt) * MIDI_EVENT_BUF);
    }
    return s_evbuf != nullptr;
}

// Parse one MTrk chunk — collects matching note-on events into s_evbuf
// tempo_us_out: filled with microseconds-per-quarter-note if Set Tempo found
// midi_channel: 9=drums GM, -1=all channels, 0-14=specific ch
// ---------------------------------------------------------------------------
static void parseTrack(uint32_t len, int tpq, uint32_t* tempo_us_out, int midi_channel) {
    uint32_t end = (uint32_t)s_f.position() + len;
    uint32_t tick = 0;
    uint8_t  status = 0;

    while (!s_err && (uint32_t)s_f.position() < end) {
        if (parse_timed_out()) { s_err = true; break; }
        tick += readVLQ();
        if (s_err) break;

        uint8_t b = readU8();

        // Meta event
        if (b == 0xFF) {
            uint8_t meta_type = readU8();
            uint32_t ml = readVLQ();
            if (meta_type == 0x51 && ml == 3 && tempo_us_out && *tempo_us_out == 0) {
                uint32_t t = (uint32_t)readU8() << 16;
                t |= (uint32_t)readU8() << 8;
                t |= (uint32_t)readU8();
                if (t > 0) *tempo_us_out = t;
            } else {
                skipN(ml);
            }
            continue;
        }
        // Sysex
        if (b == 0xF0 || b == 0xF7) {
            skipN(readVLQ());
            continue;
        }

        // Regular MIDI event — handle running status
        uint8_t d1;
        if (b & 0x80) { status = b; d1 = readU8(); }
        else           { d1 = b; }

        uint8_t ev = status & 0xF0;
        uint8_t ch = status & 0x0F;

        if (ev == 0x90 || ev == 0x80) {
            uint8_t vel = readU8();
            bool is_on  = (ev == 0x90 && vel > 0);

            uint8_t trk = 0xFF;
            if (is_on) {
                if (ch == 9) {
                    if (midi_channel == 9 || midi_channel == -1)
                        trk = gm_note_to_track(d1);
                } else {
                    if (midi_channel == -1 || midi_channel == (int)ch)
                        trk = d1 % 16;
                }
            }

            if (trk != 0xFF && s_evcount < MIDI_EVENT_BUF) {
                s_evbuf[s_evcount++] = {tick, trk};
            } else if (trk != 0xFF) {
                s_evbuf_overflow = true;
            }
        } else if (ev == 0xA0 || ev == 0xB0 || ev == 0xE0) {
            readU8();
        }
        // 0xC0, 0xD0: single data byte (d1 already consumed)
    }

    s_f.seek(end);
}

// Convert the collected event buffer to a step grid spanning up to MAX_STEPS steps.
// Finds the first bar that contains events, detects the last bar with events,
// rounds up to the nearest full bar, and fills steps[] accordingly.
// Returns the actual pattern length (multiple of STEPS_PER_BANK, min STEPS_PER_BANK).
static int eventsToSteps(int tpq, bool steps[Config::MAX_TRACKS][Config::MAX_STEPS], int* found) {
    if (s_evcount == 0 || tpq <= 0) return Config::STEPS_PER_BANK;

    uint32_t bar_ticks = (uint32_t)tpq * 4u;

    // Find tick range of matching events
    uint32_t min_tick = s_evbuf[0].tick;
    uint32_t max_tick = s_evbuf[0].tick;
    for (int i = 1; i < s_evcount; i++) {
        if (s_evbuf[i].tick < min_tick) min_tick = s_evbuf[i].tick;
        if (s_evbuf[i].tick > max_tick) max_tick = s_evbuf[i].tick;
    }

    // Snap to the start of the bar that contains the first event
    uint32_t bar_start = (min_tick / bar_ticks) * bar_ticks;

    // Calculate number of bars needed, capped at MAX_STEPS / STEPS_PER_BANK (=4)
    const int MAX_BANKS = Config::MAX_STEPS / Config::STEPS_PER_BANK;  // 4
    uint32_t span_ticks = max_tick - bar_start;
    int num_bars = (int)((span_ticks / bar_ticks) + 1);  // +1 to include the last bar
    if (num_bars > MAX_BANKS) num_bars = MAX_BANKS;
    int pattern_length = num_bars * Config::STEPS_PER_BANK;

    // Fill events across all bars in the detected span
    for (int i = 0; i < s_evcount; i++) {
        if (s_evbuf[i].tick < bar_start) continue;
        uint32_t rel  = s_evbuf[i].tick - bar_start;
        uint32_t step = (rel * (uint32_t)Config::STEPS_PER_BANK) / bar_ticks;  // 16ths within pattern
        if ((int)step < pattern_length) {
            if (!steps[s_evbuf[i].trk][step]) {
                steps[s_evbuf[i].trk][step] = true;
                if (found) (*found)++;
            }
        }
    }

    return pattern_length;
}

// ---------------------------------------------------------------------------
// Helper: open + parse all tracks of the MIDI file into s_evbuf
// Returns tpq (>0) on success, 0 on error. Fills tempo_us if found.
// ---------------------------------------------------------------------------
static uint16_t parseFile(const char* path, int midi_channel, uint32_t* tempo_us_out) {
    s_f = SD_MMC.open(path);
    s_err = false;
    s_parse_start_ms = millis();
    if (!s_f) return 0;

    uint16_t result_tpq = 0;
    char magic[4];
    bool ok = (s_f.read((uint8_t*)magic, 4) == 4) && (strncmp(magic, "MThd", 4) == 0);

    if (ok) {
        uint32_t hdr_len = readU32();
        uint16_t fmt     = readU16(); (void)fmt;
        uint16_t ntracks = readU16();
        uint16_t tpq     = readU16();
        if (!s_err && !(tpq & 0x8000)) {
            if (tpq == 0) tpq = 96;
            if (hdr_len > 6) skipN(hdr_len - 6);

            for (int t = 0; t < ntracks && !s_err; t++) {
                if (parse_timed_out()) { s_err = true; break; }
                char tmagic[4];
                if (s_f.read((uint8_t*)tmagic, 4) != 4) break;
                if (strncmp(tmagic, "MTrk", 4) != 0) break;
                uint32_t tlen = readU32();
                if (s_err) break;
                parseTrack(tlen, (int)tpq, tempo_us_out, midi_channel);
            }
            result_tpq = tpq;
        }
    }

    if (s_f) s_f.close();   // guaranteed close on every exit path
    return s_err ? 0 : result_tpq;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool midi_load_pattern(const char* path, bool steps[Config::MAX_TRACKS][Config::MAX_STEPS],
                       char* name_out, int name_max, int* steps_found_out,
                       float* bpm_out, int midi_channel, int* length_out) {
    memset(steps, 0, Config::MAX_TRACKS * Config::MAX_STEPS * sizeof(bool));

    // Extract filename (without extension) as pattern name
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (name_out && name_max > 0) {
        strncpy(name_out, base, name_max - 1);
        name_out[name_max - 1] = '\0';
        char* dot = strrchr(name_out, '.');
        if (dot) *dot = '\0';
        if ((int)strlen(name_out) > 8) name_out[8] = '\0';
    }

    uint32_t tempo_us = 0;
    s_evcount = 0;
    s_evbuf_overflow = false;
    if (!ensure_evbuf()) {
        log_e("[MIDI] Could not allocate event buffer");
        return false;
    }
    uint16_t tpq = parseFile(path, midi_channel, &tempo_us);
    if (tpq == 0) return false;

    // Auto-fallback: if nothing found on ch9, retry scanning all channels
    if (s_evcount == 0 && midi_channel == 9) {
        s_evcount = 0;
        s_evbuf_overflow = false;
        tpq = parseFile(path, -1, &tempo_us);
        if (tpq == 0) return false;
    }

    if (s_evbuf_overflow) {
        log_w("[MIDI] Event buffer overflow (>%d events) parsing %s — pattern truncated",
              MIDI_EVENT_BUF, path);
    }

    int total = 0;
    int plen = eventsToSteps((int)tpq, steps, &total);
    if (steps_found_out) *steps_found_out = total;
    if (length_out) *length_out = plen;

    if (bpm_out)
        *bpm_out = (tempo_us > 0) ? (60000000.0f / (float)tempo_us) : 0.0f;

    return total > 0;
}
