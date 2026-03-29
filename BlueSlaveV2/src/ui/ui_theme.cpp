// =============================================================================
// ui_theme.cpp - RED808 theme system with 5 visual presets
// =============================================================================
#include "ui_theme.h"

VisualTheme currentTheme = THEME_RED808;

const ThemeColors theme_presets[THEME_COUNT] = {
    // ── THEME_RED808 (default) ──
    {
        .bg = 0x0D1117, .panel = 0x161B22, .surface = 0x21262D, .border = 0x30363D,
        .text = 0xE6EDF3, .text_dim = 0x8B949E,
        .accent = 0xFF4444, .accent2 = 0xFF6B6B,
        .success = 0x3FB950, .warning = 0xD29922, .error = 0xF85149, .info = 0x58A6FF, .cyan = 0x39D2C0,
        .led_uniform = 0,
        .nav_colors = {0xFF4444, 0x58A6FF, 0x3FB950, 0xD29922, 0x39D2C0, 0x8B949E, 0xC678DD},
        .pad_colors = {0xFF4444, 0xFF8C00, 0xFFD700, 0x00CED1, 0xFF00FF, 0x00FF00, 0x00FA9A, 0x6495ED},
        .encoder_rgb = {0, 0, 0},
        .name = "RED808"
    },
    // ── THEME_OCEAN (blue) ──
    {
        .bg = 0x0A1628, .panel = 0x0F2035, .surface = 0x162D4A, .border = 0x1E3F66,
        .text = 0xD4E4F7, .text_dim = 0x7BA3CC,
        .accent = 0x4A9EFF, .accent2 = 0x6BB3FF,
        .success = 0x40C4AA, .warning = 0xF0B860, .error = 0xF07070, .info = 0x4A9EFF, .cyan = 0x40C4FF,
        .led_uniform = 0x4A9EFF,
        .nav_colors = {0x2277DD, 0x3388EE, 0x4499FF, 0x55AAFF, 0x66BBFF, 0x3366BB, 0x5588DD},
        .pad_colors = {0x1155CC, 0x2266DD, 0x3377EE, 0x4488FF, 0x2255BB, 0x3366CC, 0x4477DD, 0x5588EE},
        .encoder_rgb = {30, 100, 255},
        .name = "OCEAN"
    },
    // ── THEME_NEON (green) ──
    {
        .bg = 0x0A1A0A, .panel = 0x0F260F, .surface = 0x183318, .border = 0x264D26,
        .text = 0xD0F5D0, .text_dim = 0x7BBF7B,
        .accent = 0x39FF14, .accent2 = 0x66FF44,
        .success = 0x39FF14, .warning = 0xFFFF00, .error = 0xFF3333, .info = 0x00FFAA, .cyan = 0x00FFCC,
        .led_uniform = 0x39FF14,
        .nav_colors = {0x22DD22, 0x33EE33, 0x44FF44, 0x00FF88, 0x00FFAA, 0x33BB33, 0x55DD55},
        .pad_colors = {0x00CC00, 0x11DD11, 0x22EE22, 0x33FF33, 0x00BB44, 0x00DD66, 0x00EE88, 0x11FF99},
        .encoder_rgb = {20, 255, 20},
        .name = "NEON"
    },
    // ── THEME_SUNSET (orange/purple) ──
    {
        .bg = 0x1A0A14, .panel = 0x26101E, .surface = 0x381828, .border = 0x552244,
        .text = 0xF5D0E0, .text_dim = 0xBF7B9E,
        .accent = 0xFF6B35, .accent2 = 0xFF8855,
        .success = 0xFFAA33, .warning = 0xFF6B35, .error = 0xFF3366, .info = 0xCC66FF, .cyan = 0xFF66AA,
        .led_uniform = 0xFF6B35,
        .nav_colors = {0xFF4422, 0xFF6633, 0xFF8844, 0xCC44FF, 0xFF44AA, 0xCC6633, 0xAA33CC},
        .pad_colors = {0xFF3300, 0xFF5500, 0xFF7700, 0xFF9900, 0xCC33FF, 0xAA22DD, 0xFF3388, 0xFF55AA},
        .encoder_rgb = {255, 100, 30},
        .name = "SUNSET"
    },
    // ── THEME_RAINBOW (multicolor pro) ──
    {
        .bg = 0x0D0D18, .panel = 0x141428, .surface = 0x1E1E38, .border = 0x3A2A5A,
        .text = 0xF0F0FF, .text_dim = 0x9999CC,
        .accent = 0xFF00AA, .accent2 = 0xAA00FF,
        .success = 0x00FF66, .warning = 0xFFCC00, .error = 0xFF2255, .info = 0x00CCFF, .cyan = 0x00FFDD,
        .led_uniform = 0,
        .nav_colors = {0xFF0000, 0xFF8800, 0xFFFF00, 0x00FF00, 0x0088FF, 0x8800FF, 0xFF00FF},
        .pad_colors = {0xFF0044, 0xFF6600, 0xFFDD00, 0x00FF44, 0x00DDFF, 0x4400FF, 0xDD00FF, 0xFF0088},
        .encoder_rgb = {0, 0, 0},
        .name = "RAINBOW"
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

void ui_recreate_all_screens() {
    extern lv_obj_t* scr_menu;
    extern lv_obj_t* scr_live;
    extern lv_obj_t* scr_sequencer;
    extern lv_obj_t* scr_volumes;
    extern lv_obj_t* scr_filters;
    extern lv_obj_t* scr_settings;
    extern lv_obj_t* scr_diagnostics;
    extern lv_obj_t* scr_patterns;

    // Delete all existing screens
    lv_obj_t* screens[] = {scr_menu, scr_live, scr_sequencer, scr_volumes,
                           scr_filters, scr_settings, scr_diagnostics, scr_patterns};
    for (auto scr : screens) {
        if (scr) lv_obj_del(scr);
    }
    scr_menu = scr_live = scr_sequencer = scr_volumes = NULL;
    scr_filters = scr_settings = scr_diagnostics = scr_patterns = NULL;

    // Recreate all screens with current theme colors
    void ui_create_menu_screen();
    void ui_create_live_screen();
    void ui_create_sequencer_screen();
    void ui_create_volumes_screen();
    void ui_create_filters_screen();
    void ui_create_settings_screen();
    void ui_create_diagnostics_screen();
    void ui_create_patterns_screen();

    ui_create_menu_screen();
    ui_create_live_screen();
    ui_create_sequencer_screen();
    ui_create_volumes_screen();
    ui_create_filters_screen();
    ui_create_settings_screen();
    ui_create_diagnostics_screen();
    ui_create_patterns_screen();
}

void ui_theme_apply(VisualTheme theme) {
    if (theme >= THEME_COUNT) return;
    currentTheme = theme;

    lv_theme_t* th = lv_theme_default_init(
        lv_disp_get_default(),
        theme_accent(),
        theme_info(),
        true,
        &lv_font_montserrat_16
    );
    lv_disp_set_theme(lv_disp_get_default(), th);

    // Recreate all screens with new theme colors
    ui_recreate_all_screens();

    // Load settings screen (where user just clicked)
    extern lv_obj_t* scr_settings;
    if (scr_settings) lv_scr_load(scr_settings);

    // Invalidate ByteButton LED cache to force theme colors on next update
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
    ui_theme_apply(THEME_RED808);
}
