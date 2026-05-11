#include "comm/udp_handler.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#include "config.h"
#include "device_map.h"

namespace {
WiFiUDP g_udp;
IPAddress g_masterIp;
uint32_t g_lastWifiAttemptMs = 0;
uint32_t g_lastHeartbeatMs = 0;
wl_status_t g_lastWifiStatus = WL_IDLE_STATUS;
bool g_wifiConnectRequested = false;
bool g_wifiAssociated = false;
bool g_wifiHasIp = false;

const char* status_to_str(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

void on_wifi_event(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    g_wifiAssociated = false;
    g_wifiHasIp = false;
    g_wifiConnectRequested = false;
    if (!cfg::kDebugLog) return;
    Serial.printf(
      "[SlavePico][WiFi] disconnected reason=%d ssid=%s\n",
      static_cast<int>(info.wifi_sta_disconnected.reason),
      cfg::kWifiSsid
    );
  } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    g_wifiAssociated = true;
    g_wifiConnectRequested = true;
    if (!cfg::kDebugLog) return;
    Serial.printf("[SlavePico][WiFi] STA connected to %s\n", cfg::kWifiSsid);
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    g_wifiAssociated = true;
    g_wifiHasIp = true;
    g_wifiConnectRequested = true;
    if (!cfg::kDebugLog) return;
    Serial.printf("[SlavePico][WiFi] got ip=%s\n", WiFi.localIP().toString().c_str());
  }
}

void ensure_wifi_connected() {
  if (g_wifiHasIp || g_wifiAssociated || g_wifiConnectRequested) return;
  uint32_t now = millis();
  if (now - g_lastWifiAttemptMs < cfg::kWifiReconnectMs) return;
  g_lastWifiAttemptMs = now;
  g_wifiConnectRequested = true;
  if (cfg::kDebugLog) {
    Serial.printf("[SlavePico][WiFi] connecting to %s\n", cfg::kWifiSsid);
  }
  WiFi.begin(cfg::kWifiSsid, cfg::kWifiPass);
}

} // namespace

void udp_handler_init() {
  esp_log_level_set("rpc_core", ESP_LOG_NONE);
  esp_log_level_set("esp32-hal-hosted", ESP_LOG_NONE);
  esp_log_level_set("esp32-hal-hosted.c", ESP_LOG_NONE);
  esp_log_level_set("STA", ESP_LOG_WARN);
  WiFi.onEvent(on_wifi_event);
  // Give the P4<->C6 hosted stack a short warm-up window after boot.
  delay(2500);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  g_masterIp.fromString(cfg::kMasterIp);
  g_udp.begin(cfg::kLocalUdpPort);
  ensure_wifi_connected();
}

void udp_handler_process() {
  ensure_wifi_connected();

  wl_status_t st = WiFi.status();
  if (st != g_lastWifiStatus) {
    g_lastWifiStatus = st;
    if (cfg::kDebugLog) {
      if (st == WL_CONNECTED) {
        Serial.printf("[SlavePico][WiFi] connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      } else {
        Serial.printf("[SlavePico][WiFi] status=%d (%s)\n", static_cast<int>(st), status_to_str(st));
      }
    }
  }

  uint32_t now = millis();
  if (udp_is_ready() && (now - g_lastHeartbeatMs >= cfg::kHeartbeatMs)) {
    g_lastHeartbeatMs = now;
    udp_send_heartbeat();
  }

  // Scaffold fase 1: RX reservado para sync remoto futuro.
  while (g_udp.parsePacket() > 0) {
    while (g_udp.available()) {
      (void)g_udp.read();
    }
  }
}

void udp_send_event(const InputEvent& ev) {
  if (!udp_is_ready()) return;

  JsonDocument doc;
  doc["cmd"] = "slaveInput";
  doc["src"] = "SlavePico";
  doc["id"] = ev.controlId;
  doc["element"] = ev.elementId;
  doc["type"] = ev.eventType;
  doc["value"] = ev.value;

  if (ev.controlId == devices::CTRL_BYTEBTN_0 || ev.controlId == devices::CTRL_BYTEBTN_1) {
    doc["input"] = "byteButton";
    doc["module"] = (ev.controlId == devices::CTRL_BYTEBTN_0) ? 0 : 1;
    doc["button"] = ev.elementId;
    doc["pressed"] = true;
  } else if (ev.controlId == devices::CTRL_M5_ENC_BANK_0 || ev.controlId == devices::CTRL_M5_ENC_BANK_1) {
    doc["input"] = "m5Encoder";
    doc["module"] = (ev.controlId == devices::CTRL_M5_ENC_BANK_0) ? 0 : 1;
    doc["encoder"] = ev.elementId;
    if (ev.eventType == 0) {
      doc["delta"] = ev.value;
    } else if (ev.eventType == 1) {
      doc["pressed"] = true;
    }
  } else if (ev.controlId == devices::CTRL_I2C_HUB) {
    doc["input"] = "i2cHub";
    doc["present"] = (ev.value != 0);
  }

  g_udp.beginPacket(g_masterIp, cfg::kMasterUdpPort);
  serializeJson(doc, g_udp);
  g_udp.endPacket();

  if (cfg::kDebugLog) {
    Serial.printf("[SlavePico][UDP] event id=%u type=%u value=%d\n", ev.controlId, ev.eventType, ev.value);
  }
}

void udp_send_heartbeat() {
  if (!udp_is_ready()) return;

  JsonDocument doc;
  doc["cmd"] = "slaveHeartbeat";
  doc["src"] = "SlavePico";
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();

  g_udp.beginPacket(g_masterIp, cfg::kMasterUdpPort);
  serializeJson(doc, g_udp);
  g_udp.endPacket();

  if (cfg::kDebugLog) {
    Serial.printf("[SlavePico][UDP] heartbeat -> %s:%u\n", cfg::kMasterIp, cfg::kMasterUdpPort);
  }
}

bool udp_is_ready() {
  return g_wifiHasIp && WiFi.status() == WL_CONNECTED;
}
