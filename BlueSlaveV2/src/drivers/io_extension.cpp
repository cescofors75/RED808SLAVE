// =============================================================================
// io_extension.cpp - CH32V003 IO Expander driver (Waveshare ESP32-S3-Touch-LCD-7B)
// =============================================================================
#include "io_extension.h"
#include "i2c_driver.h"

// CH32V003 IO Expander Registers
#define CH32V003_DIRECTION_REG   0x02  // 1=output, 0=input
#define CH32V003_OUTPUT_REG      0x03
#define CH32V003_INPUT_REG       0x04
#define CH32V003_PWM_REG         0x05

static uint8_t output_state = 0xFF;

void io_ext_init() {
    // Configure all pins as outputs (bit=1 means output on CH32V003)
    i2c_write_byte(IO_EXT_ADDR, CH32V003_DIRECTION_REG, 0xFF);
    // Reset PWM register to 0 (clean up any previous PWM state)
    i2c_write_byte(IO_EXT_ADDR, CH32V003_PWM_REG, 0);
    // All outputs high EXCEPT backlight (bit 2) — keep backlight OFF during init
    // to avoid showing random PSRAM framebuffer content before LVGL renders.
    output_state = 0xFF & ~(1 << EXIO_BL);
    i2c_write_byte(IO_EXT_ADDR, CH32V003_OUTPUT_REG, output_state);
}

void io_ext_output(uint8_t pin, uint8_t value) {
    if (value) {
        output_state |= (1 << pin);
    } else {
        output_state &= ~(1 << pin);
    }
    i2c_write_byte(IO_EXT_ADDR, CH32V003_OUTPUT_REG, output_state);
}

void io_ext_backlight_on() {
    io_ext_output(EXIO_BL, 1);
}

void io_ext_backlight_off() {
    io_ext_output(EXIO_BL, 0);
}

void io_ext_backlight_set(uint8_t brightness) {
    // TODO: investigate correct CH32V003 PWM sequence from Waveshare Demo 15
    (void)brightness;
}

void io_ext_touch_reset() {
    io_ext_output(EXIO_TP_RST, 0);
    delay(10);
    io_ext_output(EXIO_TP_RST, 1);
    delay(50);
}

void io_ext_lcd_reset() {
    io_ext_output(EXIO_LCD_RST, 0);
    delay(10);
    io_ext_output(EXIO_LCD_RST, 1);
    delay(50);
}

void io_ext_sd_enable() {
    io_ext_output(EXIO_SD_CS, 0);  // CS low = selected
}

void io_ext_sd_disable() {
    io_ext_output(EXIO_SD_CS, 1);  // CS high = deselected
}
