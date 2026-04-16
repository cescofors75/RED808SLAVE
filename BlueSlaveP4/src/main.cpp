// =============================================================================
// BlueSlaveP4 — main.cpp
// RED808 V6 Visual Beast — ESP32-P4 + MIPI-DSI 7" Display
// Entry point: display init → LVGL → WiFi/UDP → UI
// P4 connects DIRECTLY to Master via ESP32-C6 WiFi (SDIO ESP-Hosted)
// =============================================================================

#include <Arduino.h>
#include "../include/config.h"
#include "drivers/display_init.h"
#include "drivers/lvgl_port.h"
#include "uart_handler.h"
#include "udp_handler.h"
#include "ui/ui_screens.h"
#include "ui/ui_theme.h"
#include "dsp_task.h"

#if P4_USB_CDC_ENABLED
#include "usb_cdc_handler.h"
#endif

static unsigned long lastScreenUpdate = 0;

void setup() {
    // 1. Debug serial
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== RED808 P4 — Visual Beast ===");
    Serial.println("Guition JC1060P470C | ESP32-P4 + C6 WiFi");

    // 2. Initialize MIPI-DSI display + backlight
    P4_LOG_PRINTLN("[INIT] Display...");
    display_init();
    P4_LOG_PRINTLN("[INIT] Display OK");

    // 3. Initialize LVGL (display driver + GT911 touch)
    P4_LOG_PRINTLN("[INIT] LVGL port...");
    lvgl_port_init();
    P4_LOG_PRINTLN("[INIT] LVGL OK");

    // 4. Apply default theme
    ui_theme_apply(THEME_OCEAN);

    // 5. Create UI screens (boot → live → seq → fx → vol → settings → perf)
    P4_LOG_PRINTLN("[INIT] UI screens...");
    ui_create_all_screens();
    P4_LOG_PRINTLN("[INIT] UI screens OK");

    // 6. Start WiFi/UDP connection to Master (via C6 SDIO)
    P4_LOG_PRINTLN("[INIT] WiFi/UDP to Master...");
    udp_handler_init();

    // 7. Start UART1 (optional S3 connection)
    P4_LOG_PRINTLN("[INIT] UART bridge to S3 (optional)...");
    uart_handler_init();

#if P4_USB_CDC_ENABLED
    // 8. Start USB Host CDC (S3 via USB-C OTG port)
    P4_LOG_PRINTLN("[INIT] USB-C Host for S3...");
    usb_cdc_init();
#endif

    // 8. Start DSP processing task (Core 0)
    P4_LOG_PRINTLN("[INIT] DSP task...");
    dsp_task_init();

    P4_LOG_PRINTLN("=== P4 Ready — Connecting to Master ===");

    // 9. Start LVGL rendering task (must be AFTER UI creation)
    lvgl_port_task_start();
}

void loop() {
    // Drain pad event queue FIRST — lowest latency pad→Master path (Core 1, no mutex)
    ui_process_pad_queue();

    // Process WiFi/UDP from Master (primary connection)
    udp_handler_process();

    // Process UART packets from S3 (optional secondary)
    uart_handler_process();

#if P4_USB_CDC_ENABLED
    // Try to connect/reconnect to S3 USB CDC device
    usb_cdc_process();
#endif

    // Update LVGL screen content at target framerate (mutex-protected)
    unsigned long now = millis();
    if (now - lastScreenUpdate >= Config::SCREEN_UPDATE_MS) {
        lastScreenUpdate = now;
        if (lvgl_port_lock(15)) {
            ui_update_current_screen();
            lvgl_port_unlock();
        }
    }
    // LVGL rendering handled by dedicated FreeRTOS task
}
