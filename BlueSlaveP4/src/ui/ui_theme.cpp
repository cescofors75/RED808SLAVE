// =============================================================================
// ui_theme.cpp — RED808 theme presets (mirror of S3)
// =============================================================================
#include "ui_theme.h"

VisualTheme currentTheme = THEME_OCEAN;

const ThemeColors theme_presets[THEME_COUNT] = {
    // THEME_RED808
    { .bg=0x0D1117, .panel=0x161B22, .surface=0x21262D, .border=0x30363D,
      .text=0xE6EDF3, .text_dim=0x8B949E,
      .accent=0xFF4444, .accent2=0xFF6B6B,
      .success=0x3FB950, .warning=0xD29922, .error=0xF85149, .info=0x58A6FF, .cyan=0x39D2C0,
      .track_colors={0xFF2020,0xFF3030,0xFF4040,0xFF5050,0xFF6060,0xFF7070,0xFF8080,0xCC2020,
                     0xDD3030,0xEE4040,0xFF5555,0xBB1010,0xCC3333,0xDD4444,0xEE5555,0xFF6666},
      .name="RED808" },
    // THEME_OCEAN
    { .bg=0x0A1628, .panel=0x0F2035, .surface=0x162D4A, .border=0x1E3F66,
      .text=0xD4E4F7, .text_dim=0x7BA3CC,
      .accent=0x4A9EFF, .accent2=0x6BB3FF,
      .success=0x40C4AA, .warning=0xF0B860, .error=0xF07070, .info=0x4A9EFF, .cyan=0x40C4FF,
      .track_colors={0x1144CC,0x2255DD,0x3366EE,0x4477FF,0x1166DD,0x2277EE,0x3388FF,0x0055BB,
                     0x1177CC,0x2288DD,0x3399EE,0x44AAFF,0x0044AA,0x1155BB,0x2266CC,0x3377DD},
      .name="OCEAN" },
    // THEME_NEON
    { .bg=0x0A1A0A, .panel=0x0F260F, .surface=0x183318, .border=0x264D26,
      .text=0xD0F5D0, .text_dim=0x7BBF7B,
      .accent=0x39FF14, .accent2=0x66FF44,
      .success=0x39FF14, .warning=0xFFFF00, .error=0xFF3333, .info=0x00FFAA, .cyan=0x00FFCC,
      .track_colors={0x00CC00,0x11DD11,0x22EE22,0x33FF33,0x00BB44,0x11CC55,0x22DD66,0x00AA00,
                     0x33EE33,0x44FF44,0x00DD88,0x11EE99,0x00CC66,0x22DD77,0x33EE88,0x44FF99},
      .name="NEON" },
    // THEME_SUNSET
    { .bg=0x1A0A14, .panel=0x26101E, .surface=0x381828, .border=0x552244,
      .text=0xF5D0E0, .text_dim=0xD099B8,
      .accent=0xFF6B35, .accent2=0xFF8855,
      .success=0xFFAA33, .warning=0xFF6B35, .error=0xFF3366, .info=0xCC66FF, .cyan=0xFF66AA,
      .track_colors={0xFF4400,0xFF5511,0xFF6622,0xFF7733,0xFF8844,0xCC44FF,0xBB33EE,0xAA22DD,
                     0xFF3366,0xFF5588,0xFF77AA,0xDD55CC,0xFF9944,0xCC55EE,0xFF6655,0xEE44BB},
      .name="SUNSET" },
    // THEME_RAINBOW
    { .bg=0x0D0D18, .panel=0x141428, .surface=0x1E1E38, .border=0x3A2A5A,
      .text=0xF0F0FF, .text_dim=0x9999CC,
      .accent=0xFF00AA, .accent2=0xAA00FF,
      .success=0x00FF66, .warning=0xFFCC00, .error=0xFF2255, .info=0x00CCFF, .cyan=0x00FFDD,
      .track_colors={0xFF0000,0xFF5500,0xFFAA00,0xFFFF00,0x88FF00,0x00FF00,0x00FF88,0x00FFFF,
                     0x0088FF,0x0000FF,0x5500FF,0xAA00FF,0xFF00FF,0xFF0088,0xFF4444,0x44FFAA},
      .name="RAINBOW" },
    // THEME_GREYSCALE
    { .bg=0x101010, .panel=0x1A1A1A, .surface=0x252525, .border=0x404040,
      .text=0xE0E0E0, .text_dim=0x808080,
      .accent=0xCCCCCC, .accent2=0xAAAAAA,
      .success=0xBBBBBB, .warning=0x999999, .error=0x666666, .info=0xDDDDDD, .cyan=0xC0C0C0,
      .track_colors={0xFFFFFF,0xEEEEEE,0xDDDDDD,0xCCCCCC,0xBBBBBB,0xAAAAAA,0x999999,0x888888,
                     0x777777,0x666666,0x555555,0x444444,0x333333,0x222222,0xF0F0F0,0xD0D0D0},
      .name="GREYSCALE" },
};

// Theme accessors
lv_color_t theme_bg()       { return lv_color_hex(theme_presets[currentTheme].bg); }
lv_color_t theme_panel()    { return lv_color_hex(theme_presets[currentTheme].panel); }
lv_color_t theme_surface()  { return lv_color_hex(theme_presets[currentTheme].surface); }
lv_color_t theme_border()   { return lv_color_hex(theme_presets[currentTheme].border); }
lv_color_t theme_text()     { return lv_color_hex(theme_presets[currentTheme].text); }
lv_color_t theme_text_dim() { return lv_color_hex(theme_presets[currentTheme].text_dim); }
lv_color_t theme_accent()   { return lv_color_hex(theme_presets[currentTheme].accent); }
lv_color_t theme_accent2()  { return lv_color_hex(theme_presets[currentTheme].accent2); }
lv_color_t theme_success()  { return lv_color_hex(theme_presets[currentTheme].success); }
lv_color_t theme_warning()  { return lv_color_hex(theme_presets[currentTheme].warning); }
lv_color_t theme_error()    { return lv_color_hex(theme_presets[currentTheme].error); }
lv_color_t theme_info()     { return lv_color_hex(theme_presets[currentTheme].info); }
lv_color_t theme_cyan()     { return lv_color_hex(theme_presets[currentTheme].cyan); }

void ui_theme_apply(VisualTheme theme) {
    if (theme >= THEME_COUNT) return;
    currentTheme = theme;
}
