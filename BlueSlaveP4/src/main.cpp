// =============================================================================
// BlueSlaveP4 — main.cpp
// RED808 V6 Visual Beast — ESP32-P4 + MIPI-DSI 7" Display
// Entry point: display init → LVGL → UART → UI
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "../include/config.h"
#include "drivers/display_init.h"
#include "drivers/lvgl_port.h"
#include "uart_handler.h"
#include "ui/ui_screens.h"
#include "ui/ui_theme.h"

// WiFi/UDP test — Master connection
static WiFiUDP udp;
static const char* MASTER_SSID = "RED808";
static const char* MASTER_PASS = "red808esp32";
static const IPAddress MASTER_IP(192, 168, 4, 1);
static const uint16_t MASTER_PORT = 8888;

static unsigned long lastScreenUpdate = 0;

void setup() {
    // 1. Debug serial
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== RED808 P4 — Visual Beast ===");
    Serial.println("Guition JC1060P470C | ESP32-P4");

    // 2. Initialize MIPI-DSI display + backlight
    P4_LOG_PRINTLN("[INIT] Display...");
    display_init();
    P4_LOG_PRINTLN("[INIT] Display OK");

    // 3. Initialize LVGL (display driver + GT911 touch)
    P4_LOG_PRINTLN("[INIT] LVGL port...");
    lvgl_port_init();
    P4_LOG_PRINTLN("[INIT] LVGL OK");

    // 4. Apply default theme
    ui_theme_apply(THEME_RED808);

    // 5. Create UI screens (boot → live → seq → fx → vol → settings → perf)
    P4_LOG_PRINTLN("[INIT] UI screens...");
    ui_create_all_screens();
    P4_LOG_PRINTLN("[INIT] UI screens OK");

    // 6. Start UART1 (binary protocol to S3)
    P4_LOG_PRINTLN("[INIT] UART bridge to S3...");
    uart_handler_init();
    P4_LOG_PRINTF("[INIT] UART1 on TX=%d RX=%d @ %d baud\n",
                  UART_S3_TX_PIN, UART_S3_RX_PIN, UART_BAUD_RATE);

    P4_LOG_PRINTLN("=== P4 Ready — Waiting for S3 ===");
}

void loop() {
    // Process all pending UART packets from S3
    uart_handler_process();

    // Update LVGL screen content at target framerate
    unsigned long now = millis();
    if (now - lastScreenUpdate >= Config::SCREEN_UPDATE_MS) {
        lastScreenUpdate = now;
        ui_update_current_screen();
    }

    // Tick LVGL (display flush + touch read)
    lvgl_port_update();
}
