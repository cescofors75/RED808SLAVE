/**
 * @file debug_utils.h
 * @brief Debug utilities and macros for RED808 V5
 * @author RED808 Team
 * @date 2026-02-06
 * 
 * Control debug output with DEBUG_MODE flag
 * Disable in production for better performance
 */

#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <Arduino.h>

// ============================================
// DEBUG CONFIGURATION
// ============================================

// Uncomment to enable debug output
#define DEBUG_MODE

// Debug levels
#define DEBUG_LEVEL_ERROR   0
#define DEBUG_LEVEL_WARNING 1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

// Set current debug level (0-3)
#ifndef DEBUG_LEVEL
  #define DEBUG_LEVEL DEBUG_LEVEL_INFO
#endif

// ============================================
// DEBUG MACROS
// ============================================

#ifdef DEBUG_MODE
  // Basic debug macros
  #define DEBUG_PRINT(x)       Serial.print(x)
  #define DEBUG_PRINTLN(x)     Serial.println(x)
  #define DEBUG_PRINTF(x, ...) Serial.printf(x, __VA_ARGS__)
  
  // Level-based debug macros
  #if DEBUG_LEVEL >= DEBUG_LEVEL_ERROR
    #define DEBUG_ERROR(x)       Serial.print("[ERROR] "); Serial.println(x)
    #define DEBUG_ERRORF(x, ...) Serial.printf("[ERROR] " x "\n", __VA_ARGS__)
  #else
    #define DEBUG_ERROR(x)
    #define DEBUG_ERRORF(x, ...)
  #endif
  
  #if DEBUG_LEVEL >= DEBUG_LEVEL_WARNING
    #define DEBUG_WARN(x)        Serial.print("[WARN] "); Serial.println(x)
    #define DEBUG_WARNF(x, ...)  Serial.printf("[WARN] " x "\n", __VA_ARGS__)
  #else
    #define DEBUG_WARN(x)
    #define DEBUG_WARNF(x, ...)
  #endif
  
  #if DEBUG_LEVEL >= DEBUG_LEVEL_INFO
    #define DEBUG_INFO(x)        Serial.print("[INFO] "); Serial.println(x)
    #define DEBUG_INFOF(x, ...)  Serial.printf("[INFO] " x "\n", __VA_ARGS__)
  #else
    #define DEBUG_INFO(x)
    #define DEBUG_INFOF(x, ...)
  #endif
  
  #if DEBUG_LEVEL >= DEBUG_LEVEL_VERBOSE
    #define DEBUG_VERBOSE(x)        Serial.print("[VERBOSE] "); Serial.println(x)
    #define DEBUG_VERBOSEF(x, ...)  Serial.printf("[VERBOSE] " x "\n", __VA_ARGS__)
  #else
    #define DEBUG_VERBOSE(x)
    #define DEBUG_VERBOSEF(x, ...)
  #endif
  
  // Timing macros
  #define DEBUG_TIME_START(name) unsigned long __debug_time_##name = millis()
  #define DEBUG_TIME_END(name)   DEBUG_PRINTF("[TIME] %s: %lums\n", #name, millis() - __debug_time_##name)
  
  // Memory macros
  #define DEBUG_MEM()            DEBUG_PRINTF("[MEM] Free heap: %d bytes\n", ESP.getFreeHeap())
  
#else
  // Disabled debug macros (zero overhead)
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x, ...)
  #define DEBUG_ERROR(x)
  #define DEBUG_ERRORF(x, ...)
  #define DEBUG_WARN(x)
  #define DEBUG_WARNF(x, ...)
  #define DEBUG_INFO(x)
  #define DEBUG_INFOF(x, ...)
  #define DEBUG_VERBOSE(x)
  #define DEBUG_VERBOSEF(x, ...)
  #define DEBUG_TIME_START(name)
  #define DEBUG_TIME_END(name)
  #define DEBUG_MEM()
#endif

// ============================================
// HELPER FUNCTIONS
// ============================================

#ifdef DEBUG_MODE
/**
 * @brief Print a separator line
 */
inline void debugSeparator() {
    Serial.println("════════════════════════════════════════════");
}

/**
 * @brief Print a header with borders
 * @param title Header title
 */
inline void debugHeader(const char* title) {
    Serial.println("\n╔════════════════════════════════════════════╗");
    Serial.printf("║  %-41s║\n", title);
    Serial.println("╚════════════════════════════════════════════╝");
}

/**
 * @brief Print binary representation of a value
 * @param value Value to print
 * @param bits Number of bits to display
 */
inline void debugBinary(uint16_t value, uint8_t bits = 16) {
    Serial.print("0b");
    for (int i = bits - 1; i >= 0; i--) {
        Serial.print((value >> i) & 1);
        if (i == 8) Serial.print(" ");
    }
}

#else
inline void debugSeparator() {}
inline void debugHeader(const char* title) {}
inline void debugBinary(uint16_t value, uint8_t bits = 16) {}
#endif

#endif // DEBUG_UTILS_H
