#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <TM1638plus.h>
#include <M5ROTATE8.h>
#include <DFRobot_VisualRotaryEncoder.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Project headers
#include "debug_utils.h"    // Debug macros and utilities
#include "config.h"         // Configuration constants
#include "system_state.h"   // State structures

// ============================================
// LEGACY DEFINES (for backward compatibility)
// TODO: Gradually replace with Config:: namespace
// ============================================
#define MAX_STEPS Config::MAX_STEPS
#define MAX_TRACKS Config::MAX_TRACKS
#define MAX_PATTERNS Config::MAX_PATTERNS
#define MAX_KITS Config::MAX_KITS
#define MIN_BPM Config::MIN_BPM
#define MAX_BPM Config::MAX_BPM
#define DEFAULT_BPM Config::DEFAULT_BPM
#define DEFAULT_VOLUME Config::DEFAULT_VOLUME
#define MAX_VOLUME Config::MAX_VOLUME
#define MAX_SAMPLES Config::MAX_SAMPLES

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
// Colores para tracks 9-16
#define COLOR_INST_PERC1   0xFC00  // Naranja oscuro - Percussion 1
#define COLOR_INST_PERC2   0xAFE0  // Lima - Percussion 2
#define COLOR_INST_SHAKER  0x5BFF  // Azul medio - Shaker
#define COLOR_INST_COWBELL 0xFBE0  // Dorado - Cowbell
#define COLOR_INST_RIDE    0x9C1F  // Púrpura claro - Ride
#define COLOR_INST_CONGA   0xFB80  // Salmón - Conga
#define COLOR_INST_BONGO   0x4FE0  // Verde lima - Bongo
#define COLOR_INST_EXTRA   0xBDFF  // Celeste - Extra

// ============================================
// ENUMS (moved to system_state.h)
// ============================================
// Screen, DisplayMode, SequencerView, VolumeMode, EncoderMode
// are now defined in system_state.h

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
M5ROTATE8 m5encoders[Config::M5_ENCODER_MODULES] = {
    M5ROTATE8(Config::M5_ENCODER_ADDR_1),
    M5ROTATE8(Config::M5_ENCODER_ADDR_2)
};

// DFRobot Visual Rotary Encoders (6 unidades)
DFRobot_VisualRotaryEncoder_I2C* dfEncoders[Config::DFROBOT_ENCODER_COUNT] = {nullptr};
bool dfEncoderConnected[Config::DFROBOT_ENCODER_COUNT] = {false};
uint8_t dfEncoderConnectedCount = 0;
uint16_t dfEncoderValues[Config::DFROBOT_ENCODER_COUNT] = {0};
bool dfEncoderButtons[Config::DFROBOT_ENCODER_COUNT] = {false};
unsigned long lastDFEncoderRead = 0;

// RotaryEncoder encoder(ENCODER_CLK, ENCODER_DT, RotaryEncoder::LatchMode::TWO03);

// GLOBAL VARIABLES
// ============================================
Pattern patterns[MAX_PATTERNS];
DrumKit kits[MAX_KITS];
DiagnosticInfo diagnostic;

// ============================================
// STATE VARIABLES
// ============================================
// Sequencer State
int currentPattern = 0;
int currentKit = 0;
int currentStep = 0;
int tempo = DEFAULT_BPM;
bool isPlaying = false;
unsigned long lastStepTime = 0;
unsigned long stepInterval = 0;

// UI State
Screen currentScreen = SCREEN_SEQUENCER;
int menuSelection = 0;
int selectedTrack = 0;
bool needsFullRedraw = true;
bool needsHeaderUpdate = false;
bool needsGridUpdate = false;
int lastDisplayedStep = -1;
int lastToggledTrack = -1;

// Hardware State
volatile int encoderPos = 0;
int lastEncoderPos = 0;
volatile bool encoderChanged = false;
EncoderMode encoderMode = ENC_MODE_VOLUME;

// Audio State
int trackVolumes[MAX_TRACKS] = {100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100};
bool trackMuted[MAX_TRACKS] = {false};

// Network State
bool udpConnected = false;
unsigned long lastUdpCheck = 0;

// Servidor Web WiFi
// Configuración WiFi - SLAVE se conecta al MASTER
const char* ssid = "RED808";          // WiFi del MASTER
const char* password = "red808esp32"; // Password del MASTER
const char* masterIP = "192.168.4.1"; // IP del MASTER
const int udpPort = 8888;              // Puerto UDP del MASTER

// WiFi reconexion state
bool wifiReconnecting = false;  // true = WiFi.begin() lanzado, esperando resultado

WiFiUDP udp;

// Variables legacy que aún se necesitan (no están en structs)
const int menuItemCount = 6;  // LIVE, SEQUENCER, VOLUMES, FILTERS, SETTINGS, DIAGNOSTICS

// Rotary Encoder variables (sin biblioteca)
static uint8_t encoderState = 0;
static const int8_t encoderStates[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

// M5 8ENCODER variables
bool m5encoderConnected = false;  // Compatibilidad: true si al menos un módulo está conectado
bool m5encoderModuleConnected[Config::M5_ENCODER_MODULES] = {false, false};
bool i2cHubDetected = false;  // Auto-detectado en setup: true si hub I2C responde en I2C_HUB_ADDR
uint8_t i2cDevicesFound[32];  // Direcciones de dispositivos I2C encontrados en el scan
uint8_t i2cDeviceCount = 0;   // Cantidad de dispositivos I2C encontrados

// Canales auto-detectados del PCA9548A (0xFF = no encontrado)
uint8_t m5HubChannel[Config::M5_ENCODER_MODULES] = {0xFF, 0xFF};
uint8_t dfRobotHubChannel[Config::DFROBOT_ENCODER_COUNT] = {0xFF, 0xFF};
uint8_t encoderLEDColors[MAX_TRACKS][3] = {{0}};  // RGB de cada track (0-15)
unsigned long lastEncoderRead = 0;
unsigned long lastM5ButtonTime[MAX_TRACKS] = {0};  // Debounce por track
bool lastM5SwitchState[Config::M5_ENCODER_MODULES] = {false, false};
unsigned long lastM5SwitchTime[Config::M5_ENCODER_MODULES] = {0};

// Volume control - DUAL MODE (Sequencer / Live Pads)
// enum VolumeMode definido en system_state.h
VolumeMode volumeMode = VOL_SEQUENCER;  // Modo por defecto
int sequencerVolume = DEFAULT_VOLUME;  // 75%
int livePadsVolume = 100;  // 100% - más fuerte para tocar en vivo

int lastSequencerVolume = DEFAULT_VOLUME;
int lastLivePadsVolume = 100;
// lastVolumeRead ahora está en audioState
unsigned long lastVolumeRead = 0;

// Variables para optimizar pantalla VOLUMES (evitar parpadeo)
int lastSeqVolDisplay = -1;
int lastLiveVolDisplay = -1;
int lastTrackVolDisplay[MAX_TRACKS] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
bool lastTrackMuteDisplay[MAX_TRACKS] = {false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false};
VolumeMode lastVolModeDisplay = VOL_SEQUENCER;
bool volumesScreenInitialized = false;  // Flag para saber si ya se dibujó todo
unsigned long lastVolumesUpdate = 0;    // Throttling temporal
int volumesPage = 0;                    // 0 = tracks 1-8, 1 = tracks 9-16

// ============================================
// FILTER FX STATE
// ============================================
TrackFilter trackFilters[MAX_TRACKS];  // Filtros por track
TrackFilter masterFilter;             // Filtro para MASTER OUT (salida global)
int filterSelectedTrack = -1;         // -1 = MASTER OUT, 0-15 = tracks individuales
int filterSelectedFX = 0;             // Filtro FX seleccionado (0=DELAY, 1=FLANGER, 2=COMPRESSOR)
bool filterScreenInitialized = false; // Flag para primer dibujo
bool needsFilterBarsUpdate = false;   // Flag para actualizar SOLO barras (sin full redraw)
bool needsFilterPanelsRedraw = false; // Flag para redibujar paneles + título (sin fillScreen)
unsigned long lastFilterUpdate = 0;   // Throttling
int lastFilterTrackDisplay = -1;      // Cache para evitar parpadeo
int lastFilterFXDisplay = -1;
uint8_t lastFilterParams[3] = {0}; // Cache 1 param per FX
bool lastFilterEnabled = false;
unsigned long lastFilterToggleTime = 0;  // Debounce para toggle ON/OFF en FILTERS

// Lectura ADC compartida (una sola lectura por loop para todos los handlers)
int currentADCValue = 0;

unsigned long ledOffTime[16] = {0};  // 16 LEDs (2 x TM1638)
bool ledActive[16] = {false};         // 16 LEDs

int audioLevels[16] = {0};
bool padPressed[16] = {false};  // 16 pads presionados
unsigned long padPressTime[16] = {0};  // Tiempo de presión para tremolo (16 pads)
unsigned long lastVizUpdate = 0;

DisplayMode currentDisplayMode = DISPLAY_BPM;
unsigned long lastDisplayChange = 0;
int lastInstrumentPlayed = -1;
unsigned long instrumentDisplayTime = 0;

SequencerView sequencerView = SEQ_VIEW_GRID;

// Variables para manejo de botones (16 botones)
uint16_t lastButtonState = 0;
unsigned long buttonPressTime[16] = {0};
unsigned long lastRepeatTime[16] = {0};

// Paginación del sequencer (auto-scroll al navegar tracks)
int sequencerPage = 0;  // 0 = tracks 0-7, 1 = tracks 8-15

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
    "CL", "T1", "T2", "CY",
    "P1", "P2", "SH", "CB",
    "RD", "CG", "BG", "EX"
};

const char* instrumentNames[MAX_TRACKS] = {
    "KICK    ",
    "SNARE   ",
    "CL-HAT  ",
    "OP-HAT  ",
    "CLAP    ",
    "TOM-LO  ",
    "TOM-HI  ",
    "CYMBAL  ",
    "PERC-1  ",
    "PERC-2  ",
    "SHAKER  ",
    "COWBELL ",
    "RIDE    ",
    "CONGA   ",
    "BONGO   ",
    "EXTRA   "
};

const char* menuItems[] = {
    "LIVE PAD",
    "SEQUENCER",
    "VOLUMENES",
    "FILTERS FX",
    "SETTINGS",
    "DIAGNOSTICS"
};

// Colores por instrumento (BD=KICK, SD=SNARE, CH=CL-HAT, OH=OP-HAT, CL=CLAP, T1=TOM-LO, T2=TOM-HI, CY=CYMBAL)
const uint16_t instrumentColors[MAX_TRACKS] = {
    COLOR_INST_KICK,    // BD (Bass Drum = KICK)
    COLOR_INST_SNARE,   // SD (Snare Drum = SNARE)
    COLOR_INST_CLHAT,   // CH (Closed Hi-Hat = CL-HAT)
    COLOR_INST_OPHAT,   // OH (Open Hi-Hat = OP-HAT)
    COLOR_INST_CLAP,    // CL (Clap = CLAP)
    COLOR_INST_TOMLO,   // T1 (Tom Low = TOM-LO)
    COLOR_INST_TOMHI,   // T2 (Tom High = TOM-HI)
    COLOR_INST_CYMBAL,  // CY (Cymbal = CYMBAL)
    COLOR_INST_PERC1,   // P1 (Percussion 1)
    COLOR_INST_PERC2,   // P2 (Percussion 2)
    COLOR_INST_SHAKER,  // SH (Shaker)
    COLOR_INST_COWBELL, // CB (Cowbell)
    COLOR_INST_RIDE,    // RD (Ride)
    COLOR_INST_CONGA,   // CG (Conga)
    COLOR_INST_BONGO,   // BG (Bongo)
    COLOR_INST_EXTRA    // EX (Extra)
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
void handleBPMPot();
void handleM5Encoders();
void updateEncoderLEDs();
void readKeypad();  // Lectura única ADC por loop
void triggerDrum(int track);
void sendUDPCommand(const char* cmd);
void sendUDPCommand(JsonDocument& doc);
void receiveUDPData();
void requestPatternFromMaster();
bool attemptWiFiConnect(int maxWaitMs);
void startWiFiReconnect();
void checkWiFiReconnect();
void drawConsoleBootScreen();
void drawSpectrumAnimation();
void drawMainMenu();
void drawMenuItems(int oldSelection, int newSelection);
void drawLiveScreen();
void drawSequencerScreen();
void drawSettingsScreen();
void drawDiagnosticsScreen();
void drawEncoderTestScreen();
void drawPatternsScreen();
void drawVolumesScreen();  // Pantalla de volúmenes
void drawFiltersScreen();  // Pantalla de filtros FX
void drawFilterPanel(int fx, bool isSelected, uint8_t amount);  // Panel individual
void drawFilterTitleBar();  // Barra de título FILTERS
void sendFilterUDP(int track, int filterType);  // Enviar filtro al MASTER
void drawSinglePattern(int patternIndex, bool isSelected);
void drawSyncingScreen();
void drawHeader();
void updateHeaderValues();  // Actualización parcial sin parpadeo
void drawLivePad(int padIndex, bool highlight);
void drawSequencerCircularScreen();
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
bool getM5EncoderRoute(int track, int& moduleIndex, int& encoderIndex);
bool selectI2CHubChannel(uint8_t channel);
void deselectI2CHub();
bool selectM5EncoderModule(int moduleIndex);
bool selectDFRobotEncoder(int index);
void scanI2CBus();
void writeM5EncoderRGBForTrack(int track, uint8_t r, uint8_t g, uint8_t b);
void updateTrackEncoderLED(int track);
void initDFRobotEncoders();
void handleDFRobotEncoders();

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

bool getM5EncoderRoute(int track, int& moduleIndex, int& encoderIndex) {
    if (track < 0 || track >= MAX_TRACKS) return false;

    moduleIndex = track / Config::ENCODERS_PER_MODULE;
    encoderIndex = track % Config::ENCODERS_PER_MODULE;

    if (moduleIndex < 0 || moduleIndex >= Config::M5_ENCODER_MODULES) return false;
    if (!m5encoderModuleConnected[moduleIndex]) return false;

    return true;
}

bool selectI2CHubChannel(uint8_t channel) {
    if (!i2cHubDetected) return true;  // Sin hub: acceso directo al bus
    if (channel > 7) return false;

    Wire.beginTransmission(Config::I2C_HUB_ADDR);
    Wire.write(1 << channel);
    return (Wire.endTransmission() == 0);
}

// Desactivar todos los canales del hub (evitar crosstalk entre lecturas)
void deselectI2CHub() {
    if (!i2cHubDetected) return;
    Wire.beginTransmission(Config::I2C_HUB_ADDR);
    Wire.write(0);
    Wire.endTransmission();
}

bool selectM5EncoderModule(int moduleIndex) {
    if (moduleIndex < 0 || moduleIndex >= Config::M5_ENCODER_MODULES) return false;
    if (!i2cHubDetected) return true;  // Sin hub: acceso directo

    uint8_t ch = m5HubChannel[moduleIndex];
    if (ch == 0xFF) return false;  // No auto-detectado
    return selectI2CHubChannel(ch);
}

bool selectDFRobotEncoder(int index) {
    if (index < 0 || index >= Config::DFROBOT_ENCODER_COUNT) return false;
    if (!i2cHubDetected) return true;  // Sin hub: acceso directo

    uint8_t ch = dfRobotHubChannel[index];
    if (ch == 0xFF) return false;  // No auto-detectado
    return selectI2CHubChannel(ch);
}

void scanI2CBus() {
    Serial.println("► I2C scan:");
    i2cDeviceCount = 0;

    // Reset auto-detect arrays
    for (int i = 0; i < Config::M5_ENCODER_MODULES; i++) m5HubChannel[i] = 0xFF;
    for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) dfRobotHubChannel[i] = 0xFF;

    Wire.flush();
    delay(10);

    // Auto-detectar hub I2C activo
    Wire.beginTransmission(Config::I2C_HUB_ADDR);
    i2cHubDetected = (Wire.endTransmission() == 0);

    if (i2cHubDetected) {
        Serial.printf("   Hub ACTIVO (PCA9548A) @ 0x%02X\n", Config::I2C_HUB_ADDR);

        int m5Found = 0;
        int dfFound = 0;

        for (uint8_t ch = 0; ch < 8; ch++) {
            if (!selectI2CHubChannel(ch)) continue;
            delay(2);

            // Buscar M5 Encoder8 (0x41)
            Wire.beginTransmission(Config::M5_ENCODER_ADDR_1);
            if (Wire.endTransmission() == 0) {
                if (i2cDeviceCount < 32) i2cDevicesFound[i2cDeviceCount++] = Config::M5_ENCODER_ADDR_1;
                if (m5Found < Config::M5_ENCODER_MODULES) {
                    m5HubChannel[m5Found] = ch;
                    Serial.printf("   - CH%d: 0x%02X [M5 Encoder8 #%d]\n", ch, Config::M5_ENCODER_ADDR_1, m5Found + 1);
                    m5Found++;
                }
            }

            // Buscar DFRobot SEN0502 (0x54-0x57)
            for (uint8_t addr = 0x54; addr <= 0x57; addr++) {
                Wire.beginTransmission(addr);
                if (Wire.endTransmission() == 0) {
                    if (i2cDeviceCount < 32) i2cDevicesFound[i2cDeviceCount++] = addr;
                    if (dfFound < Config::DFROBOT_ENCODER_COUNT) {
                        dfRobotHubChannel[dfFound] = ch;
                        Serial.printf("   - CH%d: 0x%02X [DFRobot SEN0502 #%d]\n", ch, addr, dfFound + 1);
                        dfFound++;
                    }
                }
            }

            delay(1);
        }
        deselectI2CHub();

        // Resumen auto-detect
        Serial.printf("   Auto-detect: %d M5, %d DFRobot\n", m5Found, dfFound);
        for (int i = 0; i < Config::M5_ENCODER_MODULES; i++) {
            if (m5HubChannel[i] != 0xFF)
                Serial.printf("   → M5 #%d → CH%d\n", i + 1, m5HubChannel[i]);
        }
        for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) {
            if (dfRobotHubChannel[i] != 0xFF)
                Serial.printf("   → DFRobot #%d → CH%d\n", i + 1, dfRobotHubChannel[i]);
        }
    } else {
        Serial.printf("   Hub PASIVO (sin TCA9548A @ 0x%02X)\n", Config::I2C_HUB_ADDR);
        const uint8_t knownAddrs[] = {0x41, 0x42, 0x54, 0x55, 0x56, 0x57};
        for (uint8_t k = 0; k < 6; k++) {
            Wire.beginTransmission(knownAddrs[k]);
            if (Wire.endTransmission() == 0) {
                if (i2cDeviceCount < 32) i2cDevicesFound[i2cDeviceCount++] = knownAddrs[k];
                Serial.printf("   - 0x%02X\n", knownAddrs[k]);
            }
            delay(2);
        }
    }
    Serial.printf("   Total: %d dispositivos encontrados\n", i2cDeviceCount);
}

// ============================================
// DFROBOT VISUAL ROTARY ENCODER FUNCTIONS
// ============================================
void initDFRobotEncoders() {
    Serial.println("► DFRobot Visual Rotary Encoder Init...");
    dfEncoderConnectedCount = 0;

    for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) {
        uint8_t addr = Config::DFROBOT_ENCODER_ADDRS[i];

        // Con hub pasivo: evitar duplicados (misma dirección = mismo dispositivo)
        // Con hub activo: cada encoder tiene su propio canal, duplicados OK
        if (!i2cHubDetected) {
            bool addrAlreadyUsed = false;
            for (int j = 0; j < i; j++) {
                if (dfEncoderConnected[j] && Config::DFROBOT_ENCODER_ADDRS[j] == addr) {
                    addrAlreadyUsed = true;
                    break;
                }
            }
            if (addrAlreadyUsed) {
                dfEncoderConnected[i] = false;
                Serial.printf("   DFRobot #%d @ 0x%02X: SKIP (misma dir sin hub activo)\n", i + 1, addr);
                continue;
            }
        }

        // Seleccionar canal del hub antes de inicializar
        if (!selectDFRobotEncoder(i)) {
            dfEncoderConnected[i] = false;
            Serial.printf("   DFRobot #%d @ 0x%02X: HUB CH ERROR\n", i + 1, addr);
            continue;
        }

        dfEncoders[i] = new DFRobot_VisualRotaryEncoder_I2C(addr, &Wire);
        delay(5);
        if (dfEncoders[i]->begin() == 0) {
            dfEncoderConnected[i] = true;
            dfEncoderConnectedCount++;
            dfEncoders[i]->setGainCoefficient(51);
            dfEncoderValues[i] = dfEncoders[i]->getEncoderValue();
            Serial.printf("   DFRobot #%d @ 0x%02X (CH%d): OK (val=%d)\n", 
                         i + 1, addr, dfRobotHubChannel[i], dfEncoderValues[i]);
        } else {
            dfEncoderConnected[i] = false;
            delete dfEncoders[i];
            dfEncoders[i] = nullptr;
            Serial.printf("   DFRobot #%d @ 0x%02X (CH%d): NOT FOUND\n", 
                         i + 1, addr, dfRobotHubChannel[i]);
        }
        deselectI2CHub();
    }
    Serial.printf("   %d/%d DFRobot encoders connected\n", dfEncoderConnectedCount, Config::DFROBOT_ENCODER_COUNT);
}

void handleDFRobotEncoders() {
    if (dfEncoderConnectedCount == 0) return;

    unsigned long now = millis();
    if (now - lastDFEncoderRead < Config::ENCODER_READ_INTERVAL) return;
    lastDFEncoderRead = now;

    for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) {
        if (!dfEncoderConnected[i] || !dfEncoders[i]) continue;

        // Seleccionar canal del hub antes de leer
        if (!selectDFRobotEncoder(i)) continue;

        uint16_t newVal = dfEncoders[i]->getEncoderValue();
        bool btn = dfEncoders[i]->detectButtonDown();

        deselectI2CHub();  // Liberar bus después de leer

        if (newVal != dfEncoderValues[i]) {
            int16_t delta = (int16_t)newVal - (int16_t)dfEncoderValues[i];
            dfEncoderValues[i] = newVal;

            Serial.printf("► DFRobot #%d: val=%d delta=%d\n", i + 1, newVal, delta);

            if (i == 0) {
                // #1: FX — ajustar amount del FX seleccionado
                TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
                uint8_t* param;
                const char* fxName;
                switch (filterSelectedFX) {
                    case FILTER_DELAY:      param = &f.delayAmount;   fxName = "DELAY";    break;
                    case FILTER_FLANGER:    param = &f.flangerAmount; fxName = "FLANGER";  break;
                    default:                param = &f.compAmount;    fxName = "COMPRESS"; break;
                }
                int newVal2 = constrain((int)*param + delta * 2, 0, 127);
                *param = (uint8_t)newVal2;
                sendFilterUDP(filterSelectedTrack, filterSelectedFX);
                Serial.printf("► FX %s param: %d\n", fxName, newVal2);
                needsFilterBarsUpdate = true;
                needsHeaderUpdate = true;
            } else {
                // #2: Pattern select
                changePattern(delta > 0 ? 1 : -1);
            }
        }

        // Todos los SEN0502 tienen switch
        if (btn != dfEncoderButtons[i]) {
            dfEncoderButtons[i] = btn;
            if (btn) {
                Serial.printf("► DFRobot #%d BTN pressed\n", i + 1);
                if (i == 0) {
                    // #1 btn: ciclar FX (DELAY → FLANGER → COMPRESSOR)
                    filterSelectedFX = (filterSelectedFX + 1) % FILTER_COUNT;
                    const char* fxNames[] = {"DELAY   ", "FLANGER ", "COMPRESS"};
                    tm1.displayText(fxNames[filterSelectedFX]);
                    Serial.printf("► FX changed to: %s\n", fxNames[filterSelectedFX]);
                    needsFilterBarsUpdate = true;
                } else {
                    // #2 btn: reset al patrón 0
                    changePattern(-currentPattern);
                }
            }
        }
    }
}

void writeM5EncoderRGBForTrack(int track, uint8_t r, uint8_t g, uint8_t b) {
    int moduleIndex = 0;
    int encoderIndex = 0;
    if (!getM5EncoderRoute(track, moduleIndex, encoderIndex)) return;
    if (!selectM5EncoderModule(moduleIndex)) return;
    m5encoders[moduleIndex].writeRGB(encoderIndex, r, g, b);
}

void updateTrackEncoderLED(int track) {
    if (track < 0 || track >= MAX_TRACKS) return;
    int moduleIndex = 0;
    int encoderIndex = 0;
    if (!getM5EncoderRoute(track, moduleIndex, encoderIndex)) return;
    if (!selectM5EncoderModule(moduleIndex)) return;

    if (trackMuted[track]) {
        m5encoders[moduleIndex].writeRGB(encoderIndex, 30, 0, 0);  // Rojo tenue = muteado
        return;
    }

    uint8_t brightness = map(trackVolumes[track], 0, MAX_VOLUME, 10, 255);
    uint8_t r = (encoderLEDColors[track][0] * brightness) / 255;
    uint8_t g = (encoderLEDColors[track][1] * brightness) / 255;
    uint8_t b = (encoderLEDColors[track][2] * brightness) / 255;
    m5encoders[moduleIndex].writeRGB(encoderIndex, r, g, b);
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

// Función de reconexión WiFi BLOQUEANTE (solo para botón S3 / setup)
bool attemptWiFiConnect(int maxWaitMs = 8000) {
    Serial.println("[WiFi] Attempting connection (blocking)...");
    
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < (unsigned long)maxWaitMs) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    
    return (WiFi.status() == WL_CONNECTED);
}

// Iniciar reconexión NON-BLOCKING (para usar desde el loop)
void startWiFiReconnect() {
    Serial.println("[WiFi] Starting non-blocking reconnect...");
    WiFi.disconnect(false);
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    wifiReconnecting = true;
}

void setupWiFiAndUDP() {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║   CHECKING MASTER CONNECTION       ║");
    Serial.println("╚════════════════════════════════════╝");
    Serial.printf("  SSID: %s\n", ssid);
    Serial.printf("  Master IP: %s\n", masterIP);
    Serial.printf("  UDP Port: %d\n", udpPort);
    
    // WiFi.begin() ya fue llamado al inicio del setup() (early start)
    // Comprobar si ya conectó durante las animaciones, si no esperar un poco
    Serial.println("\n[1/2] Checking WiFi (started during boot)...");
    
    bool connected = (WiFi.status() == WL_CONNECTED);
    if (!connected) {
        // Si aún no conectó, intentar bloqueante (estamos en setup, no hay problema)
        Serial.println("  Not yet connected, trying blocking connect...");
        connected = attemptWiFiConnect(8000);
    }
    
    if (connected) {
        Serial.println("✓ WiFi connected!");
        Serial.print("  IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.printf("  Signal strength: %d dBm\n", WiFi.RSSI());
        Serial.printf("  Channel: %d\n", WiFi.channel());
        Serial.printf("  MAC Address: %s\n", WiFi.macAddress().c_str());
        
        // PASO 2: Inicializar UDP y enviar hello
        Serial.println("\n[2/2] Initializing UDP communication...");
        udp.begin(udpPort);
        udpConnected = true;
        diagnostic.udpConnected = true;
        
        // Enviar hello al master
        JsonDocument doc;
        doc["cmd"] = "hello";
        doc["device"] = "SURFACE";
        sendUDPCommand(doc);
        
        Serial.println("✓ UDP initialized - Ready to send commands");
        
        // Sonido de confirmación (1 solo trigger, no 3 con delays)
        {
            JsonDocument triggerDoc;
            triggerDoc["cmd"] = "trigger";
            triggerDoc["track"] = 4;  // CLAP
            sendUDPCommand(triggerDoc);
        }
        Serial.println("♪ Connection confirmed with audio feedback");
        
        // Solicitar patrón actual al MASTER automáticamente
        delay(50);
        requestPatternFromMaster();
        Serial.println("► Auto-requesting pattern from MASTER...");
    } else {
        Serial.println("✗ WiFi connection FAILED");
        Serial.printf("  WiFi Status: %d\n", WiFi.status());
        
        // Hacer scan SOLO si falló, para diagnóstico
        Serial.println("  Scanning networks for diagnostics...");
        int n = WiFi.scanNetworks(false, false, false, 300);  // Scan rápido 300ms/canal
        bool masterFound = false;
        for (int i = 0; i < n; i++) {
            String foundSSID = WiFi.SSID(i);
            Serial.printf("    %d: %s (RSSI: %d)\n", i + 1, foundSSID.c_str(), WiFi.RSSI(i));
            if (foundSSID == ssid) masterFound = true;
        }
        WiFi.scanDelete();  // Liberar memoria del scan
        
        Serial.println("\n  TROUBLESHOOTING:");
        if (!masterFound) {
            Serial.println("    -> MASTER not detected in WiFi scan");
            Serial.println("    -> Verify MASTER device is powered ON");
        } else {
            Serial.println("    -> MASTER visible but connection failed");
            Serial.println("    -> Check WiFi password");
            Serial.println("    -> Try moving devices closer");
        }
        Serial.println("  Will auto-retry every 5 seconds in background\n");
        udpConnected = false;
        diagnostic.udpConnected = false;
        diagnostic.lastError = masterFound ? "Connection timeout" : "Master not found";
        lastUdpCheck = millis();
    }
}

// Reconexión automática NON-BLOCKING (llamar desde loop)
// NUNCA bloquea el loop - solo comprueba estado y lanza WiFi.begin() si toca
void checkWiFiReconnect() {
    unsigned long now = millis();
    
    // Si ya está conectado, verificar que siga vivo
    if (udpConnected) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("✗ WiFi LOST! Marking as disconnected.");
            udpConnected = false;
            diagnostic.udpConnected = false;
            diagnostic.lastError = "WiFi disconnected";
            lastUdpCheck = now;
            wifiReconnecting = false;
        }
        return;
    }
    
    // Si ya lanzamos WiFi.begin(), solo comprobar si conectó
    if (wifiReconnecting) {
        if (WiFi.status() == WL_CONNECTED) {
            // ¡Conectó!
            Serial.println("✓ Reconnected to MASTER!");
            Serial.printf("  IP: %s  RSSI: %d dBm\n", 
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            
            udp.begin(udpPort);
            udpConnected = true;
            diagnostic.udpConnected = true;
            diagnostic.lastError = "";
            wifiReconnecting = false;
            
            JsonDocument doc;
            doc["cmd"] = "hello";
            doc["device"] = "SURFACE";
            sendUDPCommand(doc);
            
            delay(50);
            requestPatternFromMaster();
            needsHeaderUpdate = true;
            
        } else if (now - lastUdpCheck > 10000) {
            // Llevamos 10s esperando, reintentar
            Serial.println("  WiFi reconnect timeout, retrying...");
            wifiReconnecting = false;
            lastUdpCheck = now;
        }
        return;
    }
    
    // Lanzar nuevo intento cada 8 segundos (NON-BLOCKING)
    if (now - lastUdpCheck < 8000) return;
    lastUdpCheck = now;
    
    Serial.println("\n► Auto-reconnecting to MASTER (non-blocking)...");
    startWiFiReconnect();
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
        static char incomingPacket[512];  // static = no usa stack
        int len = udp.read(incomingPacket, sizeof(incomingPacket) - 1);
        if (len > 0) {
            incomingPacket[len] = 0;  // Null terminator
            
            Serial.printf("\n◄ UDP RECEIVED (%d bytes): %s\n", len, incomingPacket);
            
            // Parsear JSON - CORRECCIÓN: Buffer de 4KB
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
                    
                    // CORRECCIÓN: Validar índice de patrón
                    if (patternNum < 0 || patternNum >= MAX_PATTERNS) {
                        Serial.printf("✗ Invalid pattern index: %d (valid: 0-%d)\n", patternNum, MAX_PATTERNS-1);
                        return;
                    }
                    
                    Serial.println("\n═══════════════════════════════════════");
                    Serial.printf("► PATTERN SYNC RECEIVED: Pattern %d\n", patternNum + 1);
                    Serial.println("═══════════════════════════════════════");
                    
                    // Parsear array de steps (flexible: acepta de 1 a MAX_TRACKS)
                    JsonArray data = doc["data"];
                    if (data && data.size() > 0) {
                        int totalStepsActive = 0;
                        int tracksReceived = min((int)data.size(), (int)MAX_TRACKS);
                        
                        Serial.printf("► Tracks received: %d/%d\n", tracksReceived, (int)MAX_TRACKS);
                        
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
                                int stepsInTrack = min((int)trackData.size(), (int)MAX_STEPS);
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
                        
                        // Forzar redibujado solo si estamos en pantallas que lo necesitan
                        if (patternNum == currentPattern) {
                            needsGridUpdate = true;
                            if (currentScreen != SCREEN_FILTERS) {
                                needsFullRedraw = true;
                            }
                            if (currentScreen == SCREEN_SEQUENCER && !isPlaying) {
                                updateStepLEDsForTrack(selectedTrack);
                            }
                            Serial.println("► Pattern synced");
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
    
    // CORRECCIÓN: Validar patrón actual
    if (currentPattern < 0 || currentPattern >= MAX_PATTERNS) {
        Serial.printf("✗ Invalid pattern: %d\n", currentPattern);
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
// enableWebServer() y disableWebServer() - DESHABILITADOS en SLAVE
// SD Card setup y WebServer removidos - no se usan en modo SLAVE

// ============================================
// SETUP
// ============================================
void setup() {
    Serial.begin(115200);
    delay(300);
    
    Serial.println("\n\n╔════════════════════════════════════╗");
    Serial.println("║   RED808 V6 - SURFACE (SLAVE)     ║");
    Serial.println("║   UDP Controller via WiFi          ║");
    Serial.println("║   Connects to MASTER DrumMachine   ║");
    Serial.println("╚════════════════════════════════════╝\n");

    // *** WIFI EARLY START - iniciar conexión ANTES de las animaciones ***
    // Así WiFi conecta en background mientras se ejecutan TFT, TM1638, LEDs, etc.
    Serial.println("► WiFi Early Start (non-blocking)...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    Serial.printf("  SSID: %s | Target: %s:%d\n", ssid, masterIP, udpPort);
    Serial.println("  Connecting while boot animations run...\n");

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
    
    // Rotary Angle Potentiometer 2 (Live Pads Volume) - pin 39/VN (ADC1)
    pinMode(ROTARY_ANGLE_PIN2, INPUT);
    Serial.println("► Rotary Pot 2 (PADS Volume) ready on pin 39 (VN)");
    
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
    
    // Inicializar buffer ADC de los 3 potenciómetros
    pinMode(ROTARY_ANGLE_PIN, INPUT);
    pinMode(ROTARY_ANGLE_PIN2, INPUT);
    pinMode(BPM_POT_PIN, INPUT);
    Serial.print("Calibrating pots... ");
    for (int i = 0; i < 10; i++) {
        analogRead(ROTARY_ANGLE_PIN);
        analogRead(ROTARY_ANGLE_PIN2);
        analogRead(BPM_POT_PIN);
        delay(10);
    }
    Serial.println("OK (SEQ pin 35, PADS pin 39/VN, BPM pin 36/VP)");
    
    // I2C Init para M5 8ENCODER
    Serial.print("► I2C Init (SDA:21, SCL:22)... ");
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setTimeOut(100);  // 100ms timeout para evitar cuelgues I2C
    Wire.setClock(100000);  // 100kHz - más estable con hubs pasivos y cables largos
    Serial.println("OK");
    scanI2CBus();
    
    // M5 8ENCODER Init (hasta 2 módulos en el mismo bus I2C)
    Serial.println("► M5 8ENCODER Init (multi-module)...");
    int connectedModules = 0;

    for (int module = 0; module < Config::M5_ENCODER_MODULES; module++) {
        const int moduleAddr = (module == 0) ? Config::M5_ENCODER_ADDR_1 : Config::M5_ENCODER_ADDR_2;
        Serial.printf("   Module %d @ 0x%02X... ", module + 1, moduleAddr);

        if (!selectM5EncoderModule(module)) {
            m5encoderModuleConnected[module] = false;
            Serial.println("HUB CH ERROR");
            continue;
        }

        if (m5encoders[module].begin()) {
            m5encoderModuleConnected[module] = true;
            connectedModules++;
            Serial.println("OK");
            Serial.printf("      Firmware version: %d\n", m5encoders[module].getVersion());
        } else {
            m5encoderModuleConnected[module] = false;
            Serial.println("NOT FOUND");
        }
    }

    m5encoderConnected = (connectedModules > 0);
    diagnostic.m5encoderOk = m5encoderConnected;

    if (m5encoderConnected) {
        Serial.printf("   %d/%d modules connected. Running LED boot sequence...\n",
                      connectedModules, Config::M5_ENCODER_MODULES);

        for (int module = 0; module < Config::M5_ENCODER_MODULES; module++) {
            if (!m5encoderModuleConnected[module]) continue;
            if (!selectM5EncoderModule(module)) continue;

            // 1. Apagar todos los LEDs del módulo
            m5encoders[module].allOff();
            delay(120);

            // 2. Todos en rojo
            for (int i = 0; i < 9; i++) {
                m5encoders[module].writeRGB(i, 255, 0, 0);
            }
            delay(220);

            // 3. Transición a colores de instrumentos para tracks de este módulo
            const int trackStart = module * Config::ENCODERS_PER_MODULE;
            const int trackEnd = min(trackStart + (int)Config::ENCODERS_PER_MODULE, (int)MAX_TRACKS);
            for (int track = trackStart; track < trackEnd; track++) {
                const int localEncoder = track - trackStart;
                uint16_t color = instrumentColors[track];

                // Convertir RGB565 a RGB888
                uint8_t r = ((color >> 11) & 0x1F) << 3;
                uint8_t g = ((color >> 5) & 0x3F) << 2;
                uint8_t b = (color & 0x1F) << 3;

                m5encoders[module].writeRGB(localEncoder, r, g, b);
                encoderLEDColors[track][0] = r;
                encoderLEDColors[track][1] = g;
                encoderLEDColors[track][2] = b;

                m5encoders[module].setAbsCounter(localEncoder, 100);  // Volumen inicial
                delay(40);
            }

            // LED extra (switch) apagado
            m5encoders[module].writeRGB(8, 0, 0, 0);
        }

        Serial.println("   LED boot sequence complete!");
    } else {
        Serial.println("   No M5 8ENCODER modules found (optional)");
    }
    
    // DFRobot Visual Rotary Encoders Init
    initDFRobotEncoders();
    
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
    
    // LED test rápido (16 LEDs)
    for (int i = 0; i < 16; i++) {
        setLED(i, true);
        delay(30);
    }
    delay(100);
    setAllLEDs(0x0000);
    
    // Spectrum Analyzer Animation
    drawSpectrumAnimation();
    
    if (udpConnected) {
        tm1.displayText("SURFACE ");
        tm2.displayText("ENLACE  ");
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
    
    // DFRobot Visual Rotary Encoders
    handleDFRobotEncoders();
    
    // Lectura única ADC para botonera analógica (3 botones)
    readKeypad();
    
    // Procesar los 3 botones analógicos
    handlePlayStopButton();
    handleMuteButton();
    handleBackButton();
    
    if (currentTime - lastVolumeRead > 100) {
        handleVolume();
        handleBPMPot();
        lastVolumeRead = currentTime;
    }
    
    if (isPlaying) {
        updateSequencer();
    }
    
    // Recibir datos UDP del MASTER (solo si conectado)
    if (udpConnected) {
        receiveUDPData();
    }
    
    // Auto-reconexión WiFi si se perdió o falló en boot
    checkWiFiReconnect();
    
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
            case SCREEN_VOLUMES:
                drawVolumesScreen();
                break;
            case SCREEN_FILTERS:
                drawFiltersScreen();
                break;
            case SCREEN_ENCODER_TEST:
                drawEncoderTestScreen();
                break;
            default:
                break;
        }
        
        needsFullRedraw = false;
        needsHeaderUpdate = false;
        needsGridUpdate = false;
        
    } else {
        // Actualización parcial cuando NO es full redraw
        if (currentScreen == SCREEN_VOLUMES) {
            drawVolumesScreen();
        }
        if (currentScreen == SCREEN_FILTERS) {
            drawFiltersScreen();
        }
        if (currentScreen == SCREEN_ENCODER_TEST) {
            drawEncoderTestScreen();
        }
        
        if (needsHeaderUpdate && currentScreen != SCREEN_MENU) {
            updateHeaderValues();  // Solo repinta valores que cambiaron
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
        
        // Usar stepInterval global (calculada por calculateStepInterval())
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
    int boostedVolume = min((int)(livePadsVolume * 1.25), (int)MAX_VOLUME);
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
    if (isPlaying && currentScreen != SCREEN_FILTERS) {
        // Durante reproducción: solo mostrar step actual (EXCEPTO en pantalla FILTERS)
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
    
    // Procesar nuevas presiones (16 botones en total)
    for (int i = 0; i < 16; i++) {
        if (newPress & (1 << i)) {
            buttonPressTime[i] = currentTime;
            lastRepeatTime[i] = currentTime;
            
            if (currentScreen == SCREEN_LIVE) {
                // S1-S16: Enviar trigger al MASTER (16 pads)
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
                if (i == 0 || i == 1) {
                    // S1-S2: Vista del sequencer
                    SequencerView newView = (i == 0) ? SEQ_VIEW_GRID : SEQ_VIEW_CIRCULAR;
                    if (newView != sequencerView) {
                        sequencerView = newView;
                        drawSettingsScreen();
                    }
                } else if (i == 2) {
                    // S3: CONNECT MASTER (buscar y conectar WiFi/UDP)
                    Serial.println("\n► S3: CONNECT TO MASTER...");
                    drawSyncingScreen();
                    setupWiFiAndUDP();
                    needsFullRedraw = true;
                } else if (i >= 4 && i < 4 + THEME_COUNT && (i - 4) < THEME_COUNT) {
                    // S5-S8: Cambiar theme (local)
                    int newTheme = i - 4;
                    if (newTheme != currentTheme) {
                        changeTheme(newTheme - currentTheme);
                        drawSettingsScreen();
                    }
                }
            } else if (currentScreen == SCREEN_SEQUENCER) {
                // Permitir edición en tiempo real incluso en play (S1-S16)
                if (i >= 0 && i < MAX_STEPS) {
                    toggleStep(selectedTrack, i);
                    updateStepLEDsForTrack(selectedTrack);
                    needsGridUpdate = true;  // Solo actualizar grid, no full redraw
                }
            } else if (currentScreen == SCREEN_VOLUMES) {
                // S1-S16: seleccionar track directo para editar volumen/mute con encoder8
                if (i >= 0 && i < MAX_TRACKS) {
                    selectedTrack = i;
                    volumesPage = selectedTrack / Config::TRACKS_PER_PAGE;
                    sequencerPage = volumesPage;
                    needsFullRedraw = true;
                    showInstrumentOnTM1638(selectedTrack);
                    Serial.printf("► VOLUME Track select: %d/%d (%s) Page:%d\n",
                                  selectedTrack + 1, MAX_TRACKS, trackNames[selectedTrack], volumesPage + 1);
                }
            } else if (currentScreen == SCREEN_FILTERS) {
                // S1-S3: Seleccionar tipo de filtro FX
                if (i < FILTER_COUNT) {
                    if (filterSelectedFX != i) {
                        filterSelectedFX = i;
                        needsFilterPanelsRedraw = true;
                    }
                    const char* fxNames[] = {"DELAY   ", "FLANGER ", "COMPRESS"};
                    tm1.displayText(fxNames[filterSelectedFX]);
                    TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
                    char statusStr[9];
                    snprintf(statusStr, sizeof(statusStr), f.enabled ? "  ON    " : "  OFF   ");
                    tm2.displayText(statusStr);
                    Serial.printf("► FX: %s\n", fxNames[filterSelectedFX]);
                }
                // S4: MASTER OUT - seleccionar o toggle ON/OFF si ya seleccionado
                else if (i == 3) {
                    if (filterSelectedTrack != -1) {
                        // Primera pulsación: seleccionar MASTER
                        filterSelectedTrack = -1;
                        needsFilterPanelsRedraw = true;
                        tm1.displayText("MASTER  ");
                        tm2.displayText("FX OUT  ");
                        setAllLEDs(0xFFFF);
                        Serial.println("► Target: MASTER OUT");
                    } else if (currentTime - lastFilterToggleTime > 250) {
                        // Ya en MASTER: toggle ON/OFF (con debounce 250ms)
                        lastFilterToggleTime = currentTime;
                        masterFilter.enabled = !masterFilter.enabled;
                        for (int fxi = 0; fxi < FILTER_COUNT; fxi++) {
                            sendFilterUDP(-1, fxi);
                        }
                        tm1.displayText("MASTER  ");
                        tm2.displayText(masterFilter.enabled ? "  ON    " : "  OFF   ");
                        needsFilterBarsUpdate = true;
                        Serial.printf("► MASTER FX: %s\n", masterFilter.enabled ? "ON" : "OFF");
                    }
                }
                // S5-S16: Seleccionar track 1-12, o toggle ON/OFF si ya seleccionado
                else if (i >= 4 && i < 16) {
                    int newTrack = i - 4;
                    if (newTrack < MAX_TRACKS) {
                        if (filterSelectedTrack != newTrack) {
                            // Primera pulsación: seleccionar track
                            filterSelectedTrack = newTrack;
                            needsFilterPanelsRedraw = true;
                            tm1.displayText(trackNames[filterSelectedTrack]);
                            char fxStr[9];
                            snprintf(fxStr, sizeof(fxStr), "FX TR%2d ", filterSelectedTrack + 1);
                            tm2.displayText(fxStr);
                            setAllLEDs(0x0000);
                            setLED(filterSelectedTrack, true);
                            Serial.printf("► Target: Track %d (%s)\n", filterSelectedTrack + 1, trackNames[filterSelectedTrack]);
                        } else {
                            // Ya seleccionado: toggle ON/OFF
                            trackFilters[newTrack].enabled = !trackFilters[newTrack].enabled;
                            sendFilterUDP(newTrack, filterSelectedFX);
                            tm1.displayText(trackNames[newTrack]);
                            tm2.displayText(trackFilters[newTrack].enabled ? "  ON    " : "  OFF   ");
                            needsFilterBarsUpdate = true;
                            Serial.printf("► Track %d FX: %s\n", newTrack + 1, trackFilters[newTrack].enabled ? "ON" : "OFF");
                        }
                    }
                }
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
    bool backPressed = (currentADCValue >= BTN_BACK_MIN && currentADCValue <= BTN_BACK_MAX);
    
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
                    
                } else if (currentScreen == SCREEN_FILTERS) {
                    // En FILTERS: encoder selecciona target (-1=MASTER, 0-7=tracks)
                    int delta = rawDelta / 2;
                    if (delta != 0) {
                        int oldTrack = filterSelectedTrack;
                        filterSelectedTrack += delta;
                        // Rango: -1 (MASTER) a MAX_TRACKS-1 (track 8)
                        if (filterSelectedTrack < -1) filterSelectedTrack = MAX_TRACKS - 1;
                        if (filterSelectedTrack >= MAX_TRACKS) filterSelectedTrack = -1;
                        
                        if (oldTrack != filterSelectedTrack) {
                            // Mostrar en TM1638
                            if (filterSelectedTrack == -1) {
                                tm1.displayText("MASTER  ");
                                tm2.displayText("FX OUT  ");
                                setAllLEDs(0xFFFF);
                            } else {
                                tm1.displayText(trackNames[filterSelectedTrack]);
                                char fxStr[9];
                                snprintf(fxStr, sizeof(fxStr), "FX TR %d ", filterSelectedTrack + 1);
                                tm2.displayText(fxStr);
                                setAllLEDs(0x0000);
                                setLED(filterSelectedTrack, true);
                            }
                            needsFilterPanelsRedraw = true;
                            Serial.printf("► Target: %s\n", 
                                filterSelectedTrack == -1 ? "MASTER" : trackNames[filterSelectedTrack]);
                        }
                    }
                    
                } else if (currentScreen == SCREEN_SEQUENCER) {
                    // En sequencer: navegación entre 16 tracks con auto-paginación
                    int delta = rawDelta / 2;
                    if (delta != 0) {
                        selectedTrack += delta;
                        if (selectedTrack < 0) selectedTrack = MAX_TRACKS - 1;
                        if (selectedTrack >= MAX_TRACKS) selectedTrack = 0;
                        
                        // Auto-paginación: cambiar página si el track sale del rango visible
                        int newPage = selectedTrack / Config::TRACKS_PER_PAGE;
                        if (newPage != sequencerPage) {
                            sequencerPage = newPage;
                            needsFullRedraw = true;  // Redibujar toda la pantalla al cambiar página
                        }
                        
                        // Mostrar instrumento en TM1638
                        showInstrumentOnTM1638(selectedTrack);
                        
                        // Actualizar LEDs para mostrar steps del track seleccionado
                        if (!isPlaying) {
                            updateStepLEDsForTrack(selectedTrack);
                        }
                        
                        needsHeaderUpdate = true;
                        needsGridUpdate = true;
                        Serial.printf("► Track: %d/%d (%s) Page:%d\n", selectedTrack + 1, MAX_TRACKS, trackNames[selectedTrack], sequencerPage + 1);
                    }
                } else if (currentScreen == SCREEN_VOLUMES) {
                    // En volúmenes: navegar tracks 1..16 y auto-cambiar página
                    int delta = rawDelta / 2;
                    if (delta != 0) {
                        selectedTrack += delta;
                        if (selectedTrack < 0) selectedTrack = MAX_TRACKS - 1;
                        if (selectedTrack >= MAX_TRACKS) selectedTrack = 0;

                        int newPage = selectedTrack / Config::TRACKS_PER_PAGE;
                        if (newPage != volumesPage) {
                            volumesPage = newPage;
                        }

                        sequencerPage = volumesPage;
                        needsFullRedraw = true;
                        showInstrumentOnTM1638(selectedTrack);
                        Serial.printf("► VOLUME NAV Track: %d/%d (%s) Page:%d\n",
                                      selectedTrack + 1, MAX_TRACKS, trackNames[selectedTrack], volumesPage + 1);
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
                    case 2: changeScreen(SCREEN_VOLUMES); break;
                    case 3: changeScreen(SCREEN_FILTERS); break;
                    case 4: changeScreen(SCREEN_SETTINGS); break;
                    case 5: changeScreen(SCREEN_DIAGNOSTICS); break;
                }
                
            } else if (currentScreen == SCREEN_FILTERS) {
                // En FILTERS: Enter toggle enable/disable del filtro seleccionado
                TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
                f.enabled = !f.enabled;
                
                if (filterSelectedTrack == -1) {
                    // MASTER: enviar ON/OFF de los 3 FX
                    for (int fxi = 0; fxi < FILTER_COUNT; fxi++) {
                        sendFilterUDP(-1, fxi);
                    }
                } else {
                    // Per-track: enviar solo el FX seleccionado
                    sendFilterUDP(filterSelectedTrack, filterSelectedFX);
                }
                
                // Feedback inmediato en TM1638
                const char* fxNames[] = {"DELAY   ", "FLANGER ", "COMPRESS"};
                tm1.displayText(fxNames[filterSelectedFX]);
                tm2.displayText(f.enabled ? "  ON    " : "  OFF   ");
                
                const char* targetName = (filterSelectedTrack == -1) ? "MASTER OUT" : trackNames[filterSelectedTrack];
                Serial.printf("► %s on %s: %s\n", 
                    fxNames[filterSelectedFX],
                    targetName, f.enabled ? "ON" : "OFF");
                needsFilterBarsUpdate = true;  // Solo actualizar ON/OFF, no full redraw
                
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
            } else if (currentScreen == SCREEN_DIAGNOSTICS) {
                // En diagnostics: ENTER abre test de encoders
                changeScreen(SCREEN_ENCODER_TEST);
            }
        }
    }
    lastBtn = btn;
}

void handlePlayStopButton() {
    static bool lastPlayStopPressed = false;
    static unsigned long lastPlayStopBtnTime = 0;
    unsigned long currentTime = millis();
    
    bool playStopPressed = (currentADCValue >= BTN_PLAY_STOP_MIN && currentADCValue <= BTN_PLAY_STOP_MAX);
    
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
            if (currentScreen != SCREEN_FILTERS) {
                setAllLEDs(0x0000);
            }
            if (currentScreen == SCREEN_SEQUENCER) {
                updateStepLEDsForTrack(selectedTrack);
            }
        }
        Serial.printf("   Sequencer: %s\n", isPlaying ? "PLAYING" : "STOPPED");
        needsHeaderUpdate = true;
        if (currentScreen != SCREEN_FILTERS) {
            needsFullRedraw = true;
        } else {
            needsFilterBarsUpdate = true;  // Solo refrescar header, no full redraw
        }
    }
    lastPlayStopPressed = playStopPressed;
}

void handleMuteButton() {
    static bool lastMutePressed = false;
    static unsigned long muteBtnPressTime = 0;
    static bool holdProcessed = false;
    unsigned long currentTime = millis();
    
    bool mutePressed = (currentADCValue >= BTN_MUTE_MIN && currentADCValue <= BTN_MUTE_MAX);
    bool noButtonPressed = (currentADCValue < BTN_NONE_THRESHOLD);
    
    // Detectar presión del botón (flanco de subida)
    if (mutePressed && !lastMutePressed) {
        muteBtnPressTime = currentTime;
        holdProcessed = false;
        Serial.printf("► MUTE BUTTON PRESSED (ADC: %d)\n", currentADCValue);
    }
    
    // Detectar HOLD (mantener presionado >1 segundo) - CLEAR
    if (mutePressed && !holdProcessed && (currentTime - muteBtnPressTime > 1000)) {
        holdProcessed = true;
        Serial.printf("► MUTE BUTTON (HOLD - CLEAR INSTRUMENT) ADC: %d\n", currentADCValue);
        
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
                trackMuted[selectedTrack] = pattern.muted[selectedTrack];
                
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

                // Sincronizar LED del M5 8ENCODER
                if (m5encoderConnected) {
                    updateTrackEncoderLED(selectedTrack);
                }
                
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
    
    bool backPressed = (currentADCValue >= BTN_BACK_MIN && currentADCValue <= BTN_BACK_MAX);
    
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
        // Desde ENCODER_TEST: volver a DIAGNOSTICS
        else if (currentScreen == SCREEN_ENCODER_TEST) {
            changeScreen(SCREEN_DIAGNOSTICS);
        }
        // Desde cualquier otra pantalla: volver al menú
        else {
            changeScreen(SCREEN_MENU);
        }
    }
    lastBackPressed = backPressed;
}

// ============================================
// KEYPAD: Lectura única ADC por loop
// ============================================
void readKeypad() {
    static int lastDebugADC = 0;
    static unsigned long lastDebugTime = 0;
    
    currentADCValue = analogRead(ANALOG_BUTTONS_PIN);
    
    // Debug: imprimir valor ADC cada 150ms si cambió significativamente
    unsigned long now = millis();
    if (now - lastDebugTime > 150 && abs(currentADCValue - lastDebugADC) > 30) {
        lastDebugTime = now;
        lastDebugADC = currentADCValue;
        
        // Identificar botón según rangos actuales
        const char* btnName = "???";
        if (currentADCValue < BTN_NONE_THRESHOLD)                                    btnName = "NONE";
        else if (currentADCValue >= BTN_PLAY_STOP_MIN && currentADCValue <= BTN_PLAY_STOP_MAX) btnName = "1:PLAY/STOP";
        else if (currentADCValue >= BTN_MUTE_MIN      && currentADCValue <= BTN_MUTE_MAX)      btnName = "2:MUTE";
        else if (currentADCValue >= BTN_BACK_MIN       && currentADCValue <= BTN_BACK_MAX)      btnName = "3:BACK";
        
        Serial.printf("[KEYPAD] ADC: %4d  -> %s\n", currentADCValue, btnName);
    }
}


void handleVolume() {
    // === DUAL POTENTIOMETER VOLUME CONTROL ===
    // Pin 35 (ROTARY_ANGLE_PIN)  → Sequencer Volume  (ADC1_CH7)
    // Pin 39 (ROTARY_ANGLE_PIN2) → Live Pads Volume   (ADC1_CH3)
    // NOTA: Ambos deben ser ADC1 porque ADC2 no funciona con WiFi activo
    static int adcSeqReadings[5] = {0, 0, 0, 0, 0};
    static int adcPadReadings[5] = {0, 0, 0, 0, 0};
    static int adcSeqIndex = 0;
    static int adcPadIndex = 0;
    static unsigned long lastSeqSent = 0;
    static unsigned long lastPadSent = 0;
    unsigned long now = millis();
    
    // --- POT 1: Sequencer Volume (pin 35) ---
    int rawSeq = analogRead(ROTARY_ANGLE_PIN);
    if (rawSeq >= 10) {  // Descartar lecturas espurias ADC
        adcSeqReadings[adcSeqIndex] = rawSeq;
        adcSeqIndex = (adcSeqIndex + 1) % 5;
    }
    int seqSum = 0;
    for (int i = 0; i < 5; i++) seqSum += adcSeqReadings[i];
    int newSeqVol = map(seqSum / 5, 0, 4095, MAX_VOLUME, 0);
    
    // --- POT 2: Live Pads Volume (pin 39) ---
    int rawPad = analogRead(ROTARY_ANGLE_PIN2);
    if (rawPad >= 10) {  // Descartar lecturas espurias ADC
        adcPadReadings[adcPadIndex] = rawPad;
        adcPadIndex = (adcPadIndex + 1) % 5;
    }
    int padSum = 0;
    for (int i = 0; i < 5; i++) padSum += adcPadReadings[i];
    int newPadVol = map(padSum / 5, 0, 4095, MAX_VOLUME, 0);
    
    // --- Actualizar Sequencer Volume (rate-limit independiente) ---
    if (abs(newSeqVol - lastSequencerVolume) > 3 && (now - lastSeqSent >= 100)) {
        lastSeqSent = now;
        sequencerVolume = newSeqVol;
        lastSequencerVolume = newSeqVol;
        
        if (udpConnected) {
            JsonDocument doc;
            doc["cmd"] = "setSequencerVolume";
            doc["value"] = sequencerVolume;
            sendUDPCommand(doc);
        }
        
        Serial.printf("► SEQ Volume: %d%% (raw:%d)\n", sequencerVolume, rawSeq);
        showVolumeOnTM1638();
        lastDisplayChange = now;
        needsHeaderUpdate = true;
    }
    
    // --- Actualizar Live Pads Volume (rate-limit independiente) ---
    if (abs(newPadVol - lastLivePadsVolume) > 3 && (now - lastPadSent >= 100)) {
        lastPadSent = now;
        livePadsVolume = newPadVol;
        lastLivePadsVolume = newPadVol;
        
        if (udpConnected) {
            int boostedVolume = min((int)(livePadsVolume * 1.25), (int)MAX_VOLUME);
            JsonDocument doc;
            doc["cmd"] = "setLiveVolume";
            doc["value"] = boostedVolume;
            sendUDPCommand(doc);
        }
        
        Serial.printf("► PAD Volume: %d%% (raw:%d)\n", livePadsVolume, rawPad);
        showVolumeOnTM1638();
        lastDisplayChange = now;
        needsHeaderUpdate = true;
    }
}

// ============================================
// BPM POTENTIOMETER HANDLING
// ============================================
void handleBPMPot() {
    // === POTENTIOMETER 3: BPM CONTROL ===
    // Pin 36/VP (BPM_POT_PIN) → BPM (ADC1_CH0)
    static int adcBpmReadings[5] = {0, 0, 0, 0, 0};
    static int adcBpmIndex = 0;
    
    int rawBpm = analogRead(BPM_POT_PIN);
    
    // Descartar lecturas espurias del ADC (ESP32 devuelve raw:0 esporádicamente)
    if (rawBpm < 10) return;
    
    adcBpmReadings[adcBpmIndex] = rawBpm;
    adcBpmIndex = (adcBpmIndex + 1) % 5;
    
    int bpmSum = 0;
    for (int i = 0; i < 5; i++) bpmSum += adcBpmReadings[i];
    int avgAdc = bpmSum / 5;
    
    // Mapear ADC (0-4095) a rango BPM (invertido: giro derecha = más BPM)
    int newBPM = map(avgAdc, 0, 4095, MAX_BPM, MIN_BPM);
    newBPM = constrain(newBPM, MIN_BPM, MAX_BPM);
    
    // Histéresis: solo actualizar si cambio >= 2 BPM
    if (abs(newBPM - tempo) >= 2) {
        tempo = newBPM;
        calculateStepInterval();
        
        if (udpConnected) {
            JsonDocument doc;
            doc["cmd"] = "tempo";
            doc["value"] = tempo;
            sendUDPCommand(doc);
        }
        
        Serial.printf("► BPM Pot: %d BPM (raw:%d)\n", tempo, rawBpm);
        showBPMOnTM1638();
        needsHeaderUpdate = true;
    }
}

// ============================================
// M5 8ENCODER HANDLING
// ============================================
void handleM5Encoders() {
    if (!m5encoderConnected) return;
    
    unsigned long currentTime = millis();
    if (currentTime - lastEncoderRead < Config::ENCODER_READ_INTERVAL) return;
    lastEncoderRead = currentTime;
    
    bool anyChange = false;
    
    // Leer encoders de todos los módulos disponibles (mapeados a tracks 0-15)
    for (int track = 0; track < MAX_TRACKS; track++) {
        int moduleIndex = 0;
        int encoderIndex = 0;
        if (!getM5EncoderRoute(track, moduleIndex, encoderIndex)) continue;
        if (!selectM5EncoderModule(moduleIndex)) continue;

        // Leer contador relativo (cambio desde última lectura)
        int32_t delta = m5encoders[moduleIndex].getRelCounter(encoderIndex);
        
        if (delta != 0) {
            anyChange = true;
            
            // Actualizar según el modo actual
            switch (encoderMode) {
                case ENC_MODE_VOLUME: {
                    // Control de volumen individual del track (0-150)
                    trackVolumes[track] += (delta * 5);  // Pasos de 5%
                    if (trackVolumes[track] < 0) trackVolumes[track] = 0;
                    if (trackVolumes[track] > MAX_VOLUME) trackVolumes[track] = MAX_VOLUME;
                    
                    // Enviar comando al MASTER
                    JsonDocument doc;
                    doc["cmd"] = "setTrackVolume";
                    doc["track"] = track;
                    doc["volume"] = trackVolumes[track];
                    sendUDPCommand(doc);
                    
                    Serial.printf("► Track %d (%s) Volume: %d%%\n", 
                                 track + 1, trackNames[track], trackVolumes[track]);
                    
                    // Actualizar brillo del LED según volumen/mute
                    updateTrackEncoderLED(track);

                    // En pantallas de edición, seguir el track movido para que se vea el cambio
                    if (currentScreen == SCREEN_VOLUMES || currentScreen == SCREEN_SEQUENCER) {
                        if (selectedTrack != track) {
                            selectedTrack = track;
                            int newPage = selectedTrack / Config::TRACKS_PER_PAGE;

                            if (currentScreen == SCREEN_VOLUMES) {
                                if (newPage != volumesPage) {
                                    volumesPage = newPage;
                                    needsFullRedraw = true;
                                } else {
                                    needsHeaderUpdate = true;
                                }
                            } else {
                                if (newPage != sequencerPage) {
                                    sequencerPage = newPage;
                                    needsFullRedraw = true;
                                } else {
                                    needsGridUpdate = true;
                                }
                            }
                        }
                    }
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
                tm1.displayText(trackNames[track]);
                char volStr[9];
                snprintf(volStr, sizeof(volStr), "VOL %3d", trackVolumes[track]);
                tm2.displayText(volStr);
            }
        }
        
        // Leer botones de los encoders como MUTE de track
        if (m5encoders[moduleIndex].getKeyPressed(encoderIndex)) {
            if (currentTime - lastM5ButtonTime[track] < 120) {
                continue;
            }
            lastM5ButtonTime[track] = currentTime;
            Serial.printf("► Encoder %d button pressed - Toggle MUTE Track %d\n", track + 1, track + 1);
            
            // Toggle mute del track
            trackMuted[track] = !trackMuted[track];
            patterns[currentPattern].muted[track] = trackMuted[track];
            
            // Enviar comando al MASTER
            JsonDocument doc;
            doc["cmd"] = "mute";
            doc["track"] = track;
            doc["value"] = trackMuted[track];
            sendUDPCommand(doc);
            
            // Actualizar LED: apagar si está muteado, color normal si no
            updateTrackEncoderLED(track);
            
            // Mostrar en TM1638
            if (currentScreen == SCREEN_SEQUENCER) {
                tm1.displayText(trackNames[track]);
                char muteStr[9];
                snprintf(muteStr, sizeof(muteStr), trackMuted[track] ? "  MUTED" : "UNMUTED");
                tm2.displayText(muteStr);
                lastDisplayChange = millis();
            }

            // Refrescar visual del sequencer para reflejar mute
            if (currentScreen == SCREEN_SEQUENCER) {
                if (!isPlaying && track == selectedTrack) {
                    updateStepLEDsForTrack(selectedTrack);
                }
                needsGridUpdate = true;
            }
            
            Serial.printf("   Track %d (%s) is now %s\n", 
                         track + 1, trackNames[track], trackMuted[track] ? "MUTED" : "UNMUTED");
        }
    }
    
    // Leer switches toggle de cada módulo con debounce
    for (int module = 0; module < Config::M5_ENCODER_MODULES; module++) {
        if (!m5encoderModuleConnected[module]) continue;
        if (!selectM5EncoderModule(module)) continue;

        uint8_t switchState = m5encoders[module].inputSwitch();
        if (switchState && !lastM5SwitchState[module] && (millis() - lastM5SwitchTime[module] > 300)) {
            lastM5SwitchTime[module] = millis();
            Serial.printf("► M5 Toggle Switch activated (module %d)\n", module + 1);
            // TODO: Usar para reset general o cambio de modo global
        }
        lastM5SwitchState[module] = (switchState != 0);
    }
    deselectI2CHub();
}

void updateEncoderLEDs() {
    if (!m5encoderConnected) return;
    
    // Actualizar LEDs según el estado actual (puede llamarse desde display)
    for (int i = 0; i < MAX_TRACKS; i++) {
        updateTrackEncoderLED(i);
    }
}

// ============================================
// TM1638 DISPLAYS
// ============================================
void showInstrumentOnTM1638(int track) {
    // No interrumpir el display de FILTERS con info de instrumentos
    if (currentScreen == SCREEN_FILTERS) return;
    
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
        if (currentTime - lastDisplayChange > 5000) {  // Aumentado de 3000ms a 5000ms
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
        
    } else if (currentScreen == SCREEN_FILTERS) {
        // FILTERS: Mostrar info del FX y target seleccionado
        // Solo actualizar cada 500ms para no hacer spam SPI
        static unsigned long lastFilterDisplayUpdate = 0;
        if (currentTime - lastFilterDisplayUpdate > 500) {
            lastFilterDisplayUpdate = currentTime;
            const char* fxNames[] = {"DELAY   ", "FLANGER ", "COMPRESS"};
            tm1.displayText(fxNames[filterSelectedFX]);
            if (filterSelectedTrack == -1) {
                tm2.displayText(isPlaying ? "MASTER >>" : "MASTER  ");
            } else {
                char fxStr[9];
                snprintf(fxStr, sizeof(fxStr), isPlaying ? "TR%d  >>>" : "TR%d     ", filterSelectedTrack + 1);
                tm2.displayText(fxStr);
            }
        }
        
    } else if (currentScreen == SCREEN_DIAGNOSTICS) {
        bool allOk = diagnostic.tftOk && diagnostic.tm1638_1_Ok && 
                     diagnostic.tm1638_2_Ok && diagnostic.encoderOk;
        tm1.displayText(allOk ? "ALL  OK " : " ERROR  ");
        tm2.displayText(diagnostic.udpConnected ? "UDP  OK " : "NO  UDP ");
    } else if (currentScreen == SCREEN_ENCODER_TEST) {
        tm1.displayText("ENC TEST");
        tm2.displayText("  LIVE  ");
    }
}

void updateLEDFeedback() {
    // No tocar LEDs cuando estamos en FILTERS (se manejan por el filter screen)
    if (currentScreen == SCREEN_FILTERS) return;
    
    unsigned long currentTime = millis();
    
    for (int i = 0; i < 16; i++) {  // 16 LEDs (2 x TM1638)
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
        
        for (int i = 0; i < 16; i++) {
            if (audioLevels[i] > 0) {
                audioLevels[i] -= 15;
                if (audioLevels[i] < 0) audioLevels[i] = 0;
            }
        }
        
        // Manejar tremolo y efecto neón en Live Pads (16 pads)
        if (currentScreen == SCREEN_LIVE) {
            uint16_t buttons = readAllButtons();
            for (int i = 0; i < 16; i++) {  // 16 pads (S1-S16)
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
        "Listo"
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
    
    // Caja de estado
    tft.fillRoundRect(90, progY + 68, 300, 60, 10, THEME_RED808.accent);
    tft.drawRoundRect(90, progY + 68, 300, 60, 10, 0xFFFF);
    tft.setTextSize(2);
    tft.setTextColor(0x0000);
    tft.setCursor(122, progY + 76);
    tft.println("ENLACE MASTER");
    tft.setTextSize(1);
    tft.setTextColor(0x0000);
    tft.setCursor(115, progY + 95);
    tft.println("WIFI STA · UDP 8888 · HANDSHAKE");
    tft.setCursor(150, progY + 108);
    tft.println("MASTER 192.168.4.1");
    
    // Iconos de check
    for (int i = 0; i < 3; i++) {
        tft.fillCircle(116 + i * 5, progY + 114, 3, 0x07E0);
        tft.fillCircle(364 + i * 5, progY + 114, 3, 0x07E0);
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
    
    int itemHeight = 42;
    int startY = 55;
    
    for (int i = 0; i < menuItemCount; i++) {
        int y = startY + i * itemHeight;
        
        if (i == menuSelection) {
            tft.fillRoundRect(30, y, 420, 38, 8, COLOR_ACCENT);
            tft.drawRoundRect(30, y, 420, 38, 8, COLOR_ACCENT2);
            tft.setTextSize(3);
            tft.setTextColor(COLOR_TEXT);
        } else {
            tft.fillRoundRect(30, y, 420, 38, 8, COLOR_NAVY_LIGHT);
            tft.setTextSize(2);
            tft.setTextColor(COLOR_TEXT_DIM);
        }
        
        tft.setCursor(50, y + (i == menuSelection ? 8 : 11));
        tft.print(menuItems[i]);
    }
}

// Función optimizada para solo actualizar items del menú sin parpadeo
void drawMenuItems(int oldSelection, int newSelection) {
    int itemHeight = 42;
    int startY = 55;
    
    // Redibujar item antiguo (no seleccionado)
    if (oldSelection >= 0 && oldSelection < menuItemCount) {
        int y = startY + oldSelection * itemHeight;
        tft.fillRoundRect(30, y, 420, 38, 8, COLOR_NAVY_LIGHT);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(50, y + 11);
        tft.print(menuItems[oldSelection]);
    }
    
    // Redibujar item nuevo (seleccionado)
    if (newSelection >= 0 && newSelection < menuItemCount) {
        int y = startY + newSelection * itemHeight;
        tft.fillRoundRect(30, y, 420, 38, 8, COLOR_ACCENT);
        tft.drawRoundRect(30, y, 420, 38, 8, COLOR_ACCENT2);
        tft.setTextSize(3);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(50, y + 8);
        tft.print(menuItems[newSelection]);
    }
}

void drawLiveScreen() {
    tft.fillScreen(COLOR_BG);
    drawHeader();
    
    // ========== 16 PADS ESTILO AKAI APC MINI (4x4) ==========
    // Layout compacto: 4 columnas x 4 filas para 16 pads
    const int padW = 112;   // Ancho de cada pad
    const int padH = 52;    // Alto reducido para 4 filas
    const int startX = 10;  // Centrado horizontal
    const int startY = 55;  
    const int spacingX = 4;
    const int spacingY = 4;
    
    // Dibujar los 16 pads (4 columnas x 4 filas)
    for (int i = 0; i < 16; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = startX + col * (padW + spacingX);
        int y = startY + row * (padH + spacingY);
        
        uint16_t baseColor = instrumentColors[i];
        
        // Color atenuado para estado OFF
        uint8_t r = ((baseColor >> 11) & 0x1F) * 4;
        uint8_t g = ((baseColor >> 5) & 0x3F) * 2;
        uint8_t b = (baseColor & 0x1F) * 4;
        uint16_t dimColor = tft.color565(r, g, b);
        uint16_t softColor = tft.color565(min((int)r + 10, 255), min((int)g + 8, 255), min((int)b + 10, 255));
        
        // Glow suave
        uint8_t rg = ((baseColor >> 11) & 0x1F) * 6;
        uint8_t gg = ((baseColor >> 5) & 0x3F) * 3;
        uint8_t bg = (baseColor & 0x1F) * 6;
        uint16_t glowColor = tft.color565(rg, gg, bg);
        uint16_t shadowColor = tft.color565(6, 6, 8);
        
        // Sombras y bisel
        tft.fillRoundRect(x + 1, y + 2, padW, padH, 8, shadowColor);
        tft.fillRoundRect(x, y, padW, padH, 8, dimColor);
        tft.fillRoundRect(x + 2, y + 2, padW - 4, padH - 4, 6, softColor);
        
        // Bordes con brillo suave
        tft.drawRoundRect(x + 1, y + 1, padW - 2, padH - 2, 7, baseColor);
        
        // Barra LED superior compacta
        tft.fillRoundRect(x + 4, y + 4, padW - 8, 6, 3, baseColor);
        tft.drawFastHLine(x + 5, y + 6, padW - 10, TFT_WHITE);
        
        // Número del pad (izquierda)
        tft.setTextSize(1);
        tft.setTextColor(baseColor);
        tft.setCursor(x + 5, y + 15);
        tft.printf("%d", i + 1);
        
        // Nombre del instrumento (centrado)
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT);
        String instName = String(instrumentNames[i]);
        instName.trim();
        if (instName.length() > 6) instName = instName.substring(0, 6);
        int textWidth = instName.length() * 12;
        tft.setCursor(x + (padW - textWidth) / 2, y + 18);
        tft.print(instName);
        
        // Track name abajo-derecha
        tft.setTextSize(1);
        tft.setTextColor(baseColor);
        tft.setCursor(x + padW - 20, y + padH - 12);
        tft.print(trackNames[i]);
    }
    
    // Footer
    tft.fillRect(0, 295, 480, 25, COLOR_PRIMARY);
    tft.fillRect(0, 295, 480, 2, COLOR_ACCENT);
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(15, 304);
    tft.print("S1-S16:");
    tft.setTextColor(COLOR_ACCENT);
    tft.print(" PLAY");
    
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print("  |  ");
    
    tft.setTextColor(COLOR_TEXT);
    tft.print("VOL:");
    tft.setTextColor(COLOR_WARNING);
    tft.printf(" %d%%", livePadsVolume);
    
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print("  |  ");
    
    tft.setTextColor(COLOR_ACCENT2);
    tft.print("BACK: Menu");
}

void drawSequencerScreen() {
    if (sequencerView == SEQ_VIEW_CIRCULAR) {
        drawSequencerCircularScreen();
        return;
    }

    static int lastStep = -1;
    
    if (needsFullRedraw) {
        tft.fillScreen(COLOR_BG);
        drawHeader();
        
        // Indicador: 16 tracks visibles
        tft.setTextSize(1);
        tft.setTextColor(COLOR_ACCENT2);
        tft.setCursor(400, 75);
        tft.print("16 TRACKS");
    }
    
    const int gridX = 8;
    const int gridY = 88;
    const int cellW = 27;
    const int cellH = 12;       // Compacto: 12px por track para ver 16
    const int cellGap = 1;
    const int labelW = 22;      // Label estrecho para textSize 1
    
    Pattern& pattern = patterns[currentPattern];
    
    // Mostrar TODOS los 16 tracks (sin paginación)
    const int trackStart = 0;
    const int tracksVisible = MAX_TRACKS;
    
    if (needsFullRedraw) {
        int gridH = tracksVisible * (cellH + cellGap) + 4;
        tft.fillRoundRect(gridX - 2, gridY - 2, 468, gridH, 6, COLOR_NAVY);
        
        tft.setTextSize(1);
        for (int s = 0; s < MAX_STEPS; s++) {
            int x = gridX + labelW + s * (cellW + 1);
            tft.setTextColor((s % 4 == 0) ? COLOR_ACCENT : COLOR_TEXT_DIM);
            tft.setCursor(x + 8, gridY - 10);
            tft.printf("%d", (s + 1) % 10);
        }
        
        tft.setTextSize(1);
        for (int i = 0; i < tracksVisible; i++) {
            int t = trackStart + i;
            int y = gridY + 2 + i * (cellH + cellGap);
            
            if (t == selectedTrack) {
                tft.fillRect(gridX, y, labelW - 2, cellH, COLOR_PRIMARY);
            }
            
            if (pattern.muted[t]) {
                tft.setTextColor(COLOR_ERROR);
                tft.setCursor(gridX + 2, y + 2);
                tft.print("M");
            } else {
                tft.setTextColor(t == selectedTrack ? TFT_WHITE : getInstrumentColor(t));
                tft.setCursor(gridX + 2, y + 2);
                tft.print(trackNames[t]);
            }
        }
        
        lastStep = -1;
    }
    
    // Redibujar etiquetas si cambi\u00f3 el grid (por ejemplo, al mutear)
    if (needsGridUpdate) {
        tft.setTextSize(1);
        for (int i = 0; i < tracksVisible; i++) {
            int t = trackStart + i;
            int y = gridY + 2 + i * (cellH + cellGap);
            
            tft.fillRect(gridX, y, labelW - 2, cellH, 
                        (t == selectedTrack) ? COLOR_PRIMARY : COLOR_NAVY);
            
            if (pattern.muted[t]) {
                tft.setTextColor(COLOR_ERROR);
                tft.setCursor(gridX + 2, y + 2);
                tft.print("M");
            } else {
                tft.setTextColor(t == selectedTrack ? TFT_WHITE : getInstrumentColor(t));
                tft.setCursor(gridX + 2, y + 2);
                tft.print(trackNames[t]);
            }
        }
    }
    
    // Dibujar celdas del grid (16 tracks, sin paginación)
    for (int i = 0; i < tracksVisible; i++) {
        int t = trackStart + i;
        for (int s = 0; s < MAX_STEPS; s++) {
            if (needsFullRedraw || needsGridUpdate || (s == currentStep || s == lastStep)) {
                int x = gridX + labelW + s * (cellW + 1);
                int y = gridY + 2 + i * (cellH + cellGap);
                
                uint16_t color;
                uint16_t border = COLOR_NAVY;
                bool isMuted = pattern.muted[t];
                
                if (isPlaying && s == currentStep) {
                    if (isMuted) {
                        color = pattern.steps[t][s] ? 0x2104 : COLOR_NAVY;
                        border = COLOR_ERROR;
                    } else {
                        color = pattern.steps[t][s] ? getInstrumentColor(t) : COLOR_NAVY_LIGHT;
                        border = pattern.steps[t][s] ? getInstrumentColor(t) : COLOR_WARNING;
                    }
                } else if (pattern.steps[t][s]) {
                    if (isMuted) {
                        color = 0x3186;
                        border = COLOR_ERROR;
                    } else {
                        color = getInstrumentColor(t);
                        border = getInstrumentColor(t);
                    }
                } else {
                    color = COLOR_NAVY_LIGHT;
                }
                
                tft.fillRect(x, y, cellW, cellH, color);
                if (border != COLOR_NAVY) {
                    tft.drawRect(x, y, cellW, cellH, border);
                }
                
                if (s % 4 == 0 && needsFullRedraw) {
                    int gridH = tracksVisible * (cellH + cellGap);
                    tft.drawFastVLine(x - 1, gridY, gridH, COLOR_ACCENT);
                }
            }
        }
    }
    
    lastStep = currentStep;
    
    if (needsFullRedraw) {
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(5, 305);
        tft.print("S1-16:TOGGLE | ENC:TRACK | HOLD:BPM | ");
        tft.setTextColor(COLOR_ACCENT);
        tft.print("VOL-HOLD:PATTERN");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" | ENCODER+BACK:SYNC");
    }
}

void drawSequencerCircularScreen() {
    static int lastStep = -1;
    static int lastTrack = -1;
    static int activeTracks[MAX_TRACKS];
    static int activeCount = 0;
    static bool activeCacheValid = false;
    
    if (needsFullRedraw) {
        tft.fillScreen(COLOR_BG);
        drawHeader();
    }
    
    const int cx = 240;
    const int cy = 185;
    const int areaY = 60;
    const int areaH = 220;
    const int startRadius = 125;
    const int gap = 14;
    const float arcSpanInactive = 7.0f;
    const float arcSpanActive = 20.0f;
    
    Pattern& pattern = patterns[currentPattern];
    bool redrawAll = needsFullRedraw || (selectedTrack != lastTrack) || !activeCacheValid;
    bool redrawSteps = needsGridUpdate;
    bool stepChanged = (currentStep != lastStep);
    
    auto drawStepArc = [&](int ringRadius, int step, uint16_t fillColor, uint16_t ringColor, bool muted, float spanDeg, int thickness) {
        float angleCenter = (-90.0f + (360.0f / MAX_STEPS) * step) * PI / 180.0f;
        float halfSpan = (spanDeg * 0.5f) * PI / 180.0f;
        float a1 = angleCenter - halfSpan;
        float a2 = angleCenter + halfSpan;
        int segments = muted ? 6 : 8;
        
        for (int t = -thickness; t <= thickness; t++) {
            int r = ringRadius + t;
            for (int i = 0; i < segments; i++) {
                float s0 = a1 + (a2 - a1) * (i / (float)segments);
                float s1 = a1 + (a2 - a1) * ((i + 1) / (float)segments);
                int x0 = cx + cos(s0) * r;
                int y0 = cy + sin(s0) * r;
                int x1 = cx + cos(s1) * r;
                int y1 = cy + sin(s1) * r;
                tft.drawLine(x0, y0, x1, y1, (t == 0) ? fillColor : ringColor);
            }
        }
    };
    
    if (redrawAll) {
        // Limpiar área central solo en redraw completo
        tft.fillRect(0, areaY, 480, areaH, COLOR_BG);
        
        // Título de pista seleccionada
        // Nombre del instrumento: quitado para despejar la visualización

        // Info en esquinas
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(8, 64);
        tft.printf("P%d", currentPattern + 1);
        tft.setCursor(430, 64);
        tft.printf("%d BPM", tempo);
        // Info extra opcional (por ahora omitida para limpiar la vista)
        
        // Determinar tracks con pasos activos
        activeCount = 0;
        for (int t = 0; t < MAX_TRACKS; t++) {
            bool hasSteps = false;
            for (int s = 0; s < MAX_STEPS; s++) {
                if (pattern.steps[t][s]) {
                    hasSteps = true;
                    break;
                }
            }
            if (hasSteps) {
                activeTracks[activeCount++] = t;
            }
        }
        activeCacheValid = true;
        
        // Guías circulares suaves
        for (int idx = 0; idx < activeCount; idx++) {
            int ringRadius = startRadius - idx * gap;
            tft.drawCircle(cx, cy, ringRadius, COLOR_BORDER);
        }
        
        // Dibujar todos los ticks
        for (int idx = 0; idx < activeCount; idx++) {
            int t = activeTracks[idx];
            bool isMuted = pattern.muted[t];
            int ringRadius = startRadius - idx * gap - (isMuted ? 3 : 0);
            
            for (int s = 0; s < MAX_STEPS; s++) {
                bool isActive = pattern.steps[t][s];
                bool isCurrent = isPlaying && (s == currentStep);
                uint16_t baseColor = isMuted ? COLOR_TEXT_DIM : getInstrumentColor(t);
                uint16_t fillColor = isActive ? baseColor : COLOR_NAVY_LIGHT;
                uint16_t ringColor = isCurrent ? COLOR_ACCENT : COLOR_BORDER;
                float span = isActive ? arcSpanActive : arcSpanInactive;
                int thickness = isActive ? (isMuted ? 2 : 4) : (isMuted ? 1 : 2);
                drawStepArc(ringRadius, s, fillColor, ringColor, isMuted, span, thickness);
            }
        }
        
        // Centro limpio y más pequeño
        tft.fillCircle(cx, cy, 16, COLOR_PRIMARY_LIGHT);
        tft.drawCircle(cx, cy, 16, COLOR_ACCENT2);
    } else if ((stepChanged || redrawSteps) && activeCacheValid) {
        int stepsToUpdate[2] = {lastStep, currentStep};
        int stepsCount = redrawSteps ? MAX_STEPS : ((lastStep >= 0 && lastStep != currentStep) ? 2 : 1);
        
        for (int idx = 0; idx < activeCount; idx++) {
            int t = activeTracks[idx];
            bool isMuted = pattern.muted[t];
            int ringRadius = startRadius - idx * gap - (isMuted ? 3 : 0);
            
            if (redrawSteps) {
                for (int s = 0; s < MAX_STEPS; s++) {
                    bool isActive = pattern.steps[t][s];
                    bool isCurrent = isPlaying && (s == currentStep);
                    uint16_t baseColor = isMuted ? COLOR_TEXT_DIM : getInstrumentColor(t);
                    uint16_t fillColor = isActive ? baseColor : COLOR_NAVY_LIGHT;
                    uint16_t ringColor = isCurrent ? COLOR_ACCENT : COLOR_BORDER;
                    float span = isActive ? arcSpanActive : arcSpanInactive;
                    int thickness = isActive ? (isMuted ? 2 : 4) : (isMuted ? 1 : 2);
                    drawStepArc(ringRadius, s, fillColor, ringColor, isMuted, span, thickness);
                }
            } else {
                for (int si = 0; si < stepsCount; si++) {
                    int s = stepsToUpdate[si];
                    if (s < 0) continue;
                    bool isActive = pattern.steps[t][s];
                    bool isCurrent = isPlaying && (s == currentStep);
                    uint16_t baseColor = isMuted ? COLOR_TEXT_DIM : getInstrumentColor(t);
                    uint16_t fillColor = isActive ? baseColor : COLOR_NAVY_LIGHT;
                    uint16_t ringColor = isCurrent ? COLOR_ACCENT : COLOR_BORDER;
                    float span = isActive ? arcSpanActive : arcSpanInactive;
                    int thickness = isActive ? (isMuted ? 2 : 4) : (isMuted ? 1 : 2);
                    drawStepArc(ringRadius, s, fillColor, ringColor, isMuted, span, thickness);
                }
            }
        }
    }
    
    lastStep = currentStep;
    lastTrack = selectedTrack;
    
    // Sin footer en vista circular para maximizar visibilidad
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
    
    // ========== COLUMNA IZQUIERDA: SEQUENCER VIEW ==========
    const int leftX = 30;
    
    tft.fillRoundRect(leftX, sectionY, 200, 28, 6, COLOR_PRIMARY);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(leftX + 14, sectionY + 6);
    tft.print("SEQ VIEW");
    
    tft.fillRoundRect(leftX, sectionY + 35, 200, 120, 10, COLOR_PRIMARY);
    tft.drawRoundRect(leftX, sectionY + 35, 200, 120, 10, COLOR_ACCENT2);
    
    // Opción GRID
    uint16_t gridColor = (sequencerView == SEQ_VIEW_GRID) ? COLOR_SUCCESS : COLOR_TEXT_DIM;
    tft.fillRoundRect(leftX + 12, sectionY + 48, 176, 38, 8,
                      sequencerView == SEQ_VIEW_GRID ? COLOR_NAVY_LIGHT : COLOR_NAVY);
    tft.drawRoundRect(leftX + 12, sectionY + 48, 176, 38, 8, gridColor);
    tft.setTextSize(2);
    tft.setTextColor(gridColor);
    tft.setCursor(leftX + 22, sectionY + 58);
    tft.print("S1  GRID");
    
    // Opción CIRCULAR
    uint16_t circColor = (sequencerView == SEQ_VIEW_CIRCULAR) ? COLOR_SUCCESS : COLOR_TEXT_DIM;
    tft.fillRoundRect(leftX + 12, sectionY + 92, 176, 38, 8,
                      sequencerView == SEQ_VIEW_CIRCULAR ? COLOR_NAVY_LIGHT : COLOR_NAVY);
    tft.drawRoundRect(leftX + 12, sectionY + 92, 176, 38, 8, circColor);
    tft.setTextSize(2);
    tft.setTextColor(circColor);
    tft.setCursor(leftX + 22, sectionY + 102);
    tft.print("S2  CIRC");
    
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
    
    // ========== WIFI STATUS + BOTÓN CONNECT ==========
    const int wifiY = sectionY + 165;
    
    // Botón S3: CONNECT MASTER
    tft.fillRoundRect(leftX, wifiY, 200, 32, 6, udpConnected ? COLOR_SUCCESS : COLOR_WARNING);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(leftX + 10, wifiY + 8);
    tft.print("S3 CONNECT");
    
    // Estado actual
    tft.fillRoundRect(rightX, wifiY, 200, 32, 6, udpConnected ? COLOR_SUCCESS : COLOR_ERROR);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(rightX + 30, wifiY + 8);
    tft.print("UDP: ");
    tft.print(udpConnected ? "CONNECTED" : "OFFLINE");
    
    // Footer con instrucciones
    tft.fillRect(0, 295, 480, 25, COLOR_PRIMARY);
    tft.fillRect(0, 295, 480, 2, COLOR_ACCENT);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(10, 305);
    tft.print("S1:GRID  S2:CIRC  S3:WiFi");
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print(" | ");
    tft.setTextColor(COLOR_TEXT);
    tft.print("S5-S8:Theme");
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
    tft.setCursor(120, 305);
    tft.print("ENTER: ENC TEST");
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print("  |  ");
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.print("BACK: MENU");
}

// ============================================
// PANTALLA TEST ENCODERS/ROTARYS - Lectura en vivo
// ============================================
void drawEncoderTestScreen() {
    static unsigned long lastTestUpdate = 0;
    static int lastPotValues[3] = {-1, -1, -1};
    static int lastEncPos = -9999;
    static bool lastEncBtn = false;
    static int32_t lastM5Counters[MAX_TRACKS];
    static bool lastM5Buttons[MAX_TRACKS];
    static uint16_t lastDFValues[Config::DFROBOT_ENCODER_COUNT];
    static bool lastDFButtons[Config::DFROBOT_ENCODER_COUNT];
    static bool testScreenInit = false;

    unsigned long now = millis();
    bool forceUpdate = needsFullRedraw;

    if (!forceUpdate && (now - lastTestUpdate < 80)) return;
    lastTestUpdate = now;

    if (forceUpdate) {
        testScreenInit = false;
        tft.fillScreen(COLOR_BG);
        drawHeader();

        // Título
        tft.fillRoundRect(120, 55, 240, 28, 6, COLOR_PRIMARY);
        tft.drawRoundRect(120, 55, 240, 28, 6, COLOR_ACCENT);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(130, 60);
        tft.print("ENCODER TEST");

        // --- Sección 1: Potenciómetros analógicos ---
        tft.setTextSize(1);
        tft.setTextColor(COLOR_ACCENT2);
        tft.setCursor(10, 92);
        tft.print("ANALOG POTS (ADC)");

        const char* potLabels[] = {"SEQ VOL (35)", "PAD VOL (39)", "BPM POT (36)"};
        for (int i = 0; i < 3; i++) {
            int y = 105 + i * 18;
            tft.fillRoundRect(10, y, 220, 16, 3, COLOR_NAVY);
            tft.setTextSize(1);
            tft.setTextColor(COLOR_TEXT_DIM);
            tft.setCursor(14, y + 4);
            tft.print(potLabels[i]);
        }

        // --- Sección 2: Encoder mecánico ---
        tft.setTextColor(COLOR_ACCENT2);
        tft.setCursor(10, 162);
        tft.print("ROTARY ENCODER (GPIO 15/14/13)");

        tft.fillRoundRect(10, 175, 220, 16, 3, COLOR_NAVY);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(14, 179);
        tft.print("POS:");

        tft.fillRoundRect(10, 193, 220, 16, 3, COLOR_NAVY);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(14, 197);
        tft.print("BTN:");

        // --- Sección 3: I2C Bus scan ---
        tft.setTextColor(COLOR_ACCENT2);
        tft.setCursor(10, 218);
        tft.printf("I2C BUS (%s)", i2cHubDetected ? "HUB ACTIVO" : "PASIVO");

        tft.fillRoundRect(10, 230, 220, 28, 3, COLOR_NAVY);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.setCursor(14, 234);
        if (i2cDeviceCount > 0) {
            tft.printf("%d dev:", i2cDeviceCount);
            for (uint8_t i = 0; i < i2cDeviceCount && i < 10; i++) {
                tft.printf(" %02X", i2cDevicesFound[i]);
            }
        } else {
            tft.print("No devices found");
        }
        // Second line: Module connection status
        tft.setCursor(14, 246);
        tft.setTextColor(m5encoderModuleConnected[0] ? COLOR_SUCCESS : COLOR_ERROR);
        tft.printf("M5#1:%s", m5encoderModuleConnected[0] ? "OK" : "--");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" ");
        tft.setTextColor(m5encoderModuleConnected[1] ? COLOR_SUCCESS : COLOR_ERROR);
        tft.printf("M5#2:%s", m5encoderModuleConnected[1] ? "OK" : "--");
        if (i2cHubDetected) {
            tft.setTextColor(COLOR_ACCENT);
            tft.print(" HUB");
        }

        // --- Sección 3b: DFRobot Visual Rotary Encoders ---
        tft.setTextColor(COLOR_ACCENT2);
        tft.setCursor(10, 264);
        tft.printf("DFROBOT ENC (%d/%d)", dfEncoderConnectedCount, Config::DFROBOT_ENCODER_COUNT);

        for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) {
            int col = i;
            int x = 10 + col * 37;
            int y = 275;
            tft.fillRoundRect(x, y, 35, 16, 2, COLOR_NAVY);
            tft.setTextSize(1);
            tft.setTextColor(dfEncoderConnected[i] ? COLOR_SUCCESS : COLOR_ERROR);
            tft.setCursor(x + 2, y + 4);
            tft.printf("%02X%s", Config::DFROBOT_ENCODER_ADDRS[i], dfEncoderConnected[i] ? "" : "--");
        }

        // --- Sección 4: M5 8ENCODER módulos ---
        tft.setTextColor(COLOR_ACCENT2);
        tft.setCursor(248, 92);
        tft.printf("M5 8ENC (%s)", i2cHubDetected ? "HUB" : "DIRECTO");

        // Dibujar grid de 16 encoders: 2 filas de 8
        for (int m = 0; m < Config::M5_ENCODER_MODULES; m++) {
            int baseY = 105 + m * 90;
            tft.setTextSize(1);
            tft.setTextColor(m5encoderModuleConnected[m] ? COLOR_SUCCESS : COLOR_ERROR);
            tft.setCursor(248, baseY);
            tft.printf("MOD%d @0x%02X %s", m + 1,
                (m == 0) ? Config::M5_ENCODER_ADDR_1 : Config::M5_ENCODER_ADDR_2,
                m5encoderModuleConnected[m] ? "OK" : "--");

            for (int e = 0; e < Config::ENCODERS_PER_MODULE; e++) {
                int track = m * Config::ENCODERS_PER_MODULE + e;
                int col = e;
                int x = 248 + col * 28;
                int y = baseY + 12;
                tft.fillRoundRect(x, y, 26, 30, 3, COLOR_NAVY);
                tft.setTextSize(1);
                tft.setTextColor(getInstrumentColor(track));
                tft.setCursor(x + 3, y + 2);
                tft.print(trackNames[track]);
            }
        }

        // Footer
        tft.fillRect(0, 295, 480, 25, COLOR_PRIMARY);
        tft.fillRect(0, 295, 480, 2, COLOR_ACCENT);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(80, 305);
        tft.print("LIVE READINGS");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print("  |  Move encoders/pots to test  |  ");
        tft.setTextColor(COLOR_ACCENT2);
        tft.print("BACK");

        // Reset cache
        for (int i = 0; i < 3; i++) lastPotValues[i] = -1;
        lastEncPos = -9999;
        lastEncBtn = false;
        for (int i = 0; i < MAX_TRACKS; i++) {
            lastM5Counters[i] = -9999;
            lastM5Buttons[i] = false;
        }
        for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) {
            lastDFValues[i] = 0xFFFF;
            lastDFButtons[i] = false;
        }
        testScreenInit = true;
    }

    // ========== ACTUALIZACIÓN EN VIVO ==========

    // 1. Potenciómetros
    int potPins[] = {ROTARY_ANGLE_PIN, ROTARY_ANGLE_PIN2, BPM_POT_PIN};
    for (int i = 0; i < 3; i++) {
        int raw = analogRead(potPins[i]);
        int pct = map(raw, 0, 4095, 0, 100);
        if (lastPotValues[i] != pct) {
            int y = 105 + i * 18;
            // Valor numérico
            tft.fillRect(150, y + 1, 78, 14, COLOR_NAVY);
            tft.setTextSize(1);
            tft.setTextColor(COLOR_ACCENT);
            tft.setCursor(152, y + 4);
            tft.printf("%4d (%3d%%)", raw, pct);
            lastPotValues[i] = pct;
        }
    }

    // 2. Encoder mecánico
    int curPos = encoderPos;
    if (curPos != lastEncPos) {
        tft.fillRect(50, 176, 178, 14, COLOR_NAVY);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_WARNING);
        tft.setCursor(52, 179);
        tft.printf("%d", curPos);
        lastEncPos = curPos;
    }
    bool curBtn = (digitalRead(ENCODER_SW) == HIGH);
    if (curBtn != lastEncBtn) {
        tft.fillRect(50, 194, 178, 14, COLOR_NAVY);
        tft.setTextSize(1);
        tft.setTextColor(curBtn ? COLOR_SUCCESS : COLOR_TEXT_DIM);
        tft.setCursor(52, 197);
        tft.print(curBtn ? "PRESSED" : "released");
        lastEncBtn = curBtn;
    }

    // 3. M5 8ENCODER módulos
    for (int m = 0; m < Config::M5_ENCODER_MODULES; m++) {
        if (!m5encoderModuleConnected[m]) continue;
        if (!selectM5EncoderModule(m)) continue;

        int baseY = 105 + m * 90;
        for (int e = 0; e < Config::ENCODERS_PER_MODULE; e++) {
            int track = m * Config::ENCODERS_PER_MODULE + e;
            int x = 248 + e * 28;
            int y = baseY + 12;

            // Leer contador absoluto
            int32_t cnt = m5encoders[m].getAbsCounter(e);
            if (cnt != lastM5Counters[track]) {
                tft.fillRect(x + 1, y + 12, 24, 8, COLOR_NAVY);
                tft.setTextSize(1);
                tft.setTextColor(COLOR_ACCENT);
                tft.setCursor(x + 2, y + 13);
                tft.printf("%d", (int)(cnt % 1000));
                lastM5Counters[track] = cnt;
            }

            // Leer botón
            bool btn = m5encoders[m].getKeyPressed(e);
            if (btn != lastM5Buttons[track]) {
                tft.fillRect(x + 1, y + 21, 24, 8, COLOR_NAVY);
                tft.setTextSize(1);
                tft.setTextColor(btn ? COLOR_SUCCESS : COLOR_NAVY);
                tft.setCursor(x + 5, y + 22);
                tft.print(btn ? "ON" : "");
                lastM5Buttons[track] = btn;
                // Flash LED on press
                if (btn) {
                    m5encoders[m].writeRGB(e, 255, 255, 255);
                } else {
                    uint16_t c = instrumentColors[track];
                    uint8_t r = ((c >> 11) & 0x1F) << 3;
                    uint8_t g = ((c >> 5) & 0x3F) << 2;
                    uint8_t b = (c & 0x1F) << 3;
                    m5encoders[m].writeRGB(e, r, g, b);
                }
            }
        }
        deselectI2CHub();
    }

    // 4. DFRobot Visual Rotary Encoders
    for (int i = 0; i < Config::DFROBOT_ENCODER_COUNT; i++) {
        if (!dfEncoderConnected[i] || !dfEncoders[i]) continue;

        // Seleccionar canal del hub antes de leer
        if (!selectDFRobotEncoder(i)) continue;
        uint16_t val = dfEncoders[i]->getEncoderValue();
        bool btn = dfEncoders[i]->detectButtonDown();
        deselectI2CHub();

        int x = 10 + i * 37;
        int y = 275;

        if (val != lastDFValues[i]) {
            // Mostrar valor debajo del label
            tft.fillRect(x + 1, y + 1, 33, 14, COLOR_NAVY);
            tft.setTextSize(1);
            tft.setTextColor(btn ? COLOR_SUCCESS : COLOR_ACCENT);
            tft.setCursor(x + 2, y + 4);
            tft.printf("%4d", val);
            lastDFValues[i] = val;
        }

        if (btn != lastDFButtons[i]) {
            // Flash bordercolor on button
            tft.drawRoundRect(x, y, 35, 16, 2, btn ? COLOR_SUCCESS : COLOR_NAVY);
            lastDFButtons[i] = btn;
        }
    }
}

// Cache para evitar redibujar si no cambió nada
static int hdr_lastTempo = -1;
static int hdr_lastSeqVol = -1;
static int hdr_lastPadVol = -1;
static bool hdr_lastPlaying = false;
static int hdr_lastPattern = -1;
static int hdr_lastTrack = -1;
static Screen hdr_lastScreen = SCREEN_BOOT;

// Actualización PARCIAL del header: solo repinta valores que cambiaron (sin parpadeo)
void updateHeaderValues() {
    bool tempoChanged   = (tempo != hdr_lastTempo);
    bool seqVolChanged  = (sequencerVolume != hdr_lastSeqVol);
    bool padVolChanged  = (livePadsVolume != hdr_lastPadVol);
    bool playChanged    = (isPlaying != hdr_lastPlaying);
    bool patternChanged = (currentPattern != hdr_lastPattern);
    bool trackChanged   = (selectedTrack != hdr_lastTrack);

    // BPM (zona x=240..300, y=10..32)
    if (tempoChanged) {
        tft.fillRect(240, 10, 60, 28, COLOR_NAVY);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_ACCENT2, COLOR_NAVY);
        tft.setCursor(240, 14);
        tft.printf("%d", tempo);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_DIM, COLOR_NAVY);
        tft.setCursor(280, 20);
        tft.print("BPM");
        hdr_lastTempo = tempo;
    }

    // Sequencer Volume (zona x=340..440, y=0..20)
    if (seqVolChanged) {
        tft.fillRect(340, 0, 100, 22, COLOR_NAVY);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_SUCCESS, COLOR_NAVY);
        tft.setCursor(340, 4);
        tft.printf("S:%3d%%", sequencerVolume);
        hdr_lastSeqVol = sequencerVolume;
    }

    // Pads Volume (zona x=340..440, y=24..48)
    if (padVolChanged) {
        tft.fillRect(340, 24, 100, 22, COLOR_NAVY);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_WARNING, COLOR_NAVY);
        tft.setCursor(340, 26);
        tft.printf("P:%3d%%", livePadsVolume);
        hdr_lastPadVol = livePadsVolume;
    }

    // Pattern (zona x=310..340, y=10..32) - solo en sequencer
    if (patternChanged && currentScreen == SCREEN_SEQUENCER) {
        tft.fillRect(310, 10, 30, 24, COLOR_NAVY);
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT, COLOR_NAVY);
        tft.setCursor(310, 14);
        tft.printf("P%d", currentPattern + 1);
        hdr_lastPattern = currentPattern;
    }

    // Instrumento activo (zona x=100..230, y=10..32) - solo en sequencer
    if (trackChanged && currentScreen == SCREEN_SEQUENCER) {
        tft.fillRect(100, 10, 130, 24, COLOR_NAVY);
        tft.setTextSize(2);
        tft.setTextColor(getInstrumentColor(selectedTrack), COLOR_NAVY);
        tft.setCursor(100, 14);
        const char* name = instrumentNames[selectedTrack];
        // Imprimir sin trailing spaces
        int len = strlen(name);
        while (len > 0 && name[len - 1] == ' ') len--;
        for (int i = 0; i < len; i++) tft.print(name[i]);
        hdr_lastTrack = selectedTrack;
    }

    // Play/Stop icon (zona x=443..467, y=12..36)
    if (playChanged) {
        tft.fillRect(443, 12, 24, 26, COLOR_NAVY);
        if (isPlaying) {
            tft.fillCircle(455, 24, 10, COLOR_SUCCESS);
            tft.fillTriangle(450, 18, 450, 30, 460, 24, COLOR_BG);
        } else {
            tft.drawCircle(455, 24, 10, COLOR_BORDER);
            tft.fillRect(451, 19, 3, 10, COLOR_TEXT_DIM);
            tft.fillRect(456, 19, 3, 10, COLOR_TEXT_DIM);
        }
        hdr_lastPlaying = isPlaying;
    }
}

// Dibujo COMPLETO del header (solo en full redraw)
void drawHeader() {
    tft.fillRect(0, 0, 480, 48, COLOR_NAVY);
    tft.drawFastHLine(0, 48, 480, COLOR_ACCENT);
    
    // Logo
    tft.setTextSize(3);
    tft.setTextColor(COLOR_TEXT, COLOR_NAVY);
    tft.setCursor(10, 10);
    tft.print("R808");
    
    // Nombre de pantalla
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACCENT, COLOR_NAVY);
    tft.setCursor(10, 36);
    if (currentScreen == SCREEN_LIVE) {
        tft.print("LIVE PADS");
    } else if (currentScreen == SCREEN_SEQUENCER) {
        tft.print("SEQUENCER");
    } else if (currentScreen == SCREEN_SETTINGS) {
        tft.print("SETTINGS");
    } else if (currentScreen == SCREEN_DIAGNOSTICS) {
        tft.print("DIAGNOSTICS");
    } else if (currentScreen == SCREEN_PATTERNS) {
        tft.print("PATTERNS");
    } else if (currentScreen == SCREEN_FILTERS) {
        tft.print("FILTERS FX");
    } else if (currentScreen == SCREEN_ENCODER_TEST) {
        tft.print("ENC TEST");
    }
    hdr_lastScreen = currentScreen;
    
    // Invalidar cache para forzar redibujado de todos los valores
    hdr_lastTempo = -1;
    hdr_lastSeqVol = -1;
    hdr_lastPadVol = -1;
    hdr_lastPlaying = !isPlaying;
    hdr_lastPattern = -1;
    hdr_lastTrack = -1;
    
    // Dibujar todos los valores dinámicos
    updateHeaderValues();
}

void drawLivePad(int padIndex, bool highlight) {
    if (padIndex < 0 || padIndex >= 16) return;
    
    // COORDENADAS IDÉNTICAS A drawLiveScreen (16 pads, 4x4)
    const int padW = 112;
    const int padH = 52;
    const int startX = 10;
    const int startY = 55;
    const int spacingX = 4;
    const int spacingY = 4;
    
    int col = padIndex % 4;
    int row = padIndex / 4;
    int x = startX + col * (padW + spacingX);
    int y = startY + row * (padH + spacingY);
    
    uint16_t baseColor = instrumentColors[padIndex];
    
    if (highlight) {
        // Efecto PRESS: brillo aumentado al 180%
        uint8_t r8 = ((baseColor >> 11) & 0x1F) << 3;
        uint8_t g8 = ((baseColor >> 5) & 0x3F) << 2;
        uint8_t b8 = (baseColor & 0x1F) << 3;
        int scale = 180;
        uint8_t r = min((r8 * scale) / 100, 255);
        uint8_t g = min((g8 * scale) / 100, 255);
        uint8_t b = min((b8 * scale) / 100, 255);
        uint16_t brightColor = tft.color565(r, g, b);
        uint16_t halo = tft.color565(min(r + 30, 255), min(g + 30, 255), min(b + 30, 255));
        uint16_t shadowColor = tft.color565(6, 6, 8);
        
        // Corona
        tft.drawRoundRect(x - 1, y - 1, padW + 2, padH + 2, 9, halo);
        
        // Cuerpo brillante
        tft.fillRoundRect(x + 1, y + 2, padW, padH, 8, shadowColor);
        tft.fillRoundRect(x, y, padW, padH, 8, brightColor);
        tft.fillRoundRect(x + 2, y + 2, padW - 4, padH - 4, 6, halo);
        tft.drawRoundRect(x + 1, y + 1, padW - 2, padH - 2, 7, brightColor);
        
        // Barra LED superior
        tft.fillRoundRect(x + 4, y + 4, padW - 8, 6, 3, halo);
        tft.drawFastHLine(x + 5, y + 6, padW - 10, TFT_WHITE);
        
        // Número
        tft.setTextSize(1);
        tft.setTextColor(COLOR_BG);
        tft.setCursor(x + 5, y + 15);
        tft.printf("%d", padIndex + 1);
    } else {
        // Estado NORMAL
        uint8_t r = ((baseColor >> 11) & 0x1F) * 4;
        uint8_t g = ((baseColor >> 5) & 0x3F) * 2;
        uint8_t b = (baseColor & 0x1F) * 4;
        uint16_t dimColor = tft.color565(r, g, b);
        uint16_t softColor = tft.color565(min((int)r + 10, 255), min((int)g + 8, 255), min((int)b + 10, 255));
        uint16_t shadowColor = tft.color565(6, 6, 8);
        
        tft.fillRoundRect(x + 1, y + 2, padW, padH, 8, shadowColor);
        tft.fillRoundRect(x, y, padW, padH, 8, dimColor);
        tft.fillRoundRect(x + 2, y + 2, padW - 4, padH - 4, 6, softColor);
        tft.drawRoundRect(x + 1, y + 1, padW - 2, padH - 2, 7, baseColor);
        
        tft.fillRoundRect(x + 4, y + 4, padW - 8, 6, 3, baseColor);
        tft.drawFastHLine(x + 5, y + 6, padW - 10, TFT_WHITE);
        
        // Número
        tft.setTextSize(1);
        tft.setTextColor(baseColor);
        tft.setCursor(x + 5, y + 15);
        tft.printf("%d", padIndex + 1);
    }
    
    // Nombre del instrumento (centrado)
    tft.setTextSize(2);
    tft.setTextColor(highlight ? COLOR_BG : COLOR_TEXT);
    String instName = String(instrumentNames[padIndex]);
    instName.trim();
    if (instName.length() > 6) instName = instName.substring(0, 6);
    int textWidth = instName.length() * 12;
    tft.setCursor(x + (padW - textWidth) / 2, y + 18);
    tft.print(instName);
    
    // Track name
    tft.setTextSize(1);
    tft.setTextColor(highlight ? COLOR_BG : baseColor);
    tft.setCursor(x + padW - 20, y + padH - 12);
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

// ============================================
// PANTALLA VOLUMENES - Estilo Estudio Moderno (SIN PARPADEO)
// ============================================
void drawVolumesScreen() {
    // Throttling: máximo 20 actualizaciones por segundo (50ms)
    unsigned long now = millis();
    bool forceUpdate = !volumesScreenInitialized || needsFullRedraw;
    
    if (!forceUpdate && (now - lastVolumesUpdate < 50)) {
        return;  // No actualizar todavía
    }
    lastVolumesUpdate = now;
    
    // Solo dibujar elementos estáticos la PRIMERA VEZ
    if (forceUpdate) {
        tft.fillScreen(COLOR_BG);
        drawHeader();
        
        // Título principal estilo consola
        tft.fillRoundRect(140, 55, 200, 35, 8, COLOR_PRIMARY);
        tft.drawRoundRect(140, 55, 200, 35, 8, COLOR_ACCENT);
        tft.setTextSize(3);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(145, 62);
        tft.print("VOLUMENES");
        
        // Footer
        tft.fillRect(0, 295, 480, 25, COLOR_PRIMARY);
        tft.fillRect(0, 295, 480, 2, COLOR_ACCENT);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(10, 305);
        tft.print("M5-8ENC: Track Volumes");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" | ");
        tft.setTextColor(COLOR_ACCENT);
        tft.print("KNOB: Master");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" | ");
        tft.setTextColor(COLOR_ACCENT2);
        tft.print("BACK: Menu");
        
        // Resetear valores para forzar primer dibujo
        lastSeqVolDisplay = -1;
        lastLiveVolDisplay = -1;
        lastVolModeDisplay = (VolumeMode)-1;
        for (int i = 0; i < MAX_TRACKS; i++) {
            lastTrackVolDisplay[i] = -1;
            lastTrackMuteDisplay[i] = false;
        }
        
        volumesScreenInitialized = true;
    }
    
    // ========== VOLÚMENES MASTER (SEQ + PAD) - Solo actualizar si cambió ==========
    const int masterY = 100;
    const int masterW = 200;
    const int masterH = 50;
    
    // SEQUENCER Volume - actualizar solo si cambió
    int x1 = 20;
    if (lastSeqVolDisplay != sequencerVolume || lastVolModeDisplay != volumeMode) {
        tft.fillRoundRect(x1, masterY, masterW, masterH, 8, COLOR_PRIMARY);
        tft.drawRoundRect(x1, masterY, masterW, masterH, 8, volumeMode == VOL_SEQUENCER ? COLOR_SUCCESS : COLOR_BORDER);
        tft.setTextSize(2);
        tft.setTextColor(volumeMode == VOL_SEQUENCER ? COLOR_SUCCESS : COLOR_TEXT);
        tft.setCursor(x1 + 10, masterY + 8);
        tft.print("SEQUENCER");
        tft.setTextSize(3);
        tft.setTextColor(COLOR_ACCENT);
        tft.setCursor(x1 + 60, masterY + 26);
        tft.printf("%3d%%", sequencerVolume);
        lastSeqVolDisplay = sequencerVolume;
    }
    
    // LIVE PADS Volume - actualizar solo si cambió
    int x2 = 260;
    if (lastLiveVolDisplay != livePadsVolume || lastVolModeDisplay != volumeMode) {
        tft.fillRoundRect(x2, masterY, masterW, masterH, 8, COLOR_PRIMARY);
        tft.drawRoundRect(x2, masterY, masterW, masterH, 8, volumeMode == VOL_LIVE_PADS ? COLOR_WARNING : COLOR_BORDER);
        tft.setTextSize(2);
        tft.setTextColor(volumeMode == VOL_LIVE_PADS ? COLOR_WARNING : COLOR_TEXT);
        tft.setCursor(x2 + 30, masterY + 8);
        tft.print("LIVE PAD");
        tft.setTextSize(3);
        tft.setTextColor(COLOR_ACCENT2);
        tft.setCursor(x2 + 60, masterY + 26);
        tft.printf("%3d%%", livePadsVolume);
        lastLiveVolDisplay = livePadsVolume;
    }
    lastVolModeDisplay = volumeMode;
    
    // ========== TRACK VOLUMES (16 tracks en 2 filas de 8) ==========
    const int sliderH = 55;
    const int sliderW = 46;
    const int spacing = 7;
    const int startX = 16;
    const int rowGap = 18;  // Espacio entre filas (label + gap)
    const int sliderStartY1 = 158;  // Fila 1: tracks 1-8
    const int sliderStartY2 = sliderStartY1 + sliderH + rowGap;  // Fila 2: tracks 9-16

    // Indicador 16 tracks
    tft.fillRect(372, 56, 100, 14, COLOR_BG);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACCENT2);
    tft.setCursor(374, 58);
    tft.print("16 TRACKS");
    
    for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
        int row = trackIndex / 8;
        int col = trackIndex % 8;
        int x = startX + col * (sliderW + spacing);
        int sliderY = (row == 0) ? sliderStartY1 : sliderStartY2;
        uint16_t trackColor = getInstrumentColor(trackIndex);
        int volPercent = trackVolumes[trackIndex];
        
        // PRIMERA VEZ: dibujar marco, fondo y labels
        if (!volumesScreenInitialized || needsFullRedraw) {
            tft.fillRoundRect(x + 1, sliderY + 1, sliderW - 2, sliderH - 2, 3, COLOR_NAVY);
            tft.drawRoundRect(x, sliderY, sliderW, sliderH, 4, COLOR_BORDER);
            
            // Label del track (abajo del slider)
            tft.setTextSize(1);
            tft.setTextColor(trackColor);
            tft.setCursor(x + (sliderW - 12) / 2, sliderY + sliderH + 3);
            tft.print(trackNames[trackIndex]);

            if (trackIndex == selectedTrack) {
                tft.drawRoundRect(x - 2, sliderY - 2, sliderW + 4, sliderH + 4, 6, COLOR_ACCENT);
            }
        }
        
        // ACTUALIZAR SOLO SI CAMBIÓ
        if (lastTrackVolDisplay[trackIndex] != volPercent || lastTrackMuteDisplay[trackIndex] != trackMuted[trackIndex]) {
            int newFillH = map(volPercent, 0, MAX_VOLUME, 0, sliderH - 8);
            
            uint16_t color1 = trackColor;
            uint8_t r = ((color1 >> 11) & 0x1F) * 5;
            uint8_t g = ((color1 >> 5) & 0x3F) * 2;
            uint8_t b = (color1 & 0x1F) * 5;
            uint16_t color2 = tft.color565(r, g, b);
            uint16_t barOuterColor = color2;
            uint16_t barInnerColor = trackColor;
            if (trackMuted[trackIndex]) {
                barOuterColor = tft.color565(130, 130, 130);
                barInnerColor = tft.color565(170, 170, 170);
            }
            
            int barAreaY = sliderY + 3;
            int barAreaH = sliderH - 8;
            tft.fillRect(x + 4, barAreaY, sliderW - 8, barAreaH, COLOR_NAVY);
            
            if (newFillH > 0) {
                int barY = sliderY + sliderH - 5 - newFillH;
                tft.fillRect(x + 4, barY, sliderW - 8, newFillH, barOuterColor);
                if (newFillH > 2) {
                    tft.fillRect(x + 5, barY + 1, sliderW - 10, newFillH - 2, barInnerColor);
                }
            }
            
            lastTrackVolDisplay[trackIndex] = volPercent;
            lastTrackMuteDisplay[trackIndex] = trackMuted[trackIndex];
        }
    }
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

// ============================================
// FILTERS FX SCREEN
// ============================================

// Constantes de layout compartidas
static const int FILTER_PANEL_W = 148;
static const int FILTER_PANEL_H = 170;
static const int FILTER_PANEL_Y = 100;
static const int FILTER_SPACING = 6;

// Helper: dibujar SOLO la barra de un FX (actualización rápida)
void drawFilterBar(int fx, uint8_t value, bool isSelected) {
    const uint16_t fxColors[] = {COLOR_ACCENT2, COLOR_INST_TOMHI, COLOR_INST_SNARE};
    int x = 10 + fx * (FILTER_PANEL_W + FILTER_SPACING);
    int barX = x + 6;
    int barY = FILTER_PANEL_Y + 55;
    int barW = FILTER_PANEL_W - 16;
    int barH = 18;
    int fillW = map(value, 0, 127, 0, barW - 4);
    
    tft.fillRect(barX + 2, barY + 2, barW - 4, barH - 4, COLOR_NAVY);
    if (fillW > 0) {
        uint16_t barColor = isSelected ? fxColors[fx] : tft.color565(40, 40, 50);
        tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, barColor);
    }
    // Valor numérico
    tft.fillRect(barX + barW - 30, FILTER_PANEL_Y + 42, 30, 12, isSelected ? COLOR_PRIMARY_LIGHT : tft.color565(12, 8, 18));
    tft.setTextSize(1);
    tft.setTextColor(isSelected ? COLOR_TEXT : COLOR_TEXT_DIM);
    tft.setCursor(barX + barW - 28, FILTER_PANEL_Y + 43);
    tft.printf("%3d", value);
}

// Helper: dibujar un panel FX completo (1 barra "AMOUNT" por FX)
void drawFilterPanel(int fx, bool isSelected, uint8_t amount) {
    const char* fxLabels[] = {"DELAY/ECHO", "FLANGER", "COMPRESSOR"};
    const uint16_t fxColors[] = {COLOR_ACCENT2, COLOR_INST_TOMHI, COLOR_INST_SNARE};
    const char* fxButtonLabels[] = {"S1", "S2", "S3"};
    
    int x = 10 + fx * (FILTER_PANEL_W + FILTER_SPACING);
    
    // Limpiar zona del triángulo
    tft.fillRect(x, FILTER_PANEL_Y - 10, FILTER_PANEL_W, 10, COLOR_BG);
    
    // Panel background
    uint16_t panelBG = isSelected ? COLOR_PRIMARY_LIGHT : tft.color565(12, 8, 18);
    uint16_t borderColor = isSelected ? fxColors[fx] : tft.color565(30, 25, 40);
    tft.fillRoundRect(x, FILTER_PANEL_Y, FILTER_PANEL_W, FILTER_PANEL_H, 6, panelBG);
    tft.drawRoundRect(x, FILTER_PANEL_Y, FILTER_PANEL_W, FILTER_PANEL_H, 6, borderColor);
    if (isSelected) {
        tft.drawRoundRect(x + 1, FILTER_PANEL_Y + 1, FILTER_PANEL_W - 2, FILTER_PANEL_H - 2, 5, borderColor);
        int cx = x + FILTER_PANEL_W / 2;
        tft.fillTriangle(cx - 6, FILTER_PANEL_Y - 2, cx + 6, FILTER_PANEL_Y - 2, cx, FILTER_PANEL_Y - 8, fxColors[fx]);
    }
    
    // FX Name header
    uint16_t headerBG = isSelected ? fxColors[fx] : tft.color565(35, 25, 45);
    tft.fillRoundRect(x + 4, FILTER_PANEL_Y + 4, FILTER_PANEL_W - 8, 22, 4, headerBG);
    tft.setTextSize(1);
    tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT_DIM);
    int nameLen = strlen(fxLabels[fx]);
    tft.setCursor(x + (FILTER_PANEL_W - nameLen * 6) / 2, FILTER_PANEL_Y + 10);
    tft.print(fxLabels[fx]);
    
    // Button indicator
    tft.setTextColor(isSelected ? fxColors[fx] : COLOR_TEXT_DIM);
    tft.setCursor(x + 4, FILTER_PANEL_Y + 30);
    tft.print(fxButtonLabels[fx]);
    if (isSelected) {
        tft.setTextColor(COLOR_WARNING);
        tft.print(" ACTIVE");
    }
    
    // Label AMOUNT
    tft.setTextSize(1);
    tft.setTextColor(isSelected ? COLOR_TEXT : tft.color565(80, 70, 90));
    tft.setCursor(x + 6, FILTER_PANEL_Y + 43);
    tft.print("AMOUNT");
    if (isSelected) {
        tft.setTextColor(fxColors[fx]);
        tft.setCursor(x + 76, FILTER_PANEL_Y + 43);
        tft.print("ROTARY");
    }
    
    // Barra grande
    int barX = x + 6;
    int barY = FILTER_PANEL_Y + 55;
    int barW = FILTER_PANEL_W - 16;
    int barH = 18;
    tft.fillRoundRect(barX, barY, barW, barH, 3, COLOR_NAVY);
    tft.drawRoundRect(barX, barY, barW, barH, 3, isSelected ? COLOR_BORDER : tft.color565(25, 20, 35));
    int fillW = map(amount, 0, 127, 0, barW - 4);
    if (fillW > 0) {
        uint16_t barColor = isSelected ? fxColors[fx] : tft.color565(40, 40, 50);
        tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, barColor);
    }
    // Valor numérico
    tft.setTextSize(1);
    tft.setTextColor(isSelected ? COLOR_TEXT : COLOR_TEXT_DIM);
    tft.setCursor(barX + barW - 28, FILTER_PANEL_Y + 43);
    tft.printf("%3d", amount);
    
    // Porcentaje grande centrado
    int pct = map(amount, 0, 127, 0, 100);
    tft.setTextSize(3);
    tft.setTextColor(isSelected ? fxColors[fx] : tft.color565(50, 45, 60));
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
    int pctLen = strlen(pctStr);
    tft.setCursor(x + (FILTER_PANEL_W - pctLen * 18) / 2, FILTER_PANEL_Y + 90);
    tft.print(pctStr);
}

// Helper: dibujar solo la barra de t\u00edtulo FILTERS (track + ON/OFF + LIVE)
void drawFilterTitleBar() {
    TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
    
    // Limpiar zona t\u00edtulo
    tft.fillRoundRect(10, 55, 460, 35, 8, COLOR_PRIMARY);
    tft.drawRoundRect(10, 55, 460, 35, 8, COLOR_ACCENT);
    tft.setTextSize(2);
    tft.setCursor(20, 63);
    if (filterSelectedTrack == -1) {
        tft.setTextColor(COLOR_WARNING);
        tft.print("FILTERS  ");
        tft.setTextColor(COLOR_ACCENT2);
        tft.print("MASTER OUT");
    } else {
        tft.setTextColor(COLOR_TEXT);
        tft.printf("FILTERS  TR%d ", filterSelectedTrack + 1);
        tft.setTextColor(COLOR_ACCENT2);
        tft.print(instrumentNames[filterSelectedTrack]);
    }
    
    // Indicador LIVE
    if (isPlaying) {
        tft.fillRoundRect(340, 58, 54, 28, 6, COLOR_ACCENT2);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_BG);
        tft.setCursor(347, 66);
        tft.print("LIVE");
    }
    
    // ON/OFF
    uint16_t statusColor = f.enabled ? COLOR_SUCCESS : COLOR_ERROR;
    tft.fillRoundRect(400, 58, 60, 28, 6, statusColor);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_BG);
    tft.setCursor(410, 63);
    tft.print(f.enabled ? "ON" : "OFF");
    
    lastFilterEnabled = f.enabled;
}

void drawFiltersScreen() {
    unsigned long now = millis();
    bool forceUpdate = !filterScreenInitialized || needsFullRedraw;
    
    if (!forceUpdate && !needsFilterPanelsRedraw && !needsFilterBarsUpdate && (now - lastFilterUpdate < 50)) {
        return;
    }
    lastFilterUpdate = now;
    
    TrackFilter& f = (filterSelectedTrack == -1) ? masterFilter : trackFilters[filterSelectedTrack];
    
    // Obtener amounts actuales de los 3 FX
    uint8_t curAmounts[3] = { f.delayAmount, f.flangerAmount, f.compAmount };
    
    // === NIVEL 1: Actualización de barra (rotary movido) ===
    if (!forceUpdate && !needsFilterPanelsRedraw && needsFilterBarsUpdate) {
        needsFilterBarsUpdate = false;
        int fx = filterSelectedFX;
        if (curAmounts[fx] != lastFilterParams[fx]) {
            drawFilterBar(fx, curAmounts[fx], true);
            lastFilterParams[fx] = curAmounts[fx];
        }
        // SIEMPRE redibujar ON/OFF badge
        {
            uint16_t statusColor = f.enabled ? COLOR_SUCCESS : COLOR_ERROR;
            tft.fillRoundRect(400, 58, 60, 28, 6, statusColor);
            tft.setTextSize(2);
            tft.setTextColor(COLOR_BG);
            tft.setCursor(f.enabled ? 414 : 410, 63);
            tft.print(f.enabled ? "ON" : "OFF");
            lastFilterEnabled = f.enabled;
        }
        return;
    }
    
    // === NIVEL 2: Redibujado de paneles + título (cambio de FX o Track) ===
    if (!forceUpdate && needsFilterPanelsRedraw) {
        needsFilterPanelsRedraw = false;
        
        drawFilterTitleBar();
        
        for (int fx = 0; fx < FILTER_COUNT; fx++) {
            bool isSel = (fx == filterSelectedFX);
            drawFilterPanel(fx, isSel, curAmounts[fx]);
            lastFilterParams[fx] = curAmounts[fx];
        }
        
        // LEDs
        setAllLEDs(0x0000);
        if (filterSelectedTrack == -1) {
            setAllLEDs(0xFFFF);
        } else {
            setLED(filterSelectedTrack, true);
        }
        
        lastFilterTrackDisplay = filterSelectedTrack;
        lastFilterFXDisplay = filterSelectedFX;
        return;
    }
    
    // === NIVEL 3: Dibujo completo (solo primera entrada desde menú) ===
    if (forceUpdate) {
        tft.fillScreen(COLOR_BG);
        drawHeader();
        drawFilterTitleBar();
        
        for (int fx = 0; fx < FILTER_COUNT; fx++) {
            bool isSel = (fx == filterSelectedFX);
            drawFilterPanel(fx, isSel, curAmounts[fx]);
            lastFilterParams[fx] = curAmounts[fx];
        }
        
        // Footer
        tft.fillRect(0, 278, 480, 42, COLOR_PRIMARY);
        tft.fillRect(0, 278, 480, 2, COLOR_ACCENT);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_ACCENT2);
        tft.setCursor(10, 286);
        tft.print("S1-S3: FX");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" | ");
        tft.setTextColor(COLOR_WARNING);
        tft.print("S4: MASTER");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" | ");
        tft.setTextColor(COLOR_TEXT);
        tft.print("S5-S12: Tracks");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" | ");
        tft.setTextColor(COLOR_SUCCESS);
        tft.print("ENTER: ON/OFF");
        tft.setTextColor(COLOR_TEXT_DIM);
        tft.print(" | ");
        tft.setTextColor(COLOR_ERROR);
        tft.print("BACK: Menu");
        
        tft.setTextColor(COLOR_ACCENT);
        tft.setCursor(10, 300);
        tft.print("ENCODER: Track  ROTARY: Amount  (Live UDP sync)");
        
        // LEDs TM1638
        setAllLEDs(0x0000);
        if (filterSelectedTrack == -1) {
            setAllLEDs(0xFFFF);
        } else {
            setLED(filterSelectedTrack, true);
        }
        
        lastFilterTrackDisplay = filterSelectedTrack;
        lastFilterFXDisplay = filterSelectedFX;
        needsFilterBarsUpdate = false;
        needsFilterPanelsRedraw = false;
        filterScreenInitialized = true;
    }
}

// ============================================
// SEND FILTER STATE VIA UDP (1 param "amount" per FX)
// ============================================
// El amount se usa como valor principal y genera sub-params proporcionales
void sendFilterUDP(int track, int filterType) {
    if (!udpConnected) return;
    
    TrackFilter& f = (track == -1) ? masterFilter : trackFilters[track];
    
    if (track >= 0) {
        // === PER-TRACK LIVE FX ===
        JsonDocument doc;
        switch (filterType) {
            case FILTER_DELAY:
                doc["cmd"] = "setTrackEcho";
                doc["track"] = track;
                doc["active"] = f.enabled;
                doc["time"] = f.delayAmount / 2;       // time proporcional
                doc["feedback"] = f.delayAmount / 3;   // feedback conservador
                doc["mix"] = f.delayAmount;             // mix = amount directo
                break;
            case FILTER_FLANGER:
                doc["cmd"] = "setTrackFlanger";
                doc["track"] = track;
                doc["active"] = f.enabled;
                doc["rate"] = f.flangerAmount;          // rate = amount
                doc["depth"] = f.flangerAmount * 3 / 4; // depth proporcional
                doc["feedback"] = f.flangerAmount / 2;  // feedback moderado
                break;
            case FILTER_COMPRESSOR:
                doc["cmd"] = "setTrackCompressor";
                doc["track"] = track;
                doc["active"] = f.enabled;
                doc["threshold"] = 127 - f.compAmount;  // más amount = menor threshold
                doc["ratio"] = f.compAmount / 2;        // ratio proporcional
                break;
        }
        sendUDPCommand(doc);
        
    } else {
        // === MASTER EFFECTS ===
        JsonDocument doc;
        const char* cmds[4];
        uint8_t vals[4];
        int count = 0;
        
        switch (filterType) {
            case FILTER_DELAY:
                cmds[0] = "setDelayActive";   vals[0] = f.enabled;
                cmds[1] = "setDelayTime";     vals[1] = f.delayAmount / 2;
                cmds[2] = "setDelayFeedback"; vals[2] = f.delayAmount / 3;
                cmds[3] = "setDelayMix";      vals[3] = f.delayAmount;
                count = 4;
                break;
            case FILTER_FLANGER:
                cmds[0] = "setFlangerActive"; vals[0] = f.enabled;
                cmds[1] = "setFlangerRate";   vals[1] = f.flangerAmount;
                cmds[2] = "setFlangerDepth";  vals[2] = f.flangerAmount * 3 / 4;
                cmds[3] = "setFlangerMix";    vals[3] = f.flangerAmount / 2;
                count = 4;
                break;
            case FILTER_COMPRESSOR:
                cmds[0] = "setCompressorActive";    vals[0] = f.enabled;
                cmds[1] = "setCompressorThreshold"; vals[1] = 127 - f.compAmount;
                cmds[2] = "setCompressorRatio";     vals[2] = f.compAmount / 2;
                cmds[3] = "setCompressorMakeupGain"; vals[3] = f.compAmount / 4;
                count = 4;
                break;
        }
        
        for (int i = 0; i < count; i++) {
            doc.clear();
            doc["cmd"] = cmds[i];
            doc["value"] = vals[i];
            sendUDPCommand(doc);
        }
    }
}

void drawSyncingScreen() {
    // Pantalla de sincronización estilo RED808
    tft.fillScreen(COLOR_BG);
    drawHeader();
    
    const int boxW = 380;
    const int boxH = 170;
    const int boxX = (480 - boxW) / 2;
    const int boxY = 95;
    
    // Glow exterior
    for (int i = 4; i > 0; i--) {
        uint16_t glow = tft.color565(10 + i * 6, 2 + i * 3, 0);
        tft.drawRoundRect(boxX - i, boxY - i, boxW + i * 2, boxH + i * 2, 14, glow);
    }
    
    // Panel principal
    tft.fillRoundRect(boxX, boxY, boxW, boxH, 12, COLOR_PRIMARY);
    tft.drawRoundRect(boxX, boxY, boxW, boxH, 12, COLOR_ACCENT);
    tft.drawRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 10, COLOR_ACCENT2);
    
    // Banda superior tipo consola
    tft.fillRoundRect(boxX + 10, boxY + 10, boxW - 20, 22, 6, COLOR_PRIMARY_LIGHT);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(boxX + 20, boxY + 16);
    tft.print("WIFI LINK STATUS");
    
    // Icono WiFi + anillo
    int centerX = 240;
    int centerY = boxY + 70;
    tft.drawCircle(centerX, centerY, 24, COLOR_ACCENT2);
    tft.drawCircle(centerX, centerY, 22, COLOR_ACCENT2);
    tft.drawFastHLine(centerX - 10, centerY + 8, 20, COLOR_ACCENT2);
    tft.fillCircle(centerX, centerY + 10, 3, COLOR_ACCENT2);
    tft.drawCircle(centerX, centerY, 16, COLOR_WARNING);
    tft.drawCircle(centerX, centerY, 10, COLOR_WARNING);
    
    // Texto principal
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(boxX + 40, boxY + 105);
    tft.print("BUSCANDO MASTER");
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_DIM);
    tft.setCursor(boxX + 70, boxY + 128);
    tft.print("SINCRONIZACION EN CURSO");
    
    // Barra de progreso sutil
    tft.drawRoundRect(boxX + 40, boxY + 145, boxW - 80, 10, 5, COLOR_BORDER);
    int pulse = (millis() / 60) % (boxW - 92);
    tft.fillRoundRect(boxX + 42 + pulse, boxY + 147, 16, 6, 3, COLOR_ACCENT);
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
    // CORRECCIÓN: Validar nuevo patrón antes de cambiar
    int newPattern = (currentPattern + delta + MAX_PATTERNS) % MAX_PATTERNS;
    if (newPattern < 0 || newPattern >= MAX_PATTERNS) {
        Serial.printf("✗ Invalid pattern: %d\n", newPattern);
        return;
    }
    
    currentPattern = newPattern;
    currentStep = 0;
    needsFullRedraw = true;
    
    // Enviar cambio de patrón al MASTER
    if (!udpConnected) {
        Serial.println("✗ Cannot change pattern: UDP not connected");
        return;
    }
    
    JsonDocument doc;
    doc["cmd"] = "selectPattern";
    doc["index"] = currentPattern;
    sendUDPCommand(doc);
    
    // Solicitar sync sin delay bloqueante
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
        sequencerPage = selectedTrack / Config::TRACKS_PER_PAGE;
        updateStepLEDsForTrack(selectedTrack);
        showInstrumentOnTM1638(selectedTrack);
        
        // Solicitar patrón actual al MASTER
        if (udpConnected) {
            requestPatternFromMaster();
        }
    }

    // Si entramos a VOLUMES, sincronizar página con track seleccionado
    if (newScreen == SCREEN_VOLUMES) {
        volumesPage = selectedTrack / Config::TRACKS_PER_PAGE;
    }
    
    // Si entramos a FILTERS, inicializar pantalla
    if (newScreen == SCREEN_FILTERS) {
        filterScreenInitialized = false;
        // Mostrar target actual en TM1638
        if (filterSelectedTrack == -1) {
            tm1.displayText("MASTER  ");
            tm2.displayText("FX OUT  ");
            setAllLEDs(0xFFFF);  // Todos LEDs = MASTER
        } else {
            tm1.displayText(trackNames[filterSelectedTrack]);
            const char* fxNames[] = {"DELAY   ", "FLANGER ", "COMPRESS"};
            tm2.displayText(fxNames[filterSelectedFX]);
            setLED(filterSelectedTrack, true);
        }
    }
    
    // Resetear flag de volúmenes al salir
    if (newScreen != SCREEN_VOLUMES) {
        volumesScreenInitialized = false;
    }
}

void toggleStep(int track, int step) {
    // CORRECCIÓN: Validar índices antes de acceder a arrays
    if (currentPattern < 0 || currentPattern >= MAX_PATTERNS) {
        Serial.printf("✗ Invalid pattern: %d\n", currentPattern);
        return;
    }
    if (track < 0 || track >= MAX_TRACKS) {
        Serial.printf("✗ Invalid track: %d\n", track);
        return;
    }
    if (step < 0 || step >= MAX_STEPS) {
        Serial.printf("✗ Invalid step: %d\n", step);
        return;
    }
    
    patterns[currentPattern].steps[track][step] = 
        !patterns[currentPattern].steps[track][step];
    needsGridUpdate = true;
    
    Serial.printf("► TOGGLE: Track %d, Step %d = %s\n", 
                  track, step, 
                  patterns[currentPattern].steps[track][step] ? "ON" : "OFF");
    
    // Enviar al MASTER
    if (!udpConnected) {
        Serial.println("✗ Cannot toggle step: UDP not connected");
        return;
    }
    
    JsonDocument doc;
    doc["cmd"] = "setStep";
    doc["track"] = track;
    doc["step"] = step;
    doc["active"] = patterns[currentPattern].steps[track][step];
    sendUDPCommand(doc);
}

// ============================================
// SERVIDOR WEB ASYNC - REMOVIDO
// ============================================
// El SLAVE no necesita servidor web. Ver MASTER para implementación web.
