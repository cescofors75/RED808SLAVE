#include "drivers/input_manager.h"

#include <Arduino.h>
#include <M5ROTATE8.h>
#include <Wire.h>

#include "config.h"
#include "device_map.h"
#include "drivers/i2c_driver.h"

namespace {
constexpr uint8_t kQueueLen = 32;
constexpr uint8_t kByteButtonStatusReg = 0x00;
constexpr uint8_t kByteButtonStatus8ByteReg = 0x60;
constexpr uint8_t kByteButtonButtons = 8;
constexpr uint8_t kByteButtonLedBrightnessReg = 0x10;
constexpr uint8_t kByteButtonLedShowModeReg = 0x19;
constexpr uint8_t kByteButtonLedRgb888Reg = 0x20;
constexpr uint8_t kByteButtonLedCount = kByteButtonButtons + 1;
constexpr uint8_t kByteButtonLedUserDefined = 0;
constexpr uint8_t kByteButtonBrightness = 160;
constexpr uint8_t kEncodersPerModule = 8;
constexpr int32_t kM5DeltaClamp = 8;
InputEvent g_queue[kQueueLen] = {};
volatile uint8_t g_head = 0;
volatile uint8_t g_tail = 0;
int16_t g_lastFader = -1;
bool g_hubDetected = false;
bool g_byteButton0Detected = false;
bool g_m5Encoder0Detected = false;
uint8_t g_prevByteButton0State = 0;
uint8_t g_byteButton0FailCount = 0;
uint32_t g_byteButton0SuspendUntilMs = 0;
uint8_t g_byteButton0PresenceCount = 0;
uint8_t g_m5Encoder0PresenceCount = 0;
uint8_t g_hubProbeChannel = 0;
uint32_t g_lastHubProbeLogMs = 0;
uint32_t g_hubRecoverUntilMs = 0;
uint8_t g_hubRecoverCount = 0;
int32_t g_prevM5Counter0[kEncodersPerModule] = {};
uint8_t g_m5ButtonConfirm0[kEncodersPerModule] = {};
bool g_m5ButtonArmed0[kEncodersPerModule] = {};
unsigned long g_lastM5ButtonPress0[kEncodersPerModule] = {};
uint32_t g_lastLedAnimMs = 0;
uint8_t g_bootAnimPhase = 0;
bool g_bootAnimDone = false;
bool g_byteButton0LedsReady = false;
M5ROTATE8 g_m5Encoder0;

bool push_event(uint8_t id, uint8_t elementId, int16_t value, uint8_t type) {
  uint8_t next = static_cast<uint8_t>((g_head + 1) % kQueueLen);
  if (next == g_tail) return false;
  g_queue[g_head] = {id, elementId, value, type};
  g_head = next;
  return true;
}

bool probe_selected_bus_device(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool detect_hub_locked() {
  Wire.beginTransmission(cfg::kI2cHubAddr);
  return Wire.endTransmission() == 0;
}

void poll_hub_base_isolation_locked() {
  uint32_t now = millis();
  if (now < g_hubRecoverUntilMs) {
    return;
  }

  bool switchOk = i2c_hub_select(g_hubProbeChannel);
  if (switchOk) {
    g_hubRecoverCount = 0;
    i2c_hub_deselect();
  } else {
    if (g_hubRecoverCount < 255) g_hubRecoverCount++;
    bool reinitOk = i2c_driver_reinit();
    g_hubRecoverUntilMs = now + 250;
    if (cfg::kDebugLog) {
      Serial.printf("[SlavePico][I2C] base recover ch=%u reinit=%d count=%u\n",
                    g_hubProbeChannel,
                    reinitOk ? 1 : 0,
                    g_hubRecoverCount);
    }
  }

  if (cfg::kDebugLog && now - g_lastHubProbeLogMs >= 500) {
    g_lastHubProbeLogMs = now;
    Serial.printf("[SlavePico][I2C] base probe ch=%u switch=%d hub=%d\n",
                  g_hubProbeChannel,
                  switchOk ? 1 : 0,
                  g_hubDetected ? 1 : 0);
  }

  g_hubProbeChannel = static_cast<uint8_t>((g_hubProbeChannel + 1) & 0x07);
}

bool read_bytebutton_status_locked(uint8_t channel, uint8_t& status) {
  status = 0;
  bool ok = false;
  if (!i2c_hub_select(channel)) return false;

  uint8_t statusBytes[kByteButtonButtons] = {};
  Wire.beginTransmission(cfg::kAddrM5ByteButton);
  Wire.write(kByteButtonStatus8ByteReg);
  if (Wire.endTransmission() == 0 &&
      Wire.requestFrom((uint8_t)cfg::kAddrM5ByteButton, (uint8_t)kByteButtonButtons) == kByteButtonButtons) {
    for (uint8_t i = 0; i < kByteButtonButtons; ++i) {
      statusBytes[i] = Wire.read();
      if (statusBytes[i]) status |= (uint8_t)(1u << i);
    }
    ok = true;
  } else {
    Wire.beginTransmission(cfg::kAddrM5ByteButton);
    Wire.write(kByteButtonStatusReg);
    if (Wire.endTransmission() == 0 &&
        Wire.requestFrom((uint8_t)cfg::kAddrM5ByteButton, (uint8_t)1) == 1) {
      status = Wire.read();
      ok = true;
    }
  }

  i2c_hub_deselect();
  return ok;
}

bool bytebutton_write_bytes(uint8_t reg, const uint8_t* data, uint8_t length) {
  Wire.beginTransmission(cfg::kAddrM5ByteButton);
  Wire.write(reg);
  for (uint8_t i = 0; i < length; ++i) {
    Wire.write(data[i]);
  }
  return Wire.endTransmission() == 0;
}

bool init_bytebutton_leds_locked(uint8_t channel) {
  bool ok = false;
  if (!i2c_hub_select(channel)) return false;
  uint8_t mode = kByteButtonLedUserDefined;
  if (bytebutton_write_bytes(kByteButtonLedShowModeReg, &mode, 1)) {
    ok = true;
    for (uint8_t led = 0; led < kByteButtonLedCount; ++led) {
      uint8_t brightness = kByteButtonBrightness;
      if (!bytebutton_write_bytes(kByteButtonLedBrightnessReg + led, &brightness, 1)) {
        ok = false;
        break;
      }
    }
  }
  i2c_hub_deselect();
  return ok;
}

bool write_bytebutton_led_locked(uint8_t channel, uint8_t led, uint8_t red, uint8_t green, uint8_t blue) {
  if (led >= kByteButtonLedCount) return false;
  if (!i2c_hub_select(channel)) return false;
  uint8_t rgb4[4] = {red, green, blue, 0};
  bool ok = bytebutton_write_bytes(kByteButtonLedRgb888Reg + led * 4, rgb4, 4);
  i2c_hub_deselect();
  return ok;
}

void write_bytebutton_idle_locked() {
  for (uint8_t led = 0; led < kByteButtonLedCount; ++led) {
    uint8_t red = 0;
    uint8_t green = (led == 0) ? 18 : 8;
    uint8_t blue = (led == 0) ? 24 : 36;
    (void)write_bytebutton_led_locked(devices::HUB_CH_BYTEBTN_0, led, red, green, blue);
  }
}

void write_m5encoder_idle_locked() {
  for (uint8_t enc = 0; enc < kEncodersPerModule; ++enc) {
    uint8_t red = 8;
    uint8_t green = static_cast<uint8_t>(14 + enc * 3);
    uint8_t blue = static_cast<uint8_t>(26 + enc * 8);
    (void)g_m5Encoder0.writeRGB(enc, red, green, blue);
  }
}

void run_led_boot_animation_locked() {
  uint32_t now = millis();
  if (now - g_lastLedAnimMs < 90) return;
  g_lastLedAnimMs = now;

  if (!g_bootAnimDone) {
    if (g_m5Encoder0Detected) {
      for (uint8_t enc = 0; enc < kEncodersPerModule; ++enc) {
        if (enc == g_bootAnimPhase % kEncodersPerModule) {
          (void)g_m5Encoder0.writeRGB(enc, 90, 10, 0);
        } else {
          (void)g_m5Encoder0.writeRGB(enc, 0, 0, 6);
        }
      }
    }

    if (g_byteButton0Detected && g_byteButton0LedsReady) {
      for (uint8_t led = 0; led < kByteButtonLedCount; ++led) {
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 4;
        if (led == g_bootAnimPhase % kByteButtonLedCount) {
          red = 0;
          green = 70;
          blue = 24;
        }
        (void)write_bytebutton_led_locked(devices::HUB_CH_BYTEBTN_0, led, red, green, blue);
      }
    }

    g_bootAnimPhase++;
    if (g_bootAnimPhase >= 18) {
      g_bootAnimDone = true;
      if (g_m5Encoder0Detected) write_m5encoder_idle_locked();
      if (g_byteButton0Detected && g_byteButton0LedsReady) write_bytebutton_idle_locked();
    }
  }
}

bool detect_m5encoder0_locked() {
  bool ok = false;
  if (!i2c_hub_select(devices::HUB_CH_M5_ENC_0)) return false;
  if (!probe_selected_bus_device(cfg::kAddrM5Encoder8)) {
    i2c_hub_deselect();
    return false;
  }

  if (!g_m5Encoder0Detected) {
    if (g_m5Encoder0.begin()) {
      for (uint8_t enc = 0; enc < kEncodersPerModule; ++enc) {
        g_m5Encoder0.resetCounter(enc);
        g_prevM5Counter0[enc] = 0;
        g_m5ButtonArmed0[enc] = true;
      }
      ok = true;
    }
  } else {
    ok = true;
  }
  i2c_hub_deselect();
  return ok;
}

} // namespace

void input_manager_init() {
  if (cfg::kEnableFaderAnalog) {
    pinMode(cfg::kFaderAnalogPin, INPUT);
  }
  for (uint8_t enc = 0; enc < kEncodersPerModule; ++enc) {
    g_m5ButtonArmed0[enc] = true;
  }
}

void input_manager_poll_i2c() {
  if (!i2c_driver_is_ready()) return;
  if (!i2c_lock(30)) return;

  bool hubDetectedNow = detect_hub_locked();
  if (hubDetectedNow != g_hubDetected) {
    g_hubDetected = hubDetectedNow;
    if (cfg::kDebugLog) {
      Serial.printf("[SlavePico][I2C] hub %s at 0x%02X\n", g_hubDetected ? "detected" : "missing", cfg::kI2cHubAddr);
    }
    (void)push_event(devices::CTRL_I2C_HUB, 0, g_hubDetected ? 1 : 0, 1);
  }

  if (!hubDetectedNow) {
    i2c_unlock();
    return;
  }

  if (cfg::kI2cBaseIsolationMode) {
    poll_hub_base_isolation_locked();
    i2c_unlock();
    return;
  }

  if (cfg::kEnableM5ByteButton) {
    uint8_t byteButtonStatus = 0;
    bool byteButton0AckNow = false;
    bool byteButton0DetectedNow = false;
    uint32_t nowMs = millis();
    if (i2c_hub_select(devices::HUB_CH_BYTEBTN_0)) {
      byteButton0AckNow = probe_selected_bus_device(cfg::kAddrM5ByteButton);
      i2c_hub_deselect();
    }
    if (byteButton0AckNow) {
      if (g_byteButton0PresenceCount < 255) g_byteButton0PresenceCount++;
    } else {
      g_byteButton0PresenceCount = 0;
    }

    if (nowMs >= g_byteButton0SuspendUntilMs && g_byteButton0PresenceCount >= 2) {
      byteButton0DetectedNow = read_bytebutton_status_locked(devices::HUB_CH_BYTEBTN_0, byteButtonStatus);
      if (byteButton0DetectedNow) {
        g_byteButton0FailCount = 0;
      } else {
        if (g_byteButton0FailCount < 255) g_byteButton0FailCount++;
        if (g_byteButton0FailCount >= 3) {
          g_byteButton0SuspendUntilMs = nowMs + 750;
          if (cfg::kDebugLog) {
            Serial.println("[SlavePico][I2C] ByteButton0 suspended after repeated read errors");
          }
        }
      }
    }

    if (byteButton0DetectedNow != g_byteButton0Detected) {
      g_byteButton0Detected = byteButton0DetectedNow;
      if (g_byteButton0Detected) {
        g_byteButton0LedsReady = init_bytebutton_leds_locked(devices::HUB_CH_BYTEBTN_0);
        if (!g_bootAnimDone && g_byteButton0LedsReady) {
          write_bytebutton_idle_locked();
        }
      } else {
        g_byteButton0LedsReady = false;
      }
      if (cfg::kDebugLog) {
        Serial.printf("[SlavePico][I2C] ByteButton0 %s on hub ch%d\n", g_byteButton0Detected ? "detected" : "missing", devices::HUB_CH_BYTEBTN_0);
      }
    }

    if (byteButton0DetectedNow) {
      uint8_t pressedEdges = byteButtonStatus & (uint8_t)~g_prevByteButton0State;
      g_prevByteButton0State = byteButtonStatus;
      for (uint8_t button = 0; button < kByteButtonButtons; ++button) {
        if ((pressedEdges & (1u << button)) == 0) continue;
        (void)push_event(devices::CTRL_BYTEBTN_0, button, 1, 1);
        if (g_byteButton0LedsReady) {
          (void)write_bytebutton_led_locked(devices::HUB_CH_BYTEBTN_0, button, 0, 90, 36);
        }
        if (cfg::kDebugLog) {
          Serial.printf("[SlavePico][I2C] ByteButton0 press b=%u mask=0x%02X\n", button, byteButtonStatus);
        }
      }
    }
  }

  if (cfg::kEnableM5Encoder) {
    bool m5Encoder0AckNow = false;
    if (i2c_hub_select(devices::HUB_CH_M5_ENC_0)) {
      m5Encoder0AckNow = probe_selected_bus_device(cfg::kAddrM5Encoder8);
      i2c_hub_deselect();
    }
    if (m5Encoder0AckNow) {
      if (g_m5Encoder0PresenceCount < 255) g_m5Encoder0PresenceCount++;
    } else {
      g_m5Encoder0PresenceCount = 0;
    }

    bool m5Encoder0DetectedNow = false;
    if (g_m5Encoder0PresenceCount >= 2) {
      m5Encoder0DetectedNow = detect_m5encoder0_locked();
    }
    if (m5Encoder0DetectedNow != g_m5Encoder0Detected) {
      g_m5Encoder0Detected = m5Encoder0DetectedNow;
      if (g_m5Encoder0Detected && !g_bootAnimDone) {
        write_m5encoder_idle_locked();
      }
      if (cfg::kDebugLog) {
        Serial.printf("[SlavePico][I2C] M5Encoder0 %s on hub ch%d\n", g_m5Encoder0Detected ? "detected" : "missing", devices::HUB_CH_M5_ENC_0);
      }
    }

    if (m5Encoder0DetectedNow) {
      for (uint8_t enc = 0; enc < kEncodersPerModule; ++enc) {
        int32_t counter = g_m5Encoder0.getAbsCounter(enc);
        int32_t delta = counter - g_prevM5Counter0[enc];
        if (delta != 0) {
          g_prevM5Counter0[enc] = counter;
          if (abs((int)delta) <= kM5DeltaClamp) {
            int16_t signedDelta = static_cast<int16_t>(-delta);
            (void)push_event(devices::CTRL_M5_ENC_BANK_0, enc, signedDelta, 0);
            uint8_t red = signedDelta > 0 ? 0 : 70;
            uint8_t green = signedDelta > 0 ? 70 : 12;
            uint8_t blue = 18;
            (void)g_m5Encoder0.writeRGB(enc, red, green, blue);
            if (cfg::kDebugLog) {
              Serial.printf("[SlavePico][I2C] M5Encoder0 enc=%u delta=%d counter=%ld\n", enc, signedDelta, (long)counter);
            }
          }
        }

        bool btnNow = g_m5Encoder0.getKeyPressed(enc);
        if (btnNow) {
          if (g_m5ButtonConfirm0[enc] < 255) g_m5ButtonConfirm0[enc]++;
        } else {
          g_m5ButtonConfirm0[enc] = 0;
          g_m5ButtonArmed0[enc] = true;
        }

        if (g_m5ButtonConfirm0[enc] >= 3 && g_m5ButtonArmed0[enc]) {
          unsigned long now = millis();
          if (now - g_lastM5ButtonPress0[enc] >= 250) {
            g_lastM5ButtonPress0[enc] = now;
            g_m5ButtonArmed0[enc] = false;
            (void)push_event(devices::CTRL_M5_ENC_BANK_0, enc, 1, 1);
            (void)g_m5Encoder0.writeRGB(enc, 90, 0, 12);
            if (cfg::kDebugLog) {
              Serial.printf("[SlavePico][I2C] M5Encoder0 button enc=%u\n", enc);
            }
          }
        }
      }
    }
  }

  if (cfg::kEnableM5ByteButton || cfg::kEnableM5Encoder) {
    run_led_boot_animation_locked();
  }

  i2c_hub_deselect();
  i2c_unlock();
}

void input_manager_poll_analog() {
  if (!cfg::kEnableFaderAnalog) return;
  int raw = analogRead(cfg::kFaderAnalogPin);
  if (g_lastFader < 0) {
    g_lastFader = static_cast<int16_t>(raw);
    return;
  }
  if (abs(raw - g_lastFader) >= 8) {
    g_lastFader = static_cast<int16_t>(raw);
    (void)push_event(devices::CTRL_FADER_0, 0, static_cast<int16_t>(raw), 2);
  }
}

bool input_manager_pop_event(InputEvent& out) {
  if (g_tail == g_head) return false;
  out = g_queue[g_tail];
  g_tail = static_cast<uint8_t>((g_tail + 1) % kQueueLen);
  return true;
}
