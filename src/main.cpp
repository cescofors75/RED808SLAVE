#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <TM1638plus.h>
#include <M5ROTATE8.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
// #include <ESPAsyncWebServer.h>  // DESHABILITADO - No se usa en SLAVE
// #include <LittleFS.h>  // DESHABILITADO - No se usa en SLAVE
// #include <FS.h>  // DESHABILITADO - No se usa en SLAVE

// ============================================
// PIN DEFINITIONS
// ============================================

// TFT Display (SPI) - Sin Touch
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define TFT_MOSI 23
#define TFT_SCK  18
#define TFT_MISO 19
//#define TFT_BL   -1

// TM1638 Module 1 (Steps 1-8)
#define TM1638_1_STB 33
#define TM1638_1_CLK 25
#define TM1638_1_DIO 26

// TM1638 Module 2 (Steps 9-16)
#define TM1638_2_STB 32
#define TM1638_2_CLK 25
#define TM1638_2_DIO 27

// Rotary Encoder (5 pins)
#define ENCODER_CLK 15
#define ENCODER_DT  12
#define ENCODER_SW  13

// 3 BUTTON PANEL - SMART OPEN ANALOG BUTTONS (3 pins: GND, VCC, SIG)
#define ANALOG_BUTTONS_PIN 34  // Pin analógico para leer los 3 botones

// Rangos de valores ADC para cada botón (CALIBRADOS según medición real)
#define BTN_PLAY_STOP_MIN  350   // Botón 1: Play/Stop (medido: 432-656)
#define BTN_PLAY_STOP_MAX  750
#define BTN_MUTE_MIN       1450  // Botón 2: Mute/Clear (medido: 1500-1550)
#define BTN_MUTE_MAX       1600
#define BTN_BACK_MIN       2250  // Botón 3: Back (medido: 2316-2376)
#define BTN_BACK_MAX       2450
#define BTN_NONE_THRESHOLD 300   // Sin botón presionado

// Rotary Angle Potentiometer (3 pins)
#define ROTARY_ANGLE_PIN 35

// Volume Toggle Button (pin 14)
#define VOLUME_TOGGLE_BTN 14

// I2C Pins for M5 8ENCODER
#define I2C_SDA 21
#define I2C_SCL 22
#define M5_8ENCODER_ADDR 0x41  // Dirección por defecto del M5 8ENCODER

// ============================================
// CONSTANTS
// ============================================
#define MAX_STEPS 16
#define MAX_TRACKS 8  // 8 tracks/instrumentos (simplificado)
#define MAX_PATTERNS 16
#define MAX_KITS 3
#define MIN_BPM 40
#define MAX_BPM 240
#define DEFAULT_BPM 120
#define DEFAULT_VOLUME 75
#define MAX_VOLUME 150  // Volumen máximo ampliado para mayor potencia
#define MAX_SAMPLES 8  // 8 LIVE PADS (simplificado)

// ============================================
// SISTEMA DE TEMAS VISUALES
// ============================================
struct ColorTheme {
    const char* name;
    uint16_t bg;
    uint16_t primary;
    uint16_t primaryLight;
    uint16_t accent;
    uint16_t accent2;
    uint16_t text;
    uint16_t textDim;
    uint16_t success;
    uint16_t warning;
    uint16_t error;
    uint16_t border;
};

// TEMA 1: RED808 - Rojo Corporativo
const ColorTheme THEME_RED808 = {
    "RED808",
    0x1800,      // BG: Rojo muy oscuro
    0xC000,      // PRIMARY: Rojo intenso
    0xE186,      // PRIMARY_LIGHT: Rojo claro
    0xF800,      // ACCENT: Rojo brillante puro
    0xFD20,      // ACCENT2: Naranja-rojo
    0xFFFF,      // TEXT: Blanco
    0xE73C,      // TEXT_DIM: Gris cálido
    0x07E0,      // SUCCESS: Verde
    0xFFE0,      // WARNING: Amarillo
    0xF800,      // ERROR: Rojo
    0x9800       // BORDER: Rojo oscuro
};

// TEMA 2: NAVY - Azul Profesional (original)
const ColorTheme THEME_NAVY = {
    "NAVY",
    0x0821,      // BG: Azul oscuro
    0x1082,      // PRIMARY: Navy
    0x2945,      // PRIMARY_LIGHT: Navy claro
    0x3D8F,      // ACCENT: Azul medio
    0x04FF,      // ACCENT2: Cian
    0xFFFF,      // TEXT: Blanco
    0xBDF7,      // TEXT_DIM: Gris azulado
    0x07E0,      // SUCCESS: Verde
    0xFD20,      // WARNING: Naranja
    0xF800,      // ERROR: Rojo
    0x4A49       // BORDER: Azul gris
};

// TEMA 3: CYBERPUNK - Púrpura/Magenta
const ColorTheme THEME_CYBER = {
    "CYBER",
    0x1006,      // BG: Púrpura muy oscuro
    0x780F,      // PRIMARY: Púrpura intenso
    0xA817,      // PRIMARY_LIGHT: Púrpura claro
    0xF81F,      // ACCENT: Magenta brillante
    0x07FF,      // ACCENT2: Cian eléctrico
    0xFFFF,      // TEXT: Blanco
    0xDEFB,      // TEXT_DIM: Gris lavanda
    0x07E0,      // SUCCESS: Verde neón
    0xFFE0,      // WARNING: Amarillo
    0xF81F,      // ERROR: Magenta
    0x8010       // BORDER: Púrpura medio
};

// TEMA 4: EMERALD - Verde/Menta
const ColorTheme THEME_EMERALD = {
    "EMERALD",
    0x0420,      // BG: Verde muy oscuro
    0x0540,      // PRIMARY: Verde bosque
    0x2E86,      // PRIMARY_LIGHT: Verde medio
    0x07E0,      // ACCENT: Verde brillante
    0x07FF,      // ACCENT2: Turquesa
    0xFFFF,      // TEXT: Blanco
    0xCE79,      // TEXT_DIM: Gris verdoso
    0x07E0,      // SUCCESS: Verde
    0xFFE0,      // WARNING: Amarillo
    0xF800,      // ERROR: Rojo
    0x0460       // BORDER: Verde oscuro
};

// Array de temas disponibles
const ColorTheme* THEMES[] = {&THEME_RED808, &THEME_NAVY, &THEME_CYBER, &THEME_EMERALD};
const int THEME_COUNT = 4;

// Tema actual (RED808 por defecto)
int currentTheme = 0;
const ColorTheme* activeTheme = &THEME_RED808;

// Macros para acceso rápido a colores del tema activo
#define COLOR_BG           (activeTheme->bg)
#define COLOR_PRIMARY      (activeTheme->primary)
#define COLOR_PRIMARY_LIGHT (activeTheme->primaryLight)
#define COLOR_ACCENT       (activeTheme->accent)
#define COLOR_ACCENT2      (activeTheme->accent2)
#define COLOR_TEXT         (activeTheme->text)
#define COLOR_TEXT_DIM     (activeTheme->textDim)
#define COLOR_SUCCESS      (activeTheme->success)
#define COLOR_WARNING      (activeTheme->warning)
#define COLOR_ERROR        (activeTheme->error)
#define COLOR_BORDER       (activeTheme->border)

// Compatibilidad con código anterior
#define COLOR_NAVY         (activeTheme->primary)
#define COLOR_NAVY_LIGHT   (activeTheme->primaryLight)

// COLORES VIVOS PARA CADA INSTRUMENTO (8 colores luminosos)
#define COLOR_INST_KICK    0xF800  // Rojo brillante - Bass Drum
#define COLOR_INST_SNARE   0xFD20  // Naranja - Snare Drum
#define COLOR_INST_CLHAT   0xFFE0  // Amarillo - Closed Hi-Hat
#define COLOR_INST_OPHAT   0x07FF  // Cian - Open Hi-Hat
#define COLOR_INST_CLAP    0xF81F  // Magenta - Clap
#define COLOR_INST_TOMLO   0x07E0  // Verde - Tom Low
#define COLOR_INST_TOMHI   0x3E7F  // Verde agua - Tom High
#define COLOR_INST_CYMBAL  0x4A7F  // Azul claro - Cymbal

// ============================================
// ENUMS
// ============================================
enum Screen {
    SCREEN_BOOT,
    SCREEN_MENU,
    SCREEN_LIVE,
    SCREEN_SEQUENCER,
    SCREEN_SETTINGS,
    SCREEN_DIAGNOSTICS,
    SCREEN_PATTERNS
};

enum DisplayMode {
    DISPLAY_BPM,
    DISPLAY_VOLUME,
    DISPLAY_PATTERN,
    DISPLAY_KIT,
    DISPLAY_INSTRUMENT,
    DISPLAY_STEP
};

// ============================================
// STRUCTURES
// ============================================
struct Pattern {
    bool steps[MAX_TRACKS][MAX_STEPS];
    bool muted[MAX_TRACKS];
    String name;
};

struct DrumKit {
    String name;
    int folder;
};

struct DiagnosticInfo {
    bool tftOk;
    bool tm1638_1_Ok;
    bool tm1638_2_Ok;
    bool encoderOk;
    bool m5encoderOk;
    bool udpConnected;
    String lastError;
    
    DiagnosticInfo() : 
        tftOk(false), 
        tm1638_1_Ok(false), 
        tm1638_2_Ok(false), 
        encoderOk(false), 
        m5encoderOk(false),
        udpConnected(false),
        lastError("") {}
};

// ============================================
// GLOBAL OBJECTS
// ============================================
TFT_eSPI tft = TFT_eSPI();
TM1638plus tm1(TM1638_1_STB, TM1638_1_CLK, TM1638_1_DIO, true);
TM1638plus tm2(TM1638_2_STB, TM1638_2_CLK, TM1638_2_DIO, true);
HardwareSerial dfplayerSerial(1);  // UART1 para DFPlayer (pines configurables)
M5ROTATE8 m5encoder(M5_8ENCODER_ADDR);  // M5 8ENCODER module
// RotaryEncoder encoder(ENCODER_CLK, ENCODER_DT, RotaryEncoder::LatchMode::TWO03);

// GLOBAL VARIABLES
// ============================================
Pattern patterns[MAX_PATTERNS];
DrumKit kits[MAX_KITS];
DiagnosticInfo diagnostic;

// M5 8ENCODER Variables
int32_t encoderValues[8] = {0};        // Valores actuales de cada encoder
int32_t lastEncoderValues[8] = {0};    // Últimos valores leídos
uint8_t encoderLEDColors[8][3];        // RGB para cada LED (canal 0-7)
bool m5encoderConnected = false;       // Estado de conexión del M5 8ENCODER
unsigned long lastEncoderRead = 0;     // Control de lectura
const unsigned long ENCODER_READ_INTERVAL = 50; // 50ms = 20Hz

// Parámetros controlables por encoder (por cada track)
enum EncoderMode {
    ENC_MODE_VOLUME,      // Volumen individual del track
    ENC_MODE_PITCH,       // Pitch/velocidad
    ENC_MODE_PAN,         // Paneo (futuro)
    ENC_MODE_EFFECT       // Efecto (futuro)
};
EncoderMode encoderMode = ENC_MODE_VOLUME;  // Modo actual
int trackVolumes[8] = {100, 100, 100, 100, 100, 100, 100, 100};  // Volumen individual por track (0-150)

Screen currentScreen = SCREEN_BOOT;

int currentPattern = 0;
int currentKit = 0;
int currentStep = 0;
// sequencerPage REMOVIDO - solo 1 página con 8 tracks
int tempo = DEFAULT_BPM;
int volume = DEFAULT_VOLUME;
unsigned long lastStepTime = 0;
unsigned long stepInterval = 0;
bool isPlaying = false;

// Servidor Web WiFi
// Configuración WiFi - SLAVE se conecta al MASTER
const char* ssid = "RED808";          // WiFi del MASTER
const char* password = "red808esp32"; // Password del MASTER
const char* masterIP = "192.168.4.1"; // IP del MASTER
const int udpPort = 8888;              // Puerto UDP del MASTER

// CONFIGURACIÓN DE IP ESTÁTICA PARA SLAVES (opcional pero recomendado)
// Para evitar conflictos de IP entre múltiples slaves y clientes web:
// - Web browsers: obtendrán IPs dinámicas desde .2 en adelante
// - Slave 1 (Surface): configurar IP estática .50 (descomentar líneas abajo)
// - Slave 2 (otro dispositivo): configurar IP estática .51
// Descomenta y configura estas líneas en cada slave:
/*
IPAddress local_IP(192, 168, 4, 50);  // Cambiar último número para cada slave
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
// Luego en setupWiFiAndUDP() agregar antes de WiFi.begin():
// WiFi.config(local_IP, gateway, subnet);
*/

WiFiUDP udp;
// AsyncWebServer server(80);  // DESHABILITADO - No usado en SLAVE
bool webServerEnabled = false;  // Siempre false en SLAVE

int selectedTrack = 0;
int selectedStep = 0;
int menuSelection = 0;

bool needsFullRedraw = true;
bool needsHeaderUpdate = false;
bool needsGridUpdate = false;
int lastDisplayedStep = -1;
int lastToggledTrack = -1; // Track que cambió para actualizar solo su fila

int lastEncoderPos = 0;

// Rotary Encoder variables (sin biblioteca)
volatile int encoderPos = 0;
volatile bool encoderChanged = false;
static uint8_t encoderState = 0;
static const int8_t encoderStates[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

// Volume control - DUAL MODE (Sequencer / Live Pads)
enum VolumeMode {
    VOL_SEQUENCER,
    VOL_LIVE_PADS
};
VolumeMode volumeMode = VOL_SEQUENCER;
int sequencerVolume = DEFAULT_VOLUME;  // 75%
int livePadsVolume = 100;  // 100% - más fuerte para tocar en vivo
int lastSequencerVolume = DEFAULT_VOLUME;
int lastLivePadsVolume = 100;
unsigned long lastVolumeRead = 0;

unsigned long ledOffTime[8] = {0};  // 8 LEDs (simplificado)
bool ledActive[8] = {false};         // 8 LEDs (simplificado)

int audioLevels[8] = {0};
bool padPressed[8] = {false};  // 8 pads presionados (simplificado)
unsigned long padPressTime[8] = {0};  // Tiempo de presión para tremolo (8 pads)
unsigned long lastVizUpdate = 0;

DisplayMode currentDisplayMode = DISPLAY_BPM;
unsigned long lastDisplayChange = 0;
int lastInstrumentPlayed = -1;
unsigned long instrumentDisplayTime = 0;

// Debug analog buttons
unsigned long lastButtonDebug = 0;
int lastAdcValue = -1;

// UDP connection state
bool udpConnected = false;
unsigned long lastUdpCheck = 0;
const unsigned long UDP_CHECK_INTERVAL = 30000;  // 30 segundos entre intentos de reconexión

// Variables para manejo de botones (8 botones)
uint16_t lastButtonState = 0;
unsigned long buttonPressTime[8] = {0};
unsigned long lastRepeatTime[8] = {0};

// ============================================
// DATA
// ============================================
const char* kitNames[MAX_KITS] = {
    "808 CLASSIC",
    "808 BRIGHT",
    "808 DRY"
};

const char* trackNames[MAX_TRACKS] = {
    "BD", "SD", "CH", "OH",
    "CL", "T1", "T2", "CY"
};

const char* instrumentNames[MAX_TRACKS] = {
    "KICK    ",
    "SNARE   ",
    "CL-HAT  ",
    "OP-HAT  ",
    "CLAP    ",
    "TOM-LO  ",
    "TOM-HI  ",
    "CYMBAL  "
};

const char* menuItems[] = {
    "LIVE PAD",
    "SEQUENCER",
    "SETTINGS",
    "DIAGNOSTICS"
};
const int menuItemCount = 4;

// Colores por instrumento (BD=KICK, SD=SNARE, CH=CL-HAT, OH=OP-HAT, CL=CLAP, T1=TOM-LO, T2=TOM-HI, CY=CYMBAL)
const uint16_t instrumentColors[MAX_TRACKS] = {
    COLOR_INST_KICK,   // BD (Bass Drum = KICK)
    COLOR_INST_SNARE,  // SD (Snare Drum = SNARE)
    COLOR_INST_CLHAT,  // CH (Closed Hi-Hat = CL-HAT)
    COLOR_INST_OPHAT,  // OH (Open Hi-Hat = OP-HAT)
    COLOR_INST_CLAP,   // CL (Clap = CLAP)
    COLOR_INST_TOMLO,  // T1 (Tom Low = TOM-LO)
    COLOR_INST_TOMHI,  // T2 (Tom High = TOM-HI)
    COLOR_INST_CYMBAL  // CY (Cymbal = CYMBAL)
};

// ============================================
// FUNCTION PROTOTYPES
// ============================================
void setupKits();
void setupWiFiAndUDP();
void calculateStepInterval();
void updateSequencer();
void handleButtons();
void handleEncoder();
void handleBackButton();
void handlePlayStopButton();
void handleMuteButton();
void handleVolume();
void handleM5Encoders();
void updateEncoderLEDs();
void debugAnalogButtons();
void triggerDrum(int track);
void testButtonsOnBoot();
void sendUDPCommand(const char* cmd);
void sendUDPCommand(JsonDocument& doc);
void receiveUDPData();
void requestPatternFromMaster();
void drawBootScreen();
void drawConsoleBootScreen();
void drawSpectrumAnimation();
void drawMainMenu();
void drawMenuItems(int oldSelection, int newSelection);
void drawLiveScreen();
void drawSequencerScreen();
void drawSettingsScreen();
void drawDiagnosticsScreen();
void drawPatternsScreen();
void drawSinglePattern(int patternIndex, bool isSelected);
void drawSyncingScreen();
void drawHeader();
void drawLivePad(int padIndex, bool highlight);
void updateTM1638Displays();
void updateStepLEDs();
void updateStepLEDsForTrack(int track);
void updateLEDFeedback();
void updateAudioVisualization();
void changeTempo(int delta);
void changePattern(int delta);
void changeKit(int delta);
void changeTheme(int delta);
void toggleStep(int track, int step);
void changeScreen(Screen newScreen);
void showInstrumentOnTM1638(int track);
void showBPMOnTM1638();
void showVolumeOnTM1638();
void setLED(int ledIndex, bool state);
void setAllLEDs(uint16_t pattern);
uint16_t readAllButtons();

// ============================================
// TM1638 HELPERS
// ============================================
void setLED(int ledIndex, bool state) {
    if (ledIndex < 8) {
        tm1.setLED(ledIndex, state ? 1 : 0);
    } else {
        tm2.setLED(ledIndex - 8, state ? 1 : 0);
    }
}

void setAllLEDs(uint16_t pattern) {
    uint8_t low = pattern & 0xFF;
    uint8_t high = (pattern >> 8) & 0xFF;
    tm1.setLEDs(low);
    tm2.setLEDs(high);
}

uint16_t readAllButtons() {
    uint8_t btn1 = tm1.readButtons();
    uint8_t btn2 = tm2.readButtons();
    return btn1 | (btn2 << 8);
}

uint16_t getInstrumentColor(int track) {
    if (track >= 0 && track < MAX_TRACKS) {
        return instrumentColors[track];
    }
    return COLOR_ACCENT;
}

// ============================================
// SETUP FUNCTIONS
// ============================================
void setupKits() {
    for (int i = 0; i < MAX_KITS; i++) {
        kits[i].name = String(kitNames[i]);
        kits[i].folder = i + 1;
    }
}

void setupPatterns() {
    // Los patrones se leerán del MASTER vía UDP
    // Solo inicializar la estructura vacía
    for (int p = 0; p < MAX_PATTERNS; p++) {
        patterns[p].name = "PTN-" + String(p + 1);
        for (int t = 0; t < MAX_TRACKS; t++) {
            patterns[p].muted[t] = false;
            for (int s = 0; s < MAX_STEPS; s++) {
                patterns[p].steps[t][s] = false;
            }
        }
    }
    Serial.println("► Patterns initialized (will sync from MASTER)");
    Serial.printf("   Pattern size: %d tracks x %d steps\n", MAX_TRACKS, MAX_STEPS);
}

// Debug: Imprimir patrón recibido
void printReceivedPattern(int patternNum) {
    Serial.printf("\n=== PATTERN %d RECEIVED ===", patternNum + 1);
    Serial.printf(" (%dx%d) ===\n", MAX_TRACKS, MAX_STEPS);
    Pattern& p = patterns[patternNum];
    
    // Header con números de steps
    Serial.print("   ");
    for (int s = 0; s < MAX_STEPS; s++) {
        Serial.printf("%2d ", s + 1);
    }
    Serial.println();
    
    // Cada instrumento
    for (int t = 0; t < MAX_TRACKS; t++) {
        Serial.printf("T%02d ", t + 1);
        for (int s = 0; s < MAX_STEPS; s++) {
            Serial.print(p.steps[t][s] ? "■  " : "·  ");
        }
        Serial.printf(" %s\n", p.muted[t] ? "[MUTED]" : "");
    }
    Serial.println("================================\n");
}

// ============================================
// UDP COMMUNICATION FUNCTIONS
// ============================================
void setupWiFiAndUDP() {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║   CONNECTING TO MASTER (WiFi UDP)  ║");
    Serial.println("╚════════════════════════════════════╝");
    Serial.printf("  SSID: %s\n", ssid);
    Serial.printf("  Master IP: %s\n", masterIP);
    Serial.printf("  UDP Port: %d\n", udpPort);
    
    // PASO 1: Escanear redes WiFi disponibles
    Serial.println("\n[1/3] Scanning WiFi networks...");
    int n = WiFi.scanNetworks();
    Serial.printf("  Found %d networks:\n", n);
    
    bool masterFound = false;
    for (int i = 0; i < n; i++) {
        String foundSSID = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);
        Serial.printf("    %d: %s (RSSI: %d)%s\n", 
                     i + 1, foundSSID.c_str(), rssi,
                     (foundSSID == ssid) ? " ← MASTER!" : "");
        if (foundSSID == ssid) {
            masterFound = true;
            Serial.printf("  ✓ MASTER '%s' is visible! Signal: %d dBm\n", ssid, rssi);
        }
    }
    
    if (!masterFound) {
        Serial.printf("  ✗ WARNING: MASTER '%s' NOT FOUND in scan!\n", ssid);
        Serial.println("    Check if MASTER is powered on and AP is active.");
    }
    
    // PASO 2: Intentar conectar al MASTER
    Serial.println("\n[2/3] Connecting to MASTER...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    // Esperar hasta 15 segundos (30 intentos x 500ms)
    int attempts = 0;
    int maxAttempts = 30;  // 15 segundos total
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        Serial.print(".");
        if (attempts % 10 == 9) {
            Serial.printf(" %d/%ds\n", (attempts + 1) / 2, maxAttempts / 2);
        }
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ WiFi connected!");
        Serial.print("  IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.printf("  Signal strength: %d dBm\n", WiFi.RSSI());
        Serial.printf("  MAC Address: %s\n", WiFi.macAddress().c_str());
        
        // PASO 3: Inicializar UDP y enviar hello
        Serial.println("\n[3/3] Initializing UDP communication...");
        udp.begin(udpPort);
        udpConnected = true;
        diagnostic.udpConnected = true;
        
        // Enviar hello al master
        JsonDocument doc;
        doc["cmd"] = "hello";
        doc["device"] = "SURFACE";
        sendUDPCommand(doc);
        
        Serial.println("✓ UDP initialized - Ready to send commands");
        
        // Sonido de confirmación al conectar: reproducir 3 samples rápidos
        for (int i = 0; i < 3; i++) {
            // Enviar comando para reproducir CLAP (sample 4)
            JsonDocument triggerDoc;
            triggerDoc["cmd"] = "trigger";
            triggerDoc["track"] = 4;  // CLAP
            sendUDPCommand(triggerDoc);
            delay(100);
        }
        Serial.println("♪ Connection confirmed with audio feedback");
        
        // Solicitar patrón actual al MASTER automáticamente
        delay(100);  // Dar tiempo al MASTER para procesar hello
        requestPatternFromMaster();
        Serial.println("► Auto-requesting pattern from MASTER...");
    } else {
        Serial.println("\n✗ WiFi connection FAILED");
        Serial.printf("  Attempts: %d/%d (%.1f seconds)\n", attempts, maxAttempts, attempts * 0.5);
        Serial.printf("  WiFi Status: %d\n", WiFi.status());
        Serial.println("\n  TROUBLESHOOTING:");
        if (!masterFound) {
            Serial.println("    → MASTER not detected in WiFi scan");
            Serial.println("    → Verify MASTER device is powered ON");
            Serial.println("    → Check MASTER has WiFi AP active");
        } else {
            Serial.println("    → MASTER detected but connection failed");
            Serial.println("    → Check WiFi password is correct");
            Serial.println("    → Try moving devices closer together");
            Serial.println("    → Check MASTER is not overloaded");
        }
        Serial.println("  Will auto-retry every 30 seconds...\n");
        udpConnected = false;
        diagnostic.udpConnected = false;
        diagnostic.lastError = masterFound ? "Connection timeout" : "Master not found";
        lastUdpCheck = millis();  // Iniciar timer de reintento
    }
}

void sendUDPCommand(const char* cmd) {
    if (!udpConnected) {
        Serial.println("✗ UDP not connected - command not sent");
        return;
    }
    
    udp.beginPacket(masterIP, udpPort);
    udp.write((uint8_t*)cmd, strlen(cmd));
    udp.endPacket();
    
    Serial.printf("► UDP: %s\n", cmd);
}

void sendUDPCommand(JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    sendUDPCommand(json.c_str());
}

// Recibir datos UDP del MASTER
void receiveUDPData() {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        char incomingPacket[512];
        int len = udp.read(incomingPacket, sizeof(incomingPacket) - 1);
        if (len > 0) {
            incomingPacket[len] = 0;  // Null terminator
            
            Serial.printf("\n◄ UDP RECEIVED (%d bytes): %s\n", len, incomingPacket);
            
            // Parsear JSON
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, incomingPacket);
            
            if (error) {
                Serial.printf("✗ JSON parse error: %s\n", error.c_str());
                return;
            }
            
            const char* cmd = doc["cmd"];
            if (cmd) {
                Serial.printf("► Command received: %s\n", cmd);
                
                // Sincronizar patrón
                if (strcmp(cmd, "pattern_sync") == 0) {
                    int patternNum = doc["pattern"] | currentPattern;
                    
                    Serial.println("\n═══════════════════════════════════════");
                    Serial.printf("► PATTERN SYNC RECEIVED: Pattern %d\n", patternNum + 1);
                    Serial.println("═══════════════════════════════════════");
                    
                    // Parsear array de steps (flexible: acepta de 1 a MAX_TRACKS)
                    JsonArray data = doc["data"];
                    if (data && data.size() > 0) {
                        int totalStepsActive = 0;
                        int tracksReceived = min((int)data.size(), MAX_TRACKS);
                        
                        Serial.printf("► Tracks received: %d/%d\n", tracksReceived, MAX_TRACKS);
                        
                        // Primero limpiar el patrón completo
                        for (int t = 0; t < MAX_TRACKS; t++) {
                            for (int s = 0; s < MAX_STEPS; s++) {
                                patterns[patternNum].steps[t][s] = false;
                            }
                        }
                        
                        // Cargar tracks recibidos
                        for (int t = 0; t < tracksReceived; t++) {
                            JsonArray trackData = data[t];
                            if (trackData) {
                                int stepsInTrack = min((int)trackData.size(), MAX_STEPS);
                                Serial.printf("   Track %02d: %d steps\n", t + 1, stepsInTrack);
                                
                                for (int s = 0; s < stepsInTrack; s++) {
                                    patterns[patternNum].steps[t][s] = trackData[s];
                                    if (trackData[s]) totalStepsActive++;
                                }
                            }
                        }
                        
                        Serial.printf("► Total active steps: %d\n", totalStepsActive);
                        
                        // Debug: imprimir patrón recibido
                        printReceivedPattern(patternNum);
                        
                        // Forzar redibujado SIEMPRE que se reciba un patrón
                        if (patternNum == currentPattern) {
                            needsFullRedraw = true;
                            needsGridUpdate = true;
                            updateStepLEDsForTrack(selectedTrack);
                            Serial.println("► Screen will be redrawn on next loop");
                        }
                        
                        // Mostrar notificación en TFT
                        if (currentScreen == SCREEN_SEQUENCER) {
                            tft.fillRect(0, 290, 320, 30, COLOR_BG);
                            tft.setTextColor(COLOR_SUCCESS, COLOR_BG);
                            tft.setTextSize(1);
                            tft.setCursor(10, 295);
                            tft.printf("SYNCED Pattern %d (%d tracks, %d steps)", 
                                      patternNum + 1, tracksReceived, totalStepsActive);
                        }
                        
                        Serial.println("✓ Pattern synchronized successfully!");
                        Serial.println("═══════════════════════════════════════\n");
                    } else {
                        Serial.println("✗ No pattern data received");
                    }
                }
                
                // Actualización del step actual (para sincronizar visualización)
                else if (strcmp(cmd, "step_update") == 0) {
                    int newStep = doc["step"] | 0;
                    if (newStep != currentStep) {
                        currentStep = newStep;
                        // Actualizar visualización si estamos en pantalla sequencer
                        if (currentScreen == SCREEN_SEQUENCER) {
                            needsGridUpdate = true;
                        }
                        Serial.printf("► Step updated: %d\n", currentStep + 1);
                    }
                }
                
                // Estado play/stop del MASTER
                else if (strcmp(cmd, "play_state") == 0) {
                    bool masterPlaying = doc["playing"] | false;
                    if (masterPlaying != isPlaying) {
                        isPlaying = masterPlaying;
                        
                        // Reset step timer cuando inicia play
                        if (isPlaying) {
                            lastStepTime = millis();
                            currentStep = 0;  // Reiniciar desde step 0
                        }
                        
                        if (currentScreen == SCREEN_SEQUENCER) {
                            needsHeaderUpdate = true;
                            needsGridUpdate = true;
                        }
                        Serial.printf("► Play state: %s\n", isPlaying ? "PLAYING" : "STOPPED");
                    }
                }
                
                // Sincronizar BPM
                else if (strcmp(cmd, "tempo_sync") == 0) {
                    int newTempo = doc["value"];
                    if (newTempo >= MIN_BPM && newTempo <= MAX_BPM) {
                        tempo = newTempo;
                        calculateStepInterval();
                        Serial.printf("✓ Tempo synced: %d BPM\n", tempo);
                        if (currentScreen == SCREEN_SEQUENCER) {
                            needsHeaderUpdate = true;
                        }
                    }
                }
                // Sincronizar step actual
                else if (strcmp(cmd, "step_sync") == 0) {
                    int newStep = doc["step"];
                    if (newStep >= 0 && newStep < MAX_STEPS) {
                        currentStep = newStep;
                        if (currentScreen == SCREEN_SEQUENCER) {
                            needsGridUpdate = true;
                        }
                    }
                }
                // Sincronizar volumen del sequencer
                else if (strcmp(cmd, "volume_seq_sync") == 0) {
                    int newVol = doc["value"];
                    if (newVol >= 0 && newVol <= MAX_VOLUME) {
                        sequencerVolume = newVol;
                        lastSequencerVolume = newVol;
                        Serial.printf("✓ Sequencer volume synced: %d%%\n", sequencerVolume);
                        needsHeaderUpdate = true;
                    }
                }
                // Sincronizar volumen de live pads
                else if (strcmp(cmd, "volume_live_sync") == 0) {
                    int newVol = doc["value"];
                    if (newVol >= 0 && newVol <= MAX_VOLUME) {
                        livePadsVolume = newVol;
                        lastLivePadsVolume = newVol;
                        Serial.printf("✓ Live pads volume synced: %d%%\n", livePadsVolume);
                        needsHeaderUpdate = true;
                    }
                }
            }
        }
    }
}

// Solicitar patrón al MASTER
void requestPatternFromMaster() {
    if (!udpConnected) {
        Serial.println("✗ Cannot request pattern: UDP not connected");
        if (currentScreen == SCREEN_SEQUENCER) {
            tft.fillRect(0, 290, 320, 30, COLOR_BG);
            tft.setTextColor(COLOR_ERROR, COLOR_BG);
            tft.setTextSize(1);
            tft.setCursor(10, 295);
            tft.print("ERROR: Not connected to MASTER");
        }
        return;
    }
    
    JsonDocument doc;
    doc["cmd"] = "get_pattern";
    doc["pattern"] = currentPattern;
    sendUDPCommand(doc);
    
    Serial.println("\n───────────────────────────────────────");
    Serial.printf("► REQUESTING Pattern %d from MASTER\n", currentPattern + 1);
    Serial.printf("   Master IP: %s:%d\n", masterIP, udpPort);
    Serial.println("   Waiting for response...");
    Serial.println("───────────────────────────────────────\n");
    
    // Mostrar en pantalla que se está sincronizando
    if (currentScreen == SCREEN_SEQUENCER) {
        tft.fillRect(0, 290, 320, 30, COLOR_BG);
        tft.setTextColor(COLOR_WARNING, COLOR_BG);
        tft.setTextSize(1);
        tft.setCursor(10, 295);
        tft.printf("Requesting Pattern %d...", currentPattern + 1);
    }
}

void calculateStepInterval() {
    stepInterval = (60000 / tempo) / 4;
}

// ============================================
// WEB SERVER - DESHABILITADO EN SLAVE
// ============================================
// El SLAVE no necesita servidor web, solo envía comandos UDP al MASTER
// Funciones deshabilitadas para mejorar performance y reducir uso de RAM

/*
void enableWebServer() {
    // DESHABILITADO - No se usa en SLAVE
}

void disableWebServer() {
    // DESHABILITADO - No se usa en SLAVE  
}

void toggleWebServer() {
    // DESHABILITADO - No se usa en SLAVE
}
*/

/*
void setupSDCard() {
    tft.fillScreen(COLOR_BG);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(120, 130);
    tft.println("Checking SD Card...");
    
    Serial.println("\n╔════════════════════════════════════════════╗");
    Serial.println("║  SD CARD INITIALIZATION                    ║");
    Serial.println("╚════════════════════════════════════════════╝");
    Serial.println("Pin Configuration:");
    Serial.printf("  CS   = Pin %d\n", SD_CS);
    Serial.printf("  MOSI = Pin %d\n", SD_MOSI);
    Serial.printf("  MISO = Pin %d\n", SD_MISO);
    Serial.printf("  SCK  = Pin %d\n", SD_SCK);
    
    // Asegurar que TFT_CS está en HIGH para no interferir
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    
    // Configurar CS como OUTPUT y ponerlo en HIGH antes de inicializar
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    delay(100);
    
    // Test de conectividad MISO
    Serial.println("\nTesting MISO line...");
    pinMode(SD_MISO, INPUT_PULLUP);
    int misoState = digitalRead(SD_MISO);
    Serial.printf("  MISO state: %s\n", misoState ? "HIGH (OK)" : "LOW (check connection)");
    
    // Test de conectividad CS
    Serial.println("\nTesting CS line...");
    digitalWrite(SD_CS, LOW);
    delay(10);
    digitalWrite(SD_CS, HIGH);
    Serial.println("  CS toggle: OK");
    
    // Enviar 80 pulsos de reloj con CS alto para resetear la SD (protocolo SPI SD)
    Serial.println("\nSending SD reset sequence...");
    digitalWrite(SD_CS, HIGH);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 10; i++) {
        SPI.transfer(0xFF);  // 10 bytes = 80 bits de reloj
    }
    SPI.endTransaction();
    delay(100);
    Serial.println("  Reset sequence sent");
    
    // Intentar varias configuraciones comunes
    bool sdOk = false;
    
    // Intento 1: Frecuencia muy baja (250kHz - modo ultra seguro)
    Serial.println("\n[1] Trying 250kHz (ultra safe mode)...");
    digitalWrite(TFT_CS, HIGH);  // TFT desactivado
    
    if (SD.begin(SD_CS, SPI, 250000)) {
        sdOk = true;
        Serial.println("  ✓ SUCCESS with 250kHz");
    } else {
        Serial.println("  ✗ Failed");
    }
    
    // Intento 2: 1MHz
    if (!sdOk) {
        Serial.println("[2] Trying 1MHz...");
        SD.end();
        delay(100);
        if (SD.begin(SD_CS, SPI, 1000000)) {
            sdOk = true;
            Serial.println("  ✓ SUCCESS with 1MHz");
        } else {
            Serial.println("  ✗ Failed");
        }
    }
    
    // Intento 3: 4MHz
    if (!sdOk) {
        Serial.println("[3] Trying 4MHz...");
        SD.end();
        delay(100);
        if (SD.begin(SD_CS, SPI, 4000000)) {
            sdOk = true;
            Serial.println("  ✓ SUCCESS with 4MHz");
        } else {
            Serial.println("  ✗ Failed");
        }
    }
    
    // Intento 4: Sin especificar frecuencia
    if (!sdOk) {
        Serial.println("[4] Trying default frequency...");
        SD.end();
        delay(100);
        if (SD.begin(SD_CS)) {
            sdOk = true;
            Serial.println("  ✓ SUCCESS with default");
        } else {
            Serial.println("  ✗ Failed");
        }
    }
    
    // Intento 5: Reiniciar SPI manualmente
    if (!sdOk) {
        Serial.println("[5] Trying manual SPI restart...");
        SPI.end();
        delay(100);
        SPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
        delay(100);
        if (SD.begin(SD_CS, SPI, 400000)) {
            sdOk = true;
            Serial.println("  ✓ SUCCESS with manual SPI");
        } else {
            Serial.println("  ✗ Failed");
        }
    }
    
    if (sdOk) {
        diagnostic.sdCardOk = true;
        
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("  ✓ SD Card: %lluMB\n", cardSize);
        
        int totalFiles = 0;
        for (int kit = 1; kit <= 3; kit++) {
            char path[20];
            snprintf(path, sizeof(path), "/%02d", kit);
            File dir = SD.open(path);
            if (dir && dir.isDirectory()) {
                File file = dir.openNextFile();
                while (file) {
                    if (!file.isDirectory()) {
                        totalFiles++;
                    }
                    file = dir.openNextFile();
                }
                dir.close();
            }
        }
        diagnostic.filesFound = totalFiles;
        Serial.printf("  ✓ Sound files found: %d\n", totalFiles);
        
        tft.setTextColor(COLOR_SUCCESS);
        tft.setCursor(120, 160);
        tft.printf("SD: %lluMB - %d files", cardSize, totalFiles);
        
    } else {
        diagnostic.sdCardOk = false;
        diagnostic.lastError = "SD Card failed";
        Serial.println("\n✗✗✗ ALL METHODS FAILED ✗✗✗");
        Serial.println("\nPossible issues:");
        Serial.println("  1. SD card not inserted or not making contact");
        Serial.println("  2. SD card corrupted or wrong format (use FAT32)");
        Serial.println("  3. Wiring issue - check all 6 connections");
        Serial.println("  4. Power issue - SD needs stable 3.3V");
        Serial.println("  5. Wrong SD module type");
        Serial.println("\nContinuing without SD card...");
        
        tft.setTextColor(COLOR_WARNING);
        tft.setCursor(120, 160);
        tft.println("SD Card not found");
    }
    
    delay(1000);
}
/*
void setupSDCard() {
    tft.fillScreen(COLOR_BG);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(120, 130);
    tft.println("Checking SD Card...");
    
    Serial.println("\n╔════════════════════════════════════════════╗");
    Serial.println("║  SD CARD INITIALIZATION                    ║");
    Serial.println("╚════════════════════════════════════════════╝");
    Serial.println("Pin Configuration:");
    Serial.printf("  CS   = Pin %d\n", SD_CS);
    Serial.printf("  MOSI = Pin %d\n", SD_MOSI);
    Serial.printf("  MISO = Pin %d\n", SD_MISO);
    Serial.printf("  SCK  = Pin %d\n", SD_SCK);
    
    // CRÍTICO: Desactivar TODOS los dispositivos SPI antes de inicializar SD
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    
    delay(100);
    
    // Secuencia de reset de la SD card (muy importante)
    Serial.println("\nSending SD reset sequence...");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1); // Inicializar SPI explícitamente
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    digitalWrite(SD_CS, HIGH);
    for (int i = 0; i < 10; i++) {
        SPI.transfer(0xFF);
    }
    SPI.endTransaction();
    delay(100);
    
    // MÉTODO CORRECTO: Pasar SPI como segundo parámetro
    bool sdOk = false;
    
    // Intento 1: Con SPI explícito y frecuencia muy baja (400kHz)
    Serial.println("\n[1] Trying 400kHz with explicit SPI...");
    digitalWrite(TFT_CS, HIGH); // TFT desactivado
    digitalWrite(SD_CS, HIGH);  // SD desactivado
    delay(10);
    
    // IMPORTANTE: SD.begin(CS_PIN, SPI_OBJECT, FREQUENCY, MOUNT_POINT, MAX_FILES)
    if (SD.begin(SD_CS, SPI, 400000, "/sd", 5)) {
        sdOk = true;
        Serial.println("  ✓ SUCCESS with 400kHz");
    }
    
    if (!sdOk) {
        Serial.println("[2] Trying 250kHz...");
        SD.end();
        delay(200);
        if (SD.begin(SD_CS, SPI, 250000, "/sd", 5)) {
            sdOk = true;
            Serial.println("  ✓ SUCCESS with 250kHz");
        }
    }
    
    if (!sdOk) {
        Serial.println("[3] Trying with SD_MMC mode (alternative)...");
        // Nota: Esto solo funciona si tu módulo soporta modo MMC
    }
    
    if (sdOk) {
        diagnostic.sdCardOk = true;
        
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        uint8_t cardType = SD.cardType();
        
        Serial.printf("  ✓ SD Card Type: ");
        switch(cardType) {
            case CARD_MMC: Serial.println("MMC"); break;
            case CARD_SD: Serial.println("SDSC"); break;
            case CARD_SDHC: Serial.println("SDHC"); break;
            default: Serial.println("UNKNOWN"); break;
        }
        
        Serial.printf("  ✓ SD Card Size: %lluMB\n", cardSize);
        
        // Contar archivos
        int totalFiles = 0;
        for (int kit = 1; kit <= 3; kit++) {
            char path[20];
            snprintf(path, sizeof(path), "/sd/%02d", kit); // IMPORTANTE: Agregar /sd/ al path
            File dir = SD.open(path);
            if (dir && dir.isDirectory()) {
                File file = dir.openNextFile();
                while (file) {
                    if (!file.isDirectory()) {
                        totalFiles++;
                        Serial.printf("    Found: %s\n", file.name());
                    }
                    file = dir.openNextFile();
                }
                dir.close();
            } else {
                Serial.printf("  ! Folder %s not found\n", path);
            }
        }
        
        diagnostic.filesFound = totalFiles;
        Serial.printf("  ✓ Total sound files: %d\n", totalFiles);
        
        // Mostrar en pantalla
        digitalWrite(TFT_CS, LOW); // Reactivar TFT
        delay(10);
        tft.setTextColor(COLOR_SUCCESS);
        tft.setCursor(120, 160);
        tft.printf("SD: %lluMB - %d files", cardSize, totalFiles);
        digitalWrite(TFT_CS, HIGH);
        
    } else {
        diagnostic.sdCardOk = false;
        diagnostic.lastError = "SD Card failed";
        
        Serial.println("\n✗✗✗ ALL METHODS FAILED ✗✗✗");
        Serial.println("\nDIAGNOSTIC CHECKLIST:");
        Serial.println("  [1] SD card inserted correctly?");
        Serial.println("  [2] SD formatted as FAT32?");
        Serial.println("  [3] Check wiring:");
        Serial.println("      - CS  -> Pin 15");
        Serial.println("      - SCK -> Pin 18 (shared)");
        Serial.println("      - MOSI-> Pin 23 (shared)");
        Serial.println("      - MISO-> Pin 19 (shared)");
        Serial.println("      - VCC -> 5V");
        Serial.println("      - GND -> GND");
        Serial.println("  [4] Try different SD card (some cards are incompatible)");
        Serial.println("  [5] Check SD module has voltage regulator (3.3V/5V)");
        
        digitalWrite(TFT_CS, LOW);
        delay(10);
        tft.setTextColor(COLOR_WARNING);
        tft.setCursor(120, 160);
        tft.println("SD Card ERROR!");
        digitalWrite(TFT_CS, HIGH);
    }
    
    delay(2000);
}  */
/*
void setupSDCard() {
    tft.fillScreen(COLOR_BG);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(120, 130);
    tft.println("Checking SD Card...");
    
    Serial.println("\n╔════════════════════════════════════════════╗");
    Serial.println("║  SD CARD INITIALIZATION                    ║");
    Serial.println("╚════════════════════════════════════════════╝");
    Serial.println("Pin Configuration:");
    Serial.printf("  CS   = Pin %d (GPIO 14)\n", SD_CS);
    Serial.printf("  MOSI = Pin %d (shared)\n", SD_MOSI);
    Serial.printf("  MISO = Pin %d (shared)\n", SD_MISO);
    Serial.printf("  SCK  = Pin %d (shared)\n", SD_SCK);
    
    // CRÍTICO: Todos los CS en HIGH antes de inicializar
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    
    delay(100);
    
    Serial.println("\nInitializing SD card...");
    
    // MÉTODO SIMPLE (funciona en ESP32)
    if (!SD.begin(SD_CS)) {
        Serial.println("✗ Card Mount Failed");
        diagnostic.sdCardOk = false;
        diagnostic.lastError = "SD Mount Failed";
        
        tft.setTextColor(COLOR_ERROR);
        tft.setCursor(100, 160);
        tft.println("SD MOUNT FAILED!");
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(80, 185);
        tft.println("Check: Card inserted? Formatted FAT32?");
        tft.setCursor(80, 200);
        tft.printf("CS connected to GPIO %d?", SD_CS);
        
        delay(3000);
        return;
    }
    
    uint8_t cardType = SD.cardType();
    
    if (cardType == CARD_NONE) {
        Serial.println("✗ No SD card attached");
        diagnostic.sdCardOk = false;
        diagnostic.lastError = "No SD card";
        
        tft.setTextColor(COLOR_WARNING);
        tft.setCursor(120, 160);
        tft.println("NO SD CARD!");
        delay(2000);
        return;
    }
    
    // ✓ SUCCESS!
    diagnostic.sdCardOk = true;
    
    Serial.print("✓ SD Card Type: ");
    switch(cardType) {
        case CARD_MMC: Serial.println("MMC"); break;
        case CARD_SD: Serial.println("SDSC"); break;
        case CARD_SDHC: Serial.println("SDHC"); break;
        default: Serial.println("UNKNOWN");
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("✓ SD Card Size: %lluMB\n", cardSize);
    
    // Contar archivos
    int totalFiles = 0;
    Serial.println("\nScanning folders:");
    
    for (int kit = 1; kit <= 3; kit++) {
        char path[10];
        snprintf(path, 10, "/%02d", kit);
        
        File dir = SD.open(path);
        if (!dir) {
            Serial.printf("  Creating folder %s\n", path);
            SD.mkdir(path);
            continue;
        }
        
        if (dir.isDirectory()) {
            Serial.printf("  %s/: ", path);
            int count = 0;
            File file = dir.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    count++;
                    totalFiles++;
                }
                file.close();
                file = dir.openNextFile();
            }
            Serial.printf("%d files\n", count);
        }
        dir.close();
    }
    
    diagnostic.filesFound = totalFiles;
    Serial.printf("\n✓ Total: %d sound files\n", totalFiles);
    
    // Mostrar en pantalla
    tft.setTextColor(COLOR_SUCCESS);
    tft.setCursor(110, 160);
    tft.printf("SD: %lluMB", cardSize);
    tft.setCursor(110, 185);
    tft.setTextColor(COLOR_ACCENT2);
    tft.printf("%d sound files ready", totalFiles);
    
    delay(1500);
}

void testSDWiring() {
    Serial.println("\n═══ SD WIRING TEST ═══");
    
    pinMode(SD_CS, OUTPUT);
    pinMode(SD_MISO, INPUT);
    
    // Test 1: CS toggle
    Serial.print("CS toggle test: ");
    digitalWrite(SD_CS, HIGH);
    delay(10);
    digitalWrite(SD_CS, LOW);
    delay(10);
    digitalWrite(SD_CS, HIGH);
    Serial.println("OK");
    
    // Test 2: MISO pullup
    Serial.print("MISO state (should be HIGH): ");
    pinMode(SD_MISO, INPUT_PULLUP);
    delay(10);
    int misoState = digitalRead(SD_MISO);
    Serial.println(misoState ? "OK (HIGH)" : "FAIL (LOW - check wiring!)");
    
    // Test 3: SPI communication test
    Serial.print("SPI communication test: ");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    digitalWrite(SD_CS, LOW);
    uint8_t response = SPI.transfer(0xFF);
    digitalWrite(SD_CS, HIGH);
    SPI.endTransaction();
    Serial.printf("Response: 0x%02X %s\n", response, 
                  (response != 0xFF && response != 0x00) ? "(OK)" : "(Check connection)");
}

void testButtonsOnBoot() {
    Serial.println("\nWaiting for button presses (5 seconds)...");
    Serial.println("Press S1-S8 on TM1638 #1, S9-S16 on TM1638 #2");
    Serial.println("─────────────────────────────────────");
    
    unsigned long testStart = millis();
    int buttonPressCount = 0;
    
    while (millis() - testStart < 5000) {
        uint16_t buttons = readAllButtons();
        
        if (buttons != 0) {
            Serial.printf("► BUTTONS: 0x%04X (", buttons);
            for (int i = 15; i >= 0; i--) {
                Serial.print((buttons >> i) & 1);
                if (i == 8) Serial.print(" ");
            }
            Serial.println(")");
            
            for (int i = 0; i < 8; i++) {  // Solo 8 botones
                if (buttons & (1 << i)) {
                    Serial.printf("  ✓ Button S%d pressed\n", i + 1);
                    setLED(i, true);
                    buttonPressCount++;
                }
            }
            
            delay(200);
            setAllLEDs(0x0000);
            delay(100);
        }
        
        delay(10);
    }
    
    Serial.println("─────────────────────────────────────");
    Serial.printf("Test complete! %d button presses detected\n", buttonPressCount);
    
    if (buttonPressCount == 0) {
        Serial.println("⚠ WARNING: No buttons detected!");
    }
    
    Serial.println("");
}
*/

// ============================================
// SETUP
// ============================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n╔════════════════════════════════════╗");
    Serial.println("║   RED808 V6 - SURFACE (SLAVE)     ║");
    Serial.println("║   UDP Controller via WiFi          ║");
    Serial.println("║   Connects to MASTER DrumMachine   ║");
    Serial.println("╚════════════════════════════════════╝\n");

    // TFT Init
    Serial.print("► TFT Init... ");
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    tft.init();
    tft.setRotation(3);
    tft.fillScreen(COLOR_BG);
    diagnostic.tftOk = true;
    Serial.println("OK (480x320)");
    
    // TM1638 #1
    Serial.print("► TM1638 #1 Init... ");
    tm1.displayBegin();
    tm1.brightness(7);
    tm1.reset();
    tm1.displayText("STEP1-8 ");
    diagnostic.tm1638_1_Ok = true;
    Serial.println("OK");
    delay(200);
    
    // TM1638 #2
    Serial.print("► TM1638 #2 Init... ");
    tm2.displayBegin();
    tm2.brightness(7);
    tm2.reset();
    tm2.displayText("STE9-16 ");
    diagnostic.tm1638_2_Ok = true;
    Serial.println("OK");
    delay(200);
    
    // Encoder
    Serial.print("► Rotary Encoder Init... ");
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);
    
    // 3 Button Panel - Analog Buttons
    pinMode(ANALOG_BUTTONS_PIN, INPUT);
    Serial.println("3 Analog Buttons ready on pin 34 (PLAY/STOP, MUTE, BACK)");
    
    // Volume Toggle Button (pin 14)
    pinMode(VOLUME_TOGGLE_BTN, INPUT_PULLUP);
    Serial.println("► Volume Toggle Button ready on pin 14 (SEQ/PADS)");
    
    // Configurar interrupciones para CLK y DT
    attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), []() {
        uint8_t clk = digitalRead(ENCODER_CLK);
        uint8_t dt = digitalRead(ENCODER_DT);
        encoderState = (encoderState << 2) | (clk << 1) | dt;
        encoderState &= 0x0F;
        int8_t change = encoderStates[encoderState];
        if (change != 0) {
            encoderPos += change;
            encoderChanged = true;
        }
    }, CHANGE);
    
    attachInterrupt(digitalPinToInterrupt(ENCODER_DT), []() {
        uint8_t clk = digitalRead(ENCODER_CLK);
        uint8_t dt = digitalRead(ENCODER_DT);
        encoderState = (encoderState << 2) | (clk << 1) | dt;
        encoderState &= 0x0F;
        int8_t change = encoderStates[encoderState];
        if (change != 0) {
            encoderPos += change;
            encoderChanged = true;
        }
    }, CHANGE);
    
    diagnostic.encoderOk = true;
    Serial.println("OK");
    
    // ADC
    Serial.print("► ADC Init... ");
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    Serial.println("OK");
    
    // I2C Init para M5 8ENCODER
    Serial.print("► I2C Init (SDA:21, SCL:22)... ");
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("OK");
    
    // M5 8ENCODER Init
    Serial.print("► M5 8ENCODER Init... ");
    if (m5encoder.begin()) {
        m5encoderConnected = true;
        Serial.println("OK");
        Serial.printf("   Firmware version: %d\n", m5encoder.getVersion());
        
        // ===== ANIMACIÓN DE BOOT CON LEDS =====
        Serial.println("   Running LED boot sequence...");
        
        // 1. Apagar todos los LEDs
        m5encoder.allOff();
        delay(200);
        
        // 2. Todos en ROJO brillante
        for (int i = 0; i < 9; i++) {
            m5encoder.writeRGB(i, 255, 0, 0);  // Rojo brillante
        }
        delay(500);
        
        // 3. Secuencia de onda (chase) en rojo
        for (int loop = 0; loop < 2; loop++) {
            for (int i = 0; i < 8; i++) {
                // Apagar todos
                m5encoder.allOff();
                // Encender actual en rojo brillante
                m5encoder.writeRGB(i, 255, 0, 0);
                delay(80);
            }
        }
        
        // 4. Efecto de respiración (fade in/out) todos en rojo
        for (int brightness = 0; brightness <= 255; brightness += 15) {
            for (int i = 0; i < 9; i++) {
                m5encoder.writeRGB(i, brightness, 0, 0);
            }
            delay(30);
        }
        for (int brightness = 255; brightness >= 0; brightness -= 15) {
            for (int i = 0; i < 9; i++) {
                m5encoder.writeRGB(i, brightness, 0, 0);
            }
            delay(30);
        }
        
        // 5. Encender todos en rojo nuevamente
        for (int i = 0; i < 9; i++) {
            m5encoder.writeRGB(i, 255, 0, 0);
        }
        delay(300);
        
        // 6. Transición a colores de instrumentos uno por uno
        for (int i = 0; i < 8; i++) {
            uint16_t color = instrumentColors[i];
            // Convertir color RGB565 a RGB888
            uint8_t r = ((color >> 11) & 0x1F) << 3;
            uint8_t g = ((color >> 5) & 0x3F) << 2;
            uint8_t b = (color & 0x1F) << 3;
            m5encoder.writeRGB(i, r, g, b);
            encoderLEDColors[i][0] = r;
            encoderLEDColors[i][1] = g;
            encoderLEDColors[i][2] = b;
            // Inicializar valores de encoder
            m5encoder.setAbsCounter(i, 100);  // Empezar en volumen 100
            encoderValues[i] = 100;
            lastEncoderValues[i] = 100;
            delay(100);
        }
        
        // LED 9 (extra) apagado
        m5encoder.writeRGB(8, 0, 0, 0);
        diagnostic.m5encoderOk = true;
        Serial.println("   LED boot sequence complete!");
    } else {
        m5encoderConnected = false;
        diagnostic.m5encoderOk = false;
        Serial.println("NOT FOUND (optional)");
    }
    
    // Boot estilo consola UNIX
    drawConsoleBootScreen();
    
    // Conectar al MASTER vía WiFi UDP
    setupWiFiAndUDP();
    
    // Mostrar estado de conexión en pantalla
    tft.fillRect(0, 270, 480, 50, 0x0000);
    tft.setTextSize(2);
    if (udpConnected) {
        tft.setTextColor(THEME_RED808.success);
        tft.setCursor(100, 280);
        tft.println("CONNECTED TO MASTER");
        tft.setTextSize(1);
        tft.setTextColor(THEME_RED808.accent2);
        tft.setCursor(150, 300);
        tft.printf("IP: %s", WiFi.localIP().toString().c_str());
    } else {
        tft.setTextColor(THEME_RED808.error);
        tft.setCursor(120, 280);
        tft.println("NO MASTER CONNECTION");
        tft.setTextSize(1);
        tft.setTextColor(THEME_RED808.warning);
        tft.setCursor(140, 300);
        tft.println("Will retry in background...");
    }
    delay(1500);
    
    setupKits();
    setupPatterns();
    calculateStepInterval();
    
    // LED test rápido (8 LEDs)
    for (int i = 0; i < 8; i++) {
        setLED(i, true);
        delay(30);
    }
    delay(100);
    setAllLEDs(0x0000);
    
    // Spectrum Analyzer Animation
    drawSpectrumAnimation();
    
    if (udpConnected) {
        tm1.displayText("SURFACE ");
        tm2.displayText("READY   ");
    } else {
        tm1.displayText("NO CONN ");
        tm2.displayText("MASTER  ");
    }
    
    delay(500);
    
    currentScreen = SCREEN_MENU;
    needsFullRedraw = true;
    
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║   RED808 SURFACE READY!            ║");
    Serial.println("║   Connected to MASTER via UDP      ║");
    Serial.println("╚════════════════════════════════════╝\n");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
    static unsigned long lastLoopTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastLoopTime < 16) {
        return;
    }
    lastLoopTime = currentTime;
    
    handleButtons();
    handleEncoder();
    
    // M5 8ENCODER: Leer encoders rotativos para control de tracks
    handleM5Encoders();
    
    // DEBUG: Mostrar valores ADC de botones analógicos
    debugAnalogButtons();
    
    handlePlayStopButton();
    handleMuteButton();
    handleBackButton();
    
    if (currentTime - lastVolumeRead > 100) {
        handleVolume();
        lastVolumeRead = currentTime;
    }
    
    if (isPlaying) {
        updateSequencer();
    }
    
    // Recibir datos UDP del MASTER
    receiveUDPData();
    
    // Reconectar WiFi si se pierde la conexión
    if (!udpConnected && (currentTime - lastUdpCheck > UDP_CHECK_INTERVAL)) {
        lastUdpCheck = currentTime;
        Serial.println("\n═══ AUTO-RETRY WiFi CONNECTION ═══");
        
        // Mostrar pantalla de sincronización
        drawSyncingScreen();
        
        // Intentar reconectar
        setupWiFiAndUDP();
        
        // Restaurar pantalla anterior
        needsFullRedraw = true;
    }
    
    updateAudioVisualization();
    
    if (needsFullRedraw) {
        switch (currentScreen) {
            case SCREEN_MENU:
                drawMainMenu();
                break;
            case SCREEN_LIVE:
                drawLiveScreen();
                break;
            case SCREEN_SEQUENCER:
                drawSequencerScreen();
                break;
            case SCREEN_SETTINGS:
                drawSettingsScreen();
                break;
            case SCREEN_DIAGNOSTICS:
                drawDiagnosticsScreen();
                break;
            case SCREEN_PATTERNS:
                drawPatternsScreen();
                break;
            default:
                break;
        }
        
        needsFullRedraw = false;
        needsHeaderUpdate = false;
        needsGridUpdate = false;
        
    } else {
        if (needsHeaderUpdate && currentScreen != SCREEN_MENU) {
            drawHeader();
            needsHeaderUpdate = false;
        }
        
        if (needsGridUpdate && currentScreen == SCREEN_SEQUENCER) {
            drawSequencerScreen();
            needsGridUpdate = false;
        }
    }
    
    updateTM1638Displays();
    updateLEDFeedback();
}

// ============================================
// SEQUENCER - SOLO VISUALIZACIÓN (No audio local)
// ============================================
void updateSequencer() {
    // SLAVE: Simular avance de steps basándose en tempo cuando está en play
    // (si el MASTER no envía step_update, el SLAVE lo calcula localmente)
    
    if (isPlaying) {
        unsigned long currentTime = millis();
        
        // Calcular intervalo entre steps basado en tempo actual
        unsigned long stepInterval = (60000 / tempo) / 4;  // 4 steps por beat (16th notes)
        
        if (currentTime - lastStepTime >= stepInterval) {
            lastStepTime = currentTime;
            currentStep = (currentStep + 1) % MAX_STEPS;
            
            // Forzar actualización de visualización en sequencer
            if (currentScreen == SCREEN_SEQUENCER) {
                needsGridUpdate = true;
            }
        }
    }
    
    // Actualizar LEDs basándose en currentStep
    updateStepLEDs();
}

// ============================================
// DRUM TRIGGER (UDP)
// ============================================

void triggerDrum(int track) {
    // Enviar comando UDP al MASTER para reproducir el instrumento
    // La velocity está basada en el volumen de Live Pads (con boost del 25%)
    int boostedVolume = min((int)(livePadsVolume * 1.25), MAX_VOLUME);
    int velocity = map(boostedVolume, 0, MAX_VOLUME, 0, 127);
    velocity = constrain(velocity, 0, 127);
    
    JsonDocument doc;
    doc["cmd"] = "trigger";
    doc["pad"] = track;
    doc["vel"] = velocity;
    sendUDPCommand(doc);
    
    // Actualizar visualización local
    audioLevels[track] = 100;
    showInstrumentOnTM1638(track);
}

void updateStepLEDs() {
    if (isPlaying) {
        // Durante reproducción: solo mostrar step actual
        setAllLEDs(0x0000);
        setLED(currentStep, true);
        
        Pattern& pattern = patterns[currentPattern];
        for (int track = 0; track < MAX_TRACKS; track++) {
            if (pattern.steps[track][currentStep] && !pattern.muted[track]) {
                ledActive[currentStep] = true;
                ledOffTime[currentStep] = millis() + 100;
            }
        }
    }
}

// Nueva función: Actualizar LEDs para mostrar steps del track seleccionado
void updateStepLEDsForTrack(int track) {
    Pattern& pattern = patterns[currentPattern];
    uint16_t ledPattern = 0;
    
    // Construir patrón de LEDs basado en los steps del track
    for (int i = 0; i < MAX_STEPS; i++) {
        if (pattern.steps[track][i]) {
            ledPattern |= (1 << i);
        }
    }
    
    setAllLEDs(ledPattern);
}

// ============================================
// INPUT HANDLING
// ============================================
void handleButtons() {
    uint16_t buttons = readAllButtons();
    unsigned long currentTime = millis();
    
    // Detectar nuevas presiones (flanco de subida)
    uint16_t newPress = buttons & ~lastButtonState;
    
    // Procesar nuevas presiones (solo 8 botones)
    for (int i = 0; i < 8; i++) {
        if (newPress & (1 << i)) {
            buttonPressTime[i] = currentTime;
            lastRepeatTime[i] = currentTime;
            
            if (currentScreen == SCREEN_LIVE) {
                // S1-S8: Enviar trigger al MASTER
                triggerDrum(i);
                setLED(i, true);
                ledActive[i] = true;
                ledOffTime[i] = currentTime + 150;
                padPressed[i] = true;
                padPressTime[i] = currentTime;
                
                // Redibujar pad con efecto neón
                drawLivePad(i, true);
                
                if (needsFullRedraw) {
                    drawLiveScreen();
                }
            } else if (currentScreen == SCREEN_SETTINGS) {
                if (i >= 0 && i < MAX_KITS) {
                    // S1-S3: Cambiar kit (enviar al MASTER)
                    JsonDocument doc;
                    doc["cmd"] = "kit";
                    doc["value"] = i;
                    sendUDPCommand(doc);
                    currentKit = i;
                    drawSettingsScreen();
                } else if (i == 3) {
                    // S4: (Deshabilitado - antes era Toggle WiFi)
                    // toggleWebServer();  // DESHABILITADO
                    // drawSettingsScreen();
                    Serial.println("► S4: Web server deshabilitado en SLAVE");
                } else if (i >= 4 && i < 4 + THEME_COUNT && (i - 4) < THEME_COUNT) {
                    // S5-S7: Cambiar theme (local)
                    int newTheme = i - 4;
                    if (newTheme != currentTheme) {
                        changeTheme(newTheme - currentTheme);
                        drawSettingsScreen();
                    }
                }
            } else if (currentScreen == SCREEN_SEQUENCER) {
                // Permitir edición en tiempo real incluso en play
                toggleStep(selectedTrack, i);
                updateStepLEDsForTrack(selectedTrack);
                needsGridUpdate = true;  // Solo actualizar grid, no full redraw
            } else if (currentScreen == SCREEN_PATTERNS) {
                // S1-S6: Seleccionar patrón (máximo 6 patrones)
                if (i < 6) {
                    currentPattern = i;
                    changePattern(0);  // Enviar al MASTER y solicitar sync
                    needsFullRedraw = true;
                    Serial.printf("► Pattern selected: %d\n", currentPattern + 1);
                }
            }
        }
    }
    
    lastButtonState = buttons;
}

void handleEncoder() {
    static bool encoderBtnHeld = false;
    static unsigned long encoderBtnPressTime = 0;
    static int menuAccumulator = 0;  // Acumulador para movimientos del menú
    bool btn = digitalRead(ENCODER_SW);
    unsigned long currentTime = millis();
    
    // Detectar si el botón del encoder está presionado
    // LÓGICA INVERTIDA: HIGH = presionado (tu encoder específico)
    if (btn == HIGH && !encoderBtnHeld) {
        encoderBtnHeld = true;
        encoderBtnPressTime = currentTime;
    } else if (btn == LOW) {
        encoderBtnHeld = false;
    }
    
    // isHolding = TRUE solo si está PRESIONADO más de 300ms
    bool isHolding = encoderBtnHeld && (currentTime - encoderBtnPressTime > 300);
    
    // Detectar si el botón BACK está presionado (para BACK+Encoder)
    int adcValue = analogRead(ANALOG_BUTTONS_PIN);
    bool backPressed = (adcValue >= BTN_BACK_MIN && adcValue <= BTN_BACK_MAX);
    
    // Manejar rotación del encoder
    if (encoderChanged) {
        encoderChanged = false;
        int currentPos = encoderPos;
        int rawDelta = -(currentPos - lastEncoderPos); // Invertido, sin dividir
        
        if (rawDelta != 0) {
            lastEncoderPos = currentPos;
            
            // SI ESTÁ EN HOLD: ajustar BPM en cualquier pantalla
            if (isHolding) {
                int delta = rawDelta / 2;
                if (delta != 0) {
                    tempo = constrain(tempo + delta * 5, MIN_BPM, MAX_BPM);
                    calculateStepInterval();
                    needsHeaderUpdate = true;
                    
                    // Enviar al MASTER
                    JsonDocument doc;
                    doc["cmd"] = "tempo";
                    doc["value"] = tempo;
                    sendUDPCommand(doc);
                    
                    // Mostrar en TM1638
                    char display1[9];
                    snprintf(display1, 9, "BPM %3d ", tempo);
                    tm1.displayText(display1);
                    tm2.displayText("        ");
                    
                    Serial.printf("► BPM: %d (Encoder Hold) - Sent to MASTER\n", tempo);
                }
            }
            // SIN HOLD: navegación normal según pantalla
            else {
                if (currentScreen == SCREEN_MENU) {
                    // En menú: usar acumulador para movimientos suaves (umbral de 3)
                    menuAccumulator += rawDelta;
                    if (abs(menuAccumulator) >= 3) {
                        int delta = menuAccumulator / 3;
                        menuAccumulator %= 3;  // Mantener resto
                        
                        int oldSelection = menuSelection;
                        menuSelection += delta;
                        if (menuSelection < 0) menuSelection = menuItemCount - 1;
                        if (menuSelection >= menuItemCount) menuSelection = 0;
                        
                        // Solo redibujar items que cambiaron
                        if (oldSelection != menuSelection) {
                            drawMenuItems(oldSelection, menuSelection);
                        }
                        Serial.printf("► Menu: %d\n", menuSelection);
                    }
                    
                } else if (currentScreen == SCREEN_SETTINGS) {
                    // En settings: navegar entre opciones (Drum Kits, Themes, etc.)
                    int delta = rawDelta / 2;
                    if (delta != 0) {
                        // Navegar por drum kits con el encoder
                        changeKit(delta);
                        Serial.printf("► Kit: %d\n", currentKit);
                    }
                    
                } else if (currentScreen == SCREEN_SETTINGS) {
                    // En settings: navegar entre opciones (Drum Kits, Themes, etc.)
                    int delta = rawDelta / 2;
                    if (delta != 0) {
                        // Navegar por drum kits con el encoder
                        changeKit(delta);
                        Serial.printf("► Kit: %d\n", currentKit);
                    }
                    
                } else if (currentScreen == SCREEN_PATTERNS) {
                    // En pantalla de patrones: navegar con encoder
                    int delta = rawDelta / 2;
                    if (delta != 0) {
                        int oldPattern = currentPattern;
                        currentPattern += delta;
                        
                        // Limitar a 6 patrones (0-5)
                        if (currentPattern < 0) currentPattern = 5;
                        if (currentPattern > 5) currentPattern = 0;
                        
                        if (oldPattern != currentPattern) {
                            // Redibujar solo los 2 patrones que cambiaron (sin parpadeo)
                            drawSinglePattern(oldPattern, false);  // Deseleccionar anterior
                            drawSinglePattern(currentPattern, true);  // Seleccionar nuevo
                            Serial.printf("► Pattern selected: %d\n", currentPattern + 1);
                        }
                    }
                    
                } else if (currentScreen == SCREEN_SEQUENCER) {
                    // En sequencer: navegación entre 8 tracks (sin páginas)
                    int delta = rawDelta / 2;
                    if (delta != 0) {
                        selectedTrack += delta;
                        if (selectedTrack < 0) selectedTrack = MAX_TRACKS - 1;
                        if (selectedTrack >= MAX_TRACKS) selectedTrack = 0;
                        
                        // Mostrar instrumento en TM1638
                        showInstrumentOnTM1638(selectedTrack);
                        
                        // Actualizar LEDs para mostrar steps del track seleccionado
                        if (!isPlaying) {
                            updateStepLEDsForTrack(selectedTrack);
                        }
                        
                        needsHeaderUpdate = true;
                        Serial.printf("► Track: %d (%s)\n", selectedTrack, trackNames[selectedTrack]);
                    }
                }
            }
        }
    }
    
    // Manejar botón del encoder (solo click corto, no hold)
    static bool lastBtn = LOW;
    static unsigned long lastBtnTime = 0;
    
    // Detectar click corto (liberación rápida)
    // LÓGICA INVERTIDA: LOW cuando suelta (HIGH era presionado)
    if (btn == LOW && lastBtn == HIGH) {
        unsigned long pressDuration = currentTime - encoderBtnPressTime;
        
        if (pressDuration < 300 && (currentTime - lastBtnTime > 50)) {
            lastBtnTime = currentTime;
            Serial.println("► ENCODER BTN (ENTER)");
            
            if (currentScreen == SCREEN_MENU) {
                // En menú: Enter selecciona opción
                switch (menuSelection) {
                    case 0: changeScreen(SCREEN_LIVE); break;
                    case 1: changeScreen(SCREEN_SEQUENCER); break;
                    case 2: changeScreen(SCREEN_SETTINGS); break;
                    case 3: changeScreen(SCREEN_DIAGNOSTICS); break;
                }
                
            } else if (currentScreen == SCREEN_PATTERNS) {
                // En pantalla de patrones: Enter confirma selección
                changePattern(0);  // Enviar al MASTER y solicitar sync
                Serial.printf("► Pattern confirmed: %d\n", currentPattern + 1);
                
            } else if (currentScreen == SCREEN_SEQUENCER) {
                // En sequencer: Play/Stop
                isPlaying = !isPlaying;
                if (!isPlaying) {
                    currentStep = 0;
                    setAllLEDs(0x0000);
                    updateStepLEDsForTrack(selectedTrack);
                }
                needsFullRedraw = true;
            }
        }
    }
    lastBtn = btn;
}

void handlePlayStopButton() {
    static bool lastPlayStopPressed = false;
    static unsigned long lastPlayStopBtnTime = 0;
    unsigned long currentTime = millis();
    
    int adcValue = analogRead(ANALOG_BUTTONS_PIN);
    bool playStopPressed = (adcValue >= BTN_PLAY_STOP_MIN && adcValue <= BTN_PLAY_STOP_MAX);
    
    // Detectar flanco de subida (presión)
    if (playStopPressed && !lastPlayStopPressed) {
        lastPlayStopBtnTime = currentTime;
    }
    
    // Detectar flanco de bajada (liberación) con debounce
    if (!playStopPressed && lastPlayStopPressed && (currentTime - lastPlayStopBtnTime > 50)) {
        Serial.printf("► PLAY/STOP BUTTON\n");
        
        // Enviar comando al MASTER
        JsonDocument doc;
        if (isPlaying) {
            doc["cmd"] = "stop";
        } else {
            doc["cmd"] = "start";
        }
        sendUDPCommand(doc);
        
        // Actualizar estado local
        isPlaying = !isPlaying;
        if (!isPlaying) {
            currentStep = 0;
            setAllLEDs(0x0000);
            if (currentScreen == SCREEN_SEQUENCER) {
                updateStepLEDsForTrack(selectedTrack);
            }
        }
        Serial.printf("   Sequencer: %s\n", isPlaying ? "PLAYING" : "STOPPED");
        needsFullRedraw = true;
    }
    lastPlayStopPressed = playStopPressed;
}

void handleMuteButton() {
    static bool lastMutePressed = false;
    static unsigned long muteBtnPressTime = 0;
    static bool holdProcessed = false;
    static int lastMuteAdcValue = 0;
    unsigned long currentTime = millis();
    
    int adcValue = analogRead(ANALOG_BUTTONS_PIN);
    bool mutePressed = (adcValue >= BTN_MUTE_MIN && adcValue <= BTN_MUTE_MAX);
    bool noButtonPressed = (adcValue < BTN_NONE_THRESHOLD);
    
    // Detectar presión del botón (flanco de subida)
    if (mutePressed && !lastMutePressed) {
        muteBtnPressTime = currentTime;
        holdProcessed = false;
        lastMuteAdcValue = adcValue;
        Serial.printf("► MUTE BUTTON PRESSED (ADC: %d)\n", adcValue);
    }
    
    // Detectar HOLD (mantener presionado >1 segundo) - CLEAR
    if (mutePressed && !holdProcessed && (currentTime - muteBtnPressTime > 1000)) {
        holdProcessed = true;
        Serial.printf("► MUTE BUTTON (HOLD - CLEAR INSTRUMENT) ADC: %d\n", adcValue);
        
        // Solo funciona en SEQUENCER
        if (currentScreen == SCREEN_SEQUENCER) {
            // Limpiar todos los steps del instrumento seleccionado
            Pattern& pattern = patterns[currentPattern];
            for (int s = 0; s < MAX_STEPS; s++) {
                pattern.steps[selectedTrack][s] = false;
            }
            Serial.printf("   ✓ Cleared all steps for Track %d (%s)\n", 
                         selectedTrack, trackNames[selectedTrack]);
            
            // Actualizar display
            updateStepLEDsForTrack(selectedTrack);
            needsFullRedraw = true;
            
            // Feedback visual
            tm1.displayText("CLEARED ");
            tm2.displayText(instrumentNames[selectedTrack]);
        }
    }
    
    // Detectar liberación del botón - TOGGLE MUTE (solo si fue click corto)
    if (noButtonPressed && lastMutePressed && !holdProcessed) {
        unsigned long pressDuration = currentTime - muteBtnPressTime;
        
        // Solo toggle mute si la presión fue corta (<1000ms) y mayor que debounce
        if (pressDuration > 50 && pressDuration < 1000) {
            Serial.printf("► MUTE BUTTON (CLICK - TOGGLE MUTE) Duration: %lums\n", pressDuration);
            
            // Solo funciona en SEQUENCER
            if (currentScreen == SCREEN_SEQUENCER) {
                // Toggle mute del track seleccionado
                Pattern& pattern = patterns[currentPattern];
                pattern.muted[selectedTrack] = !pattern.muted[selectedTrack];
                
                Serial.printf("   ✓ Track %d (%s): %s\n", 
                             selectedTrack, trackNames[selectedTrack],
                             pattern.muted[selectedTrack] ? "MUTED" : "UNMUTED");
                
                // Feedback visual en TM1638
                if (pattern.muted[selectedTrack]) {
                    tm1.displayText("MUTED  ");
                } else {
                    tm1.displayText("UNMUTED");
                }
                tm2.displayText(instrumentNames[selectedTrack]);
                
                // Actualizar grid para reflejar mute sin parpadeo
                if (currentScreen == SCREEN_SEQUENCER) {
                    needsGridUpdate = true;
                } else {
                    needsHeaderUpdate = true;
                }
            }
        }
    }
    
    lastMutePressed = mutePressed;
}

void handleBackButton() {
    static bool lastBackPressed = false;
    static unsigned long lastBackBtnTime = 0;
    unsigned long currentTime = millis();
    
    int adcValue = analogRead(ANALOG_BUTTONS_PIN);
    bool backPressed = (adcValue >= BTN_BACK_MIN && adcValue <= BTN_BACK_MAX);
    
    // Detectar flanco de subida (presión)
    if (backPressed && !lastBackPressed) {
        lastBackBtnTime = currentTime;
    }
    
    // Detectar flanco de bajada (liberación) solo si se presionó antes
    if (!backPressed && lastBackPressed && (currentTime - lastBackBtnTime > 50)) {
        Serial.printf("► BACK BUTTON\n");
        
        // ATAJO ESPECIAL: BACK + ENCODER PRESIONADO = SYNC PATTERN
        bool encoderPressed = (digitalRead(ENCODER_SW) == HIGH);
        if (encoderPressed && currentScreen == SCREEN_SEQUENCER) {
            Serial.println("► ENCODER+BACK = REQUESTING PATTERN SYNC!");
            requestPatternFromMaster();
            return;  // No cambiar de pantalla
        }
        
        // Desde MENU: mostrar selección de patrones
        if (currentScreen == SCREEN_MENU) {
            changeScreen(SCREEN_PATTERNS);
        }
        // Desde PATTERNS: volver a MENU
        else if (currentScreen == SCREEN_PATTERNS) {
            changeScreen(SCREEN_MENU);
        }
        // Desde cualquier otra pantalla: volver al menú
        else {
            changeScreen(SCREEN_MENU);
        }
    }
    lastBackPressed = backPressed;
}

void debugAnalogButtons() {
    unsigned long currentTime = millis();
    
    // Mostrar valor ADC cada 200ms
    if (currentTime - lastButtonDebug > 200) {
        lastButtonDebug = currentTime;
        
        int adcValue = analogRead(ANALOG_BUTTONS_PIN);
        
        // Solo mostrar si el valor cambió significativamente
        if (abs(adcValue - lastAdcValue) > 50) {
            lastAdcValue = adcValue;
            
            Serial.printf("\n═══════════════════════════════════\n");
            Serial.printf("ADC Value: %4d\n", adcValue);
            
            // Determinar qué botón está presionado
            if (adcValue < BTN_NONE_THRESHOLD || adcValue > 4000) {
                Serial.println("Status: NO BUTTON PRESSED (pull-up)");
            }
            else if (adcValue >= BTN_PLAY_STOP_MIN && adcValue <= BTN_PLAY_STOP_MAX) {
                Serial.println("Button: PLAY/STOP ✓");
            }
            else if (adcValue >= BTN_MUTE_MIN && adcValue <= BTN_MUTE_MAX) {
                Serial.println("Button: MUTE/CLEAR ✓");
            }
            else if (adcValue >= BTN_BACK_MIN && adcValue <= BTN_BACK_MAX) {
                Serial.println("Button: BACK ✓");
            }
            else {
                Serial.println("Status: ⚠️ VALUE OUT OF RANGE!");
                Serial.println("\nSuggested action:");
                
                if (adcValue < BTN_PLAY_STOP_MIN) {
                    Serial.printf("  → Lower BTN_PLAY_STOP_MIN to ~%d\n", adcValue - 50);
                }
                else if (adcValue > BTN_PLAY_STOP_MAX && adcValue < BTN_MUTE_MIN) {
                    Serial.printf("  → Adjust: BTN_PLAY_STOP_MAX=%d, BTN_MUTE_MIN=%d\n", 
                                 adcValue + 100, adcValue - 100);
                }
                else if (adcValue > BTN_MUTE_MAX && adcValue < BTN_BACK_MIN) {
                    Serial.printf("  → Adjust: BTN_MUTE_MAX=%d, BTN_BACK_MIN=%d\n", 
                                 adcValue + 100, adcValue - 100);
                }
                else if (adcValue > BTN_BACK_MAX) {
                    Serial.printf("  → Raise BTN_BACK_MAX to ~%d\n", adcValue + 100);
                }
            }
            Serial.printf("═══════════════════════════════════\n\n");
        }
    }
}

void handleVolume() {
    static bool lastToggleBtnState = HIGH;
    static int lastPotValue = -1;  // Valor del potenciómetro en el último cambio de modo
    static bool potMoved = true;   // Flag para detectar si el pot se movió después de cambiar modo
    
    // Leer botón toggle (pin 14) con pull-up interno
    bool toggleBtnState = digitalRead(VOLUME_TOGGLE_BTN);
    
    // Detectar flanco de bajada (botón presionado)
    if (toggleBtnState == LOW && lastToggleBtnState == HIGH) {
        delay(50);  // Debounce
        
        // Capturar posición actual del potenciómetro
        int raw = analogRead(ROTARY_ANGLE_PIN);
        lastPotValue = map(raw, 0, 4095, MAX_VOLUME, 0);
        potMoved = false;  // El pot NO se ha movido desde el cambio de modo
        
        // Alternar modo de volumen
        volumeMode = (volumeMode == VOL_SEQUENCER) ? VOL_LIVE_PADS : VOL_SEQUENCER;
        
        Serial.printf("► Volume Mode: %s (pot locked at %d%% until moved)\n", 
                     volumeMode == VOL_SEQUENCER ? "SEQUENCER" : "LIVE PADS",
                     lastPotValue);
        
        // Mostrar en TM1638
        tm1.displayText(volumeMode == VOL_SEQUENCER ? "SEQ VOL " : "PAD VOL ");
        tm2.displayText("        ");
        lastDisplayChange = millis();
        currentDisplayMode = DISPLAY_VOLUME;
        needsHeaderUpdate = true;
    }
    lastToggleBtnState = toggleBtnState;
    
    // Leer potenciómetro (0-100%)
    int raw = analogRead(ROTARY_ANGLE_PIN);
    int newVol = map(raw, 0, 4095, MAX_VOLUME, 0);  // Invertido: girar derecha = subir volumen
    
    // Detectar si el potenciómetro se movió desde el cambio de modo
    if (!potMoved && abs(newVol - lastPotValue) > 5) {  // Umbral de 5% para detectar movimiento
        potMoved = true;
        Serial.println("► Potentiometer moved - volume control unlocked");
    }
    
    // Solo actualizar volumen si el pot se movió después del cambio de modo
    if (potMoved) {
        if (volumeMode == VOL_SEQUENCER) {
            if (abs(newVol - lastSequencerVolume) > 2) {  // Hysteresis de 2%
                sequencerVolume = newVol;
                lastSequencerVolume = newVol;
                
                // Enviar al MASTER
                JsonDocument doc;
                doc["cmd"] = "setSequencerVolume";
                doc["value"] = sequencerVolume;
                sendUDPCommand(doc);
                
                Serial.printf("► Sequencer Volume: %d%%\n", sequencerVolume);
                showVolumeOnTM1638();
                lastDisplayChange = millis();
                needsHeaderUpdate = true;
            }
        } else {
            if (abs(newVol - lastLivePadsVolume) > 2) {  // Hysteresis de 2%
                livePadsVolume = newVol;
                lastLivePadsVolume = newVol;
                
                // Enviar al MASTER con boost del 25% (pero limitado a MAX_VOLUME)
                int boostedVolume = min((int)(livePadsVolume * 1.25), MAX_VOLUME);
                JsonDocument doc;
                doc["cmd"] = "setLiveVolume";
                doc["value"] = boostedVolume;
                sendUDPCommand(doc);
                
                Serial.printf("► Live Pads Volume: %d%% (sent: %d%% +25%% boost)\n", 
                             livePadsVolume, boostedVolume);
                showVolumeOnTM1638();
                lastDisplayChange = millis();
                needsHeaderUpdate = true;
            }
        }
    }
}

// ============================================
// M5 8ENCODER HANDLING
// ============================================
void handleM5Encoders() {
    if (!m5encoderConnected) return;
    
    unsigned long currentTime = millis();
    if (currentTime - lastEncoderRead < ENCODER_READ_INTERVAL) return;
    lastEncoderRead = currentTime;
    
    bool anyChange = false;
    
    // Leer los 8 encoders
    for (int i = 0; i < 8; i++) {
        // Leer contador relativo (cambio desde última lectura)
        int32_t delta = m5encoder.getRelCounter(i);
        
        if (delta != 0) {
            anyChange = true;
            
            // Actualizar según el modo actual
            switch (encoderMode) {
                case ENC_MODE_VOLUME: {
                    // Control de volumen individual del track (0-150)
                    trackVolumes[i] += (delta * 5);  // Pasos de 5%
                    if (trackVolumes[i] < 0) trackVolumes[i] = 0;
                    if (trackVolumes[i] > MAX_VOLUME) trackVolumes[i] = MAX_VOLUME;
                    
                    // Enviar comando al MASTER
                    JsonDocument doc;
                    doc["cmd"] = "track_volume";
                    doc["track"] = i;
                    doc["volume"] = trackVolumes[i];
                    sendUDPCommand(doc);
                    
                    Serial.printf("► Track %d (%s) Volume: %d%%\n", 
                                 i + 1, trackNames[i], trackVolumes[i]);
                    
                    // Actualizar brillo del LED según volumen
                    uint8_t brightness = map(trackVolumes[i], 0, MAX_VOLUME, 10, 255);
                    uint8_t r = (encoderLEDColors[i][0] * brightness) / 255;
                    uint8_t g = (encoderLEDColors[i][1] * brightness) / 255;
                    uint8_t b = (encoderLEDColors[i][2] * brightness) / 255;
                    m5encoder.writeRGB(i, r, g, b);
                    break;
                }
                    
                // Modos futuros: PITCH, PAN, EFFECT
                case ENC_MODE_PITCH:
                case ENC_MODE_PAN:
                case ENC_MODE_EFFECT:
                default:
                    // TODO: Implementar otros modos
                    break;
            }
            
            // Mostrar en TM1638
            if (currentScreen == SCREEN_SEQUENCER) {
                tm1.displayText(trackNames[i]);
                char volStr[9];
                snprintf(volStr, sizeof(volStr), "VOL %3d", trackVolumes[i]);
                tm2.displayText(volStr);
            }
        }
        
        // Leer botones de los encoders (para cambiar modo futuro)
        if (m5encoder.getKeyPressed(i)) {
            Serial.printf("► Encoder %d button pressed\n", i + 1);
            // TODO: Usar para cambiar modo (VOLUME/PITCH/PAN/EFFECT)
        }
    }
    
    // Leer switch toggle del módulo
    uint8_t switchState = m5encoder.inputSwitch();
    if (switchState) {
        Serial.println("► M5 Toggle Switch activated");
        // TODO: Usar para reset general o cambio de modo global
    }
}

void updateEncoderLEDs() {
    if (!m5encoderConnected) return;
    
    // Actualizar LEDs según el estado actual (puede llamarse desde display)
    for (int i = 0; i < 8; i++) {
        uint8_t brightness = map(trackVolumes[i], 0, MAX_VOLUME, 10, 255);
        uint8_t r = (encoderLEDColors[i][0] * brightness) / 255;
        uint8_t g = (encoderLEDColors[i][1] * brightness) / 255;
        uint8_t b = (encoderLEDColors[i][2] * brightness) / 255;
        m5encoder.writeRGB(i, r, g, b);
    }
}

// ============================================
// TM1638 DISPLAYS
// ============================================
void showInstrumentOnTM1638(int track) {
    lastInstrumentPlayed = track;
    instrumentDisplayTime = millis();
    currentDisplayMode = DISPLAY_INSTRUMENT;
    
    tm1.displayText(instrumentNames[track]);
    
    char display[9];
    snprintf(display, 9, "TRACK %d ", track + 1);
    tm2.displayText(display);
}

void showBPMOnTM1638() {
    currentDisplayMode = DISPLAY_BPM;
    char display1[9], display2[9];
    snprintf(display1, 9, "BPM %3d ", tempo);
    snprintf(display2, 9, "STEP %2d ", currentStep + 1);
    tm1.displayText(display1);
    tm2.displayText(display2);
}

void showVolumeOnTM1638() {
    currentDisplayMode = DISPLAY_VOLUME;
    char display1[9], display2[9];
    
    if (volumeMode == VOL_SEQUENCER) {
        snprintf(display1, 9, "SEQ %3d%%", sequencerVolume);
        snprintf(display2, 9, "--------");
    } else {
        snprintf(display1, 9, "PAD %3d%%", livePadsVolume);
        snprintf(display2, 9, "--------");
    }
    
    tm1.displayText(display1);
    tm2.displayText(display2);
}

void updateTM1638Displays() {
    unsigned long currentTime = millis();
    
    if (currentDisplayMode == DISPLAY_INSTRUMENT) {
        if (currentTime - instrumentDisplayTime > 2000) {
            currentDisplayMode = DISPLAY_BPM;
            showBPMOnTM1638();
        }
        return;
    }
    
    if (currentScreen == SCREEN_MENU) {
        tm1.displayText("RED 808 ");
        tm2.displayText("  MENU  ");
        
    } else if (currentScreen == SCREEN_LIVE) {
        if (currentTime - lastDisplayChange > 3000) {
            lastDisplayChange = currentTime;
            if (currentDisplayMode == DISPLAY_BPM) {
                showVolumeOnTM1638();
            } else {
                showBPMOnTM1638();
            }
        }
        
    } else if (currentScreen == SCREEN_SEQUENCER) {
        char display1[9], display2[9];
        if (isPlaying) {
            snprintf(display1, 9, "PLAY %2d ", currentStep + 1);
            snprintf(display2, 9, "BPM %3d ", tempo);
        } else {
            // Mostrar instrumento completo en ambos displays
            snprintf(display1, 9, "TR %d    ", selectedTrack + 1);
            tm1.displayText(display1);
            tm2.displayText(instrumentNames[selectedTrack]);
            return; // Salir temprano para no sobrescribir
        }
        tm1.displayText(display1);
        tm2.displayText(display2);
        
    } else if (currentScreen == SCREEN_SETTINGS) {
        char display1[9];
        snprintf(display1, 9, "KIT  %d  ", currentKit + 1);
        tm1.displayText(display1);
        tm2.displayText(kits[currentKit].name.substring(0, 8).c_str());
        
    } else if (currentScreen == SCREEN_DIAGNOSTICS) {
        bool allOk = diagnostic.tftOk && diagnostic.tm1638_1_Ok && 
                     diagnostic.tm1638_2_Ok && diagnostic.encoderOk;
        tm1.displayText(allOk ? "ALL  OK " : " ERROR  ");
        tm2.displayText(diagnostic.udpConnected ? "UDP  OK " : "NO  UDP ");
    }
}

void updateLEDFeedback() {
    unsigned long currentTime = millis();
    
    for (int i = 0; i < 8; i++) {  // Solo 8 LEDs
        if (ledActive[i]) {
            if (currentTime >= ledOffTime[i]) {
                setLED(i, false);
                ledActive[i] = false;
            }
        }
    }
}

void updateAudioVisualization() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastVizUpdate > 50) {
        lastVizUpdate = currentTime;
        
        for (int i = 0; i < 8; i++) {
            if (audioLevels[i] > 0) {
                audioLevels[i] -= 15;
                if (audioLevels[i] < 0) audioLevels[i] = 0;
            }
        }
        
        // Manejar tremolo y efecto neón en Live Pads
        if (currentScreen == SCREEN_LIVE) {
            uint16_t buttons = readAllButtons();
            for (int i = 0; i < 8; i++) {  // Solo 8 pads
                bool isPressed = buttons & (1 << i);
                
                if (isPressed && padPressed[i]) {
                    // Mantener presionado - Tremolo cada 150ms
                    if (currentTime - padPressTime[i] > 150) {
                        triggerDrum(i);
                        padPressTime[i] = currentTime;
                    }
                } else if (!isPressed && padPressed[i]) {
                    // Se soltó el botón - quitar efecto neón
                    padPressed[i] = false;
                    drawLivePad(i, false);
                }
            }
        }
    }
}

// ============================================
// DRAW FUNCTIONS
// ============================================
void drawBootScreen() {
    // Esta función ya no se usa - reemplazada por drawConsoleBootScreen()
}

void drawConsoleBootScreen() {
    // Fondo degradado oscuro moderno
    for (int y = 0; y < 320; y += 2) {
        uint8_t brightness = map(y, 0, 320, 8, 2);
        uint16_t color = tft.color565(brightness, 0, 0);
        tft.drawFastHLine(0, y, 480, color);
    }
    
    // LOGO PRINCIPAL - Grande y con efecto
    tft.fillRoundRect(90, 30, 300, 80, 10, THEME_RED808.primary);
    tft.drawRoundRect(90, 30, 300, 80, 10, THEME_RED808.accent);
    tft.drawRoundRect(92, 32, 296, 76, 8, THEME_RED808.accent2);
    
    tft.setTextSize(6);
    tft.setTextColor(THEME_RED808.accent);
    tft.setCursor(125, 50);
    tft.println("RED808");
    
    // Líneas decorativas
    for (int i = 0; i < 3; i++) {
        tft.drawFastHLine(50, 120 + i, 380, THEME_RED808.accent);
    }
    
    delay(400);
    
    // TARJETAS DE SISTEMA - Diseño moderno tipo dashboard
    const int cardY = 140;
    const int cardH = 50;
    const int cardW = 140;
    const int spacing = 10;
    
    // Tarjeta 1: CPU
    int x1 = 20;
    tft.fillRoundRect(x1, cardY, cardW, cardH, 8, 0x2000);
    tft.drawRoundRect(x1, cardY, cardW, cardH, 8, THEME_RED808.accent);
    tft.setTextSize(1);
    tft.setTextColor(THEME_RED808.accent2);
    tft.setCursor(x1 + 10, cardY + 10);
    tft.println("ESP32");
    tft.setTextSize(2);
    tft.setTextColor(0xFFFF);
    tft.setCursor(x1 + 10, cardY + 25);
    tft.println("240MHz");
    delay(200);
    
    // Tarjeta 2: RAM
    int x2 = x1 + cardW + spacing;
    tft.fillRoundRect(x2, cardY, cardW, cardH, 8, 0x2000);
    tft.drawRoundRect(x2, cardY, cardW, cardH, 8, THEME_RED808.accent);
    tft.setTextSize(1);
    tft.setTextColor(THEME_RED808.accent2);
    tft.setCursor(x2 + 10, cardY + 10);
    tft.println("MEMORY");
    tft.setTextSize(2);
    tft.setTextColor(0xFFFF);
    tft.setCursor(x2 + 10, cardY + 25);
    tft.println("320KB");
    delay(200);
    
    // Tarjeta 3: DISPLAY
    int x3 = x2 + cardW + spacing;
    tft.fillRoundRect(x3, cardY, cardW, cardH, 8, 0x2000);
    tft.drawRoundRect(x3, cardY, cardW, cardH, 8, THEME_RED808.accent);
    tft.setTextSize(1);
    tft.setTextColor(THEME_RED808.accent2);
    tft.setCursor(x3 + 10, cardY + 10);
    tft.println("DISPLAY");
    tft.setTextSize(2);
    tft.setTextColor(0xFFFF);
    tft.setCursor(x3 + 10, cardY + 25);
    tft.println("480x320");
    delay(200);
    
    // BARRA DE PROGRESO MODERNA
    int progY = 210;
    tft.setTextSize(1);
    tft.setTextColor(THEME_RED808.accent2);
    tft.setCursor(20, progY);
    tft.println("INITIALIZING SYSTEM...");
    
    const int barX = 20;
    const int barY = progY + 20;
    const int barW = 440;
    const int barH = 20;
    
    // Fondo de la barra
    tft.fillRoundRect(barX, barY, barW, barH, 10, 0x1800);
    tft.drawRoundRect(barX, barY, barW, barH, 10, THEME_RED808.accent);
    
    // Animación de carga progresiva
    const char* stages[] = {
        "Hardware",
        "Displays",
        "TM1638 #1",
        "TM1638 #2",
        "Encoder",
        "Buttons",
        "WiFi",
        "UDP Client",
        "Patterns",
        "Ready"
    };
    
    for (int i = 0; i < 10; i++) {
        int progress = map(i + 1, 0, 10, 0, barW - 4);
        
        // Barra de progreso con gradiente
        for (int p = 0; p < progress; p += 2) {
            uint8_t red = map(p, 0, progress, 200, 255);
            uint16_t color = tft.color565(red, 0, 0);
            tft.fillRect(barX + 2 + p, barY + 2, 2, barH - 4, color);
        }
        
        // Texto del stage actual
        tft.fillRect(20, progY + 45, 200, 16, 0x0000);
        tft.setTextSize(2);
        tft.setTextColor(THEME_RED808.accent);
        tft.setCursor(20, progY + 45);
        tft.printf("> %s", stages[i]);
        
        // Porcentaje
        tft.fillRect(380, progY + 45, 80, 16, 0x0000);
        tft.setTextColor(0xFFFF);
        tft.setCursor(380, progY + 45);
        tft.printf("%d%%", (i + 1) * 10);
        
        delay(180);
    }
    
    delay(200);
    
    // MENSAJE FINAL - Grande y centrado
    tft.fillRect(0, progY + 70, 480, 40, 0x0000);
    
    // Caja de "READY"
    tft.fillRoundRect(140, progY + 70, 200, 45, 10, THEME_RED808.accent);
    tft.drawRoundRect(140, progY + 70, 200, 45, 10, 0xFFFF);
    tft.setTextSize(3);
    tft.setTextColor(0x0000);
    tft.setCursor(175, progY + 80);
    tft.println("READY");
    
    // Iconos de check
    for (int i = 0; i < 3; i++) {
        tft.fillCircle(120 + i * 5, progY + 92, 3, 0x07E0);
        tft.fillCircle(360 + i * 5, progY + 92, 3, 0x07E0);
    }
    
    delay(800);
}

void drawSpectrumAnimation() {
    // Fondo negro con degradado sutil
    for (int y = 0; y < 320; y += 4) {
        uint8_t brightness = map(y, 0, 320, 5, 0);
        uint16_t color = tft.color565(brightness, 0, 0);
        tft.fillRect(0, y, 480, 4, color);
    }
    
    // LOGO CON EFECTO GLOW
    // Sombra/glow exterior
    for (int offset = 4; offset > 0; offset--) {
        uint8_t brightness = map(offset, 4, 1, 50, 150);
        uint16_t glowColor = tft.color565(brightness, 0, 0);
        tft.drawRoundRect(86 - offset, 26 - offset, 308 + offset * 2, 88 + offset * 2, 12, glowColor);
    }
    
    // Caja del logo
    tft.fillRoundRect(90, 30, 300, 80, 10, 0x1000);
    tft.drawRoundRect(90, 30, 300, 80, 10, THEME_RED808.accent);
    tft.drawRoundRect(92, 32, 296, 76, 8, THEME_RED808.accent2);
    
    tft.setTextSize(7);
    tft.setTextColor(THEME_RED808.accent);
    tft.setCursor(115, 45);
    tft.println("RED808");
    
    // Subtítulo
    tft.setTextSize(2);
    tft.setTextColor(THEME_RED808.accent2);
    tft.setCursor(160, 125);
    tft.println("DRUM MACHINE");
    
    // VISUALIZADOR DE AUDIO MODERNO - Circular
    const int centerX = 240;
    const int centerY = 210;
    const int numBars = 24;
    const float angleStep = 360.0 / numBars;
    
    // Buffer de alturas previas para suavizado
    static int prevHeights[24] = {0};
    
    for (int frame = 0; frame < 25; frame++) {
        // Borrar área del círculo
        tft.fillCircle(centerX, centerY, 75, 0x0000);
        
        // Dibujar barras radiales
        for (int i = 0; i < numBars; i++) {
            float angle = i * angleStep * PI / 180.0;
            
            // Altura con onda suave
            float wave = sin((frame * 0.3) + (i * 0.4)) * 20 + 40;
            int targetHeight = random(20, 60) * 0.3 + wave * 0.7;
            
            // Suavizado
            prevHeights[i] = (prevHeights[i] * 0.7) + (targetHeight * 0.3);
            int height = prevHeights[i];
            
            // Posición inicial (centro)
            int startRadius = 15;
            int endRadius = startRadius + height;
            
            // Calcular posiciones
            int x1 = centerX + cos(angle) * startRadius;
            int y1 = centerY + sin(angle) * startRadius;
            int x2 = centerX + cos(angle) * endRadius;
            int y2 = centerY + sin(angle) * endRadius;
            
            // Gradiente de color según altura
            uint8_t red = map(height, 20, 60, 150, 255);
            uint16_t color = tft.color565(red, 0, 0);
            
            // Dibujar línea gruesa
            for (int thick = -2; thick <= 2; thick++) {
                int x1t = x1 + cos(angle + PI/2) * thick;
                int y1t = y1 + sin(angle + PI/2) * thick;
                int x2t = x2 + cos(angle + PI/2) * thick;
                int y2t = y2 + sin(angle + PI/2) * thick;
                tft.drawLine(x1t, y1t, x2t, y2t, color);
            }
            
            // Punto brillante en el extremo
            tft.fillCircle(x2, y2, 2, THEME_RED808.accent);
        }
        
        // Círculo central decorativo
        tft.fillCircle(centerX, centerY, 12, THEME_RED808.primary);
        tft.drawCircle(centerX, centerY, 12, THEME_RED808.accent);
        tft.drawCircle(centerX, centerY, 13, THEME_RED808.accent2);
        
        // Texto LOADING centrado con parpadeo suave
        if (frame % 8 < 6) {  // Parpadeo más lento
            tft.setTextSize(2);
            tft.setTextColor(THEME_RED808.accent);
            tft.setCursor(185, 295);
            tft.println("LOADING...");
        } else {
            tft.fillRect(185, 295, 110, 20, 0x0000);
        }
        
        delay(70);  // Animación fluida
    }
    
    // Transición suave final
    delay(500);
}

void drawMainMenu() {
    tft.fillScreen(COLOR_BG);
    
    tft.fillRect(0, 0, 480, 50, COLOR_NAVY);
    tft.drawFastHLine(0, 50, 480, COLOR_ACCENT);
    
    // Título
    tft.setTextSize(4);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(150, 10);
    tft.println("RED808");
    
    // Info adicional en header
    tft.setTextSize(1);
    tft.setTextColor(udpConnected ? COLOR_SUCCESS : COLOR_ERROR);
    tft.setCursor(10, 10);
    tft.print(udpConnected ? "MASTER OK" : "NO MASTER");
    
    // Uptime
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.setCursor(10, 23);
    uint32_t uptimeSeconds = millis() / 1000;
    tft.printf("UP: %02d:%02d:%02d", uptimeSeconds / 3600, (uptimeSeconds % 3600) / 60, uptimeSeconds % 60);
    
    // Memoria
    tft.setCursor(10, 36);
    tft.printf("RAM: %dK", ESP.getFreeHeap() / 1024);
    
    int itemHeight = 52;
    int startY = 70;
    
    for (int i = 0; i < menuItemCount; i++) {
        int y = startY + i * itemHeight;
        
        if (i == menuSelection) {
            tft.fillRoundRect(30, y, 420, 45, 8, COLOR_ACCENT);
            tft.drawRoundRect(30, y, 420, 45, 8, COLOR_ACCENT2);
            tft.setTextSize(3);
            tft.setTextColor(COLOR_TEXT);
        } else {
            tft.fillRoundRect(30, y, 420, 45, 8, COLOR_NAVY_LIGHT);
            tft.setTextSize(2);
            tft.setTextColor(COLOR_TEXT_DIM);
        }
        
        tft.setCursor(50, y + (i == menuSelection ? 12 : 14));
        tft.print(menuItems[i]);
    }
}

// Función optimizada para solo actualizar items del menú sin parpadeo
void drawMenuItems(int oldSelection, int newSelection) {
    int itemHeight = 52;
    int startY = 70;
    
    // Redibujar item antiguo (no seleccionado)
    if (oldSelection >= 0 && oldSelection < menuItemCount) {
        int y = startY + oldSelection * itemHeight;
        tft.fillRoundRect(30, y, 420, 45, 8, COLOR_NAVY_LIGHT);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(50, y + 14);
        tft.print(menuItems[oldSelection]);
    }
    
    // Redibujar item nuevo (seleccionado)
    if (newSelection >= 0 && newSelection < menuItemCount) {
        int y = startY + newSelection * itemHeight;
        tft.fillRoundRect(30, y, 420, 45, 8, COLOR_ACCENT);
        tft.drawRoundRect(30, y, 420, 45, 8, COLOR_ACCENT2);
        tft.setTextSize(3);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(50, y + 12);
        tft.print(menuItems[newSelection]);
    }
}

void drawLiveScreen() {
    tft.fillScreen(COLOR_BG);
    drawHeader();
    
    // ========== 8 PADS GRANDES ESTILO AKAI APC MINI ==========
    // Maximizar tamaño de pads para llenar pantalla 480x320
    const int padW = 115;  // Más anchos
    const int padH = 110;  // Más altos
    const int startX = 8;
    const int startY = 65;  
    const int spacingX = 5;
    const int spacingY = 8;
    
    // Colores vibrantes estilo AKAI APC (RGB saturados)
    const uint16_t padColors[8] = {
        tft.color565(255, 20, 20),    // KICK - Rojo intenso
        tft.color565(255, 240, 0),    // SNARE - Amarillo brillante
        tft.color565(0, 255, 255),    // CL-HAT - Cyan eléctrico
        tft.color565(255, 160, 0),    // OP-HAT - Naranja vibrante
        tft.color565(255, 0, 200),    // CLAP - Magenta
        tft.color565(150, 255, 0),    // TOM-LO - Verde lima brillante
        tft.color565(0, 180, 255),    // TOM-HI - Azul brillante
        tft.color565(255, 80, 150)    // CYMBAL - Rosa neón
    };
    
    // Dibujar los 8 pads con diseño mejorado (4 columnas x 2 filas)
    for (int i = 0; i < 8; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = startX + col * (padW + spacingX);
        int y = startY + row * (padH + spacingY);
        
        uint16_t baseColor = padColors[i];
        
        // Color atenuado para estado OFF (más oscuro)
        uint8_t r = ((baseColor >> 11) & 0x1F) * 3;
        uint8_t g = ((baseColor >> 5) & 0x3F) * 1.5;
        uint8_t b = (baseColor & 0x1F) * 3;
        uint16_t dimColor = tft.color565(r, g, b);
        
        // Fondo con gradiente simulado (3 capas)
        tft.fillRoundRect(x, y, padW, padH, 10, COLOR_BG);
        tft.fillRoundRect(x+1, y+1, padW-2, padH-2, 9, dimColor);
        
        // Borde doble para efecto 3D
        tft.drawRoundRect(x, y, padW, padH, 10, baseColor);
        tft.drawRoundRect(x+1, y+1, padW-2, padH-2, 9, baseColor);
        
        // Barra superior LED estilo AKAI (más grande)
        tft.fillRoundRect(x + 4, y + 4, padW - 8, 12, 4, baseColor);
        
        // Efecto glow en la barra
        uint8_t rg = ((baseColor >> 11) & 0x1F) * 6;
        uint8_t gg = ((baseColor >> 5) & 0x3F) * 3;
        uint8_t bg = (baseColor & 0x1F) * 6;
        uint16_t glowColor = tft.color565(rg, gg, bg);
        tft.drawRoundRect(x + 3, y + 3, padW - 6, 14, 5, glowColor);
        
        // Número del pad (más grande y con fondo)
        tft.fillCircle(x + 15, y + 26, 9, COLOR_BG);
        tft.drawCircle(x + 15, y + 26, 10, baseColor);
        tft.setTextSize(2);
        tft.setTextColor(baseColor);
        tft.setCursor(x + (i < 9 ? 11 : 8), y + 19);
        tft.printf("%d", i + 1);
        
        // Nombre del instrumento (más grande, centrado)
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT);
        String instName = String(instrumentNames[i]);
        instName.trim();
        if (instName.length() > 7) {
            instName = instName.substring(0, 7);
        }
        int textWidth = instName.length() * 12;
        tft.setCursor(x + (padW - textWidth) / 2, y + 50);
        tft.print(instName);
        
        // Track name grande abajo
        tft.setTextSize(2);
        tft.setTextColor(baseColor);
        tft.setCursor(x + padW - 32, y + padH - 22);
        tft.print(trackNames[i]);
    }
    
    // Footer con instrucciones
    tft.fillRect(0, 302, 480, 18, COLOR_PRIMARY);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(30, 307);
    tft.print("S1-S8: PLAY INSTRUMENTS");
    tft.setTextColor(COLOR_WARNING);
    tft.setCursor(230, 307);
    tft.print("VOLUME: KNOB");
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(360, 307);
    tft.print("BACK: MENU");
}

void drawSequencerScreen() {
    static int lastStep = -1;
    
    if (needsFullRedraw) {
        tft.fillScreen(COLOR_BG);
        drawHeader();
        
        // Sin indicador de página (solo hay una página con 8 tracks)
    }
    
    const int gridX = 8;
    const int gridY = 88;
    const int cellW = 27;
    const int cellH = 24;
    const int labelW = 38;
    
    Pattern& pattern = patterns[currentPattern];
    
    // Mostrar todos los 8 tracks (sin páginas)
    int trackStart = 0;
    int trackEnd = MAX_TRACKS;
    
    if (needsFullRedraw) {
        tft.fillRoundRect(gridX - 2, gridY - 2, 468, 210, 8, COLOR_NAVY);
        
        tft.setTextSize(1);
        for (int s = 0; s < MAX_STEPS; s++) {
            int x = gridX + labelW + s * (cellW + 1);
            tft.setTextColor((s % 4 == 0) ? COLOR_ACCENT : COLOR_TEXT_DIM);
            tft.setCursor(x + 6, gridY - 10);
            tft.printf("%d", (s + 1) % 10);
        }
        
        tft.setTextSize(2);
        for (int i = 0; i < 8; i++) {
            int t = trackStart + i;  // Track real (0-7 o 8-15)
            int y = gridY + 2 + i * (cellH + 2);
            
            // Resaltar track seleccionado con fondo
            if (t == selectedTrack) {
                tft.fillRect(gridX, y - 1, labelW - 2, cellH + 2, COLOR_PRIMARY);
            }
            
            // Mostrar MUTED con indicador visual claro
            if (pattern.muted[t]) {
                tft.setTextColor(COLOR_ERROR);
                tft.setCursor(gridX + 2, y + 2);
                tft.print("[M]");
            } else {
                // Usar color único por instrumento
                tft.setTextColor(t == selectedTrack ? TFT_WHITE : getInstrumentColor(t));
                tft.setCursor(gridX + 4, y + 2);
                tft.print(trackNames[t]);
            }
        }
        
        lastStep = -1;
    }
    
    // Redibujar etiquetas si cambi\u00f3 el grid (por ejemplo, al mutear)
    if (needsGridUpdate) {
        tft.setTextSize(2);
        for (int i = 0; i < 8; i++) {
            int t = trackStart + i;  // Track real
            int y = gridY + 2 + i * (cellH + 2);
            
            // Limpiar \u00e1rea de etiqueta
            tft.fillRect(gridX, y - 1, labelW - 2, cellH + 2, 
                        (t == selectedTrack) ? COLOR_PRIMARY : COLOR_NAVY);
            
            // Redibujar etiqueta con estado actualizado
            if (pattern.muted[t]) {
                tft.setTextColor(COLOR_ERROR);
                tft.setCursor(gridX + 2, y + 2);
                tft.print("[M]");
            } else {
                tft.setTextColor(t == selectedTrack ? TFT_WHITE : getInstrumentColor(t));
                tft.setCursor(gridX + 4, y + 2);
                tft.print(trackNames[t]);
            }
        }
    }
    
    // Dibujar celdas del grid (8 tracks, sin páginas)
    for (int i = 0; i < 8; i++) {
        int t = trackStart + i;  // Track real (0-7)
        for (int s = 0; s < MAX_STEPS; s++) {
            if (needsFullRedraw || needsGridUpdate || (s == currentStep || s == lastStep)) {
                int x = gridX + labelW + s * (cellW + 1);
                int y = gridY + 2 + i * (cellH + 2);  // Usar 'i' para posición visual
                
                uint16_t color;
                uint16_t border = COLOR_NAVY;
                
                // Si el track está muteado, oscurecer todo
                bool isMuted = pattern.muted[t];
                
                if (isPlaying && s == currentStep) {
                    // Durante reproducción, usar color del instrumento si está activo
                    if (isMuted) {
                        color = pattern.steps[t][s] ? 0x2104 : COLOR_NAVY;  // Gris oscuro si muted
                        border = COLOR_ERROR;
                    } else {
                        color = pattern.steps[t][s] ? getInstrumentColor(t) : COLOR_NAVY_LIGHT;
                        border = pattern.steps[t][s] ? getInstrumentColor(t) : COLOR_WARNING;
                    }
                } else if (pattern.steps[t][s]) {
                    // Step activo: usar color del instrumento o gris si muted
                    if (isMuted) {
                        color = 0x3186;  // Gris medio para indicar muted
                        border = COLOR_ERROR;
                    } else {
                        color = getInstrumentColor(t);
                        border = getInstrumentColor(t);
                    }
                } else {
                    color = COLOR_NAVY_LIGHT;
                }
                
                tft.fillRoundRect(x, y, cellW, cellH, 3, color);
                if (border != COLOR_NAVY) {
                    tft.drawRoundRect(x, y, cellW, cellH, 3, border);
                }
                
                if (s % 4 == 0 && needsFullRedraw) {
                    tft.drawFastVLine(x - 1, gridY, 200, COLOR_ACCENT);
                }
            }
        }
    }
    
    lastStep = currentStep;
    
    if (needsFullRedraw) {
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(5, 305);
        tft.print("S1-8:TOGGLE | ENC:TRACK | HOLD:BPM | ");
        tft.setTextColor(COLOR_ACCENT);
        tft.print("VOL-HOLD:PATTERN");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" | ENCODER+BACK:SYNC");
    }
}

void drawSettingsScreen() {
    tft.fillScreen(COLOR_BG);
    drawHeader();
    
    // Título principal con badge
    tft.fillRoundRect(150, 55, 180, 32, 8, COLOR_PRIMARY);
    tft.drawRoundRect(150, 55, 180, 32, 8, COLOR_ACCENT);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(165, 63);
    tft.print("SETTINGS");
    
    const int sectionY = 100;
    
    // ========== COLUMNA IZQUIERDA: SYSTEM INFO ==========
    const int leftX = 30;
    
    // Panel System Info
    tft.fillRoundRect(leftX, sectionY, 200, 28, 6, COLOR_PRIMARY);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(leftX + 40, sectionY + 6);
    tft.print("SYSTEM");
    
    tft.fillRoundRect(leftX, sectionY + 35, 200, 120, 10, COLOR_PRIMARY);
    tft.drawRoundRect(leftX, sectionY + 35, 200, 120, 10, COLOR_ACCENT2);
    
    int infoY = sectionY + 48;
    tft.setTextSize(1);
    
    // Samplers
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(leftX + 10, infoY);
    tft.print("SAMPLES:");
    tft.setTextColor(COLOR_SUCCESS);
    tft.setCursor(leftX + 100, infoY);
    tft.print("16 x 16");
    infoY += 18;
    
    // Memoria
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(leftX + 10, infoY);
    tft.print("FREE RAM:");
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(leftX + 100, infoY);
    uint32_t freeHeap = ESP.getFreeHeap();
    tft.printf("%d KB", freeHeap / 1024);
    infoY += 18;
    
    // Firmware
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(leftX + 10, infoY);
    tft.print("VERSION:");
    tft.setTextColor(COLOR_WARNING);
    tft.setCursor(leftX + 100, infoY);
    tft.print("v5.0");
    infoY += 18;
    
    // Conexión
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(leftX + 10, infoY);
    tft.print("MASTER:");
    tft.setTextColor(udpConnected ? COLOR_SUCCESS : COLOR_ERROR);
    tft.setCursor(leftX + 100, infoY);
    tft.print(udpConnected ? "ONLINE" : "OFFLINE");
    infoY += 18;
    
    // Pattern
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(leftX + 10, infoY);
    tft.print("PATTERN:");
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(leftX + 100, infoY);
    tft.printf("%d / %d", currentPattern + 1, MAX_PATTERNS);
    infoY += 18;
    
    // Uptime
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(leftX + 10, infoY);
    tft.print("UPTIME:");
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.setCursor(leftX + 100, infoY);
    uint32_t uptimeSeconds = millis() / 1000;
    uint32_t hours = uptimeSeconds / 3600;
    uint32_t minutes = (uptimeSeconds % 3600) / 60;
    tft.printf("%02d:%02d", hours, minutes);
    
    // ========== COLUMNA DERECHA: THEMES ==========
    const int rightX = 250;
    
    // Panel Themes
    tft.fillRoundRect(rightX, sectionY, 200, 28, 6, COLOR_PRIMARY);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(rightX + 40, sectionY + 6);
    tft.print("THEMES");
    
    // Theme selector (4 themes en 2x2)
    for (int i = 0; i < THEME_COUNT; i++) {
        const ColorTheme* theme = THEMES[i];
        int col = i % 2;
        int row = i / 2;
        int x = rightX + 20 + col * 80;
        int y = sectionY + 48 + row * 55;
        
        if (i == currentTheme) {
            tft.fillRoundRect(x - 3, y - 3, 66, 48, 6, COLOR_ACCENT);
        }
        
        tft.fillRoundRect(x, y, 60, 42, 5, theme->primary);
        tft.drawRoundRect(x, y, 60, 42, 5, theme->accent);
        
        // Indicador de color
        tft.fillCircle(x + 30, y + 15, 10, theme->accent);
        
        // Label
        tft.setTextSize(1);
        tft.setTextColor(i == currentTheme ? COLOR_ACCENT : COLOR_TEXT_DIM);
        tft.setCursor(x + 5, y + 32);
        String label = String(theme->name);
        if (label.length() > 7) label = label.substring(0, 7);
        tft.print(label);
    }
    
    // ========== WIFI STATUS ==========
    const int wifiY = sectionY + 165;
    
    tft.fillRoundRect(rightX, wifiY, 200, 32, 6, webServerEnabled ? COLOR_SUCCESS : COLOR_ERROR);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(rightX + 40, wifiY + 8);
    tft.print("WiFi: ");
    tft.print(webServerEnabled ? "ON" : "OFF");
    
    // Footer con instrucciones
    tft.fillRect(0, 295, 480, 25, COLOR_PRIMARY);
    tft.fillRect(0, 295, 480, 2, COLOR_ACCENT);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(10, 305);
    tft.print("S4:WiFi");
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print(" | ");
    tft.setTextColor(COLOR_TEXT);
    tft.print("S5-S7:Theme");
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print(" | ");
    tft.setTextColor(COLOR_ACCENT);
    tft.print("BACK:Menu");
}

void drawDiagnosticsScreen() {
    tft.fillScreen(COLOR_BG);
    drawHeader();
    
    tft.setTextSize(2);
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(160, 58);
    tft.println("DIAGNOSTICS");
    
    int y = 88;
    const int lineHeight = 34;
    
    const char* items[] = {
        "TFT DISPLAY", 
        "TM1638 #1 (S1-8)", 
        "TM1638 #2 (S9-16)", 
        "ROTARY ENCODER",
        "M5 8ENCODER I2C",
        "WiFi CONNECTION",
        "UDP -> MASTER"
    };
    bool status[] = {
        diagnostic.tftOk,
        diagnostic.tm1638_1_Ok,
        diagnostic.tm1638_2_Ok,
        diagnostic.encoderOk,
        diagnostic.m5encoderOk,
        WiFi.status() == WL_CONNECTED,
        diagnostic.udpConnected
    };
    
    for (int i = 0; i < 7; i++) {
        tft.fillRoundRect(32, y + 2, 416, 28, 6, COLOR_NAVY);
        tft.fillRoundRect(30, y, 416, 28, 6, COLOR_NAVY_LIGHT);
        
        uint16_t indicatorColor = status[i] ? COLOR_SUCCESS : COLOR_ERROR;
        tft.fillCircle(50, y + 14, 7, indicatorColor);
        tft.drawCircle(50, y + 14, 8, indicatorColor);
        
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(70, y + 6);
        tft.print(items[i]);
        
        tft.setTextSize(2);
        tft.setTextColor(status[i] ? COLOR_SUCCESS : COLOR_ERROR);
        tft.setCursor(390, y + 6);
        tft.print(status[i] ? "OK" : "ERR");
        
        y += lineHeight;
    }
    
    y += 8;
    
    // Panel de información de red
    tft.fillRoundRect(30, y, 420, 50, 6, COLOR_NAVY);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(45, y + 5);
    tft.println("NETWORK INFO:");
    
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(45, y + 20);
    if (udpConnected) {
        tft.printf("IP: %s", WiFi.localIP().toString().c_str());
        tft.setCursor(45, y + 35);
        tft.printf("MASTER: %s:%d  RSSI: %ddBm", masterIP, udpPort, WiFi.RSSI());
    } else {
        tft.setTextColor(COLOR_ERROR);
        tft.println("NOT CONNECTED TO MASTER");
        tft.setCursor(45, y + 35);
        tft.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "OK (no UDP)" : "DISCONNECTED");
    }
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.setCursor(180, 305);
    tft.println("BACK: MENU");
}

void drawHeader() {
    tft.fillRect(0, 0, 480, 48, COLOR_NAVY);
    tft.drawFastHLine(0, 48, 480, COLOR_ACCENT);
    
    tft.setTextSize(3);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(10, 10);
    tft.println("R808");
    
    // Mostrar nombre de pantalla actual
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACCENT);
    tft.setCursor(10, 36);
    if (currentScreen == SCREEN_LIVE) {
        tft.print("LIVE PADS");
    } else if (currentScreen == SCREEN_SEQUENCER) {
        tft.print("SEQUENCER");
        // Mostrar instrumento activo con su color
        tft.setTextSize(2);
        tft.setTextColor(getInstrumentColor(selectedTrack));
        tft.setCursor(100, 14);
        String instName = String(instrumentNames[selectedTrack]);
        instName.trim();
        tft.print(instName.c_str());
    } else if (currentScreen == SCREEN_SETTINGS) {
        tft.print("SETTINGS");
    } else if (currentScreen == SCREEN_DIAGNOSTICS) {
        tft.print("DIAGNOSTICS");
    } else if (currentScreen == SCREEN_PATTERNS) {
        tft.print("PATTERNS");
    }
    
    tft.setTextSize(2);
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(240, 14);
    tft.printf("%d", tempo);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.setCursor(280, 20);
    tft.print("BPM");
    
    // Mostrar patrón en sequencer
    if (currentScreen == SCREEN_SEQUENCER) {
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(320, 14);
        tft.printf("P%d", currentPattern + 1);
    }
    
    // Mostrar ambos volúmenes con indicador de modo activo
    tft.setTextSize(1);
    tft.setTextColor(volumeMode == VOL_SEQUENCER ? COLOR_SUCCESS : COLOR_TEXT_DIM);
    tft.setCursor(370, 12);
    tft.printf("SEQ:%d%%", sequencerVolume);
    
    tft.setTextColor(volumeMode == VOL_LIVE_PADS ? COLOR_SUCCESS : COLOR_TEXT_DIM);
    tft.setCursor(370, 24);
    tft.printf("PAD:%d%%", livePadsVolume);
    
    // Icono Play/Stop (solo icono, sin texto)
    if (isPlaying) {
        tft.fillCircle(455, 24, 10, COLOR_SUCCESS);
        tft.fillTriangle(450, 18, 450, 30, 460, 24, COLOR_BG);
    } else {
        tft.drawCircle(455, 24, 10, COLOR_BORDER);
        tft.fillRect(451, 19, 3, 10, COLOR_TEXT_DIM);
        tft.fillRect(456, 19, 3, 10, COLOR_TEXT_DIM);
    }
}

void drawLivePad(int padIndex, bool highlight) {
    // COORDENADAS IDÉNTICAS A drawLiveScreen
    const int padW = 115;  // Mismo que drawLiveScreen
    const int padH = 110;  // Mismo que drawLiveScreen
    const int startX = 8;
    const int startY = 65; // Mismo que drawLiveScreen
    const int spacingX = 5;
    const int spacingY = 8; // Mismo que drawLiveScreen
    
    int col = padIndex % 4;
    int row = padIndex / 4;
    int x = startX + col * (padW + spacingX);
    int y = startY + row * (padH + spacingY);
    
    // Colores vibrantes estilo AKAI (mismos que drawLiveScreen)
    const uint16_t padColors[8] = {
        tft.color565(255, 20, 20),    // KICK - Rojo intenso
        tft.color565(255, 240, 0),    // SNARE - Amarillo brillante
        tft.color565(0, 255, 255),    // CL-HAT - Cyan eléctrico
        tft.color565(255, 160, 0),    // OP-HAT - Naranja vibrante
        tft.color565(255, 0, 200),    // CLAP - Magenta
        tft.color565(150, 255, 0),    // TOM-LO - Verde lima brillante
        tft.color565(0, 180, 255),    // TOM-HI - Azul brillante
        tft.color565(255, 80, 150)    // CYMBAL - Rosa neón
    };
    
    uint16_t baseColor = padColors[padIndex];
    
    if (highlight) {
        // Efecto PRESS: pad completamente iluminado
        tft.fillRoundRect(x, y, padW, padH, 10, COLOR_BG);
        tft.fillRoundRect(x+1, y+1, padW-2, padH-2, 9, baseColor);
        
        // Bordes brillantes
        tft.drawRoundRect(x, y, padW, padH, 10, TFT_WHITE);
        tft.drawRoundRect(x+1, y+1, padW-2, padH-2, 9, TFT_WHITE);
        tft.drawRoundRect(x+2, y+2, padW-4, padH-4, 8, baseColor);
        
        // Barra superior blanca brillante
        tft.fillRoundRect(x + 4, y + 4, padW - 8, 12, 4, TFT_WHITE);
        tft.drawRoundRect(x + 3, y + 3, padW - 6, 14, 5, TFT_WHITE);
        
        // Número con fondo blanco
        tft.fillCircle(x + 15, y + 26, 9, TFT_WHITE);
        tft.drawCircle(x + 15, y + 26, 10, TFT_WHITE);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_BG);
        tft.setCursor(x + (padIndex < 9 ? 11 : 8), y + 19);
        tft.printf("%d", padIndex + 1);
        
    } else {
        // Estado NORMAL: color atenuado
        uint8_t r = ((baseColor >> 11) & 0x1F) * 3;
        uint8_t g = ((baseColor >> 5) & 0x3F) * 1.5;
        uint8_t b = (baseColor & 0x1F) * 3;
        uint16_t dimColor = tft.color565(r, g, b);
        
        tft.fillRoundRect(x, y, padW, padH, 10, COLOR_BG);
        tft.fillRoundRect(x+1, y+1, padW-2, padH-2, 9, dimColor);
        
        // Borde doble
        tft.drawRoundRect(x, y, padW, padH, 10, baseColor);
        tft.drawRoundRect(x+1, y+1, padW-2, padH-2, 9, baseColor);
        
        // Barra superior LED
        tft.fillRoundRect(x + 4, y + 4, padW - 8, 12, 4, baseColor);
        
        // Efecto glow
        uint8_t rg = ((baseColor >> 11) & 0x1F) * 6;
        uint8_t gg = ((baseColor >> 5) & 0x3F) * 3;
        uint8_t bg = (baseColor & 0x1F) * 6;
        uint16_t glowColor = tft.color565(rg, gg, bg);
        tft.drawRoundRect(x + 3, y + 3, padW - 6, 14, 5, glowColor);
        
        // Número con fondo
        tft.fillCircle(x + 15, y + 26, 9, COLOR_BG);
        tft.drawCircle(x + 15, y + 26, 10, baseColor);
        tft.setTextSize(2);
        tft.setTextColor(baseColor);
        tft.setCursor(x + (padIndex < 9 ? 11 : 8), y + 19);
        tft.printf("%d", padIndex + 1);
    }
    
    // Nombre del instrumento
    tft.setTextSize(2);
    tft.setTextColor(highlight ? COLOR_BG : COLOR_TEXT);
    String instName = String(instrumentNames[padIndex]);
    instName.trim();
    if (instName.length() > 7) {
        instName = instName.substring(0, 7);
    }
    int textWidth = instName.length() * 12;
    tft.setCursor(x + (padW - textWidth) / 2, y + 50);
    tft.print(instName);
    
    // Track name
    tft.setTextSize(2);
    tft.setTextColor(highlight ? COLOR_BG : baseColor);
    tft.setCursor(x + padW - 32, y + padH - 22);
    tft.print(trackNames[padIndex]);
}

// ============================================
// TM1638 DISPLAY UPDATES
// ============================================

void drawPatternsScreen() {
    tft.fillScreen(COLOR_BG);
    drawHeader();
    
    // Título principal
    tft.fillRoundRect(120, 55, 240, 35, 8, COLOR_PRIMARY);
    tft.drawRoundRect(120, 55, 240, 35, 8, COLOR_ACCENT);
    tft.setTextSize(3);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(135, 62);
    tft.print("PATTERNS");
    
    // Grid de patrones (6 patrones en 2 filas de 3)
    const int maxPatterns = 6;
    const int cols = 3;
    const int rows = 2;
    const int padW = 140;
    const int padH = 80;
    const int startX = 25;
    const int startY = 110;
    const int spacingX = 15;
    const int spacingY = 15;
    
    for (int i = 0; i < maxPatterns; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = startX + col * (padW + spacingX);
        int y = startY + row * (padH + spacingY);
        
        bool isSelected = (i == currentPattern);
        
        // Fondo del botón
        if (isSelected) {
            tft.fillRoundRect(x, y, padW, padH, 8, COLOR_ACCENT);
            tft.drawRoundRect(x, y, padW, padH, 8, COLOR_ACCENT2);
        } else {
            tft.fillRoundRect(x, y, padW, padH, 8, COLOR_PRIMARY);
            tft.drawRoundRect(x, y, padW, padH, 8, COLOR_BORDER);
        }
        
        // Número de patrón (grande)
        tft.setTextSize(4);
        tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
        tft.setCursor(x + 15, y + 15);
        tft.printf("%d", i + 1);
        
        // Nombre del patrón
        tft.setTextSize(1);
        tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT_DIM);
        tft.setCursor(x + 10, y + 60);
        String patternName = patterns[i].name;
        if (patternName.length() > 15) {
            patternName = patternName.substring(0, 15);
        }
        tft.print(patternName);
    }
    
    // Instrucciones en el footer
    tft.fillRect(0, 295, 480, 25, COLOR_PRIMARY);
    tft.fillRect(0, 295, 480, 2, COLOR_ACCENT);
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(10, 305);
    tft.print("ENCODER:Navigate");
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print(" | ");
    tft.setTextColor(COLOR_ACCENT);
    tft.print("ENTER:Select");
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print(" | ");
    tft.setTextColor(COLOR_ACCENT2);
    tft.print("BACK:Menu");
}

void drawSinglePattern(int patternIndex, bool isSelected) {
    // Redibujar un solo patrón sin parpadeo
    if (patternIndex < 0 || patternIndex >= 6) return;
    
    const int cols = 3;
    const int padW = 140;
    const int padH = 80;
    const int startX = 25;
    const int startY = 110;
    const int spacingX = 15;
    const int spacingY = 15;
    
    int col = patternIndex % cols;
    int row = patternIndex / cols;
    int x = startX + col * (padW + spacingX);
    int y = startY + row * (padH + spacingY);
    
    // Fondo del botón
    if (isSelected) {
        tft.fillRoundRect(x, y, padW, padH, 8, COLOR_ACCENT);
        tft.drawRoundRect(x, y, padW, padH, 8, COLOR_ACCENT2);
    } else {
        tft.fillRoundRect(x, y, padW, padH, 8, COLOR_PRIMARY);
        tft.drawRoundRect(x, y, padW, padH, 8, COLOR_BORDER);
    }
    
    // Número de patrón (grande)
    tft.setTextSize(4);
    tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    tft.setCursor(x + 15, y + 15);
    tft.printf("%d", patternIndex + 1);
    
    // Nombre del patrón
    tft.setTextSize(1);
    tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT_DIM);
    tft.setCursor(x + 10, y + 60);
    String patternName = patterns[patternIndex].name;
    if (patternName.length() > 15) {
        patternName = patternName.substring(0, 15);
    }
    tft.print(patternName);
}

void drawSyncingScreen() {
    // Pantalla temporal de sincronización
    tft.fillScreen(COLOR_BG);
    
    // Caja central con mensaje
    int boxW = 360;
    int boxH = 140;
    int boxX = (480 - boxW) / 2;
    int boxY = (320 - boxH) / 2;
    
    // Fondo de la caja con glow
    for (int i = 3; i > 0; i--) {
        uint8_t brightness = map(i, 3, 1, 30, 80);
        uint16_t glowColor = tft.color565(brightness, brightness, 0);
        tft.drawRoundRect(boxX - i, boxY - i, boxW + i * 2, boxH + i * 2, 12, glowColor);
    }
    
    tft.fillRoundRect(boxX, boxY, boxW, boxH, 10, COLOR_PRIMARY);
    tft.drawRoundRect(boxX, boxY, boxW, boxH, 10, COLOR_WARNING);
    tft.drawRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 8, COLOR_ACCENT);
    
    // Icono de búsqueda (círculo con puntos animados)
    int centerX = 240;
    int centerY = boxY + 50;
    tft.drawCircle(centerX, centerY, 25, COLOR_WARNING);
    tft.drawCircle(centerX, centerY, 23, COLOR_WARNING);
    
    // Puntos alrededor del círculo
    for (int i = 0; i < 8; i++) {
        float angle = i * 45 * PI / 180.0;
        int x = centerX + cos(angle) * 30;
        int y = centerY + sin(angle) * 30;
        tft.fillCircle(x, y, 3, COLOR_ACCENT2);
    }
    
    // Texto principal
    tft.setTextSize(3);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(boxX + 35, boxY + 95);
    tft.print("BUSCANDO");
    
    tft.setTextSize(2);
    tft.setTextColor(COLOR_ACCENT);
    tft.setCursor(boxX + 85, boxY + 120);
    tft.print("SINCRONIZACION");
    
    // Mensaje inferior
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.setCursor(boxX + 90, boxY + boxH - 15);
    tft.print("Conectando al MASTER...");
    
    delay(500);  // Mostrar por medio segundo para que sea visible
}

// ============================================
// CONTROL FUNCTIONS
// ============================================
void changeTempo(int delta) {
    tempo = constrain(tempo + delta, MIN_BPM, MAX_BPM);
    calculateStepInterval();
    
    // Enviar al MASTER
    JsonDocument doc;
    doc["cmd"] = "tempo";
    doc["value"] = tempo;
    sendUDPCommand(doc);
}

void changePattern(int delta) {
    currentPattern = (currentPattern + delta + MAX_PATTERNS) % MAX_PATTERNS;
    currentStep = 0;
    needsFullRedraw = true;
    
    // Enviar cambio de patrón al MASTER
    JsonDocument doc;
    doc["cmd"] = "selectPattern";
    doc["index"] = currentPattern;
    sendUDPCommand(doc);
    
    // Esperar un poco y solicitar el nuevo patrón automáticamente
    delay(50);
    requestPatternFromMaster();
    
    Serial.printf("► Pattern changed to %d, requesting sync...\n", currentPattern + 1);
}

void changeKit(int delta) {
    currentKit = (currentKit + delta + MAX_KITS) % MAX_KITS;
    needsFullRedraw = true;
    
    // Enviar al MASTER
    JsonDocument doc;
    doc["cmd"] = "kit";
    doc["value"] = currentKit;
    sendUDPCommand(doc);
}

void changeTheme(int delta) {
    currentTheme = (currentTheme + delta + THEME_COUNT) % THEME_COUNT;
    activeTheme = THEMES[currentTheme];
    needsFullRedraw = true;
    
    // Actualizar TM1638 con nombre del tema
    char display[9];
    snprintf(display, 9, "%-8s", activeTheme->name);
    tm1.displayText(display);
    tm2.displayText(display);
    
    Serial.printf("► Theme changed to: %s\n", activeTheme->name);
}

void changeScreen(Screen newScreen) {
    currentScreen = newScreen;
    needsFullRedraw = true;
    lastDisplayChange = millis();
    setAllLEDs(0x0000);
    
    // Si entramos al sequencer, mostrar LEDs del track seleccionado y solicitar patrón
    if (newScreen == SCREEN_SEQUENCER && !isPlaying) {
        updateStepLEDsForTrack(selectedTrack);
        showInstrumentOnTM1638(selectedTrack);
        
        // Solicitar patrón actual al MASTER
        if (udpConnected) {
            requestPatternFromMaster();
        }
    }
}

void toggleStep(int track, int step) {
    patterns[currentPattern].steps[track][step] = 
        !patterns[currentPattern].steps[track][step];
    needsGridUpdate = true;
    
    Serial.printf("► TOGGLE: Track %d, Step %d = %s\n", 
                  track, step, 
                  patterns[currentPattern].steps[track][step] ? "ON" : "OFF");
    
    // Enviar al MASTER
    JsonDocument doc;
    doc["cmd"] = "toggleStep";
    doc["pattern"] = currentPattern;
    doc["track"] = track;
    doc["step"] = step;
    doc["state"] = patterns[currentPattern].steps[track][step];
    sendUDPCommand(doc);
}

// ============================================
// SERVIDOR WEB ASYNC - DESHABILITADO EN SLAVE
// ============================================
// El SLAVE no necesita servidor web, toda la función está comentada
/*
void setupWebServer() {
    if (!fsOK) {
        Serial.println("✗ Error montando LittleFS - servidor continuará sin archivos");
    } else {
        Serial.println("► LittleFS montado correctamente");
        
        // Listar archivos para debug
        fs::File root = LittleFS.open("/");
        fs::File file = root.openNextFile();
        Serial.println("► Archivos en LittleFS:");
        while(file) {
            Serial.print("  - ");
            Serial.print(file.name());
            Serial.print(" (");
            Serial.print(file.size());
            Serial.println(" bytes)");
            file = root.openNextFile();
        }
    }
    
    // Configurar WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("RED808", "12345678");
    
    Serial.println("► WiFi AP configurado: RED808");
    Serial.print("► IP Address: ");
    Serial.println(WiFi.softAPIP());
    
    // ========== ARCHIVOS ESTÁTICOS ==========
    if (fsOK) {
        // Servir index.html desde LittleFS
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            if (LittleFS.exists("/index.html")) {
                request->send(LittleFS, "/index.html", "text/html");
            } else {
                request->send(404, "text/plain", "index.html no encontrado");
            }
        });
        
        // Servir CSS
        server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
            if (LittleFS.exists("/style.css")) {
                request->send(LittleFS, "/style.css", "text/css");
            } else {
                request->send(404, "text/plain", "style.css no encontrado");
            }
        });
        
        // Servir JavaScript
        server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
            if (LittleFS.exists("/script.js")) {
                request->send(LittleFS, "/script.js", "application/javascript");
            } else {
                request->send(404, "text/plain", "script.js no encontrado");
            }
        });
    } else {
        // Fallback: página de error si LittleFS falló
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            String html = "<html><body style='font-family:monospace;background:#000;color:#f00;padding:40px'>";
            html += "<h1>ERROR: LittleFS no montado</h1>";
            html += "<p>Los archivos web no se subieron correctamente.</p>";
            html += "<p>Ejecuta: <code>pio run --target uploadfs</code></p>";
            html += "</body></html>";
            request->send(200, "text/html", html);
        });
    }
    
    // ========== ENDPOINTS DE STATUS ==========
    // Estado completo del sistema (JSON)
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        json += "\"bpm\":" + String(tempo);
        json += ",\"pattern\":" + String(currentPattern);
        json += ",\"kit\":" + String(currentKit);
        json += ",\"playing\":" + String(isPlaying ? "true" : "false");
        json += ",\"step\":" + String(currentStep);
        json += ",\"track\":" + String(selectedTrack);
        json += ",\"volume\":" + String(volume);
        json += ",\"kitName\":\"" + kits[currentKit].name + "\"";
        json += ",\"time\":" + String(millis() / 1000.0, 1);
        json += "}";
        request->send(200, "application/json", json);
    });
    
    // ========== ENDPOINTS DE CONTROL ==========
    // Play/Stop
    server.on("/play", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!isPlaying) {
            isPlaying = true;
            Serial.println("► PLAY (Web)");
        }
        request->send(200, "application/json", "{\"status\":\"playing\"}");
    });
    
    server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request){
        if (isPlaying) {
            isPlaying = false;
            currentStep = 0;
            setAllLEDs(0x0000);
            Serial.println("► STOP (Web)");
        }
        request->send(200, "application/json", "{\"status\":\"stopped\"}");
    });
    
    // Toggle Play/Stop
    server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
        isPlaying = !isPlaying;
        if (!isPlaying) {
            currentStep = 0;
            setAllLEDs(0x0000);
        }
        String response = "{\"playing\":" + String(isPlaying ? "true" : "false") + "}";
        request->send(200, "application/json", response);
    });
    
    // Cambiar Tempo
    server.on("/tempo", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("delta")) {
            int delta = request->getParam("delta")->value().toInt();
            changeTempo(delta);
            Serial.printf("► TEMPO changed: %d BPM (Web)\n", tempo);
        } else if (request->hasParam("value")) {
            int newTempo = request->getParam("value")->value().toInt();
            tempo = constrain(newTempo, MIN_BPM, MAX_BPM);
            stepInterval = (60000 / tempo) / 4;
            
            // Enviar al MASTER
            JsonDocument doc;
            doc["cmd"] = "tempo";
            doc["value"] = tempo;
            sendUDPCommand(doc);
            
            Serial.printf("► TEMPO set: %d BPM (Web) - Sent to MASTER\n", tempo);
        }
        request->send(200, "application/json", "{\"bpm\":" + String(tempo) + "}");
    });
    
    // Cambiar Pattern
    server.on("/pattern", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("pattern")) {
            int newPattern = request->getParam("pattern")->value().toInt();
            if (newPattern >= 0 && newPattern < MAX_PATTERNS) {
                changePattern(newPattern - currentPattern);
                Serial.printf("► PATTERN changed: %d (Web)\n", currentPattern + 1);
            }
        }
        request->send(200, "application/json", "{\"pattern\":" + String(currentPattern) + "}");
    });
    
    // Cambiar Kit
    server.on("/kit", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("kit")) {
            int newKit = request->getParam("kit")->value().toInt();
            if (newKit >= 0 && newKit < MAX_KITS) {
                changeKit(newKit - currentKit);
                Serial.printf("► KIT changed: %s (Web)\n", kits[currentKit].name.c_str());
            }
        }
        request->send(200, "application/json", "{\"kit\":" + String(currentKit) + "}");
    });
    
    // Toggle Step (Control bidireccional del sequencer)
    server.on("/step", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("track") && request->hasParam("step")) {
            int track = request->getParam("track")->value().toInt();
            int step = request->getParam("step")->value().toInt();
            
            if (track >= 0 && track < MAX_TRACKS && step >= 0 && step < MAX_STEPS) {
                // Si se especifica valor, usarlo; sino, toggle
                if (request->hasParam("value")) {
                    int value = request->getParam("value")->value().toInt();
                    patterns[currentPattern].steps[track][step] = (value != 0);
                } else {
                    patterns[currentPattern].steps[track][step] = 
                        !patterns[currentPattern].steps[track][step];
                }
                
                needsGridUpdate = true;
                Serial.printf("► STEP toggled: T%d S%d = %s (Web)\n", 
                              track, step, 
                              patterns[currentPattern].steps[track][step] ? "ON" : "OFF");
            }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // Endpoint para obtener pattern completo
    server.on("/getpattern", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{\"pattern\":" + String(currentPattern) + ",\"data\":[";
        
        for (int track = 0; track < MAX_TRACKS; track++) {
            json += "[";
            for (int step = 0; step < MAX_STEPS; step++) {
                json += patterns[currentPattern].steps[track][step] ? "1" : "0";
                if (step < MAX_STEPS - 1) json += ",";
            }
            json += "]";
            if (track < MAX_TRACKS - 1) json += ",";
        }
        
        json += "]}";
        request->send(200, "application/json", json);
    });
    
    // ========== NUEVOS ENDPOINTS ==========
    // Control de volumen
    server.on("/volume", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("value")) {
            int newVolume = request->getParam("value")->value().toInt();
            volume = constrain(newVolume, 0, 30);
            
            // Enviar al MASTER
            JsonDocument doc;
            doc["cmd"] = "setVolume";
            doc["value"] = volume;
            sendUDPCommand(doc);
            
            Serial.printf("► VOLUME set: %d (Web)\n", volume);
        }
        request->send(200, "application/json", "{\"volume\":" + String(volume) + "}");
    });
    
    // Clear track (borrar todos los steps de un track)
    server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("track")) {
            int track = request->getParam("track")->value().toInt();
            if (track >= 0 && track < MAX_TRACKS) {
                for (int step = 0; step < MAX_STEPS; step++) {
                    patterns[currentPattern].steps[track][step] = false;
                }
                needsGridUpdate = true;
                Serial.printf("► TRACK %d cleared (Web)\n", track + 1);
            }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // Mute track (silenciar track - solo visual por ahora)
    server.on("/mute", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("track")) {
            int track = request->getParam("track")->value().toInt();
            if (track >= 0 && track < MAX_TRACKS) {
                // Toggle mute (implementar lógica si es necesario)
                Serial.printf("► TRACK %d mute toggled (Web)\n", track + 1);
            }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // Copy pattern
    server.on("/copy", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("from") && request->hasParam("to")) {
            int fromPattern = request->getParam("from")->value().toInt();
            int toPattern = request->getParam("to")->value().toInt();
            
            if (fromPattern >= 0 && fromPattern < MAX_PATTERNS && 
                toPattern >= 0 && toPattern < MAX_PATTERNS) {
                
                // Copiar todos los steps
                for (int track = 0; track < MAX_TRACKS; track++) {
                    for (int step = 0; step < MAX_STEPS; step++) {
                        patterns[toPattern].steps[track][step] = 
                            patterns[fromPattern].steps[track][step];
                    }
                }
                
                Serial.printf("► PATTERN %d copied to %d (Web)\n", fromPattern + 1, toPattern + 1);
            }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // Trigger sample (para Live Pads)
    server.on("/trigger", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("track")) {
            int track = request->getParam("track")->value().toInt();
            if (track >= 0 && track < MAX_TRACKS) {
                // Enviar trigger al MASTER
                triggerDrum(track);
                Serial.printf("► PAD %d triggered (Web)\n", track + 1);
            }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // Recibir patrón completo del MASTER (para sincronización)
    server.on("/syncpattern", HTTP_POST, [](AsyncWebServerRequest *request){
        // Este endpoint recibe el patrón completo del MASTER
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        // Parsear JSON con el patrón
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (!error) {
            int patternNum = doc["pattern"] | currentPattern;
            
            Serial.printf("\n► RECEIVING PATTERN %d FROM MASTER (%dx%d)...\n", 
                         patternNum + 1, MAX_TRACKS, MAX_STEPS);
            
            // Cargar patrón
            if (doc["data"].is<JsonArray>()) {
                JsonArray dataArray = doc["data"].as<JsonArray>();
                int t = 0;
                for (JsonArray trackData : dataArray) {
                    if (t < MAX_TRACKS) {
                        int s = 0;
                        for (bool stepValue : trackData) {
                            if (s < MAX_STEPS) {
                                patterns[patternNum].steps[t][s] = stepValue;
                                s++;
                            }
                        }
                        t++;
                    }
                }
            }
            
            // Imprimir patrón recibido para debug
            printReceivedPattern(patternNum);
            
            needsFullRedraw = true;
            Serial.println("✓ Pattern synchronized from MASTER");
        } else {
            Serial.printf("✗ JSON parse error: %s\n", error.c_str());
        }
    });
    
    // ========== ENDPOINTS DE DIAGNÓSTICO ==========
    // Información completa del sistema
    server.on("/diagnostic", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        
        // WiFi Stats
        json += "\"wifi\":{";
        json += "\"ssid\":\"" + String(WiFi.softAPSSID()) + "\"";
        json += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
        json += ",\"clients\":" + String(WiFi.softAPgetStationNum());
        json += ",\"mac\":\"" + WiFi.softAPmacAddress() + "\"";
        json += ",\"channel\":" + String(WiFi.channel());
        json += "},";
        
        // System Info
        json += "\"system\":{";
        json += "\"uptime\":" + String(millis() / 1000);
        json += ",\"freeHeap\":" + String(ESP.getFreeHeap());
        json += ",\"heapSize\":" + String(ESP.getHeapSize());
        json += ",\"chipModel\":\"" + String(ESP.getChipModel()) + "\"";
        json += ",\"cpuFreq\":" + String(ESP.getCpuFreqMHz());
        json += ",\"flashSize\":" + String(ESP.getFlashChipSize());
        json += "},";
        
        // Filesystem Info
        json += "\"filesystem\":{";
        json += "\"totalBytes\":" + String(LittleFS.totalBytes());
        json += ",\"usedBytes\":" + String(LittleFS.usedBytes());
        json += ",\"mounted\":" + String(LittleFS.begin() ? "true" : "false");
        json += "},";
        
        // Hardware Status
        json += "\"hardware\":{";
        json += "\"tft\":" + String(diagnostic.tftOk ? "true" : "false");
        json += ",\"tm1638_1\":" + String(diagnostic.tm1638_1_Ok ? "true" : "false");
        json += ",\"tm1638_2\":" + String(diagnostic.tm1638_2_Ok ? "true" : "false");
        json += ",\"encoder\":" + String(diagnostic.encoderOk ? "true" : "false");
        json += ",\"udpConnected\":" + String(diagnostic.udpConnected ? "true" : "false");
        json += "},";
        
        // RED808 Status
        json += "\"red808\":{";
        json += "\"version\":\"V6-SURFACE\"";
        json += ",\"bpm\":" + String(tempo);
        json += ",\"pattern\":" + String(currentPattern);
        json += ",\"kit\":" + String(currentKit);
        json += ",\"playing\":" + String(isPlaying ? "true" : "false");
        json += ",\"volume\":" + String(volume);
        json += "}";
        
        json += "}";
        request->send(200, "application/json", json);
    });
    
    // Info de sistema simplificada
    server.on("/system-info", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        json += "\"uptime\":" + String(millis() / 1000);
        json += ",\"freeHeap\":" + String(ESP.getFreeHeap());
        json += ",\"wifiClients\":" + String(WiFi.softAPgetStationNum());
        json += ",\"version\":\"RED808 V6-SURFACE\"";
        json += "}";
        request->send(200, "application/json", json);
    });
    
    // Iniciar servidor
    server.begin();
    webServerEnabled = true;
    Serial.println("► WebServer INICIADO en http://" + WiFi.softAPIP().toString());
    Serial.println("► Interfaz web profesional disponible");
}
*/
// Función setupWebServer() completamente comentada - SLAVE no requiere servidor web