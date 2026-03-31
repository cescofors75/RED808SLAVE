// =============================================================================
// ui_theme.cpp - RED808 theme system with 5 visual presets
// =============================================================================
#include "ui_theme.h"

VisualTheme currentTheme = THEME_OCEAN;

// Mutable track colors — updated on theme change
lv_color_t inst_colors[16] = {};

const ThemeColors theme_presets[THEME_COUNT] = {
    // ── THEME_RED808 — all reds ──
    {
        .bg = 0x0D1117, .panel = 0x161B22, .surface = 0x21262D, .border = 0x30363D,
        .text = 0xE6EDF3, .text_dim = 0x8B949E,
        .accent = 0xFF4444, .accent2 = 0xFF6B6B,
        .success = 0x3FB950, .warning = 0xD29922, .error = 0xF85149, .info = 0x58A6FF, .cyan = 0x39D2C0,
        .led_uniform = 0xFF2222,
        .nav_colors = {0xFF1111, 0xFF3333, 0xFF5555, 0xFF2222, 0xFF4444, 0xFF0000, 0xFF6666},
        .pad_colors = {0xFF0000, 0xFF1A1A, 0xFF3333, 0xFF4D4D, 0xFF6666, 0xCC0000, 0xE60000, 0xFF8080},
        .encoder_rgb = {255, 20, 20},
        .track_colors = {
            0xFF2020, 0xFF3030, 0xFF4040, 0xFF5050,
            0xFF6060, 0xFF7070, 0xFF8080, 0xCC2020,
            0xDD3030, 0xEE4040, 0xFF5555, 0xBB1010,
            0xCC3333, 0xDD4444, 0xEE5555, 0xFF6666
        },
        .name = "RED808"
    },
    // ── THEME_OCEAN — all blues ──
    {
        .bg = 0x0A1628, .panel = 0x0F2035, .surface = 0x162D4A, .border = 0x1E3F66,
        .text = 0xD4E4F7, .text_dim = 0x7BA3CC,
        .accent = 0x4A9EFF, .accent2 = 0x6BB3FF,
        .success = 0x40C4AA, .warning = 0xF0B860, .error = 0xF07070, .info = 0x4A9EFF, .cyan = 0x40C4FF,
        .led_uniform = 0x4A9EFF,
        .nav_colors = {0x2277DD, 0x3388EE, 0x4499FF, 0x55AAFF, 0x66BBFF, 0x3366BB, 0x5588DD},
        .pad_colors = {0x1155CC, 0x2266DD, 0x3377EE, 0x4488FF, 0x2255BB, 0x3366CC, 0x4477DD, 0x5588EE},
        .encoder_rgb = {30, 100, 255},
        .track_colors = {
            0x1144CC, 0x2255DD, 0x3366EE, 0x4477FF,
            0x1166DD, 0x2277EE, 0x3388FF, 0x0055BB,
            0x1177CC, 0x2288DD, 0x3399EE, 0x44AAFF,
            0x0044AA, 0x1155BB, 0x2266CC, 0x3377DD
        },
        .name = "OCEAN"
    },
    // ── THEME_NEON — all greens ──
    {
        .bg = 0x0A1A0A, .panel = 0x0F260F, .surface = 0x183318, .border = 0x264D26,
        .text = 0xD0F5D0, .text_dim = 0x7BBF7B,
        .accent = 0x39FF14, .accent2 = 0x66FF44,
        .success = 0x39FF14, .warning = 0xFFFF00, .error = 0xFF3333, .info = 0x00FFAA, .cyan = 0x00FFCC,
        .led_uniform = 0x39FF14,
        .nav_colors = {0x22DD22, 0x33EE33, 0x44FF44, 0x00FF88, 0x00FFAA, 0x33BB33, 0x55DD55},
        .pad_colors = {0x00CC00, 0x11DD11, 0x22EE22, 0x33FF33, 0x00BB44, 0x00DD66, 0x00EE88, 0x11FF99},
        .encoder_rgb = {20, 255, 20},
        .track_colors = {
            0x00CC00, 0x11DD11, 0x22EE22, 0x33FF33,
            0x00BB44, 0x11CC55, 0x22DD66, 0x00AA00,
            0x33EE33, 0x44FF44, 0x00DD88, 0x11EE99,
            0x00CC66, 0x22DD77, 0x33EE88, 0x44FF99
        },
        .name = "NEON"
    },
    // ── THEME_SUNSET — orange/purple ──
    {
        .bg = 0x1A0A14, .panel = 0x26101E, .surface = 0x381828, .border = 0x552244,
        .text = 0xF5D0E0, .text_dim = 0xBF7B9E,
        .accent = 0xFF6B35, .accent2 = 0xFF8855,
        .success = 0xFFAA33, .warning = 0xFF6B35, .error = 0xFF3366, .info = 0xCC66FF, .cyan = 0xFF66AA,
        .led_uniform = 0xFF6B35,
        .nav_colors = {0xFF4422, 0xFF6633, 0xFF8844, 0xCC44FF, 0xFF44AA, 0xCC6633, 0xAA33CC},
        .pad_colors = {0xFF3300, 0xFF5500, 0xFF7700, 0xFF9900, 0xCC33FF, 0xAA22DD, 0xFF3388, 0xFF55AA},
        .encoder_rgb = {255, 100, 30},
        .track_colors = {
            0xFF4400, 0xFF5511, 0xFF6622, 0xFF7733,
            0xFF8844, 0xCC44FF, 0xBB33EE, 0xAA22DD,
            0xFF3366, 0xFF5588, 0xFF77AA, 0xDD55CC,
            0xFF9944, 0xCC55EE, 0xFF6655, 0xEE44BB
        },
        .name = "SUNSET"
    },
    // ── THEME_RAINBOW — full spectrum ──
    {
        .bg = 0x0D0D18, .panel = 0x141428, .surface = 0x1E1E38, .border = 0x3A2A5A,
        .text = 0xF0F0FF, .text_dim = 0x9999CC,
        .accent = 0xFF00AA, .accent2 = 0xAA00FF,
        .success = 0x00FF66, .warning = 0xFFCC00, .error = 0xFF2255, .info = 0x00CCFF, .cyan = 0x00FFDD,
        .led_uniform = 0,
        .nav_colors = {0xFF0000, 0xFF8800, 0xFFFF00, 0x00FF00, 0x0088FF, 0x8800FF, 0xFF00FF},
        .pad_colors = {0xFF0044, 0xFF6600, 0xFFDD00, 0x00FF44, 0x00DDFF, 0x4400FF, 0xDD00FF, 0xFF0088},
        .encoder_rgb = {0, 0, 0},
        .track_colors = {
            0xFF0000, 0xFF5500, 0xFFAA00, 0xFFFF00,
            0x88FF00, 0x00FF00, 0x00FF88, 0x00FFFF,
            0x0088FF, 0x0000FF, 0x5500FF, 0xAA00FF,
            0xFF00FF, 0xFF0088, 0xFF4444, 0x44FFAA
        },
        .name = "RAINBOW"
    },
    // ── THEME_GREYSCALE — pure monochrome, zero color ──
    {
        .bg = 0x0E0E0E, .panel = 0x1A1A1A, .surface = 0x262626, .border = 0x3C3C3C,
        .text = 0xD5D5D5, .text_dim = 0x777777,
        .accent = 0xCCCCCC, .accent2 = 0xAAAAAA,
        .success = 0x999999, .warning = 0x888888, .error = 0x666666, .info = 0xBBBBBB, .cyan = 0xFFFFFF,
        .led_uniform = 0xCCCCCC,
        .nav_colors = {0x555555, 0x777777, 0x999999, 0xBBBBBB, 0xDDDDDD, 0x444444, 0x666666},
        .pad_colors = {0x404040, 0x505050, 0x606060, 0x707070, 0x808080, 0x909090, 0xA0A0A0, 0xB0B0B0},
        .encoder_rgb = {180, 180, 180},
        .track_colors = {
            0x333333, 0x404040, 0x4D4D4D, 0x5A5A5A,
            0x666666, 0x737373, 0x808080, 0x8D8D8D,
            0x999999, 0xA6A6A6, 0xB3B3B3, 0xC0C0C0,
            0x3D3D3D, 0x575757, 0x717171, 0x8B8B8B
        },
        .name = "GREYSCALE"
    },
};

// ── Runtime color accessors ──
static inline const ThemeColors& tc() { return theme_presets[currentTheme]; }

lv_color_t theme_bg()       { return lv_color_hex(tc().bg); }
lv_color_t theme_panel()    { return lv_color_hex(tc().panel); }
lv_color_t theme_surface()  { return lv_color_hex(tc().surface); }
lv_color_t theme_border()   { return lv_color_hex(tc().border); }
lv_color_t theme_text()     { return lv_color_hex(tc().text); }
lv_color_t theme_text_dim() { return lv_color_hex(tc().text_dim); }
lv_color_t theme_accent()   { return lv_color_hex(tc().accent); }
lv_color_t theme_accent2()  { return lv_color_hex(tc().accent2); }
lv_color_t theme_success()  { return lv_color_hex(tc().success); }
lv_color_t theme_warning()  { return lv_color_hex(tc().warning); }
lv_color_t theme_error()    { return lv_color_hex(tc().error); }
lv_color_t theme_info()     { return lv_color_hex(tc().info); }
lv_color_t theme_cyan()     { return lv_color_hex(tc().cyan); }

// ── LED helpers for hardware ──
uint32_t theme_nav_color(int index) {
    if (index < 0 || index >= 7) return 0x333333;
    return tc().nav_colors[index];
}

uint32_t theme_pad_color(int pad) {
    if (pad < 0 || pad >= 8) return 0x333333;
    return tc().pad_colors[pad];
}

void theme_encoder_color(int track, uint8_t out_rgb[3]) {
    if (tc().led_uniform) {
        out_rgb[0] = tc().encoder_rgb[0];
        out_rgb[1] = tc().encoder_rgb[1];
        out_rgb[2] = tc().encoder_rgb[2];
    } else {
        // Default per-track colors from inst_colors
        extern uint8_t encoderLEDColors[16][3];
        out_rgb[0] = encoderLEDColors[track][0];
        out_rgb[1] = encoderLEDColors[track][1];
        out_rgb[2] = encoderLEDColors[track][2];
    }
}

// Compare two lv_color_t values (RGB565) — avoids 32-bit roundtrip precision loss
static inline bool color_eq(lv_color_t a, lv_color_t b) {
    return a.full == b.full;
}

// Recursively restyle all children with current theme colors
static void restyle_recursive(lv_obj_t* obj,
                               const lv_color_t p_track[16], const lv_color_t c_track[16],
                               const lv_color_t p_ui[13],   const lv_color_t c_ui[13]) {
    if (!obj) return;

    // Check bg color against UI palette and track palette
    lv_color_t bg = lv_obj_get_style_bg_color(obj, 0);
    for (int i = 0; i < 13; i++) {
        if (color_eq(bg, p_ui[i])) { lv_obj_set_style_bg_color(obj, c_ui[i], 0); goto bg_done; }
    }
    for (int i = 0; i < 16; i++) {
        if (color_eq(bg, p_track[i])) { lv_obj_set_style_bg_color(obj, c_track[i], 0); break; }
    }
    bg_done:

    // Check border color
    lv_color_t bd = lv_obj_get_style_border_color(obj, 0);
    for (int i = 0; i < 13; i++) {
        if (color_eq(bd, p_ui[i])) { lv_obj_set_style_border_color(obj, c_ui[i], 0); break; }
    }

    // Check text color
    lv_color_t tx = lv_obj_get_style_text_color(obj, 0);
    for (int i = 0; i < 13; i++) {
        if (color_eq(tx, p_ui[i])) { lv_obj_set_style_text_color(obj, c_ui[i], 0); goto tx_done; }
    }
    for (int i = 0; i < 16; i++) {
        if (color_eq(tx, p_track[i])) { lv_obj_set_style_text_color(obj, c_track[i], 0); break; }
    }
    tx_done:

    // Slider/arc indicator bg
    lv_color_t ind = lv_obj_get_style_bg_color(obj, LV_PART_INDICATOR);
    for (int i = 0; i < 16; i++) {
        if (color_eq(ind, p_track[i])) { lv_obj_set_style_bg_color(obj, c_track[i], LV_PART_INDICATOR); break; }
    }

    // Arc track color
    lv_color_t arc = lv_obj_get_style_arc_color(obj, LV_PART_MAIN);
    if (color_eq(arc, p_ui[2])) lv_obj_set_style_arc_color(obj, c_ui[2], LV_PART_MAIN); // surface

    // Arc indicator
    lv_color_t arci = lv_obj_get_style_arc_color(obj, LV_PART_INDICATOR);
    for (int i = 0; i < 16; i++) {
        if (color_eq(arci, p_track[i])) { lv_obj_set_style_arc_color(obj, c_track[i], LV_PART_INDICATOR); break; }
    }

    // Recurse
    uint32_t cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < cnt; i++) {
        restyle_recursive(lv_obj_get_child(obj, i), p_track, c_track, p_ui, c_ui);
    }
}

// Build lv_color_t arrays from ThemeColors for comparison
static void build_ui_palette(const ThemeColors& t, lv_color_t out[13]) {
    out[0]  = lv_color_hex(t.bg);
    out[1]  = lv_color_hex(t.panel);
    out[2]  = lv_color_hex(t.surface);
    out[3]  = lv_color_hex(t.border);
    out[4]  = lv_color_hex(t.text);
    out[5]  = lv_color_hex(t.text_dim);
    out[6]  = lv_color_hex(t.accent);
    out[7]  = lv_color_hex(t.accent2);
    out[8]  = lv_color_hex(t.warning);
    out[9]  = lv_color_hex(t.info);
    out[10] = lv_color_hex(t.success);
    out[11] = lv_color_hex(t.error);
    out[12] = lv_color_hex(t.cyan);
}

static void build_track_palette(const ThemeColors& t, lv_color_t out[16]) {
    for (int i = 0; i < 16; i++) out[i] = lv_color_hex(t.track_colors[i]);
}

static void sync_inst_colors() {
    const ThemeColors& cur = theme_presets[currentTheme];
    for (int i = 0; i < 16; i++) {
        inst_colors[i] = lv_color_hex(cur.track_colors[i]);
    }
}

void ui_theme_apply(VisualTheme theme) {
    if (theme >= THEME_COUNT) return;
    const ThemeColors& prev = theme_presets[currentTheme];

    // Build old palettes for comparison
    lv_color_t p_ui[13], c_ui[13];
    lv_color_t p_track[16], c_track[16];
    build_ui_palette(prev, p_ui);
    build_track_palette(prev, p_track);

    currentTheme = theme;
    const ThemeColors& cur = theme_presets[currentTheme];
    build_ui_palette(cur, c_ui);
    build_track_palette(cur, c_track);

    // Update inst_colors global
    sync_inst_colors();

    // Update LVGL default theme
    lv_theme_t* th = lv_theme_default_init(
        lv_disp_get_default(),
        theme_accent(),
        theme_info(),
        true,
        &lv_font_montserrat_16
    );
    lv_disp_set_theme(lv_disp_get_default(), th);

    // Walk all screens and remap old colors → new colors
    extern lv_obj_t* scr_menu;
    extern lv_obj_t* scr_live;
    extern lv_obj_t* scr_sequencer;
    extern lv_obj_t* scr_volumes;
    extern lv_obj_t* scr_filters;
    extern lv_obj_t* scr_settings;
    extern lv_obj_t* scr_diagnostics;
    extern lv_obj_t* scr_patterns;
    extern lv_obj_t* scr_sdcard;
    extern lv_obj_t* scr_performance;
    extern lv_obj_t* scr_samples;
    lv_obj_t* screens[] = {scr_menu, scr_live, scr_sequencer, scr_volumes,
                           scr_filters, scr_settings, scr_diagnostics, scr_patterns,
                           scr_sdcard, scr_performance, scr_samples};
    for (auto scr : screens) {
        if (!scr) continue;
        lv_obj_set_style_bg_color(scr, lv_color_hex(cur.bg), 0);
        restyle_recursive(scr, p_track, c_track, p_ui, c_ui);
    }

    lv_obj_invalidate(lv_scr_act());

    // Invalidate ByteButton LED cache
    extern uint32_t byteButtonLedCache[];
    extern bool byteButtonLedInitialized;
    if (byteButtonLedInitialized) {
        for (int i = 0; i < 9; i++) byteButtonLedCache[i] = 0xDEAD;
    }

    // Signal main.cpp to refresh M5 encoder LEDs
    extern volatile bool themeJustChanged;
    themeJustChanged = true;
}

void ui_theme_init() {
    sync_inst_colors();
    lv_theme_t* th = lv_theme_default_init(
        lv_disp_get_default(),
        theme_accent(),
        theme_info(),
        true,
        &lv_font_montserrat_16
    );
    lv_disp_set_theme(lv_disp_get_default(), th);
}
