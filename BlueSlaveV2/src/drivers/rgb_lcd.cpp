// =============================================================================
// rgb_lcd.cpp - RGB LCD driver for Waveshare ESP32-S3-Touch-LCD-7B
// ESP-IDF 5.x — bounce buffers + register_event_callbacks API
// =============================================================================
#include "rgb_lcd.h"
#include "io_extension.h"
#include "esp_log.h"

static const char* TAG = "RGB_LCD";
static esp_lcd_panel_handle_t panel_handle = NULL;

esp_lcd_panel_handle_t rgb_lcd_init() {
    // Reset LCD via IO extension
    io_ext_lcd_reset();

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.timings.pclk_hz = LCD_PCLK_HZ;
    panel_config.timings.h_res = SCREEN_WIDTH;
    panel_config.timings.v_res = SCREEN_HEIGHT;
    panel_config.timings.hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH;
    panel_config.timings.hsync_back_porch  = LCD_HSYNC_BACK_PORCH;
    panel_config.timings.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;
    panel_config.timings.vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH;
    panel_config.timings.vsync_back_porch  = LCD_VSYNC_BACK_PORCH;
    panel_config.timings.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;
    panel_config.timings.flags.pclk_active_neg = true;
    panel_config.data_width = 16;
    panel_config.num_fbs = 2;   // Double buffer for zero-copy swap
    panel_config.hsync_gpio_num = LCD_HSYNC;
    panel_config.vsync_gpio_num = LCD_VSYNC;
    panel_config.de_gpio_num    = LCD_DE;
    panel_config.pclk_gpio_num  = LCD_PCLK;
    panel_config.disp_gpio_num  = -1;

    // RGB565 data pins
    panel_config.data_gpio_nums[0]  = LCD_B3;
    panel_config.data_gpio_nums[1]  = LCD_B4;
    panel_config.data_gpio_nums[2]  = LCD_B5;
    panel_config.data_gpio_nums[3]  = LCD_B6;
    panel_config.data_gpio_nums[4]  = LCD_B7;
    panel_config.data_gpio_nums[5]  = LCD_G2;
    panel_config.data_gpio_nums[6]  = LCD_G3;
    panel_config.data_gpio_nums[7]  = LCD_G4;
    panel_config.data_gpio_nums[8]  = LCD_G5;
    panel_config.data_gpio_nums[9]  = LCD_G6;
    panel_config.data_gpio_nums[10] = LCD_G7;
    panel_config.data_gpio_nums[11] = LCD_R3;
    panel_config.data_gpio_nums[12] = LCD_R4;
    panel_config.data_gpio_nums[13] = LCD_R5;
    panel_config.data_gpio_nums[14] = LCD_R6;
    panel_config.data_gpio_nums[15] = LCD_R7;

    panel_config.flags.fb_in_psram = true;
    panel_config.flags.bb_invalidate_cache = 1;
    panel_config.dma_burst_size = 64;  // DMA burst alignment

    // Bounce buffers: DMA reads from internal SRAM instead of PSRAM.
    // Eliminates PSRAM bus contention between LCD DMA, CPU, and WiFi.
    panel_config.bounce_buffer_size_px = LCD_BOUNCE_BUF;

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_LOGI(TAG, "RGB LCD initialized: %dx%d @ %dMHz, bounce=%d px",
             SCREEN_WIDTH, SCREEN_HEIGHT, LCD_PCLK_HZ / 1000000, LCD_BOUNCE_BUF);

    return panel_handle;
}

void rgb_lcd_get_frame_buffers(esp_lcd_panel_handle_t panel, void** fb0, void** fb1) {
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, fb0, fb1));
}

void rgb_lcd_refresh(esp_lcd_panel_handle_t panel) {
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_refresh(panel));
}

void rgb_lcd_register_vsync_cb(esp_lcd_panel_handle_t panel,
                                bool (*cb)(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*),
                                void* user_ctx) {
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_vsync = cb;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, user_ctx));
}
