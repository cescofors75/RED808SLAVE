// =============================================================================
// ui_screens.cpp — P4 UI screens (data-driven from UART state)
// All screen rendering reads from P4State (p4.*) — no direct hardware access.
// =============================================================================

#include "ui_screens.h"
#include "ui_theme.h"
#include "../udp_handler.h"
#include "../uart_handler.h"
#include "../dsp_task.h"
#include "config.h"
#include <Arduino.h>
#include <atomic>
#include <math.h>

// ── IntelliSense fallbacks ───────────────────────────────────────────────────
// The real values are provided via -D flags in platformio.ini and via
// config.h / lv_conf.h. These fallbacks only kick in for editor analysis
// when IntelliSense can't resolve the build system's include/define graph.
#ifndef LCD_H_RES
#define LCD_H_RES 1024
#endif
#ifndef LCD_V_RES
#define LCD_V_RES 600
#endif
#ifndef UI_H
#define UI_H LCD_V_RES
#endif
#if defined(__INTELLISENSE__)
LV_FONT_DECLARE(lv_font_montserrat_10)
#endif

// ── Pad event queue: touch_task (Core 0) → loop (Core 1) ──
// Each entry packs (velocity << 8) | pad to carry MPC-style velocity all the
// way to UDP without adding a parallel array. Decouples UDP/UART send from
// the LVGL mutex.
static volatile uint16_t s_pad_q[32];
static std::atomic<uint8_t> s_pad_qh{0};
static std::atomic<uint8_t> s_pad_qt{0};

// Direct touch bypass: flag used by touch_task to early-out when not on LIVE
static std::atomic<bool> g_live_screen_active{false};

static inline void enqueue_pad_event(uint8_t pad, uint8_t velocity) {
    uint8_t h = s_pad_qh.load(std::memory_order_relaxed);
    s_pad_q[h & 0x1F] = (uint16_t)((velocity << 8) | pad);
    s_pad_qh.store(h + 1, std::memory_order_release);
}

// =============================================================================
// MPC-STYLE PLAYBACK STATE — note repeat, 16 levels, velocity fade
// =============================================================================
// Note repeat: subdivisions per beat = {1, 2, 4, 8, 3, 6} for
// 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T respectively. Index 0 = OFF.
static const uint8_t     NR_SUBDIV_PER_BEAT[7] = {0, 1, 2, 4, 8, 3, 6};
static const char* const NR_LABEL[7] = {"NR\nOFF", "NR\n1/4", "NR\n1/8",
                                         "NR\n1/16", "NR\n1/32",
                                         "NR\n1/8T", "NR\n1/16T"};
static volatile uint8_t s_nr_idx = 0;                  // 0 = OFF

// 16 Levels: all 16 pads become 16 velocities of a single source pad
static volatile bool    s_16l_active   = false;
static volatile uint8_t s_16l_src_pad  = 0;            // last non-16L tap

// Per-pad state, written by touch_task (Core 0) and read by update_live_screen
// (Core 0 LVGL task). Single writer / single reader per field → no locks.
static volatile bool          s_pad_held[16] = {};
static volatile unsigned long s_pad_repeat_next_ms[16] = {};
static volatile uint8_t       s_pad_held_velocity[16] = {};

// Velocity fade visualisation: each press stores (velocity, start_ms) and
// update_live_screen interpolates an exponential decay over FADE_MS to drive
// the pad background opacity. Quantised into 8 brightness bands so LVGL only
// re-invalidates a pad rect when the band actually changes (keeps partial
// refresh cheap even with 16 pads decaying at once).
static const int FADE_MS = 320;
static volatile uint8_t       s_pad_flash_vel[16] = {};
static volatile unsigned long s_pad_flash_start_ms[16] = {};

static inline void ui_pad_flash_start(uint8_t pad, uint8_t velocity) {
    if (pad >= 16) return;
    s_pad_flash_vel[pad]      = velocity ? velocity : 1;
    s_pad_flash_start_ms[pad] = millis();
}

static unsigned long ui_nr_interval_ms(void) {
    // Use current tempo from P4State (UART-synced). BPM can be 0 briefly at
    // boot; clamp to 40..300 for safety.
    extern struct P4State p4;
    int bpm_x10 = p4.bpm_int * 10 + p4.bpm_frac;
    if (bpm_x10 < 400)  bpm_x10 = 1200;
    if (bpm_x10 > 3000) bpm_x10 = 3000;
    uint8_t idx = s_nr_idx;
    if (idx == 0 || idx >= (sizeof(NR_SUBDIV_PER_BEAT) / sizeof(NR_SUBDIV_PER_BEAT[0]))) return 0;
    uint32_t div = NR_SUBDIV_PER_BEAT[idx];
    // interval = 60000 ms / (bpm * div). bpm_x10 is BPM*10, so:
    //   ms = 600000 / (bpm_x10 * div)
    unsigned long ms = 600000UL / ((unsigned long)bpm_x10 * div);
    if (ms < 15) ms = 15;   // safety floor (~66 Hz max retrigger)
    return ms;
}


// Screen objects
lv_obj_t* scr_boot = NULL;
lv_obj_t* scr_live = NULL;
lv_obj_t* scr_sequencer = NULL;
lv_obj_t* scr_fx = NULL;
lv_obj_t* scr_volumes = NULL;
lv_obj_t* scr_sdcard = NULL;
lv_obj_t* scr_performance = NULL;

// Header widgets
static lv_obj_t* header_bar = NULL;
static lv_obj_t* hdr_bpm_label = NULL;
static lv_obj_t* hdr_pattern_label = NULL;
static lv_obj_t* hdr_play_btn = NULL;
static lv_obj_t* hdr_play_label = NULL;
static lv_obj_t* hdr_pattern_minus_btn = NULL;
static lv_obj_t* hdr_pattern_plus_btn = NULL;
static lv_obj_t* hdr_wifi_label = NULL;
static lv_obj_t* hdr_s3_label = NULL;
static lv_obj_t* hdr_step_dots[16] = {};

// Current active screen index + history for back navigation
static int active_screen = 0;
static int prev_active_screen = 0;

// Track names
static const char* trackNames[] = {
    "BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL",
    "MA", "CY", "HT", "LT", "MC", "MT", "HC", "LC"
};

static int ui_layout_w(void) {
    return LCD_H_RES;
}

static int ui_layout_h(void) {
    // The current panel path renders 1024x600 without framebuffer rotation.
    // Keep the live layout inside the visible 600px height.
    return (UI_H > LCD_V_RES) ? LCD_V_RES : UI_H;
}

// =============================================================================
// HELPER: Section shell (styled container)
// =============================================================================
lv_obj_t* create_section_shell(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, RED808_BORDER, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_pad_all(obj, 14, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static bool ui_use_udp_transport(void) {
    return p4.wifi_connected || p4.master_connected;
}

static lv_obj_t* create_header_button(lv_obj_t* parent, int x, int y, int w, int h,
                                      const char* text, lv_color_t bg, lv_color_t border) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, border, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label, RED808_TEXT, 0);
    lv_obj_center(label);
    return btn;
}

static void header_play_cb(lv_event_t* e) {
    LV_UNUSED(e);
    bool next_play = !p4.is_playing;
    // Send to Master (sound) AND S3 (state sync)
    if (next_play) udp_send_start();
    else udp_send_stop();
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_PLAY_TOGGLE, 0);
    p4.is_playing = next_play;
}

static void header_pattern_cb(lv_event_t* e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int next_pattern = p4.current_pattern + delta;
    if (next_pattern < 0) next_pattern = Config::MAX_PATTERNS - 1;
    if (next_pattern >= Config::MAX_PATTERNS) next_pattern = 0;

    p4.current_pattern = next_pattern;
    // Send to Master (sound) AND S3 (state sync)
    udp_send_select_pattern(next_pattern);
    udp_send_get_pattern(next_pattern);
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_PATTERN_SEL, (uint8_t)next_pattern);
}

// =============================================================================
// BACK BUTTON — replaces the old header bar (floating top-left corner)
// =============================================================================
void ui_create_header(lv_obj_t* parent) {
    // Nullify all header widget pointers — not used anymore
    header_bar = NULL;
    hdr_bpm_label = NULL; hdr_pattern_label = NULL;
    hdr_play_btn = NULL; hdr_play_label = NULL;
    hdr_pattern_minus_btn = NULL; hdr_pattern_plus_btn = NULL;
    hdr_wifi_label = NULL; hdr_s3_label = NULL;
    for (int i = 0; i < 16; i++) hdr_step_dots[i] = NULL;

    // Small floating back button (top-left)
    lv_obj_t* back_btn = lv_btn_create(parent);
    lv_obj_set_size(back_btn, 44, 34);
    lv_obj_set_pos(back_btn, 8, 8);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_bg_color(back_btn, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, RED808_BORDER, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        LV_UNUSED(e);
        if (active_screen != 2) ui_navigate_to(2);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(back_lbl, RED808_TEXT, 0);
    lv_obj_center(back_lbl);
}

void ui_update_header(void) {
    static int prev_bpm = -1, prev_frac = -1, prev_pat = -1;
    static bool prev_play = false, prev_wifi = false, prev_s3 = false;

    if (p4.bpm_int != prev_bpm || p4.bpm_frac != prev_frac) {
        prev_bpm = p4.bpm_int;
        prev_frac = p4.bpm_frac;
        if (hdr_bpm_label) lv_label_set_text_fmt(hdr_bpm_label, "%d.%d", p4.bpm_int, p4.bpm_frac);
    }

    if (p4.current_pattern != prev_pat) {
        prev_pat = p4.current_pattern;
        if (hdr_pattern_label) lv_label_set_text_fmt(hdr_pattern_label, "P%02d", p4.current_pattern + 1);
    }

    if (p4.is_playing != prev_play) {
        prev_play = p4.is_playing;
        if (hdr_play_btn && hdr_play_label) {
            lv_label_set_text(hdr_play_label, p4.is_playing ? "PAUSE" : "PLAY");
            lv_obj_set_style_bg_color(hdr_play_btn, p4.is_playing ? RED808_SUCCESS : RED808_ACCENT, 0);
            lv_obj_set_style_border_color(hdr_play_btn, p4.is_playing ? RED808_CYAN : RED808_ACCENT2, 0);
        }
    }

    if (p4.wifi_connected != prev_wifi) {
        prev_wifi = p4.wifi_connected;
        if (hdr_wifi_label) {
            lv_label_set_text(hdr_wifi_label, p4.wifi_connected ? "NET OK" : "NET OFF");
            lv_obj_set_style_text_color(hdr_wifi_label,
                p4.wifi_connected ? RED808_SUCCESS : RED808_ERROR, 0);
        }
    }

    if (p4.s3_connected != prev_s3) {
        prev_s3 = p4.s3_connected;
        if (hdr_s3_label) {
            lv_label_set_text(hdr_s3_label, p4.s3_connected ? "AUX ON" : "AUX OFF");
            lv_obj_set_style_text_color(hdr_s3_label,
                p4.s3_connected ? RED808_INFO : RED808_TEXT_DIM, 0);
        }
    }
}

// =============================================================================
// BOOT SCREEN
// =============================================================================
static void create_boot_screen(void) {
    scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, RED808_BG, 0);
    lv_obj_clear_flag(scr_boot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(scr_boot);
    lv_label_set_text(title, "RED808 P4");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(title, RED808_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t* sub = lv_label_create(scr_boot);
    lv_label_set_text(sub, "Connecting to RED808 Master...");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sub, RED808_TEXT_DIM, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t* spinner = lv_spinner_create(scr_boot, 1000, 60);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 80);
}

// =============================================================================
// LIVE GRID SCREEN — 8×4 full-screen grid (4×4 pads + 4×4 controls)
// =============================================================================
static lv_obj_t* live_pad_btns[16] = {};
static lv_obj_t* live_pad_labels[16] = {};
static lv_obj_t* live_spectrum_bars[16] = {};  // spectrum bar per pad (bottom of pad)

// Right-side control widgets (dynamic updates)
static lv_obj_t* grid_play_btn = NULL;
static lv_obj_t* grid_play_lbl = NULL;
static lv_obj_t* grid_bpm_lbl = NULL;
static lv_obj_t* grid_pat_lbl = NULL;
static lv_obj_t* grid_step_lbl = NULL;
static lv_obj_t* grid_nr_btn  = NULL;   // Note Repeat toggle + subdivision cycler
static lv_obj_t* grid_nr_lbl  = NULL;
static lv_obj_t* grid_16l_btn = NULL;   // 16 Levels toggle
static lv_obj_t* grid_16l_lbl = NULL;
// Link status indicators (replaces the old "LIVE" badge)
static lv_obj_t* grid_mstr_dot = NULL;  // Master (UDP to ESP32-C6 AP) link
static lv_obj_t* grid_mstr_lbl = NULL;
static lv_obj_t* grid_aux_dot  = NULL;  // Aux (UART to ESP32-S3) link
static lv_obj_t* grid_aux_lbl  = NULL;
static lv_obj_t* grid_vol_lbl = NULL;

// Sync Pads LEDs — pads illuminate automatically with sequencer
static lv_obj_t* grid_sync_btn = NULL;
static bool sync_pads_active = false;  // OFF by default (synced with S3)

// Ripple effect — pool of expanding ring objects
static constexpr int RIPPLE_POOL = 4;
static constexpr int RIPPLE_FRAMES = 12;      // animation steps at ~60Hz = 200ms
static constexpr int RIPPLE_MAX_R = 80;        // max radius in pixels
struct RippleState {
    lv_obj_t* obj = nullptr;
    int frame = 0;         // 0 = inactive
    lv_color_t color;
    lv_coord_t cx, cy;     // center position (absolute on scr_live)
};
static RippleState ripples[RIPPLE_POOL];

static void ripple_spawn(int pad) {
    // DISABLED — ripple overlay forced LVGL to invalidate a large expanding
    // area every frame for 200ms per tap. On the live screen this stacks up
    // when tapping fast and drowns the render task. The pad border already
    // flashes on press, which is enough feedback.
    (void)pad;
    return;
    if (pad < 0 || pad >= 16 || !live_pad_btns[pad] || !scr_live) return;
    // Calculate pad center in screen coordinates
    lv_coord_t px = lv_obj_get_x(live_pad_btns[pad]);
    lv_coord_t py = lv_obj_get_y(live_pad_btns[pad]);
    lv_coord_t pw = lv_obj_get_width(live_pad_btns[pad]);
    lv_coord_t ph = lv_obj_get_height(live_pad_btns[pad]);
    lv_coord_t cx = px + pw / 2;
    lv_coord_t cy = py + ph / 2;
    lv_color_t tc = lv_color_hex(theme_presets[currentTheme].track_colors[pad]);

    // Find free or oldest ripple slot
    int slot = 0;
    for (int i = 0; i < RIPPLE_POOL; i++) {
        if (ripples[i].frame == 0) { slot = i; break; }
        if (ripples[i].frame > ripples[slot].frame) slot = i;
    }

    RippleState& r = ripples[slot];
    r.frame = 1;
    r.color = tc;
    r.cx = cx;
    r.cy = cy;

    if (!r.obj) {
        r.obj = lv_obj_create(scr_live);
        lv_obj_clear_flag(r.obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(r.obj, LV_OPA_0, 0);
        lv_obj_set_style_shadow_width(r.obj, 0, 0);
    }
    // Reset visual
    lv_obj_set_size(r.obj, 10, 10);
    lv_obj_set_pos(r.obj, cx - 5, cy - 5);
    lv_obj_set_style_radius(r.obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r.obj, 3, 0);
    lv_obj_set_style_border_color(r.obj, tc, 0);
    lv_obj_set_style_border_opa(r.obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(r.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(r.obj);
}

// Called each frame from update_live_screen to animate active ripples
static void ripple_update(void) {
    for (int i = 0; i < RIPPLE_POOL; i++) {
        RippleState& r = ripples[i];
        if (r.frame == 0 || !r.obj) continue;

        r.frame++;
        if (r.frame > RIPPLE_FRAMES) {
            // Animation done — hide and recycle
            r.frame = 0;
            lv_obj_add_flag(r.obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        float t = (float)(r.frame - 1) / (float)RIPPLE_FRAMES;  // 0..1
        int sz = 10 + (int)(t * RIPPLE_MAX_R * 2);
        lv_obj_set_size(r.obj, sz, sz);
        lv_obj_set_pos(r.obj, r.cx - sz / 2, r.cy - sz / 2);
        lv_obj_set_style_border_opa(r.obj, (lv_opa_t)(255 * (1.0f - t)), 0);
        // Border thins as it expands
        lv_obj_set_style_border_width(r.obj, (lv_coord_t)(3 * (1.0f - t * 0.7f)), 0);
    }
}

static void pad_touch_cb(lv_event_t* e) {
    // Safety fallback for the LVGL button event. In practice the GT911 direct
    // path (ui_pad_frame_update) already serviced the press at 200Hz; this
    // only fires if LVGL somehow received a touch the cache did not classify
    // as a pad (shouldn't happen with matching geometry). No-op is safe — the
    // real velocity-aware tap handling lives in ui_pad_frame_update().
    LV_UNUSED(e);
}

static void grid_nav_cb(lv_event_t* e) {
    int screen_id = (int)(intptr_t)lv_event_get_user_data(e);
    ui_navigate_to(screen_id);
}

static void grid_sync_cb(lv_event_t* e) {
    LV_UNUSED(e);
    sync_pads_active = !sync_pads_active;
    if (grid_sync_btn) {
        lv_obj_set_style_bg_color(grid_sync_btn,
            sync_pads_active ? RED808_SUCCESS : RED808_SURFACE, 0);
        lv_obj_set_style_border_color(grid_sync_btn,
            sync_pads_active ? RED808_CYAN : RED808_BORDER, 0);
        lv_obj_t* lbl = lv_obj_get_child(grid_sync_btn, 0);
        if (lbl) lv_label_set_text(lbl, sync_pads_active ? "SYNC\nON" : "SYNC\nOFF");
    }
    // Sync state to S3
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_SYNC_PADS, sync_pads_active ? 1 : 0);
}

// Cycle Note Repeat: OFF → 1/4 → 1/8 → 1/16 → 1/32 → 1/8T → 1/16T → OFF
static void grid_nr_cb(lv_event_t* e) {
    LV_UNUSED(e);
    uint8_t idx = (uint8_t)((s_nr_idx + 1) % 7);
    s_nr_idx = idx;
    if (grid_nr_btn) {
        bool on = (idx != 0);
        lv_obj_set_style_bg_color(grid_nr_btn,
            on ? RED808_ACCENT : RED808_SURFACE, 0);
        lv_obj_set_style_border_color(grid_nr_btn,
            on ? RED808_ACCENT2 : RED808_BORDER, 0);
        if (grid_nr_lbl) lv_label_set_text(grid_nr_lbl, NR_LABEL[idx]);
    }
}

// Toggle 16 Levels — all pads play the last-tapped sample at 16 velocities
static void grid_16l_cb(lv_event_t* e) {
    LV_UNUSED(e);
    bool on = !s_16l_active;
    s_16l_active = on;
    if (grid_16l_btn) {
        lv_obj_set_style_bg_color(grid_16l_btn,
            on ? RED808_CYAN : RED808_SURFACE, 0);
        lv_obj_set_style_border_color(grid_16l_btn,
            on ? RED808_ACCENT : RED808_BORDER, 0);
        if (grid_16l_lbl) {
            if (on) {
                lv_label_set_text_fmt(grid_16l_lbl, "16 LVL\nSRC %d", s_16l_src_pad + 1);
            } else {
                lv_label_set_text(grid_16l_lbl, "16 LVL\nOFF");
            }
        }
    }
}

// Called when S3 sends sync toggle — update UI without re-sending
void ui_live_set_sync_p4(bool on) {
    sync_pads_active = on;
    if (grid_sync_btn) {
        lv_obj_set_style_bg_color(grid_sync_btn,
            on ? RED808_SUCCESS : RED808_SURFACE, 0);
        lv_obj_set_style_border_color(grid_sync_btn,
            on ? RED808_CYAN : RED808_BORDER, 0);
        lv_obj_t* lbl = lv_obj_get_child(grid_sync_btn, 0);
        if (lbl) lv_label_set_text(lbl, on ? "SYNC\nON" : "SYNC\nOFF");
    }
}

static void grid_theme_cb(lv_event_t* e) {
    LV_UNUSED(e);
    int next = ((int)currentTheme + 1) % THEME_COUNT;
    p4.theme = next;
    ui_theme_apply((VisualTheme)next);
    // Sync theme to S3
    uart_send_to_s3(MSG_TOUCH_CMD, TCMD_THEME_NEXT, (uint8_t)next);
}

// Helper: styled control button
static lv_obj_t* create_ctrl_btn(lv_obj_t* parent, int x, int y, int w, int h,
                                  const char* text, lv_color_t border_color,
                                  const lv_font_t* font) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, border_color, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, RED808_TEXT, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return btn;
}

// Helper: info display cell (non-clickable)
static lv_obj_t* create_info_cell(lv_obj_t* parent, int x, int y, int w, int h,
                                   const char* title, const char* value,
                                   lv_color_t value_color, lv_obj_t** value_out) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_bg_color(panel, RED808_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, RED808_BORDER, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(panel);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t, RED808_TEXT_DIM, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* v = lv_label_create(panel);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(v, value_color, 0);
    lv_obj_align(v, LV_ALIGN_CENTER, 0, 10);

    if (value_out) *value_out = v;
    return panel;
}

static void create_live_screen(void) {
    scr_live = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_live, RED808_BG, 0);
    lv_obj_clear_flag(scr_live, LV_OBJ_FLAG_SCROLLABLE);

    // 8×4 full-screen grid: 1024×600
    // Left 4 cols = pads, Right 4 cols = controls
    const int M = 8, G = 4, CG = 8, CW = 122, CH = 143;
    #define COL_X(c) ((c) < 4 ? (M + (c)*(CW+G)) : (M + 4*(CW+G) + CG + ((c)-4)*(CW+G)))
    #define ROW_Y(r) (M + (r)*(CH+G))

    // Vertical separator
    lv_obj_t* sep = lv_obj_create(scr_live);
    lv_obj_set_size(sep, 2, LCD_V_RES - 2*M);
    lv_obj_set_pos(sep, M + 4*(CW+G) + CG/2 - 1, M);
    lv_obj_set_style_bg_color(sep, RED808_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 1, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // === LEFT 4×4: Drum Pads (Neon Ring Style) ===
    for (int i = 0; i < 16; i++) {
        int c = i % 4, r = i / 4;
        lv_color_t tc = lv_color_hex(theme_presets[currentTheme].track_colors[i]);

        live_pad_btns[i] = lv_btn_create(scr_live);
        lv_obj_set_size(live_pad_btns[i], CW, CH);
        lv_obj_set_pos(live_pad_btns[i], COL_X(c), ROW_Y(r));
        // Dark interior
        lv_obj_set_style_radius(live_pad_btns[i], 16, 0);
        lv_obj_set_style_bg_color(live_pad_btns[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(live_pad_btns[i], LV_OPA_COVER, 0);
        // Simple flat ring — no outline, no shadow (alpha blending + blur are
        // the most expensive ops in LVGL software renderer and were repainted
        // on every frame under full_refresh).
        lv_obj_set_style_border_width(live_pad_btns[i], 3, 0);
        lv_obj_set_style_border_color(live_pad_btns[i], tc, 0);
        lv_obj_set_style_outline_width(live_pad_btns[i], 0, 0);
        lv_obj_set_style_shadow_width(live_pad_btns[i], 0, 0);
        lv_obj_add_event_cb(live_pad_btns[i], pad_touch_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);

        live_pad_labels[i] = lv_label_create(live_pad_btns[i]);
        lv_label_set_text(live_pad_labels[i], trackNames[i]);
        lv_obj_set_style_text_font(live_pad_labels[i], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(live_pad_labels[i], tc, 0);
        lv_obj_center(live_pad_labels[i]);

        // Spectrum bar — thin horizontal bar at bottom of pad, grows upward
        live_spectrum_bars[i] = lv_obj_create(live_pad_btns[i]);
        lv_obj_set_size(live_spectrum_bars[i], CW - 20, 0);  // starts at 0 height
        lv_obj_align(live_spectrum_bars[i], LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(live_spectrum_bars[i], tc, 0);
        lv_obj_set_style_bg_opa(live_spectrum_bars[i], LV_OPA_60, 0);
        lv_obj_set_style_border_width(live_spectrum_bars[i], 0, 0);
        lv_obj_set_style_radius(live_spectrum_bars[i], 3, 0);
        lv_obj_clear_flag(live_spectrum_bars[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    // === RIGHT 4×4: Controls ===

    // --- Row 0: Transport ---
    // [4,0] PLAY / PAUSE
    grid_play_btn = lv_btn_create(scr_live);
    lv_obj_set_size(grid_play_btn, CW, CH);
    lv_obj_set_pos(grid_play_btn, COL_X(4), ROW_Y(0));
    lv_obj_set_style_radius(grid_play_btn, 14, 0);
    lv_obj_set_style_bg_color(grid_play_btn, RED808_ACCENT, 0);
    lv_obj_set_style_bg_opa(grid_play_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid_play_btn, 3, 0);
    lv_obj_set_style_border_color(grid_play_btn, RED808_ACCENT2, 0);
    lv_obj_set_style_shadow_width(grid_play_btn, 0, 0);
    lv_obj_add_event_cb(grid_play_btn, header_play_cb, LV_EVENT_CLICKED, NULL);
    grid_play_lbl = lv_label_create(grid_play_btn);
    lv_label_set_text(grid_play_lbl, LV_SYMBOL_PLAY "\nPLAY");
    lv_obj_set_style_text_font(grid_play_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(grid_play_lbl, RED808_TEXT, 0);
    lv_obj_set_style_text_align(grid_play_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(grid_play_lbl);

    // [5,0] PAT -
    lv_obj_t* b;
    b = create_ctrl_btn(scr_live, COL_X(5), ROW_Y(0), CW, CH,
                         LV_SYMBOL_LEFT "\nPAT", RED808_WARNING, &lv_font_montserrat_20);
    lv_obj_add_event_cb(b, header_pattern_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);

    // [6,0] PAT +
    b = create_ctrl_btn(scr_live, COL_X(6), ROW_Y(0), CW, CH,
                         "PAT\n" LV_SYMBOL_RIGHT, RED808_WARNING, &lv_font_montserrat_20);
    lv_obj_add_event_cb(b, header_pattern_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);

    // [7,0] BPM display
    create_info_cell(scr_live, COL_X(7), ROW_Y(0), CW, CH,
                     "BPM", "120.0", RED808_ACCENT, &grid_bpm_lbl);

    // --- Row 1: Screen Navigation ---
    static const char* nav_texts[] = {"STEPS", "FX", "MIXER"};
    static const int nav_screens[] = {3, 8, 7};
    lv_color_t nav_colors[] = {RED808_CYAN, RED808_INFO, RED808_SUCCESS};
    for (int i = 0; i < 3; i++) {
        b = create_ctrl_btn(scr_live, COL_X(4 + i), ROW_Y(1), CW, CH,
                             nav_texts[i], nav_colors[i], &lv_font_montserrat_20);
        lv_obj_add_event_cb(b, grid_nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)nav_screens[i]);
    }
    // SYNC button — replaces PERF, toggles pad LED sync with sequencer
    grid_sync_btn = create_ctrl_btn(scr_live, COL_X(7), ROW_Y(1), CW, CH,
                                     "SYNC\nOFF", RED808_SURFACE, &lv_font_montserrat_20);
    lv_obj_set_style_bg_color(grid_sync_btn, RED808_SURFACE, 0);
    lv_obj_set_style_border_color(grid_sync_btn, RED808_BORDER, 0);
    lv_obj_add_event_cb(grid_sync_btn, grid_sync_cb, LV_EVENT_CLICKED, NULL);

    // --- Row 2: System ---
    // [4,2] SD CARD
    b = create_ctrl_btn(scr_live, COL_X(4), ROW_Y(2), CW, CH,
                         LV_SYMBOL_DRIVE "\nSD", RED808_CYAN, &lv_font_montserrat_18);
    lv_obj_add_event_cb(b, grid_nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)9);

    // [5,2] THEME
    b = create_ctrl_btn(scr_live, COL_X(5), ROW_Y(2), CW, CH,
                         "THEME\n" LV_SYMBOL_RIGHT, RED808_ACCENT2, &lv_font_montserrat_18);
    lv_obj_add_event_cb(b, grid_theme_cb, LV_EVENT_CLICKED, NULL);

    // [6,2] NOTE REPEAT cycler — OFF / 1/4 / 1/8 / 1/16 / 1/32 / 1/8T / 1/16T
    grid_nr_btn = create_ctrl_btn(scr_live, COL_X(6), ROW_Y(2), CW, CH,
                                  NR_LABEL[s_nr_idx], RED808_SURFACE,
                                  &lv_font_montserrat_20);
    lv_obj_set_style_border_color(grid_nr_btn, RED808_BORDER, 0);
    grid_nr_lbl = lv_obj_get_child(grid_nr_btn, 0);
    lv_obj_add_event_cb(grid_nr_btn, grid_nr_cb, LV_EVENT_CLICKED, NULL);

    // [7,2] 16 LEVELS toggle — re-maps all 16 pads to velocity ladder of last tap
    grid_16l_btn = create_ctrl_btn(scr_live, COL_X(7), ROW_Y(2), CW, CH,
                                   "16 LVL\nOFF", RED808_SURFACE,
                                   &lv_font_montserrat_20);
    lv_obj_set_style_border_color(grid_16l_btn, RED808_BORDER, 0);
    grid_16l_lbl = lv_obj_get_child(grid_16l_btn, 0);
    lv_obj_add_event_cb(grid_16l_btn, grid_16l_cb, LV_EVENT_CLICKED, NULL);

    // --- Row 3: Info Displays ---
    // [4,3] Pattern
    create_info_cell(scr_live, COL_X(4), ROW_Y(3), CW, CH,
                     "PATTERN", "P01", RED808_WARNING, &grid_pat_lbl);

    // [5,3] Step
    create_info_cell(scr_live, COL_X(5), ROW_Y(3), CW, CH,
                     "STEP", "--", RED808_CYAN, &grid_step_lbl);

    // [6,3] Volume
    create_info_cell(scr_live, COL_X(6), ROW_Y(3), CW, CH,
                     "VOL", "75", RED808_ACCENT, &grid_vol_lbl);

    // [7,3] Link status: MSTR (to Master via C6 WiFi) + AUX (to S3 via UART)
    lv_obj_t* link_ind = lv_obj_create(scr_live);
    lv_obj_set_size(link_ind, CW, CH);
    lv_obj_set_pos(link_ind, COL_X(7), ROW_Y(3));
    lv_obj_set_style_radius(link_ind, 14, 0);
    lv_obj_set_style_bg_color(link_ind, RED808_SURFACE, 0);
    lv_obj_set_style_bg_opa(link_ind, LV_OPA_80, 0);
    lv_obj_set_style_border_width(link_ind, 2, 0);
    lv_obj_set_style_border_color(link_ind, RED808_BORDER, 0);
    lv_obj_set_style_pad_all(link_ind, 6, 0);
    lv_obj_clear_flag(link_ind, LV_OBJ_FLAG_SCROLLABLE);

    // ── Row A: MSTR (top half) ──────────────────────────────────────────
    grid_mstr_dot = lv_obj_create(link_ind);
    lv_obj_set_size(grid_mstr_dot, 14, 14);
    lv_obj_align(grid_mstr_dot, LV_ALIGN_LEFT_MID, 0, -18);
    lv_obj_set_style_radius(grid_mstr_dot, 7, 0);
    lv_obj_set_style_bg_color(grid_mstr_dot, RED808_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(grid_mstr_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid_mstr_dot, 0, 0);
    lv_obj_clear_flag(grid_mstr_dot, LV_OBJ_FLAG_SCROLLABLE);

    grid_mstr_lbl = lv_label_create(link_ind);
    lv_label_set_text(grid_mstr_lbl, "MSTR --");
    lv_obj_set_style_text_font(grid_mstr_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(grid_mstr_lbl, RED808_TEXT_DIM, 0);
    lv_obj_align(grid_mstr_lbl, LV_ALIGN_LEFT_MID, 22, -18);

    // ── Row B: AUX  (bottom half) ───────────────────────────────────────
    grid_aux_dot = lv_obj_create(link_ind);
    lv_obj_set_size(grid_aux_dot, 14, 14);
    lv_obj_align(grid_aux_dot, LV_ALIGN_LEFT_MID, 0, 18);
    lv_obj_set_style_radius(grid_aux_dot, 7, 0);
    lv_obj_set_style_bg_color(grid_aux_dot, RED808_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(grid_aux_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid_aux_dot, 0, 0);
    lv_obj_clear_flag(grid_aux_dot, LV_OBJ_FLAG_SCROLLABLE);

    grid_aux_lbl = lv_label_create(link_ind);
    lv_label_set_text(grid_aux_lbl, "AUX --");
    lv_obj_set_style_text_font(grid_aux_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(grid_aux_lbl, RED808_TEXT_DIM, 0);
    lv_obj_align(grid_aux_lbl, LV_ALIGN_LEFT_MID, 22, 18);

    #undef COL_X
    #undef ROW_Y
}

static void update_live_screen(void) {
    unsigned long now = millis();

    // ── MPC-style pad fade: velocity-weighted exponential decay ──
    // Each pad maps its current "brightness" to one of 8 bands (0 = idle,
    // 7 = peak hit). LVGL only re-invalidates a pad when its band actually
    // changes, keeping partial refresh cheap even when all 16 pads fire.
    static int prev_sync_step = -1;
    bool step_changed = (p4.current_step != prev_sync_step);
    if (step_changed) prev_sync_step = p4.current_step;
    static uint8_t pad_prev_band[16] = {};
    for (int i = 0; i < 16; i++) {
        if (!live_pad_btns[i]) continue;

        uint8_t band = 0;
        uint8_t vel  = s_pad_flash_vel[i];
        if (vel) {
            unsigned long el = now - s_pad_flash_start_ms[i];
            if (el >= (unsigned long)FADE_MS) {
                s_pad_flash_vel[i] = 0;   // fade complete → back to idle
            } else {
                float t   = (float)el / (float)FADE_MS;           // 0..1
                float env = expf(-3.2f * t);                       // 1→~0.04
                float b   = (vel / 127.0f) * env * 7.999f;
                band = (uint8_t)b;
                if (band > 7) band = 7;
            }
        }
        // Sequencer sync floor: if this pad is active on the current step,
        // render at mid brightness so the groove is always visible.
        if (sync_pads_active && p4.is_playing && p4.steps[i][p4.current_step]) {
            if (band < 4) band = 4;
        }
        if (band == pad_prev_band[i]) continue;
        pad_prev_band[i] = band;

        lv_color_t tc = lv_color_hex(theme_presets[currentTheme].track_colors[i]);

        if (band == 0) {
            // Idle: black interior + colored ring
            lv_obj_set_style_bg_color(live_pad_btns[i], lv_color_black(), 0);
            lv_obj_set_style_bg_opa(live_pad_btns[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(live_pad_btns[i], 3, 0);
            lv_obj_set_style_border_color(live_pad_btns[i], tc, 0);
            lv_obj_set_style_border_opa(live_pad_btns[i], LV_OPA_COVER, 0);
            if (live_pad_labels[i])
                lv_obj_set_style_text_color(live_pad_labels[i], tc, 0);
        } else {
            // Lit: blend black↔track color by band, brighten border
            // opa_fill = 32 + (band * 223 / 7): band=1→~64, band=7→255
            lv_opa_t opa_fill = (lv_opa_t)(32 + (band * 223) / 7);
            lv_obj_set_style_bg_color(live_pad_btns[i], tc, 0);
            lv_obj_set_style_bg_opa(live_pad_btns[i], opa_fill, 0);
            // Border widens on hard hits (band 6-7) and stays colored for soft
            lv_coord_t bw = (band >= 6) ? 4 : 3;
            lv_obj_set_style_border_width(live_pad_btns[i], bw, 0);
            lv_color_t bc = (band >= 6) ? lv_color_white() : tc;
            lv_obj_set_style_border_color(live_pad_btns[i], bc, 0);
            lv_obj_set_style_border_opa(live_pad_btns[i], LV_OPA_COVER, 0);
            if (live_pad_labels[i]) {
                lv_color_t lc = (band >= 5) ? lv_color_white() : tc;
                lv_obj_set_style_text_color(live_pad_labels[i], lc, 0);
            }
        }
    }

    // Play button state
    static bool gp_prev_play = false;
    if (grid_play_btn && grid_play_lbl && p4.is_playing != gp_prev_play) {
        gp_prev_play = p4.is_playing;
        lv_label_set_text(grid_play_lbl, p4.is_playing
            ? LV_SYMBOL_PAUSE "\nPAUSE" : LV_SYMBOL_PLAY "\nPLAY");
        lv_obj_set_style_bg_color(grid_play_btn,
            p4.is_playing ? RED808_SUCCESS : RED808_ACCENT, 0);
        lv_obj_set_style_border_color(grid_play_btn,
            p4.is_playing ? RED808_CYAN : RED808_ACCENT2, 0);
    }

    // BPM
    static int gp_prev_bpm = -1, gp_prev_frac = -1;
    if (grid_bpm_lbl && (p4.bpm_int != gp_prev_bpm || p4.bpm_frac != gp_prev_frac)) {
        gp_prev_bpm = p4.bpm_int;
        gp_prev_frac = p4.bpm_frac;
        lv_label_set_text_fmt(grid_bpm_lbl, "%d.%d", p4.bpm_int, p4.bpm_frac);
    }

    // Pattern
    static int gp_prev_pat = -1;
    if (grid_pat_lbl && p4.current_pattern != gp_prev_pat) {
        gp_prev_pat = p4.current_pattern;
        lv_label_set_text_fmt(grid_pat_lbl, "P%02d", p4.current_pattern + 1);
    }

    // Step — only show the running step while playing; show "--" when paused
    // or disconnected so the counter doesn't appear to run on the home screen.
    static int gp_prev_step = -2;   // -2 = never set, -1 = currently showing "--"
    if (grid_step_lbl) {
        if (!p4.is_playing) {
            if (gp_prev_step != -1) {
                gp_prev_step = -1;
                lv_label_set_text(grid_step_lbl, "--");
            }
        } else if (p4.current_step != gp_prev_step) {
            gp_prev_step = p4.current_step;
            lv_label_set_text_fmt(grid_step_lbl, "%02d", p4.current_step + 1);
        }
    }

    // Link-status indicators (edge-triggered so LVGL only redraws on change).
    // MSTR = UDP link to Master (over ESP32-C6 AP); AUX = UART link to S3.
    static int8_t prev_mstr = -1;
    static int8_t prev_aux  = -1;
    bool mstr_on = p4.master_connected;
    bool aux_on  = p4.s3_connected;
    if ((int8_t)mstr_on != prev_mstr && grid_mstr_dot && grid_mstr_lbl) {
        prev_mstr = mstr_on;
        lv_color_t c = mstr_on ? RED808_SUCCESS : RED808_TEXT_DIM;
        lv_obj_set_style_bg_color(grid_mstr_dot, c, 0);
        lv_obj_set_style_text_color(grid_mstr_lbl, c, 0);
        lv_label_set_text(grid_mstr_lbl, mstr_on ? "MSTR OK" : "MSTR --");
    }
    if ((int8_t)aux_on != prev_aux && grid_aux_dot && grid_aux_lbl) {
        prev_aux = aux_on;
        lv_color_t c = aux_on ? RED808_INFO : RED808_TEXT_DIM;
        lv_obj_set_style_bg_color(grid_aux_dot, c, 0);
        lv_obj_set_style_text_color(grid_aux_lbl, c, 0);
        lv_label_set_text(grid_aux_lbl, aux_on ? "AUX OK" : "AUX --");
    }

    // 16 Levels source pad label tracking — keeps the right-side button in
    // sync with whichever pad the player last tapped outside of 16L mode.
    static uint8_t prev_16l_src = 255;
    if (grid_16l_lbl && s_16l_active) {
        uint8_t cur = s_16l_src_pad;
        if (cur != prev_16l_src) {
            prev_16l_src = cur;
            lv_label_set_text_fmt(grid_16l_lbl, "16 LVL\nSRC %d", cur + 1);
        }
    } else {
        prev_16l_src = 255;
    }

    // Volume
    static int gp_prev_vol = -1;
    if (grid_vol_lbl && p4.master_volume != gp_prev_vol) {
        gp_prev_vol = p4.master_volume;
        lv_label_set_text_fmt(grid_vol_lbl, "%d", p4.master_volume);
    }

    // Spectrum bars — read from DSP task
    {
        const SpectrumData& sp = dsp_get_spectrum();
        static uint8_t prev_bar_h[16] = {};
        const int MAX_BAR_H = 60;  // max bar height in pixels (pad is 143px)
        for (int i = 0; i < 16; i++) {
            if (!live_spectrum_bars[i]) continue;
            uint8_t h_px = (uint8_t)((sp.bars[i] * MAX_BAR_H) / 255);
            if (h_px == prev_bar_h[i]) continue;
            prev_bar_h[i] = h_px;
            lv_obj_set_height(live_spectrum_bars[i], h_px);
            lv_obj_align(live_spectrum_bars[i], LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_set_style_bg_opa(live_spectrum_bars[i],
                h_px > 0 ? LV_OPA_60 : LV_OPA_0, 0);
        }
    }

    // Ripple animation
    ripple_update();
}

// =============================================================================
// FX LAB SCREEN — 2 pages × 3 circles
//   Page 0: FLANGER (enc0) | DELAY (enc1) | REVERB (enc2)
//   Page 1: DRIVE (pot3)   | CUTOFF (pot1) | RESONANCE (pot2)
// =============================================================================
static int fx_page = 0;   // 0 or 1

// 6 widgets, indices 0-5 (3 per page, page*3 + slot)
static lv_obj_t* fx_arcs[6]        = {};
static lv_obj_t* fx_value_labels[6]= {};
static lv_obj_t* fx_name_labels[6] = {};
static lv_obj_t* fx_toggle_btns[6] = {};  // ON/OFF toggle per FX
static lv_obj_t* fx_pct_ring[6]    = {};  // outer glow ring (decorative)
static lv_obj_t* fx_page_dot[2]    = {};  // page indicator dots
static lv_obj_t* fx_page_lbl       = NULL;

// FX metadata (page × 3)
static const char*    fx_names[6]  = {"FLANGER","DELAY","REVERB","DRIVE","CUTOFF","RESONANCE"};
static const uint32_t fx_colors[6] = {0x58A6FF, 0xB58BFF, 0x39D2C0,
                                       0xFF6B35, 0xFFD700, 0xFF8F5A};
static const char*    fx_src[6]    = {"ENC 1","ENC 2","ENC 3","POT","POT","POT"};

// Callback: toggle FX mute on card click
static void fx_toggle_cb(lv_event_t* e) {
    int cell = (int)(intptr_t)lv_event_get_user_data(e);
    if (cell < 0 || cell > 5) return;
    if (cell < 3) {
        // Encoder FX (0=Flanger, 1=Delay, 2=Reverb)
        int enc_id = cell;  // direct 1:1 mapping
        p4.enc_muted[enc_id] = !p4.enc_muted[enc_id];
        bool m = p4.enc_muted[enc_id];
        if (udp_wifi_connected())
            udp_send_fx_enc(enc_id, p4.enc_value[enc_id], m);
    } else {
        // Pot FX (cell3=pot1, cell4=pot2, cell5=pot3)
        int pot_idx = cell - 3;  // 0,1,2 → pot_muted[0,1,2]
        p4.pot_muted[pot_idx] = !p4.pot_muted[pot_idx];
    }
}

// Callback: page navigation
static void fx_page_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    fx_page = (fx_page + dir + 2) % 2;
    // Update page indicator
    if (fx_page_lbl)
        lv_label_set_text_fmt(fx_page_lbl, "%d / 2", fx_page + 1);
    for (int p = 0; p < 2; p++) {
        if (fx_page_dot[p])
            lv_obj_set_style_bg_opa(fx_page_dot[p], p == fx_page ? LV_OPA_COVER : LV_OPA_30, 0);
    }
    // Show/hide appropriate cells
    int base = fx_page * 3;
    for (int c = 0; c < 6; c++) {
        bool vis = (c >= base && c < base + 3);
        if (fx_arcs[c]) {
            lv_obj_t* card = lv_obj_get_parent(fx_arcs[c]);
            if (card) {
                if (vis) lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
                else     lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static void create_fx_screen(void) {
    scr_fx = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_fx, RED808_BG, 0);
    lv_obj_clear_flag(scr_fx, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_fx);

    // ── Title row ──
    lv_obj_t* title = lv_label_create(scr_fx);
    lv_label_set_text(title, LV_SYMBOL_AUDIO "  FX LAB");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, RED808_ACCENT, 0);
    lv_obj_set_pos(title, 60, 10);

    // ── 3 FX cards (single page: Flanger, Delay, Reverb) ──
    // Canvas: 1024×600, title row ~50px
    const int CARD_Y   = 50;
    const int CARD_H   = LCD_V_RES - CARD_Y - 8;   // ~542px
    const int MARGIN   = 12;
    const int CARD_GAP = 10;
    const int CARD_W   = (LCD_H_RES - 2 * MARGIN - 2 * CARD_GAP) / 3;  // ~330px

    // Arc size: make it large and centered
    const int ARC_SIZE = constrain(CARD_W - 40, 200, 290);  // ~290px

    for (int cell = 0; cell < 3; cell++) {

        int x = MARGIN + cell * (CARD_W + CARD_GAP);

        // Card container
        lv_obj_t* card = lv_obj_create(scr_fx);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_pos(card, x, CARD_Y);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        // Themed card background
        lv_obj_set_style_bg_color(card, RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(fx_colors[cell]), 0);
        lv_obj_set_style_border_opa(card, LV_OPA_40, 0);
        // Outer neon glow
        lv_obj_set_style_outline_width(card, 4, 0);
        lv_obj_set_style_outline_color(card, lv_color_hex(fx_colors[cell]), 0);
        lv_obj_set_style_outline_opa(card, LV_OPA_20, 0);
        lv_obj_set_style_outline_pad(card, 2, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        // Make card clickable for toggle
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, fx_toggle_cb, LV_EVENT_CLICKED, (void*)(intptr_t)cell);

        // FX Name — top center, neon style
        fx_name_labels[cell] = lv_label_create(card);
        lv_label_set_text(fx_name_labels[cell], fx_names[cell]);
        lv_obj_set_style_text_font(fx_name_labels[cell], &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(fx_name_labels[cell], lv_color_hex(fx_colors[cell]), 0);
        lv_obj_set_width(fx_name_labels[cell], CARD_W);
        lv_obj_set_style_text_align(fx_name_labels[cell], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(fx_name_labels[cell], LV_ALIGN_TOP_MID, 0, 14);

        // Source tag — subtle under name
        lv_obj_t* src_lbl = lv_label_create(card);
        lv_label_set_text(src_lbl, fx_src[cell]);
        lv_obj_set_style_text_font(src_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(src_lbl, lv_color_hex(fx_colors[cell] & 0x7F7F7F), 0);
        lv_obj_set_width(src_lbl, CARD_W);
        lv_obj_set_style_text_align(src_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(src_lbl, LV_ALIGN_TOP_MID, 0, 42);

        // ── BIG ARC (neon circle indicator) ──
        fx_arcs[cell] = lv_arc_create(card);
        lv_obj_set_size(fx_arcs[cell], ARC_SIZE, ARC_SIZE);
        lv_obj_align(fx_arcs[cell], LV_ALIGN_CENTER, 0, -18);
        lv_arc_set_rotation(fx_arcs[cell], 135);
        lv_arc_set_bg_angles(fx_arcs[cell], 0, 270);
        lv_arc_set_range(fx_arcs[cell], 0, 127);
        lv_arc_set_value(fx_arcs[cell], 0);
        lv_obj_clear_flag(fx_arcs[cell], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(fx_arcs[cell], NULL, LV_PART_KNOB);
        // Track (background ring) — dim, theme-aware
        lv_obj_set_style_arc_width(fx_arcs[cell], 14, LV_PART_MAIN);
        lv_obj_set_style_arc_color(fx_arcs[cell], RED808_BORDER, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(fx_arcs[cell], LV_OPA_COVER, LV_PART_MAIN);
        // Indicator (filled arc) — neon glow
        lv_obj_set_style_arc_width(fx_arcs[cell], 20, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(fx_arcs[cell], lv_color_hex(fx_colors[cell]), LV_PART_INDICATOR);

        // Value label — center of arc (big neon number)
        fx_value_labels[cell] = lv_label_create(card);
        lv_label_set_text(fx_value_labels[cell], "000");
        lv_obj_set_style_text_font(fx_value_labels[cell], &lv_font_montserrat_40, 0);
        lv_obj_set_style_text_color(fx_value_labels[cell], lv_color_hex(fx_colors[cell]), 0);
        lv_obj_set_width(fx_value_labels[cell], CARD_W);
        lv_obj_set_style_text_align(fx_value_labels[cell], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(fx_value_labels[cell], LV_ALIGN_CENTER, 0, -18);

        // Percentage sub-label
        lv_obj_t* pct = lv_label_create(card);
        lv_label_set_text(pct, "%");
        lv_obj_set_style_text_font(pct, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pct, lv_color_hex(fx_colors[cell] & 0x9F9F9F), 0);
        lv_obj_align(pct, LV_ALIGN_CENTER, ARC_SIZE / 4, -2);

        // ── ON/OFF Toggle Button ──
        fx_toggle_btns[cell] = lv_btn_create(card);
        lv_obj_set_size(fx_toggle_btns[cell], 100, 38);
        lv_obj_align(fx_toggle_btns[cell], LV_ALIGN_BOTTOM_MID, 0, -14);
        lv_obj_set_style_radius(fx_toggle_btns[cell], 19, 0);
        lv_obj_set_style_bg_color(fx_toggle_btns[cell], lv_color_hex(fx_colors[cell]), 0);
        lv_obj_set_style_bg_opa(fx_toggle_btns[cell], LV_OPA_20, 0);
        lv_obj_set_style_border_width(fx_toggle_btns[cell], 2, 0);
        lv_obj_set_style_border_color(fx_toggle_btns[cell], lv_color_hex(fx_colors[cell]), 0);
        lv_obj_set_style_border_opa(fx_toggle_btns[cell], LV_OPA_80, 0);
        lv_obj_set_style_shadow_width(fx_toggle_btns[cell], 12, 0);
        lv_obj_set_style_shadow_color(fx_toggle_btns[cell], lv_color_hex(fx_colors[cell]), 0);
        lv_obj_set_style_shadow_opa(fx_toggle_btns[cell], LV_OPA_40, 0);
        lv_obj_clear_flag(fx_toggle_btns[cell], LV_OBJ_FLAG_CLICKABLE);  // card handles click
        lv_obj_t* tog_lbl = lv_label_create(fx_toggle_btns[cell]);
        lv_label_set_text(tog_lbl, "ON");
        lv_obj_set_style_text_font(tog_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tog_lbl, lv_color_hex(fx_colors[cell]), 0);
        lv_obj_center(tog_lbl);
    }
}

static void update_fx_screen(void) {
    // Page 0 cells 0..2: encoders (Flanger, Delay, Reverb)
    // Page 1 cells 3..5: pots — cell3=DRIVE(pot3), cell4=CUTOFF(pot1), cell5=RESONANCE(pot2)
    // Map each card to its underlying raw value + mute flag.
    struct CellSrc { int val; bool muted; };
    CellSrc src[6] = {
        { p4.enc_value[0], p4.enc_muted[0] },  // Flanger
        { p4.enc_value[1], p4.enc_muted[1] },  // Delay
        { p4.enc_value[2], p4.enc_muted[2] },  // Reverb
        { p4.pot_value[3], p4.pot_muted[0] },  // Drive (S3 pot3 → pot_muted[0])
        { p4.pot_value[1], p4.pot_muted[1] },  // Cutoff (disabled, pot1 unused)
        { p4.pot_value[2], p4.pot_muted[2] },  // Resonance (S3 pot2)
    };

    for (int cell = 0; cell < 6; cell++) {
        int val   = src[cell].val;
        bool muted = src[cell].muted;

        int display_val = muted ? 0 : val;
        int pct = (int)((float)display_val / 127.0f * 100.0f + 0.5f);

        if (fx_arcs[cell])
            lv_arc_set_value(fx_arcs[cell], display_val);

        if (fx_value_labels[cell])
            lv_label_set_text_fmt(fx_value_labels[cell], "%d", pct);

        // Update card border glow intensity based on value
        lv_obj_t* card = fx_arcs[cell] ? lv_obj_get_parent(fx_arcs[cell]) : NULL;
        if (card && !muted && val > 0) {
            lv_obj_set_style_border_opa(card, LV_OPA_90, 0);
            lv_obj_set_style_outline_opa(card, LV_OPA_50, 0);
        } else if (card) {
            lv_obj_set_style_border_opa(card, muted ? LV_OPA_20 : LV_OPA_40, 0);
            lv_obj_set_style_outline_opa(card, LV_OPA_10, 0);
        }

        // Update toggle button
        if (fx_toggle_btns[cell]) {
            lv_obj_t* lbl = lv_obj_get_child(fx_toggle_btns[cell], 0);
            if (lbl) lv_label_set_text(lbl, muted ? "OFF" : "ON");
            lv_color_t tc = lv_color_hex(fx_colors[cell]);
            lv_obj_set_style_bg_opa(fx_toggle_btns[cell], muted ? LV_OPA_10 : LV_OPA_20, 0);
            lv_obj_set_style_shadow_opa(fx_toggle_btns[cell], muted ? LV_OPA_0 : LV_OPA_40, 0);
            if (lbl) lv_obj_set_style_text_color(lbl, muted ? RED808_TEXT_DIM : tc, 0);
        }

        // Update arc indicator color (dim if muted)
        if (fx_arcs[cell]) {
            lv_obj_set_style_arc_opa(fx_arcs[cell], muted ? LV_OPA_20 : LV_OPA_COVER, LV_PART_INDICATOR);
        }
    }
}

// =============================================================================
// =============================================================================
// SEQUENCER SCREEN — Studio-grade 16-track × 16-step grid (1024×600)
// =============================================================================
static lv_obj_t* seq_step_btns[16][16]  = {};
static lv_obj_t* seq_track_labels[16]   = {};
static lv_obj_t* seq_mute_btns[16]      = {};
static lv_obj_t* seq_solo_btns[16]      = {};
static lv_obj_t* seq_solo_labels[16]    = {};
static lv_obj_t* seq_ruler_labels[16]   = {};  // beat/step number ruler
static lv_obj_t* seq_beat_bg[4]         = {};  // beat group shading panels
static lv_obj_t* seq_playhead_line      = NULL; // glowing vertical playhead
static lv_obj_t* seq_status_step_lbl    = NULL; // bottom "STEP 05 / 16"
static lv_obj_t* seq_status_pat_lbl     = NULL; // bottom "PATTERN 01"
static int        seq_step_x[16]        = {};  // precomputed step column X

// Layout — landscape 1024×600 (LCD native, LVGL canvas)
static const int SEQ_RULER_Y    = 44;   // ruler starts below header
static const int SEQ_RULER_H    = 14;   // ruler height
static const int SEQ_GRID_Y     = 58;   // first track row Y
static const int SEQ_TRACK_H    = 32;   // track row height
static const int SEQ_TRACK_GAP  = 1;    // gap between rows
static const int SEQ_STRIPE_W   = 4;    // left color accent stripe
static const int SEQ_NAME_X     = 4;    // track name button X
static const int SEQ_NAME_W     = 54;   // track name button width
static const int SEQ_GRID_X     = 62;   // step grid start X
static const int SEQ_CELL_W     = 54;   // step cell width
static const int SEQ_BEAT_GAP   = 5;    // gap between beat groups (every 4 steps)
static const int SEQ_CELL_GAP   = 1;    // gap between cells within a beat
static const int SEQ_SOLO_X     = 960;  // solo button X
static const int SEQ_SOLO_W     = 32;   // solo button width
static const int SEQ_STATUS_Y   = 586;  // bottom status bar Y
static const int SEQ_STATUS_H   = 14;   // bottom status bar height

static void seq_step_cb(lv_event_t* e) {
    int data = (int)(intptr_t)lv_event_get_user_data(e);
    int track = (data >> 8) & 0xFF;
    int step  = data & 0xFF;
    if (track < 16 && step < 16) {
        bool next = !p4.steps[track][step];
        p4.steps[track][step] = next;
        if (ui_use_udp_transport()) udp_send_set_step(track, step, next);
        // Push updated pattern to S3 (so S3 pad-sync sees the change)
        uart_send_pattern_to_s3(p4.current_pattern, p4.steps);
    }
}

static void seq_mute_cb(lv_event_t* e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    if (track < 16) {
        bool next = !p4.track_muted[track];
        p4.track_muted[track] = next;
        if (ui_use_udp_transport()) udp_send_mute(track, next);
    }
}

static void seq_solo_cb(lv_event_t* e) {
    int track = (int)(intptr_t)lv_event_get_user_data(e);
    if (track >= 16) return;
    // Exclusive solo: toggle OFF if already soloed; otherwise clear all and solo this one
    bool newSolo = !p4.track_solo[track];
    for (int i = 0; i < 16; i++) p4.track_solo[i] = false;
    p4.track_solo[track] = newSolo;
    if (ui_use_udp_transport()) udp_send_solo(track, newSolo);
}

static void create_sequencer_screen(void) {
    scr_sequencer = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sequencer, RED808_BG, 0);
    lv_obj_clear_flag(scr_sequencer, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_sequencer);

    // ── Precompute step X positions ──
    {
        int xOff = SEQ_GRID_X;
        for (int s = 0; s < 16; s++) {
            if (s > 0 && (s % 4) == 0) xOff += SEQ_BEAT_GAP;
            seq_step_x[s] = xOff;
            xOff += SEQ_CELL_W + SEQ_CELL_GAP;
        }
    }
    const int grid_bottom = SEQ_GRID_Y + 16 * (SEQ_TRACK_H + SEQ_TRACK_GAP) - SEQ_TRACK_GAP;

    // ── Beat group shading panels (drawn first = behind everything) ──
    // 4 groups × 4 steps each, alternating subtle bg
    const uint32_t beat_shade[2] = { 0x0B0B0B, 0x0F0F0F };
    for (int b = 0; b < 4; b++) {
        int bx = seq_step_x[b * 4];
        int bw = seq_step_x[b * 4 + 3] + SEQ_CELL_W - bx + 2;
        seq_beat_bg[b] = lv_obj_create(scr_sequencer);
        lv_obj_set_pos(seq_beat_bg[b], bx - 1, SEQ_RULER_Y);
        lv_obj_set_size(seq_beat_bg[b], bw, grid_bottom - SEQ_RULER_Y + SEQ_STATUS_H);
        lv_obj_set_style_bg_color(seq_beat_bg[b], lv_color_hex(beat_shade[b & 1]), 0);
        lv_obj_set_style_bg_opa(seq_beat_bg[b], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(seq_beat_bg[b], 0, 0);
        lv_obj_set_style_radius(seq_beat_bg[b], 0, 0);
        lv_obj_clear_flag(seq_beat_bg[b], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    // ── Beat separator lines between groups ──
    for (int b = 1; b < 4; b++) {
        lv_obj_t* sep = lv_obj_create(scr_sequencer);
        lv_obj_set_pos(sep, seq_step_x[b * 4] - 3, SEQ_RULER_Y);
        lv_obj_set_size(sep, 1, grid_bottom - SEQ_RULER_Y);
        lv_obj_set_style_bg_color(sep, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_set_style_radius(sep, 0, 0);
        lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    // ── Step ruler: beat numbers 1–16 ──
    for (int s = 0; s < 16; s++) {
        bool is_beat_start = (s % 4 == 0);
        seq_ruler_labels[s] = lv_label_create(scr_sequencer);
        lv_label_set_text_fmt(seq_ruler_labels[s], "%d", s + 1);
        lv_obj_set_style_text_font(seq_ruler_labels[s],
            is_beat_start ? &lv_font_montserrat_14 : &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(seq_ruler_labels[s],
            is_beat_start ? RED808_ACCENT : RED808_TEXT_DIM, 0);
        lv_obj_set_width(seq_ruler_labels[s], SEQ_CELL_W);
        lv_obj_set_pos(seq_ruler_labels[s], seq_step_x[s], SEQ_RULER_Y);
        lv_obj_set_style_text_align(seq_ruler_labels[s], LV_TEXT_ALIGN_CENTER, 0);
    }

    // ── Ruler bottom separator line ──
    {
        lv_obj_t* rl = lv_obj_create(scr_sequencer);
        lv_obj_set_pos(rl, SEQ_GRID_X - 2, SEQ_GRID_Y - 2);
        lv_obj_set_size(rl, seq_step_x[15] + SEQ_CELL_W - SEQ_GRID_X + 4, 1);
        lv_obj_set_style_bg_color(rl, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(rl, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rl, 0, 0);
        lv_obj_set_style_radius(rl, 0, 0);
        lv_obj_clear_flag(rl, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    // ── Track rows ──
    for (int t = 0; t < 16; t++) {
        int rowY = SEQ_GRID_Y + t * (SEQ_TRACK_H + SEQ_TRACK_GAP);
        lv_color_t tc = lv_color_hex(theme_presets[currentTheme].track_colors[t]);

        // Alternating row background for legibility
        lv_obj_t* row_bg = lv_obj_create(scr_sequencer);
        lv_obj_set_pos(row_bg, 0, rowY);
        lv_obj_set_size(row_bg, LCD_H_RES, SEQ_TRACK_H);
        lv_obj_set_style_bg_color(row_bg, lv_color_hex(t & 1 ? 0x0E0E0E : 0x0A0A0A), 0);
        lv_obj_set_style_bg_opa(row_bg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row_bg, 0, 0);
        lv_obj_set_style_radius(row_bg, 0, 0);
        lv_obj_clear_flag(row_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        // Left color accent stripe
        lv_obj_t* stripe = lv_obj_create(scr_sequencer);
        lv_obj_set_pos(stripe, 0, rowY);
        lv_obj_set_size(stripe, SEQ_STRIPE_W, SEQ_TRACK_H);
        lv_obj_set_style_bg_color(stripe, tc, 0);
        lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(stripe, 0, 0);
        lv_obj_set_style_radius(stripe, 0, 0);
        lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        // Track name + number button (tap = mute toggle)
        seq_mute_btns[t] = lv_btn_create(scr_sequencer);
        lv_obj_set_size(seq_mute_btns[t], SEQ_NAME_W, SEQ_TRACK_H);
        lv_obj_set_pos(seq_mute_btns[t], SEQ_NAME_X, rowY);
        lv_obj_set_style_radius(seq_mute_btns[t], 3, 0);
        lv_obj_set_style_bg_color(seq_mute_btns[t], RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(seq_mute_btns[t], LV_OPA_50, 0);
        lv_obj_set_style_border_width(seq_mute_btns[t], 1, 0);
        lv_obj_set_style_border_color(seq_mute_btns[t], tc, 0);
        lv_obj_set_style_border_opa(seq_mute_btns[t], LV_OPA_60, 0);
        lv_obj_set_style_shadow_width(seq_mute_btns[t], 0, 0);
        lv_obj_set_style_pad_all(seq_mute_btns[t], 0, 0);
        lv_obj_add_event_cb(seq_mute_btns[t], seq_mute_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t);

        // Track label: "01\nBD" — number above, name below
        seq_track_labels[t] = lv_label_create(seq_mute_btns[t]);
        lv_label_set_text_fmt(seq_track_labels[t], "%02d\n%s", t + 1, trackNames[t]);
        lv_obj_set_style_text_font(seq_track_labels[t], &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(seq_track_labels[t], tc, 0);
        lv_obj_set_style_text_align(seq_track_labels[t], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(seq_track_labels[t], 1, 0);
        lv_obj_center(seq_track_labels[t]);

        // ── 16 step cells ──
        for (int s = 0; s < 16; s++) {
            bool act = p4.steps[t][s];
            seq_step_btns[t][s] = lv_obj_create(scr_sequencer);
            lv_obj_set_size(seq_step_btns[t][s], SEQ_CELL_W, SEQ_TRACK_H);
            lv_obj_set_pos(seq_step_btns[t][s], seq_step_x[s], rowY);
            lv_obj_set_style_radius(seq_step_btns[t][s], 4, 0);
            lv_obj_set_style_bg_color(seq_step_btns[t][s], act ? tc : RED808_SURFACE, 0);
            lv_obj_set_style_bg_opa(seq_step_btns[t][s], act ? LV_OPA_80 : LV_OPA_40, 0);
            lv_obj_set_style_border_width(seq_step_btns[t][s], 1, 0);
            lv_obj_set_style_border_color(seq_step_btns[t][s], act ? tc : lv_color_hex(0x1E1E1E), 0);
            lv_obj_set_style_shadow_width(seq_step_btns[t][s], act ? 8 : 0, 0);
            lv_obj_set_style_shadow_color(seq_step_btns[t][s], tc, 0);
            lv_obj_set_style_shadow_opa(seq_step_btns[t][s], LV_OPA_60, 0);
            lv_obj_clear_flag(seq_step_btns[t][s], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(seq_step_btns[t][s], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(seq_step_btns[t][s], seq_step_cb, LV_EVENT_CLICKED,
                                (void*)(intptr_t)((t << 8) | s));

            // Bottom accent line inside active cells (velocity-like feel)
            if (act) {
                lv_obj_t* accent = lv_obj_create(seq_step_btns[t][s]);
                lv_obj_set_size(accent, SEQ_CELL_W - 8, 3);
                lv_obj_align(accent, LV_ALIGN_BOTTOM_MID, 0, -3);
                lv_obj_set_style_bg_color(accent, lv_color_white(), 0);
                lv_obj_set_style_bg_opa(accent, LV_OPA_40, 0);
                lv_obj_set_style_radius(accent, 2, 0);
                lv_obj_set_style_border_width(accent, 0, 0);
                lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            }
        }

        // ── Solo button ──
        seq_solo_btns[t] = lv_btn_create(scr_sequencer);
        lv_obj_set_size(seq_solo_btns[t], SEQ_SOLO_W, SEQ_TRACK_H);
        lv_obj_set_pos(seq_solo_btns[t], SEQ_SOLO_X, rowY);
        lv_obj_set_style_radius(seq_solo_btns[t], 3, 0);
        lv_obj_set_style_bg_color(seq_solo_btns[t], RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(seq_solo_btns[t], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(seq_solo_btns[t], 1, 0);
        lv_obj_set_style_border_color(seq_solo_btns[t], RED808_BORDER, 0);
        lv_obj_set_style_shadow_width(seq_solo_btns[t], 0, 0);
        lv_obj_set_style_pad_all(seq_solo_btns[t], 0, 0);
        lv_obj_add_event_cb(seq_solo_btns[t], seq_solo_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t);
        seq_solo_labels[t] = lv_label_create(seq_solo_btns[t]);
        lv_label_set_text(seq_solo_labels[t], "S");
        lv_obj_set_style_text_font(seq_solo_labels[t], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(seq_solo_labels[t], RED808_TEXT_DIM, 0);
        lv_obj_center(seq_solo_labels[t]);
    }   // end for(t)

    // ── Glowing vertical playhead (spans all rows, created last = on top) ──
    seq_playhead_line = lv_obj_create(scr_sequencer);
    lv_obj_set_pos(seq_playhead_line, seq_step_x[0], SEQ_RULER_Y);
    lv_obj_set_size(seq_playhead_line, SEQ_CELL_W, grid_bottom - SEQ_RULER_Y);
    lv_obj_set_style_radius(seq_playhead_line, 0, 0);
    lv_obj_set_style_bg_color(seq_playhead_line, RED808_WARNING, 0);
    lv_obj_set_style_bg_opa(seq_playhead_line, LV_OPA_20, 0);
    lv_obj_set_style_border_width(seq_playhead_line, 0, 0);
    lv_obj_set_style_border_color(seq_playhead_line, RED808_WARNING, 0);
    lv_obj_set_style_shadow_width(seq_playhead_line, 28, 0);
    lv_obj_set_style_shadow_color(seq_playhead_line, RED808_WARNING, 0);
    lv_obj_set_style_shadow_opa(seq_playhead_line, LV_OPA_50, 0);
    lv_obj_clear_flag(seq_playhead_line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(seq_playhead_line, LV_OBJ_FLAG_HIDDEN);

    // ── Status bar (bottom strip) ──
    {
        lv_obj_t* bar = lv_obj_create(scr_sequencer);
        lv_obj_set_pos(bar, 0, SEQ_STATUS_Y);
        lv_obj_set_size(bar, LCD_H_RES, SEQ_STATUS_H);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x060606), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        seq_status_pat_lbl = lv_label_create(scr_sequencer);
        lv_label_set_text_fmt(seq_status_pat_lbl, "PATTERN %02d", p4.current_pattern + 1);
        lv_obj_set_style_text_font(seq_status_pat_lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(seq_status_pat_lbl, RED808_TEXT_DIM, 0);
        lv_obj_set_pos(seq_status_pat_lbl, 70, SEQ_STATUS_Y + 2);

        seq_status_step_lbl = lv_label_create(scr_sequencer);
        lv_label_set_text(seq_status_step_lbl, "STEP -- / 16");
        lv_obj_set_style_text_font(seq_status_step_lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(seq_status_step_lbl, RED808_ACCENT, 0);
        lv_obj_set_width(seq_status_step_lbl, 120);
        lv_obj_set_style_text_align(seq_status_step_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(seq_status_step_lbl, LCD_H_RES / 2 - 60, SEQ_STATUS_Y + 2);

        lv_obj_t* bpm_lbl = lv_label_create(scr_sequencer);
        lv_label_set_text_fmt(bpm_lbl, "BPM %d.%d", p4.bpm_int, p4.bpm_frac);
        lv_obj_set_style_text_font(bpm_lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(bpm_lbl, RED808_INFO, 0);
        lv_obj_set_pos(bpm_lbl, LCD_H_RES - 90, SEQ_STATUS_Y + 2);
    }
}   // end create_sequencer_screen

static void update_sequencer_screen(void) {
    int step = p4.current_step;
    bool playing = p4.is_playing;

    // ── Move / show glowing playhead line ──
    if (seq_playhead_line) {
        if (playing && step >= 0 && step < 16) {
            lv_obj_set_x(seq_playhead_line, seq_step_x[step]);
            lv_obj_clear_flag(seq_playhead_line, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(seq_playhead_line, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // ── Status bar step counter ──
    if (seq_status_step_lbl) {
        if (playing && step >= 0 && step < 16) {
            lv_label_set_text_fmt(seq_status_step_lbl, "STEP %02d / 16", step + 1);
        } else {
            lv_label_set_text(seq_status_step_lbl, "STEP -- / 16");
        }
    }
    if (seq_status_pat_lbl) {
        lv_label_set_text_fmt(seq_status_pat_lbl, "PATTERN %02d", p4.current_pattern + 1);
    }

    // ── Per-track updates ──
    for (int t = 0; t < 16; t++) {
        lv_color_t tc = lv_color_hex(theme_presets[currentTheme].track_colors[t]);
        bool muted = p4.track_muted[t];
        bool soloed = p4.track_solo[t];

        // Mute button appearance
        if (seq_mute_btns[t]) {
            if (muted) {
                lv_obj_set_style_bg_color(seq_mute_btns[t], RED808_ERROR, 0);
                lv_obj_set_style_bg_opa(seq_mute_btns[t], LV_OPA_90, 0);
                lv_obj_set_style_border_color(seq_mute_btns[t], RED808_ERROR, 0);
            } else {
                lv_obj_set_style_bg_color(seq_mute_btns[t], RED808_SURFACE, 0);
                lv_obj_set_style_bg_opa(seq_mute_btns[t], LV_OPA_50, 0);
                lv_obj_set_style_border_color(seq_mute_btns[t], tc, 0);
            }
        }
        if (seq_track_labels[t]) {
            lv_obj_set_style_text_color(seq_track_labels[t],
                muted ? lv_color_white() : tc, 0);
        }

        // Solo button
        if (seq_solo_btns[t]) {
            lv_obj_set_style_bg_color(seq_solo_btns[t], soloed ? tc : RED808_SURFACE, 0);
            lv_obj_set_style_border_color(seq_solo_btns[t], soloed ? tc : RED808_BORDER, 0);
            lv_obj_set_style_shadow_width(seq_solo_btns[t], soloed ? 14 : 0, 0);
            lv_obj_set_style_shadow_color(seq_solo_btns[t], tc, 0);
        }
        if (seq_solo_labels[t]) {
            lv_obj_set_style_text_color(seq_solo_labels[t],
                soloed ? lv_color_black() : RED808_TEXT_DIM, 0);
        }

        // Step cells
        for (int s = 0; s < 16; s++) {
            if (!seq_step_btns[t][s]) continue;
            bool active = p4.steps[t][s];
            bool is_cur = playing && (step == s);

            lv_color_t bg;
            lv_opa_t opa;
            lv_color_t border;
            int shadow_w;

            if (is_cur && active) {
                bg = lv_color_white();
                opa = LV_OPA_COVER;
                border = tc;
                shadow_w = 20;
            } else if (is_cur) {
                bg = lv_color_hex(0x262626);
                opa = LV_OPA_COVER;
                border = RED808_WARNING;
                shadow_w = 0;
            } else if (active) {
                bg = tc;
                opa = muted ? LV_OPA_20 : LV_OPA_80;
                border = tc;
                shadow_w = muted ? 0 : 8;
            } else {
                bg = RED808_SURFACE;
                opa = LV_OPA_40;
                border = lv_color_hex(0x1E1E1E);
                shadow_w = 0;
            }

            lv_obj_set_style_bg_color(seq_step_btns[t][s], bg, 0);
            lv_obj_set_style_bg_opa(seq_step_btns[t][s], opa, 0);
            lv_obj_set_style_border_color(seq_step_btns[t][s], border, 0);
            lv_obj_set_style_shadow_width(seq_step_btns[t][s], shadow_w, 0);
            lv_obj_set_style_shadow_color(seq_step_btns[t][s],
                is_cur ? RED808_WARNING : tc, 0);
        }
    }
}

// =============================================================================
// MIXER SCREEN — 16 track faders in a single row
// =============================================================================
static lv_obj_t* vol_sliders[16] = {};
static lv_obj_t* vol_labels[16] = {};
static lv_obj_t* vol_name_labels[16] = {};
static lv_obj_t* vol_mute_dots[16] = {};
static lv_obj_t* vol_strip_panels[16] = {};

static void vol_slider_cb(lv_event_t* e) {
    int trk = (int)(intptr_t)lv_event_get_user_data(e);
    if (trk < 0 || trk >= 16) return;
    lv_obj_t* slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    p4.track_volume[trk] = val;
    udp_send_set_track_volume(trk, val);
    uart_send_to_s3(MSG_TRACK, TRK_VOLUME | (trk & 0x0F), (uint8_t)val);
}

static void create_volumes_screen(void) {
    scr_volumes = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_volumes, RED808_BG, 0);
    lv_obj_clear_flag(scr_volumes, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_header(scr_volumes);

    // Layout in actual LVGL canvas coordinates (landscape 1024×600)
    const int LW = LCD_H_RES;   // 1024 — full display width
    const int LH = LCD_V_RES;   // 600  — full display height

    lv_obj_t* title = lv_label_create(scr_volumes);
    lv_label_set_text(title, LV_SYMBOL_VOLUME_MAX "  MIXER");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(title, 60, 10);

    // Single row of 16 strips filling the full display width
    int margin = 10;
    int gap    = 4;
    int total_w = LW - 2 * margin;
    int strip_w = (total_w - 15 * gap) / 16;   // ~56px each
    int y_top   = 50;
    int y_bottom = LH - 8;
    int strip_h  = y_bottom - y_top;            // ~508px
    int name_h   = 14;
    int value_h  = 14;
    int mute_h   = 10;
    int slider_h = strip_h - name_h - value_h - mute_h - 18;

    for (int i = 0; i < 16; i++) {
        int x = margin + i * (strip_w + gap);
        int cx = x + strip_w / 2;
        lv_color_t tc = lv_color_hex(theme_presets[currentTheme].track_colors[i]);

        // Strip panel background
        vol_strip_panels[i] = lv_obj_create(scr_volumes);
        lv_obj_set_size(vol_strip_panels[i], strip_w, strip_h);
        lv_obj_set_pos(vol_strip_panels[i], x, y_top);
        lv_obj_set_style_bg_color(vol_strip_panels[i], RED808_SURFACE, 0);
        lv_obj_set_style_bg_opa(vol_strip_panels[i], LV_OPA_40, 0);
        lv_obj_set_style_radius(vol_strip_panels[i], 8, 0);
        lv_obj_set_style_border_width(vol_strip_panels[i], 1, 0);
        lv_obj_set_style_border_color(vol_strip_panels[i], RED808_BORDER, 0);
        lv_obj_clear_flag(vol_strip_panels[i], LV_OBJ_FLAG_SCROLLABLE);

        // Track name
        vol_name_labels[i] = lv_label_create(scr_volumes);
        lv_label_set_text(vol_name_labels[i], trackNames[i]);
        lv_obj_set_style_text_font(vol_name_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(vol_name_labels[i], tc, 0);
        lv_obj_set_style_text_align(vol_name_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(vol_name_labels[i], x, y_top + 2);
        lv_obj_set_width(vol_name_labels[i], strip_w);

        // Vertical slider (fader)
        int y_sl = y_top + name_h + 4;
        vol_sliders[i] = lv_slider_create(scr_volumes);
        lv_obj_set_size(vol_sliders[i], 8, slider_h);
        lv_obj_set_pos(vol_sliders[i], cx - 4, y_sl);
        lv_slider_set_range(vol_sliders[i], 0, Config::MAX_VOLUME);
        lv_slider_set_value(vol_sliders[i], p4.track_volume[i], LV_ANIM_OFF);
        lv_obj_set_style_bg_color(vol_sliders[i], lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vol_sliders[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(vol_sliders[i], 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(vol_sliders[i], tc, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(vol_sliders[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(vol_sliders[i], 4, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(vol_sliders[i], lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(vol_sliders[i], LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(vol_sliders[i], 5, LV_PART_KNOB);
        lv_obj_set_style_radius(vol_sliders[i], LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_shadow_color(vol_sliders[i], tc, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(vol_sliders[i], 8, LV_PART_KNOB);
        lv_obj_set_style_shadow_opa(vol_sliders[i], LV_OPA_60, LV_PART_KNOB);
        lv_obj_set_style_border_color(vol_sliders[i], tc, LV_PART_KNOB);
        lv_obj_set_style_border_width(vol_sliders[i], 2, LV_PART_KNOB);
        lv_obj_add_event_cb(vol_sliders[i], vol_slider_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);

        // Color bar at bottom of slider
        lv_obj_t* color_bar = lv_obj_create(scr_volumes);
        lv_obj_set_size(color_bar, strip_w - 6, 3);
        lv_obj_set_pos(color_bar, x + 3, y_sl + slider_h + 2);
        lv_obj_set_style_bg_color(color_bar, tc, 0);
        lv_obj_set_style_bg_opa(color_bar, LV_OPA_80, 0);
        lv_obj_set_style_radius(color_bar, 1, 0);
        lv_obj_set_style_border_width(color_bar, 0, 0);
        lv_obj_clear_flag(color_bar, LV_OBJ_FLAG_SCROLLABLE);

        // Value label
        int y_val = y_sl + slider_h + 8;
        vol_labels[i] = lv_label_create(scr_volumes);
        lv_label_set_text_fmt(vol_labels[i], "%d", p4.track_volume[i]);
        lv_obj_set_style_text_font(vol_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(vol_labels[i], RED808_TEXT, 0);
        lv_obj_set_style_text_align(vol_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(vol_labels[i], x, y_val);
        lv_obj_set_width(vol_labels[i], strip_w);

        // Mute dot
        vol_mute_dots[i] = lv_obj_create(scr_volumes);
        lv_obj_set_size(vol_mute_dots[i], 8, 8);
        lv_obj_set_pos(vol_mute_dots[i], cx - 4, y_val + value_h + 2);
        lv_obj_set_style_radius(vol_mute_dots[i], 4, 0);
        lv_obj_set_style_bg_color(vol_mute_dots[i], RED808_SUCCESS, 0);
        lv_obj_set_style_bg_opa(vol_mute_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(vol_mute_dots[i], 0, 0);
        lv_obj_clear_flag(vol_mute_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void update_volumes_screen(void) {
    for (int i = 0; i < 16; i++) {
        if (vol_sliders[i]) lv_slider_set_value(vol_sliders[i], p4.track_volume[i], LV_ANIM_OFF);
        if (vol_labels[i]) lv_label_set_text_fmt(vol_labels[i], "%d", p4.track_volume[i]);
        if (vol_mute_dots[i]) {
            lv_obj_set_style_bg_color(vol_mute_dots[i],
                p4.track_muted[i] ? RED808_ERROR : RED808_SUCCESS, 0);
        }
        if (vol_strip_panels[i]) {
            lv_obj_set_style_border_color(vol_strip_panels[i],
                p4.track_muted[i] ? RED808_ERROR : RED808_BORDER, 0);
            lv_obj_set_style_bg_opa(vol_strip_panels[i],
                p4.track_muted[i] ? LV_OPA_20 : LV_OPA_40, 0);
        }
        if (vol_name_labels[i]) {
            lv_obj_set_style_text_color(vol_name_labels[i],
                p4.track_muted[i] ? RED808_TEXT_DIM :
                lv_color_hex(theme_presets[currentTheme].track_colors[i]), 0);
        }
    }
}

// =============================================================================
// SD CARD SCREEN — Remote browse S3's SD card via UART
// =============================================================================

// SD screen widgets
static lv_obj_t* sd_left_panel  = NULL;
static lv_obj_t* sd_right_panel = NULL;
static lv_obj_t* sd_status_lbl  = NULL;
static lv_obj_t* sd_path_lbl    = NULL;
static lv_obj_t* sd_file_list   = NULL;
static lv_obj_t* sd_selected_lbl = NULL;
static lv_obj_t* sd_pad_btns[16] = {};
static lv_obj_t* sd_load_btn    = NULL;
static lv_obj_t* sd_load_lbl    = NULL;
// MIDI section
static lv_obj_t* sd_wav_section       = NULL;
static lv_obj_t* sd_midi_section      = NULL;
static lv_obj_t* sd_midi_pat_btns[10] = {};
static lv_obj_t* sd_midi_load_btn     = NULL;
static lv_obj_t* sd_midi_info_lbl     = NULL;
static lv_obj_t* sd_midi_status_lbl   = NULL;
static int        sd_midi_target_slot  = 6;   // default: P07
static bool       sd_is_midi_mode      = false;

// Forward declarations
static void sd_refresh_ui(void);
static void sd_switch_panel_mode(bool midi_mode);
static void sd_midi_pat_btn_cb(lv_event_t* e);
static void sd_midi_load_btn_cb(lv_event_t* e);

static void sd_switch_panel_mode(bool midi_mode) {
    sd_is_midi_mode = midi_mode;
    if (sd_wav_section) {
        if (midi_mode) lv_obj_add_flag(sd_wav_section, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_clear_flag(sd_wav_section, LV_OBJ_FLAG_HIDDEN);
    }
    if (sd_midi_section) {
        if (midi_mode) lv_obj_clear_flag(sd_midi_section, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(sd_midi_section, LV_OBJ_FLAG_HIDDEN);
    }
}

static void sd_file_btn_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= p4sd.entry_count) return;
    const P4SdEntry& entry = p4sd.entries[idx];
    if (!entry.is_dir) {
        // Track selection type immediately (before S3 response arrives)
        p4sd.selected_is_midi = entry.is_midi;
        sd_switch_panel_mode(entry.is_midi);
        if (entry.is_midi && sd_midi_info_lbl) {
            lv_label_set_text(sd_midi_info_lbl, entry.name);
        }
        if (entry.is_midi && sd_midi_load_btn) {
            lv_obj_clear_state(sd_midi_load_btn, LV_STATE_DISABLED);
        }
        if (entry.is_midi && sd_midi_status_lbl) {
            lv_label_set_text(sd_midi_status_lbl, "");
        }
        if (entry.is_midi) {
            // Reset any stale load result when picking a new file
            p4sd.midi_load_result = -2;
        }
    }
    uart_send_sd_select((uint8_t)idx);
}

static void sd_back_btn_cb(lv_event_t* e) {
    (void)e;
    p4sd.selected_is_midi = false;
    sd_switch_panel_mode(false);
    uart_send_sd_back();
}

static void sd_pad_btn_cb(lv_event_t* e) {
    int pad = (int)(intptr_t)lv_event_get_user_data(e);
    if (pad < 0 || pad >= 16) return;
    p4sd.selected_pad = pad;
    // Update pad button highlights
    for (int i = 0; i < 16; i++) {
        if (sd_pad_btns[i]) {
            lv_obj_set_style_bg_color(sd_pad_btns[i],
                i == pad ? RED808_ACCENT : lv_color_hex(0x222233), 0);
        }
    }
}

static void sd_midi_pat_btn_cb(lv_event_t* e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot < 0 || slot > 5) return;
    sd_midi_target_slot = slot;
    for (int i = 0; i < 6; i++) {
        if (!sd_midi_pat_btns[i]) continue;
        bool sel = (i == slot);
        lv_obj_set_style_bg_color(sd_midi_pat_btns[i],
            sel ? RED808_ACCENT : lv_color_hex(0x1A2A3A), 0);
        lv_obj_set_style_border_color(sd_midi_pat_btns[i],
            sel ? RED808_CYAN : lv_color_hex(0x334455), 0);
    }
}

static void sd_midi_load_btn_cb(lv_event_t* e) {
    (void)e;
    if (p4sd.selected_file[0] == '\0') return;
    if (sd_midi_status_lbl) lv_label_set_text(sd_midi_status_lbl, "Loading MIDI...");
    uart_send_sd_load_midi((uint8_t)sd_midi_target_slot);
}

static void sd_load_btn_cb(lv_event_t* e) {
    (void)e;
    if (p4sd.selected_file[0] == '\0') return;
    uart_send_sd_load((uint8_t)p4sd.selected_pad);
}

static void sd_refresh_ui(void) {
    if (!sd_file_list) return;
    lv_obj_clean(sd_file_list);

    // Update status
    if (sd_status_lbl) {
        lv_label_set_text(sd_status_lbl, p4sd.mounted ? "READY" : "NO SD CARD");
        lv_obj_set_style_text_color(sd_status_lbl,
            p4sd.mounted ? RED808_SUCCESS : RED808_WARNING, 0);
    }
    // Update path
    if (sd_path_lbl) lv_label_set_text(sd_path_lbl, p4sd.path);

    // Update selected file
    if (sd_selected_lbl) {
        if (p4sd.selected_file[0])
            lv_label_set_text(sd_selected_lbl, p4sd.selected_file);
        else
            lv_label_set_text(sd_selected_lbl, "");
    }
    // Enable/disable LOAD button
    if (sd_load_btn) {
        if (p4sd.selected_file[0] && !p4sd.selected_is_midi)
            lv_obj_clear_state(sd_load_btn, LV_STATE_DISABLED);
        else
            lv_obj_add_state(sd_load_btn, LV_STATE_DISABLED);
    }
    if (sd_midi_load_btn) {
        if (p4sd.selected_file[0] && p4sd.selected_is_midi)
            lv_obj_clear_state(sd_midi_load_btn, LV_STATE_DISABLED);
        else
            lv_obj_add_state(sd_midi_load_btn, LV_STATE_DISABLED);
    }
    // MIDI status: update label from midi_load_result set by uart_handler
    if (sd_midi_status_lbl && p4sd.selected_is_midi) {
        int8_t r = p4sd.midi_load_result;
        if (r == -1) {
            lv_label_set_text(sd_midi_status_lbl, "Loading MIDI...");
            lv_obj_set_style_text_color(sd_midi_status_lbl, RED808_WARNING, 0);
        } else if (r == 0x7F) {
            lv_label_set_text(sd_midi_status_lbl, "MIDI parse failed");
            lv_obj_set_style_text_color(sd_midi_status_lbl, RED808_ACCENT, 0);
        } else if (r >= 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Loaded \u2192 Pat %02d", r + 1);
            lv_label_set_text(sd_midi_status_lbl, buf);
            lv_obj_set_style_text_color(sd_midi_status_lbl, RED808_SUCCESS, 0);
        }
    }

    if (!p4sd.mounted) {
        lv_obj_t* lbl = lv_label_create(sd_file_list);
        lv_label_set_text(lbl, "SD NOT MOUNTED");
        lv_obj_set_style_text_color(lbl, RED808_ACCENT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        return;
    }

    // "Back" button if not root
    if (strcmp(p4sd.path, "/") != 0 && p4sd.path[0] != '\0') {
        lv_obj_t* back_btn = lv_btn_create(sd_file_list);
        lv_obj_set_size(back_btn, 580, 44);
        lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333344), 0);
        lv_obj_set_style_radius(back_btn, 6, 0);
        lv_obj_t* back_lbl = lv_label_create(back_btn);
        lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  .. (back)");
        lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xCCCCCC), 0);
        lv_obj_center(back_lbl);
        lv_obj_add_event_cb(back_btn, sd_back_btn_cb, LV_EVENT_CLICKED, NULL);
    }

    // File/directory entries
    for (int i = 0; i < p4sd.entry_count; i++) {
        lv_obj_t* btn = lv_btn_create(sd_file_list);
        lv_obj_set_size(btn, 580, 44);
        lv_obj_set_style_radius(btn, 6, 0);

        bool is_dir  = p4sd.entries[i].is_dir;
        bool is_midi = p4sd.entries[i].is_midi;
        lv_obj_set_style_bg_color(btn,
            is_dir  ? lv_color_hex(0x1A3A5C) :
            is_midi ? lv_color_hex(0x3A2A00) : lv_color_hex(0x1A2A1A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x446688), LV_STATE_PRESSED);

        lv_obj_t* lbl = lv_label_create(btn);
        char display[64];
        snprintf(display, sizeof(display),
            is_dir  ? LV_SYMBOL_DIRECTORY "  %s" :
            is_midi ? LV_SYMBOL_FILE "  %s [MIDI]" : LV_SYMBOL_AUDIO "  %s",
            p4sd.entries[i].name);
        lv_label_set_text(lbl, display);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl,
            is_dir  ? RED808_CYAN :
            is_midi ? RED808_WARNING : RED808_SUCCESS, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);

        lv_obj_add_event_cb(btn, sd_file_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    if (p4sd.list_complete && p4sd.entry_count == 0) {
        lv_obj_t* lbl = lv_label_create(sd_file_list);
        lv_label_set_text(lbl, "No files found (.wav / .mid)");
        lv_obj_set_style_text_color(lbl, RED808_TEXT_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    }
}

static void create_sdcard_screen(void) {
    scr_sdcard = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_sdcard, RED808_BG, 0);
    lv_obj_clear_flag(scr_sdcard, LV_OBJ_FLAG_SCROLLABLE);

    // Landscape layout: 1024×600
    // Left panel (file browser): 620px wide
    // Right panel (pad assign):  380px wide
    const int TOP    = 8;
    const int PANEL_H = LCD_V_RES - TOP - 8;  // ~584px
    const int LEFT_W  = 620;
    const int RIGHT_W = LCD_H_RES - LEFT_W - 16;  // ~392px
    const int GAP     = 8;

    // ── Left Panel: file browser ──
    sd_left_panel = lv_obj_create(scr_sdcard);
    lv_obj_set_size(sd_left_panel, LEFT_W, PANEL_H);
    lv_obj_set_pos(sd_left_panel, 4, TOP);
    lv_obj_set_style_bg_color(sd_left_panel, lv_color_hex(0x0D1520), 0);
    lv_obj_set_style_bg_opa(sd_left_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sd_left_panel, RED808_INFO, 0);
    lv_obj_set_style_border_width(sd_left_panel, 1, 0);
    lv_obj_set_style_radius(sd_left_panel, 8, 0);
    lv_obj_set_style_pad_all(sd_left_panel, 8, 0);
    lv_obj_clear_flag(sd_left_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* title_lbl = lv_label_create(sd_left_panel);
    lv_label_set_text(title_lbl, LV_SYMBOL_DRIVE "  SD CARD BROWSER");
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_lbl, RED808_CYAN, 0);
    lv_obj_set_pos(title_lbl, 8, 4);

    // Status label
    sd_status_lbl = lv_label_create(sd_left_panel);
    lv_label_set_text(sd_status_lbl, "CONNECTING...");
    lv_obj_set_style_text_font(sd_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_status_lbl, RED808_WARNING, 0);
    lv_obj_set_pos(sd_status_lbl, 420, 8);

    // Path label
    sd_path_lbl = lv_label_create(sd_left_panel);
    lv_label_set_text(sd_path_lbl, "/");
    lv_obj_set_style_text_font(sd_path_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_path_lbl, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(sd_path_lbl, 8, 30);

    // Scrollable file list
    sd_file_list = lv_obj_create(sd_left_panel);
    lv_obj_set_size(sd_file_list, LEFT_W - 24, PANEL_H - 72);
    lv_obj_set_pos(sd_file_list, 4, 54);
    lv_obj_set_style_bg_opa(sd_file_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sd_file_list, 0, 0);
    lv_obj_set_style_pad_row(sd_file_list, 4, 0);
    lv_obj_set_style_pad_all(sd_file_list, 2, 0);
    lv_obj_set_flex_flow(sd_file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(sd_file_list, LV_DIR_VER);
    lv_obj_add_flag(sd_file_list, LV_OBJ_FLAG_SCROLLABLE);

    // ── Right Panel: WAV pad assign + MIDI pattern slot ──
    sd_right_panel = lv_obj_create(scr_sdcard);
    lv_obj_set_size(sd_right_panel, RIGHT_W, PANEL_H);
    lv_obj_set_pos(sd_right_panel, LEFT_W + GAP, TOP);
    lv_obj_set_style_bg_color(sd_right_panel, lv_color_hex(0x0D1520), 0);
    lv_obj_set_style_bg_opa(sd_right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sd_right_panel, RED808_ACCENT, 0);
    lv_obj_set_style_border_width(sd_right_panel, 1, 0);
    lv_obj_set_style_radius(sd_right_panel, 8, 0);
    lv_obj_set_style_pad_all(sd_right_panel, 8, 0);
    lv_obj_clear_flag(sd_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // ── WAV section (default visible) ─────────────────────────────────────
    sd_wav_section = lv_obj_create(sd_right_panel);
    lv_obj_set_size(sd_wav_section, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(sd_wav_section, 0, 0);
    lv_obj_set_style_bg_opa(sd_wav_section, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sd_wav_section, 0, 0);
    lv_obj_set_style_pad_all(sd_wav_section, 0, 0);
    lv_obj_clear_flag(sd_wav_section, LV_OBJ_FLAG_SCROLLABLE);

    // "ASSIGN TO PAD" title
    lv_obj_t* assign_lbl = lv_label_create(sd_wav_section);
    lv_label_set_text(assign_lbl, "ASSIGN TO PAD");
    lv_obj_set_style_text_font(assign_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(assign_lbl, RED808_ACCENT, 0);
    lv_obj_set_pos(assign_lbl, 8, 4);

    // 4x4 pad grid — fit within RIGHT_W
    int pad_gap = 6;
    int pad_w = (RIGHT_W - 32 - 3 * pad_gap) / 4;
    int pad_h = 56;
    int px_start = 8, py_start = 36;
    for (int i = 0; i < 16; i++) {
        int col = i % 4;
        int row = i / 4;
        int px = px_start + col * (pad_w + pad_gap);
        int py = py_start + row * (pad_h + pad_gap);

        lv_obj_t* btn = lv_btn_create(sd_wav_section);
        lv_obj_set_size(btn, pad_w, pad_h);
        lv_obj_set_pos(btn, px, py);
        lv_obj_set_style_bg_color(btn, i == 0 ? RED808_ACCENT : lv_color_hex(0x222233), 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x444466), 0);

        lv_obj_t* num_lbl = lv_label_create(btn);
        char num_str[12];
        snprintf(num_str, sizeof(num_str), "%s\n%d", trackNames[i], i);
        lv_label_set_text(num_lbl, num_str);
        lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num_lbl, lv_color_hex(theme_presets[currentTheme].track_colors[i]), 0);
        lv_obj_set_style_text_align(num_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(num_lbl);

        sd_pad_btns[i] = btn;
        lv_obj_add_event_cb(btn, sd_pad_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    // Selected file label
    sd_selected_lbl = lv_label_create(sd_wav_section);
    lv_label_set_text(sd_selected_lbl, "");
    lv_obj_set_width(sd_selected_lbl, RIGHT_W - 24);
    lv_obj_set_style_text_font(sd_selected_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_selected_lbl, RED808_SUCCESS, 0);
    lv_obj_set_style_text_align(sd_selected_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(sd_selected_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(sd_selected_lbl, 8, 300);

    // LOAD WAV button
    sd_load_btn = lv_btn_create(sd_wav_section);
    lv_obj_set_size(sd_load_btn, RIGHT_W - 24, 60);
    lv_obj_set_pos(sd_load_btn, 8, 360);
    lv_obj_set_style_bg_color(sd_load_btn, RED808_ACCENT, 0);
    lv_obj_set_style_bg_color(sd_load_btn, lv_color_hex(0x882200), LV_STATE_DISABLED);
    lv_obj_set_style_radius(sd_load_btn, 10, 0);
    lv_obj_add_state(sd_load_btn, LV_STATE_DISABLED);

    sd_load_lbl = lv_label_create(sd_load_btn);
    lv_label_set_text(sd_load_lbl, LV_SYMBOL_UPLOAD "  LOAD TO PAD");
    lv_obj_set_style_text_font(sd_load_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sd_load_lbl, lv_color_white(), 0);
    lv_obj_center(sd_load_lbl);
    lv_obj_add_event_cb(sd_load_btn, sd_load_btn_cb, LV_EVENT_CLICKED, NULL);

    // ── MIDI section (hidden by default) ──────────────────────────────────
    sd_midi_section = lv_obj_create(sd_right_panel);
    lv_obj_set_size(sd_midi_section, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(sd_midi_section, 0, 0);
    lv_obj_set_style_bg_opa(sd_midi_section, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sd_midi_section, 0, 0);
    lv_obj_set_style_pad_all(sd_midi_section, 0, 0);
    lv_obj_clear_flag(sd_midi_section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sd_midi_section, LV_OBJ_FLAG_HIDDEN);

    // MIDI title
    lv_obj_t* midi_title = lv_label_create(sd_midi_section);
    lv_label_set_text(midi_title, LV_SYMBOL_AUDIO "  LOAD MIDI TO PATTERN");
    lv_obj_set_style_text_font(midi_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(midi_title, RED808_WARNING, 0);
    lv_obj_set_pos(midi_title, 8, 4);

    // File info label
    sd_midi_info_lbl = lv_label_create(sd_midi_section);
    lv_label_set_text(sd_midi_info_lbl, "");
    lv_obj_set_style_text_font(sd_midi_info_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_midi_info_lbl, RED808_TEXT_DIM, 0);
    lv_obj_set_pos(sd_midi_info_lbl, 8, 30);
    lv_obj_set_width(sd_midi_info_lbl, RIGHT_W - 24);
    lv_label_set_long_mode(sd_midi_info_lbl, LV_LABEL_LONG_DOT);

    // "SELECT TARGET SLOT:"
    lv_obj_t* slot_lbl = lv_label_create(sd_midi_section);
    lv_label_set_text(slot_lbl, "SELECT TARGET PATTERN SLOT:");
    lv_obj_set_style_text_font(slot_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(slot_lbl, RED808_CYAN, 0);
    lv_obj_set_pos(slot_lbl, 8, 54);

    // Pattern slot grid 2×3 (P01-P06 = slots 0-5, valid master patterns)
    {
        int mp_btn_w = (RIGHT_W - 16 - 10) / 2;  // 2 cols, gap=10
        int mp_btn_h = 52;
        int mp_gap   = 6;
        int mp_x0 = 0, mp_y0 = 76;
        for (int i = 0; i < 6; i++) {
            int col = i % 2, row = i / 2;
            int slot_id = i;
            int bx = mp_x0 + col * (mp_btn_w + 10);
            int by = mp_y0 + row * (mp_btn_h + mp_gap);

            lv_obj_t* btn = lv_btn_create(sd_midi_section);
            lv_obj_set_size(btn, mp_btn_w, mp_btn_h);
            lv_obj_set_pos(btn, bx, by);
            lv_obj_set_style_bg_color(btn,
                slot_id == sd_midi_target_slot ? RED808_ACCENT : lv_color_hex(0x1A2A3A), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn,
                slot_id == sd_midi_target_slot ? RED808_CYAN : lv_color_hex(0x334455), 0);
            lv_obj_set_style_radius(btn, 8, 0);

            lv_obj_t* bl = lv_label_create(btn);
            char bname[8];
            snprintf(bname, sizeof(bname), "P%02d", slot_id + 1);
            lv_label_set_text(bl, bname);
            lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(bl,
                slot_id == sd_midi_target_slot ? lv_color_white() : RED808_TEXT_DIM, 0);
            lv_obj_center(bl);

            sd_midi_pat_btns[i] = btn;
            lv_obj_add_event_cb(btn, sd_midi_pat_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)slot_id);
        }
    }

    // LOAD MIDI PATTERN button
    sd_midi_load_btn = lv_btn_create(sd_midi_section);
    lv_obj_set_size(sd_midi_load_btn, RIGHT_W - 24, 56);
    lv_obj_set_pos(sd_midi_load_btn, 8, 362);
    lv_obj_set_style_bg_color(sd_midi_load_btn, RED808_WARNING, 0);
    lv_obj_set_style_bg_color(sd_midi_load_btn, lv_color_hex(0x554400), LV_STATE_DISABLED);
    lv_obj_set_style_radius(sd_midi_load_btn, 10, 0);
    lv_obj_add_state(sd_midi_load_btn, LV_STATE_DISABLED);
    lv_obj_add_event_cb(sd_midi_load_btn, sd_midi_load_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* midi_load_lbl = lv_label_create(sd_midi_load_btn);
    lv_label_set_text(midi_load_lbl, LV_SYMBOL_DOWNLOAD "  LOAD MIDI PATTERN");
    lv_obj_set_style_text_font(midi_load_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(midi_load_lbl, lv_color_black(), 0);
    lv_obj_center(midi_load_lbl);

    // Status label
    sd_midi_status_lbl = lv_label_create(sd_midi_section);
    lv_label_set_text(sd_midi_status_lbl, "");
    lv_obj_set_style_text_font(sd_midi_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_midi_status_lbl, RED808_SUCCESS, 0);
    lv_obj_set_pos(sd_midi_status_lbl, 8, 426);
    lv_obj_set_width(sd_midi_status_lbl, RIGHT_W - 24);
    lv_label_set_long_mode(sd_midi_status_lbl, LV_LABEL_LONG_WRAP);

    // BACK button (return to live)
    lv_obj_t* back_btn = lv_btn_create(sd_right_panel);
    lv_obj_set_size(back_btn, RIGHT_W - 24, 50);
    lv_obj_set_pos(back_btn, 8, PANEL_H - 74);
    lv_obj_set_style_bg_color(back_btn, RED808_SURFACE, 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, RED808_BORDER, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  BACK TO LIVE");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(back_lbl, RED808_TEXT, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        (void)e;
        ui_navigate_to(2);  // SCREEN_LIVE
    }, LV_EVENT_CLICKED, NULL);
}

// =============================================================================
// PERFORMANCE SCREEN (placeholder)
// =============================================================================
static void create_performance_screen(void) {
    scr_performance = NULL;  // unused screen — stub
}

// =============================================================================
// PUBLIC API
// =============================================================================
void ui_create_all_screens(void) {
    create_boot_screen();
    create_live_screen();
    create_sequencer_screen();
    create_fx_screen();
    create_volumes_screen();
    create_sdcard_screen();
    create_performance_screen();

    // Start on boot screen
    lv_scr_load(scr_boot);
    active_screen = 0;
}

// =============================================================================
// THEME RELOAD — delete and recreate all themed screens with new colors
// =============================================================================
static void ui_reload_themed_screens(void) {
    int saved_screen = active_screen;

    // Navigate to boot temporarily so we can safely delete active screens
    lv_scr_load(scr_boot);

    // Delete all themed screens (nullify pointers before delete to avoid stale refs)
    if (scr_live)        { lv_obj_del(scr_live);        scr_live        = NULL; }
    if (scr_sequencer)   { lv_obj_del(scr_sequencer);   scr_sequencer   = NULL; }
    if (scr_fx)          { lv_obj_del(scr_fx);          scr_fx          = NULL; }
    if (scr_volumes)     { lv_obj_del(scr_volumes);     scr_volumes     = NULL; }
    if (scr_sdcard)      { lv_obj_del(scr_sdcard);      scr_sdcard      = NULL; }
    // scr_performance is NULL (stubbed)

    // Clear widget pointers (prevent stale access in update functions)
    header_bar = NULL; hdr_bpm_label = NULL; hdr_pattern_label = NULL;
    hdr_play_btn = NULL; hdr_play_label = NULL;
    hdr_pattern_minus_btn = NULL; hdr_pattern_plus_btn = NULL;
    hdr_wifi_label = NULL; hdr_s3_label = NULL;
    for (int i = 0; i < 16; i++) hdr_step_dots[i] = NULL;
    for (int i = 0; i < 16; i++) {
        live_pad_btns[i] = NULL; live_pad_labels[i] = NULL;
        live_spectrum_bars[i] = NULL;
    }
    // Invalidate ripple pool — objects are children of scr_live (already deleted)
    for (int i = 0; i < RIPPLE_POOL; i++) {
        ripples[i].obj = nullptr;
        ripples[i].frame = 0;
    }
    grid_play_btn = NULL; grid_play_lbl = NULL; grid_bpm_lbl = NULL;
    grid_pat_lbl = NULL; grid_step_lbl = NULL;
    grid_nr_btn = NULL; grid_nr_lbl = NULL;
    grid_16l_btn = NULL; grid_16l_lbl = NULL;
    grid_mstr_dot = NULL; grid_mstr_lbl = NULL;
    grid_aux_dot  = NULL; grid_aux_lbl  = NULL;
    grid_vol_lbl = NULL; grid_sync_btn = NULL;
    for (int i = 0; i < 3; i++) {
        fx_arcs[i] = NULL; fx_value_labels[i] = NULL;
        fx_name_labels[i] = NULL; fx_toggle_btns[i] = NULL;
        fx_pct_ring[i] = NULL;
    }
    fx_page_dot[0] = NULL; fx_page_dot[1] = NULL; fx_page_lbl = NULL;
    fx_page = 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) seq_step_btns[i][j] = NULL;
        seq_track_labels[i] = NULL; seq_mute_btns[i] = NULL;
        seq_solo_btns[i] = NULL;
        seq_solo_labels[i] = NULL;
        seq_ruler_labels[i] = NULL;
    }
    for (int b = 0; b < 4; b++) seq_beat_bg[b] = NULL;
    seq_playhead_line = NULL;
    seq_status_step_lbl = NULL;
    seq_status_pat_lbl = NULL;
    for (int i = 0; i < 16; i++) {
        vol_sliders[i] = NULL; vol_labels[i] = NULL;
        vol_name_labels[i] = NULL; vol_mute_dots[i] = NULL;
        vol_strip_panels[i] = NULL;
    }

    // Clear SD screen widgets
    sd_left_panel = NULL; sd_right_panel = NULL; sd_status_lbl = NULL;
    sd_path_lbl = NULL; sd_file_list = NULL; sd_selected_lbl = NULL;
    sd_load_btn = NULL; sd_load_lbl = NULL;
    for (int i = 0; i < 16; i++) sd_pad_btns[i] = NULL;
    sd_wav_section = NULL; sd_midi_section = NULL;
    sd_midi_info_lbl = NULL; sd_midi_status_lbl = NULL; sd_midi_load_btn = NULL;
    for (int i = 0; i < 6; i++) sd_midi_pat_btns[i] = NULL;
    sd_is_midi_mode = false;

    // Recreate with new theme colors
    create_live_screen();
    create_sequencer_screen();
    create_fx_screen();
    create_volumes_screen();
    create_sdcard_screen();
    create_performance_screen();

    // Restore navigation (go to live if was on unknown screen)
    int nav_to = (saved_screen == 9) ? 9 : 2;  // stay in sdcard if we were there
    if (saved_screen == 3) nav_to = 3;
    if (saved_screen == 7) nav_to = 7;
    if (saved_screen == 8) nav_to = 8;
    ui_navigate_to(nav_to);
}

void ui_navigate_to(int screen_id) {
    lv_obj_t* targets[] = {
        scr_boot, NULL, scr_live, scr_sequencer, NULL,
        NULL, NULL, scr_volumes, scr_fx, scr_sdcard, scr_performance
    };
    int count = sizeof(targets) / sizeof(targets[0]);
    if (screen_id >= 0 && screen_id < count && targets[screen_id]) {
        lv_scr_load_anim(targets[screen_id], LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
        prev_active_screen = active_screen;
        active_screen = screen_id;
        // Request SD listing when entering SD screen
        if (screen_id == 9) uart_send_sd_mount();
    }
    // Enable/disable direct touch bypass for live pads
    g_live_screen_active.store(screen_id == 2, std::memory_order_release);
}

// =============================================================================
// PAD QUEUE DRAIN — called from loop() on Core 1 (outside LVGL mutex)
// =============================================================================
void ui_process_pad_queue(void) {
    uint8_t t = s_pad_qt.load(std::memory_order_relaxed);
    uint8_t h = s_pad_qh.load(std::memory_order_acquire);
    while (t != h) {
        uint16_t ev = s_pad_q[t & 0x1F];
        t++;
        uint8_t pad      = (uint8_t)(ev & 0xFF);
        uint8_t velocity = (uint8_t)((ev >> 8) & 0xFF);
        if (!velocity) velocity = 100;   // defensive floor
        // Feed DSP spectrum with real velocity
        dsp_notify_pad(pad, velocity);
        // Notify S3 via UART (fast, 5 bytes). Legacy TCMD_PAD_TAP ignores
        // velocity; we still send it for forward compatibility.
        uart_send_to_s3(MSG_TOUCH_CMD, TCMD_PAD_TAP, pad);
        // Then send UDP to master with MPC-style velocity
        if (p4.wifi_connected || p4.master_connected) {
            udp_send_trigger(pad, velocity);
        }
        // Mirror to legacy binary flash timer so screens that still read
        // p4.pad_flash_until (e.g. sequencer sync highlight) keep working.
        p4.pad_flash_until[pad] = millis() + 80;
    }
    s_pad_qt.store(t, std::memory_order_relaxed);
}

// =============================================================================
// LIVE PAD HIT GEOMETRY — shared between LVGL layout and GT911 touch_task
// =============================================================================
// Pad grid geometry (must match create_live_screen layout below).
// M=8 (margin), CW/CH = pad size, SX/SY = stride (pad + gap).
static constexpr int LIVE_M  = 8;
static constexpr int LIVE_CW = 122;
static constexpr int LIVE_CH = 143;
static constexpr int LIVE_SX = 126;
static constexpr int LIVE_SY = 147;

int ui_pad_from_xy(uint16_t x, uint16_t y) {
    if (!g_live_screen_active.load(std::memory_order_acquire)) return -1;
    if (x < LIVE_M || x >= (LIVE_M + 4 * LIVE_SX)) return -1;
    if (y < LIVE_M || y >= (LIVE_M + 4 * LIVE_SY)) return -1;
    int col  = (x - LIVE_M) / LIVE_SX;
    int row  = (y - LIVE_M) / LIVE_SY;
    int x_in = (x - LIVE_M) % LIVE_SX;
    int y_in = (y - LIVE_M) % LIVE_SY;
    if (x_in >= LIVE_CW || y_in >= LIVE_CH) return -1;
    if (col >= 4 || row >= 4) return -1;
    return row * 4 + col;
}

// =============================================================================
// PAD FRAME UPDATE — called from GT911 touch_task (Core 0, 200Hz)
// Rising edge → enqueue event (with 16 Levels remapping if active) and arm
// note-repeat timer. Falling edge → cancel repeat. Held → fire repeats on
// schedule using the current tempo & subdivision.
// =============================================================================
void ui_pad_frame_update(const bool pressed[16], const uint8_t velocity[16]) {
    if (!g_live_screen_active.load(std::memory_order_acquire)) {
        // Clear state so we don't fire phantom repeats when leaving LIVE
        for (int p = 0; p < 16; p++) {
            s_pad_held[p] = false;
            s_pad_repeat_next_ms[p] = 0;
        }
        return;
    }

    unsigned long now = millis();
    unsigned long nr_interval = ui_nr_interval_ms();    // 0 if NR off

    for (int p = 0; p < 16; p++) {
        bool was_held = s_pad_held[p];
        bool is_held  = pressed[p];

        if (is_held && !was_held) {
            // ── Rising edge: real finger-down ──
            uint8_t vel = velocity[p] ? velocity[p] : 100;
            s_pad_held_velocity[p] = vel;

            uint8_t send_pad = p;
            uint8_t send_vel = vel;
            if (s_16l_active) {
                // Remap to 16 velocities of the stored source pad
                send_pad = s_16l_src_pad;
                send_vel = (uint8_t)(((p + 1) * 127) / 16);  // 7..127
                if (send_vel < 8) send_vel = 8;
            } else {
                s_16l_src_pad = (uint8_t)p;   // remember for future 16L
            }
            enqueue_pad_event(send_pad, send_vel);
            ui_pad_flash_start(p, vel);

            // Arm note-repeat timer
            s_pad_repeat_next_ms[p] = (nr_interval && s_nr_idx)
                ? (now + nr_interval) : 0;
        } else if (!is_held && was_held) {
            // ── Falling edge: finger lifted ──
            s_pad_repeat_next_ms[p] = 0;
        } else if (is_held && nr_interval && s_pad_repeat_next_ms[p]
                   && now >= s_pad_repeat_next_ms[p]) {
            // ── Held + note-repeat tick ──
            uint8_t vel = s_pad_held_velocity[p] ? s_pad_held_velocity[p] : 100;
            uint8_t send_pad = p;
            uint8_t send_vel = vel;
            if (s_16l_active) {
                send_pad = s_16l_src_pad;
                send_vel = (uint8_t)(((p + 1) * 127) / 16);
                if (send_vel < 8) send_vel = 8;
            }
            enqueue_pad_event(send_pad, send_vel);
            ui_pad_flash_start(p, vel);
            // Schedule next tick; if we fell behind, catch up without drifting
            // into the far past (e.g. after a blocked frame).
            unsigned long next = s_pad_repeat_next_ms[p] + nr_interval;
            if (next <= now) next = now + nr_interval;
            s_pad_repeat_next_ms[p] = next;
        }

        s_pad_held[p] = is_held;
    }
}

void ui_update_current_screen(void) {
    // Auto-navigate from boot to live when Master or optional S3 connects
    if (active_screen == 0 && (p4.master_connected || p4.s3_connected)) {
        ui_navigate_to(2);  // SCREEN_LIVE
    }

    // Theme change — recreate all screens with new palette
    static int prev_theme = -1;
    if (p4.theme != prev_theme && prev_theme != -1) {
        prev_theme = p4.theme;
        ui_theme_apply((VisualTheme)p4.theme);
        ui_reload_themed_screens();
        return;  // screens recreated; update functions have fresh state
    }
    if (prev_theme == -1) prev_theme = p4.theme;

    // Navigate if S3 sends screen command
    static int prev_screen = -1;
    if (p4.current_screen != prev_screen) {
        prev_screen = p4.current_screen;
        ui_navigate_to(p4.current_screen);
    }

    ui_update_header();

    // Update active screen content
    lv_obj_t* active = lv_scr_act();
    if (active == scr_live) update_live_screen();
    else if (active == scr_sequencer) update_sequencer_screen();
    else if (active == scr_fx) update_fx_screen();
    else if (active == scr_volumes) update_volumes_screen();
    else if (active == scr_sdcard && p4sd.needs_refresh) {
        p4sd.needs_refresh = false;
        sd_refresh_ui();
    }
}
