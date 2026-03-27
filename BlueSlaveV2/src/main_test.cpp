// =============================================================================
// main_test.cpp - Minimal test for Waveshare ESP32-S3-Touch-LCD-7B
// Just Serial + PSRAM check + I2C scan
// =============================================================================
#include <Arduino.h>
#include <Wire.h>

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial

    Serial.println("\n========================================");
    Serial.println("  WAVESHARE ESP32-S3-Touch-LCD-7B TEST");
    Serial.println("========================================\n");

    // Chip info
    Serial.printf("Chip Model: %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("CPU Freq: %d MHz, Cores: %d\n", ESP.getCpuFreqMHz(), ESP.getChipCores());
    Serial.printf("Flash: %d KB (%d MHz)\n", ESP.getFlashChipSize() / 1024, ESP.getFlashChipSpeed() / 1000000);
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    
    // PSRAM check
    if (psramFound()) {
        Serial.printf("PSRAM: YES - %d bytes total, %d bytes free\n", ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        Serial.println("PSRAM: NOT FOUND *** THIS IS A PROBLEM ***");
    }

    // I2C scan
    Serial.println("\n--- I2C Scan (SDA=8, SCL=9) ---");
    Wire.begin(8, 9, 400000);
    delay(50);
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  Found device at 0x%02X", addr);
            if (addr == 0x24) Serial.print(" (CH32V003 IO Extension)");
            if (addr == 0x5D) Serial.print(" (GT911 Touch)");
            if (addr == 0x14) Serial.print(" (GT911 Touch alt)");
            if (addr == 0x70) Serial.print(" (PCA9548A Hub)");
            if (addr == 0x41) Serial.print(" (M5 ROTATE8)");
            if (addr == 0x54) Serial.print(" (DFRobot SEN0502)");
            Serial.println();
            found++;
        }
    }
    Serial.printf("Found %d I2C devices\n", found);

    Serial.println("\n--- TEST COMPLETE ---");
    Serial.println("If you see this, basic boot is working!\n");
}

void loop() {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.printf("[%lu] Heap: %d  PSRAM: %s (%d bytes)\n",
            millis() / 1000, ESP.getFreeHeap(),
            psramFound() ? "YES" : "NO", ESP.getFreePsram());
    }
    delay(100);
}
