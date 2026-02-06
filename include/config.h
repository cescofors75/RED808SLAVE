/**
 * @file config.h
 * @brief Configuración global del proyecto RED808 V5
 * @author RED808 Team
 * @date 2026-02-06
 * 
 * Constantes del sistema usando constexpr para type-safety
 * Reemplaza los #define antiguos con constantes tipadas
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================
// NAMESPACE DE CONFIGURACIÓN
// ============================================

namespace Config {
    
    // ============================================
    // SECUENCIADOR
    // ============================================
    constexpr uint8_t MAX_STEPS = 16;       ///< Número de pasos por patrón
    constexpr uint8_t MAX_TRACKS = 8;       ///< Número de tracks/instrumentos
    constexpr uint8_t MAX_PATTERNS = 16;    ///< Número de patrones almacenables
    constexpr uint8_t MAX_KITS = 3;         ///< Número de kits de sonido
    
    // ============================================
    // TEMPO Y TIMING
    // ============================================
    constexpr uint8_t MIN_BPM = 40;         ///< BPM mínimo
    constexpr uint8_t MAX_BPM = 240;        ///< BPM máximo
    constexpr uint8_t DEFAULT_BPM = 120;    ///< BPM por defecto
    
    // ============================================
    // AUDIO Y VOLUMEN
    // ============================================
    constexpr uint8_t DEFAULT_VOLUME = 75;      ///< Volumen por defecto (0-150)
    constexpr uint8_t MAX_VOLUME = 150;         ///< Volumen máximo ampliado
    constexpr uint8_t MAX_SAMPLES = 8;          ///< Número de live pads
    constexpr uint8_t DEFAULT_TRACK_VOLUME = 100; ///< Volumen por defecto de cada track
    
    // ============================================
    // ENCODERS Y TIMINGS
    // ============================================
    constexpr unsigned long ENCODER_READ_INTERVAL = 50;  ///< Intervalo de lectura M5 encoder (ms)
    constexpr unsigned long BUTTON_DEBOUNCE_TIME = 50;   ///< Debounce de botones (ms)
    constexpr unsigned long LED_FLASH_DURATION = 100;    ///< Duración flash LED (ms)
    constexpr unsigned long SCREEN_UPDATE_MIN = 16;      ///< Mínimo entre updates pantalla (ms) ~60fps
    
    // ============================================
    // WIFI/UDP
    // ============================================
    constexpr unsigned long UDP_CHECK_INTERVAL = 30000;  ///< Intervalo verificación UDP (ms)
    constexpr unsigned long WIFI_TIMEOUT = 20000;        ///< Timeout conexión WiFi (ms)
    constexpr uint16_t UDP_PORT = 8888;                  ///< Puerto UDP para comunicación
    
    // ============================================
    // DISPLAY
    // ============================================
    constexpr uint16_t SCREEN_WIDTH = 480;   ///< Ancho de pantalla TFT
    constexpr uint16_t SCREEN_HEIGHT = 320;  ///< Alto de pantalla TFT
    constexpr uint8_t MENU_ITEMS = 5;        ///< Número de items en menú principal
    
    // ============================================
    // HARDWARE I2C
    // ============================================
    constexpr uint8_t M5_ENCODER_ADDR = 0x41;  ///< Dirección I2C del M5 8ENCODER
    
} // namespace Config

// ============================================
// PIN DEFINITIONS (mantener como #define por requerimientos del compilador)
// ============================================

// TFT Display (SPI)
#ifndef TFT_CS
  #define TFT_CS   5
  #define TFT_DC   2
  #define TFT_RST  4
  #define TFT_MOSI 23
  #define TFT_SCK  18
  #define TFT_MISO 19
#endif

// TM1638 Module 1 (Steps 1-8)
#ifndef TM1638_1_STB
  #define TM1638_1_STB 33
  #define TM1638_1_CLK 25
  #define TM1638_1_DIO 26
#endif

// TM1638 Module 2 (Steps 9-16)
#ifndef TM1638_2_STB
  #define TM1638_2_STB 32
  #define TM1638_2_CLK 25
  #define TM1638_2_DIO 27
#endif

// Rotary Encoder
#ifndef ENCODER_CLK
  #define ENCODER_CLK 15
  #define ENCODER_DT  16
  #define ENCODER_SW  13
#endif

// Analog Buttons
#ifndef ANALOG_BUTTONS_PIN
  #define ANALOG_BUTTONS_PIN 34
  
  // Rangos ADC calibrados
  #define BTN_PLAY_STOP_MIN  350
  #define BTN_PLAY_STOP_MAX  750
  #define BTN_MUTE_MIN       1450
  #define BTN_MUTE_MAX       1600
  #define BTN_BACK_MIN       2250
  #define BTN_BACK_MAX       2450
  #define BTN_NONE_THRESHOLD 300
#endif

// Rotary Angle Potentiometer
#ifndef ROTARY_ANGLE_PIN
  #define ROTARY_ANGLE_PIN 35
#endif

// Volume Toggle Button
#ifndef VOLUME_TOGGLE_BTN
  #define VOLUME_TOGGLE_BTN 14
#endif

// I2C Pins
#ifndef I2C_SDA
  #define I2C_SDA 21
  #define I2C_SCL 22
#endif

#endif // CONFIG_H
