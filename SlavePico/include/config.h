#pragma once

#include <Arduino.h>

namespace cfg {

static constexpr bool kDebugLog = true;

// WiFi / UDP (igual que P4)
static constexpr const char* kWifiSsid = "RED808";
static constexpr const char* kWifiPass = "red808esp32";
static constexpr const char* kMasterIp = "192.168.4.1";
static constexpr uint16_t kMasterUdpPort = 8888;
static constexpr uint16_t kLocalUdpPort = 8890;

// Tiempos de red
static constexpr uint32_t kWifiReconnectMs = 2500;
static constexpr uint32_t kHeartbeatMs = 2000;

// I2C base
static constexpr uint8_t kI2cSdaPin = 7;
static constexpr uint8_t kI2cSclPin = 8;
static constexpr uint32_t kI2cClockHz = 100000;
static constexpr uint8_t kI2cHubAddr = 0x70; // PCA9548A
static constexpr uint32_t kI2cHubSettleUs = 8;
static constexpr bool kI2cBaseIsolationMode = false;

// Direcciones I2C de modulos
static constexpr uint8_t kAddrDfRobotRotary = 0x54;
static constexpr uint8_t kAddrM5Encoder8 = 0x41;
static constexpr uint8_t kAddrM5ByteButton = 0x47;
static constexpr bool kEnableDfRotary = true;
static constexpr bool kEnableM5Encoder = false;
static constexpr bool kEnableM5ByteButton = false;

static constexpr float kTempoMin = 60.0f;
static constexpr float kTempoMax = 200.0f;

// Polling
static constexpr bool kEnableI2cPolling = true;
static constexpr uint32_t kInputPollMs = 10;
static constexpr uint32_t kButtonPollMs = 12;
static constexpr uint32_t kFaderPollMs = 15;

// Fader unit analog
static constexpr bool kEnableFaderAnalog = false;
static constexpr uint8_t kFaderAnalogPin = 0;

// Reserva fase 2 (rotary analog directos)
static constexpr uint8_t kReservedAnalogRotaryPins[4] = {1, 2, 3, 4};

} // namespace cfg
