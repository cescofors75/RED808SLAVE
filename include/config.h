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
    constexpr uint8_t MAX_TRACKS = 16;      ///< Número de tracks/instrumentos (2 páginas x 8)
    constexpr uint8_t TRACKS_PER_PAGE = 8;  ///< Tracks visibles por página en sequencer
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
    constexpr uint8_t MAX_SAMPLES = 16;         ///< Número de live pads
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
    // DISPLAY (Waveshare ESP32-S3-Touch-LCD-7, RGB 1024x600)
    // ============================================
    constexpr uint16_t SCREEN_WIDTH = 1024;  ///< Ancho de pantalla LCD
    constexpr uint16_t SCREEN_HEIGHT = 600;  ///< Alto de pantalla LCD
    constexpr uint8_t MENU_ITEMS = 6;        ///< Número de items en menú principal
    
    // ============================================
    // HARDWARE I2C
    // ============================================
    constexpr uint8_t M5_ENCODER_MODULES = 2;       ///< Cantidad de módulos M5 8ENCODER esperados
    constexpr uint8_t ENCODERS_PER_MODULE = 8;      ///< Encoders por cada módulo M5 8ENCODER
    constexpr uint8_t M5_ENCODER_ADDR_1 = 0x41;     ///< Dirección I2C módulo Encoder8 #1
    constexpr uint8_t M5_ENCODER_ADDR_2 = 0x41;     ///< Dirección I2C módulo Encoder8 #2 (misma dir, diferente canal hub)

    // ============================================
    // DFROBOT VISUAL ROTARY ENCODERS (SEN0502 x2)
    // ============================================
    // 2x SEN0502: corona LED, encoder infinito, switch
    // Cada uno en un canal diferente del PCA9548A (auto-detectado)
    //
    // FUNCIONES ASIGNADAS:
    //   #1: FX (ajuste param principal) - btn: cambiar efecto (DELAY/FLANGER/COMPRESSOR)
    //   #2: Pattern select              - btn: reset patrón 0
    //
    // OTROS CONTROLES ANALÓGICOS (no DFRobot, no I2C):
    //   - 3x Rotary Angle finito sin botón (pots GPIO 35/39/36)
    //   - 1x Rotary infinito con botón (encoder mecánico)
    constexpr uint8_t DFROBOT_ENCODER_COUNT = 2;     ///< Total de DFRobot rotary encoders
    constexpr uint8_t DFROBOT_ENCODER_ADDRS[2] = {
        0x54, 0x54  // Misma dirección, separados por canal PCA9548A
    };

    // ============================================
    // HUB I2C PCA9548A (auto-detectado en runtime)
    // ============================================
    // Hub pasivo: todos comparten bus (direcciones deben ser únicas)
    // Hub activo (PCA9548A): auto-detectado en runtime, permite misma dirección en canales diferentes
    //
    // ASIGNACIÓN DE CANALES PCA9548A (auto-detectado por scanI2CBus):
    //   CH0: M5 Encoder8 #1 (0x41)
    //   CH1: M5 Encoder8 #2 (0x41)
    //   CH2-CH3: DFRobot SEN0502 #1-#2 (0x54) — se detectan en orden
    //   CH4-CH7: libres para expansión
    constexpr uint8_t I2C_HUB_ADDR = 0x70;          ///< Dirección del hub PCA9548A
    
} // namespace Config

// ============================================
// PIN DEFINITIONS - Waveshare ESP32-S3-Touch-LCD-7
// ============================================
// NOTE: LCD uses RGB parallel (GPIO 0,1,2,3,5,7,10,14,17,18,21,38,39,40,41,42,45,46,47,48)
//       Touch INT = GPIO 4
//       I2C = GPIO 8, 9
//       USB = GPIO 19, 20
//       PSRAM = GPIO 33-37
//       Free GPIOs: 6, 11, 12, 13, 15, 16
// ============================================

// I2C Pins (shared bus: CH422G, GT911, PCA9548A, M5ROTATE8, DFRobot)
#ifndef I2C_SDA
  #define I2C_SDA 8
  #define I2C_SCL 9
#endif

// Rotary Encoder (optional, if connected)
#ifndef ENCODER_CLK
  #define ENCODER_CLK 15
  #define ENCODER_DT  16
  #define ENCODER_SW  13
#endif

// Analog input (only GPIO 6 available as ADC1 - all others used by LCD)
// Can be used for one potentiometer or analog button set
#ifndef ANALOG_INPUT_PIN
  #define ANALOG_INPUT_PIN 6
#endif

#endif // CONFIG_H
