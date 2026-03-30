# Protocolo UDP Master-Slave - DrumMachine ESP32-S3

## Configuración de Red

### MASTER (DrumMachine Principal)
- **IP**: `192.168.4.1` (Access Point)
- **Puerto UDP**: `8888`
- **WiFi SSID**: `RED808`
- **WiFi Password**: `red808esp32`

### SLAVE (Controlador Secundario)
- Conecta al WiFi del MASTER
- Obtiene IP automática (DHCP): `192.168.4.x`
- Puerto local: Cualquiera (aleatorio)

---

## Formato de Comunicación

**Protocolo**: UDP (sin conexión, rápido, ideal para tiempo real)  
**Formato de Datos**: JSON (texto plano)  
**Tamaño Máximo**: 512 bytes por paquete  
**Encoding**: UTF-8

### Respuesta Estándar del MASTER
```json
{"status":"ok"}
```

### Respuesta de Error
```json
{"status":"error","msg":"Invalid JSON"}
```

---

## Comandos del SLAVE al MASTER

### 1. CONTROL DEL SEQUENCER

#### Iniciar secuenciador
```json
{"cmd":"start"}
```

#### Detener secuenciador
```json
{"cmd":"stop"}
```

#### Cambiar tempo (60-200 BPM)
```json
{"cmd":"tempo","value":140}
```
- `value`: BPM (entero o float)

#### Cambiar patrón activo (0-15)
```json
{"cmd":"selectPattern","index":0}
```
- `index`: 0-15 (número de patrón)

---

### 2. EDICIÓN DE PATRÓN

#### Activar/desactivar un step
```json
{"cmd":"setStep","track":0,"step":0,"active":true}
```
- `track`: 0-15 (pista/instrumento)
- `step`: 0-15 (posición en el patrón)
- `active`: true/false

#### Silenciar/activar track
```json
{"cmd":"mute","track":0,"value":true}
```
- `track`: 0-15
- `value`: true (mute) / false (unmute)

#### Toggle loop en track
```json
{"cmd":"toggleLoop","track":0}
```
- `track`: 0-15

#### Pausar loop
```json
{"cmd":"pauseLoop","track":0}
```
- `track`: 0-15

---

### 3. TRIGGER DE PADS (LIVE)

#### Trigger manual con velocity
```json
{"cmd":"trigger","pad":0,"vel":127}
```
- `pad`: 0-15 (instrumento)
- `vel`: 0-127 (velocity/volumen, opcional, default=127)

#### Trigger simple
```json
{"cmd":"trigger","pad":5}
```

**Mapeo de Pads**:
```
0=BD  1=SD  2=CH  3=OH  4=CP  5=CB  6=RS  7=CL
8=MA  9=CY  10=HT 11=LT 12=MC 13=MT 14=HC 15=LC
```

---

### 4. CONTROL DE VOLUMEN

#### Volumen maestro (0-150)
```json
{"cmd":"setVolume","value":80}
```

#### Volumen del sequencer (0-150)
```json
{"cmd":"setSequencerVolume","value":70}
```

#### Volumen de live pads (0-150)
```json
{"cmd":"setLiveVolume","value":90}
```

#### **NUEVO: Volumen por track (0-100)**
```json
{"cmd":"setTrackVolume","track":0,"volume":75}
```
- `track`: 0-7 (pista del sequencer)
- `volume`: 0-100 (0% a 100%, default=100)

**Nota**: El volumen final del track es: `sequencerVolume * trackVolume / 100`

#### **NUEVO: Obtener volumen de un track**
```json
{"cmd":"getTrackVolume","track":0}
```

**Respuesta del MASTER**:
```json
{"type":"trackVolume","track":0,"volume":75}
```

#### **NUEVO: Obtener todos los volúmenes de tracks**
```json
{"cmd":"getTrackVolumes"}
```

**Respuesta del MASTER**:
```json
{"type":"trackVolumes","volumes":[100,75,90,100,80,100,100,100]}
```

---

### 5. EFECTOS DE AUDIO

#### Tipo de filtro
```json
{"cmd":"setFilter","type":1}
```
- `type`: 0=None, 1=LowPass, 2=HighPass, 3=BandPass, 4=Notch

#### Frecuencia de corte (20-20000 Hz)
```json
{"cmd":"setFilterCutoff","value":2000}
```

#### Resonancia del filtro (0.1-10.0)
```json
{"cmd":"setFilterResonance","value":3.0}
```

#### Bit Crush (1-16 bits)
```json
{"cmd":"setBitCrush","value":8}
```

#### Distorsión (0.0-1.0)
```json
{"cmd":"setDistortion","value":0.5}
```

#### Reducción de Sample Rate (1000-44100 Hz)
```json
{"cmd":"setSampleRate","value":11025}
```

---

### 6. CARGA DE SAMPLES

#### Cargar sample en un pad
```json
{"cmd":"loadSample","family":"BD","filename":"BD_01.wav","pad":0}
```
- `family`: "BD","SD","CH","OH","CP","CB","RS","CL","MA","CY","HT","LT","MC","MT","HC","LC"
- `filename`: Nombre del archivo WAV en la carpeta
- `pad`: 0-15 (destino)

---

### 7. LED RGB

#### Modo mono (un solo color)
```json
{"cmd":"setLedMonoMode","value":true}
```
- `value`: true/false

---

## Comandos del MASTER al SLAVE

### SINCRONIZACIÓN DE PATRÓN

#### Solicitud del SLAVE al MASTER
```json
{"cmd":"get_pattern","pattern":0}
```
- `pattern`: 0-15 (opcional, si se omite devuelve el patrón activo)

#### Respuesta del MASTER al SLAVE
```json
{
  "cmd":"pattern_sync",
  "pattern":0,
  "data":[
    [1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0],
    [0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0],
    [1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0],
    [0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
  ]
}
```

**Estructura de `data`**:
- Array de 16 tracks
- Cada track tiene 16 steps
- `1` = step activo, `0` = step inactivo

---

## Ejemplo Completo - Código SLAVE (ESP32/Arduino)

```cpp
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Configuración WiFi del MASTER
const char* ssid = "RED808";
const char* password = "red808esp32";
const char* masterIP = "192.168.4.1";
const int udpPort = 8888;

WiFiUDP udp;

// Almacenar patrón sincronizado
bool syncedPattern[16][16];  // [track][step]
int currentPattern = -1;

void setup() {
  Serial.begin(115200);
  
  // Conectar al MASTER
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✓ Conectado al MASTER");
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  
  // Iniciar UDP
  udp.begin(0);  // Puerto local aleatorio
}

// Enviar comando al MASTER
void sendCommand(JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  
  udp.beginPacket(masterIP, udpPort);
  udp.print(json);
  udp.endPacket();
  
  Serial.printf("→ MASTER: %s\n", json.c_str());
}

// Trigger pad
void triggerPad(int pad, int vel = 127) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "trigger";
  doc["pad"] = pad;
  doc["vel"] = vel;
  sendCommand(doc);
}

// Cambiar tempo
void setTempo(int bpm) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "tempo";
  doc["value"] = bpm;
  sendCommand(doc);
}

// Iniciar/detener secuenciador
void start() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "start";
  sendCommand(doc);
}

void stop() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "stop";
  sendCommand(doc);
}

// Solicitar sincronización de patrón
void requestPattern(int patternNum) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "get_pattern";
  doc["pattern"] = patternNum;
  sendCommand(doc);
}

// Recibir respuesta del MASTER
void receiveResponse() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buffer[2048];
    int len = udp.read(buffer, 2047);
    buffer[len] = 0;
    
    Serial.printf("← MASTER: %s\n", buffer);
    
    // Parsear JSON
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, buffer) == DeserializationError::Ok) {
      
      // Respuesta estándar
      if (doc.containsKey("status")) {
        const char* status = doc["status"];
        if (strcmp(status, "ok") == 0) {
          Serial.println("✓ Comando OK");
        } else {
          Serial.printf("✗ Error: %s\n", doc["msg"].as<const char*>());
        }
      }
      
      // Sincronización de patrón
      else if (doc["cmd"] == "pattern_sync") {
        currentPattern = doc["pattern"];
        JsonArray data = doc["data"];
        
        // Copiar patrón
        for (int t = 0; t < 16; t++) {
          JsonArray track = data[t];
          for (int s = 0; s < 16; s++) {
            syncedPattern[t][s] = track[s] ? true : false;
          }
        }
        
        Serial.printf("✓ Patrón %d sincronizado!\n", currentPattern);
      }
    }
  }
}

// Establecer step en el secuenciador
void setStep(int track, int step, bool active) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "setStep";
  doc["track"] = track;
  doc["step"] = step;
  doc["active"] = active;
  sendCommand(doc);
}

// Cambiar volumen maestro
void setVolume(int volume) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "setVolume";
  doc["value"] = volume;
  sendCommand(doc);
}

void loop() {
  // Ejemplo: Solicitar patrón 0
  requestPattern(0);
  delay(100);
  receiveResponse();
  
  // Ejemplo: Trigger pads
  triggerPad(0, 127);  // BD
  delay(500);
  triggerPad(1, 120);  // SD
  delay(500);
  
  // Ejemplo: Cambiar tempo
  setTempo(140);
  delay(100);
  receiveResponse();
  
  // Ejemplo: Iniciar secuenciador
  start();
  delay(100);
  receiveResponse();
  
  delay(5000);
  
  // Detener
  stop();
  delay(100);
  receiveResponse();
  
  delay(5000);
}
```

---

## Resumen de Flujo de Datos

```
SLAVE → MASTER: Comandos de control (trigger, tempo, start, stop, etc.)
MASTER → SLAVE: {"status":"ok"} o {"status":"error","msg":"..."}

SLAVE → MASTER: {"cmd":"get_pattern","pattern":0}
MASTER → SLAVE: {"cmd":"pattern_sync","pattern":0,"data":[[...],[...],...]}
```

---

## Tabla Resumen de Comandos UDP

| Categoría | Comando | Parámetros | Descripción |
|-----------|---------|------------|-------------|
| **Sequencer** | `start` | - | Iniciar secuenciador |
| | `stop` | - | Detener secuenciador |
| | `tempo` | `value` (60-200) | Cambiar BPM |
| | `selectPattern` | `index` (0-15) | Cambiar patrón activo |
| **Pattern** | `setStep` | `track`, `step`, `active` | Activar/desactivar step |
| | `mute` | `track`, `value` | Silenciar/activar track |
| | `toggleLoop` | `track` | Toggle loop en track |
| | `pauseLoop` | `track` | Pausar loop |
| | `setStepVelocity` | `track`, `step`, `velocity` | Establecer velocity de step (0-127) |
| | `getStepVelocity` | `track`, `step` | Obtener velocity de step |
| **Pads** | `trigger` | `pad`, `vel` (opcional) | Trigger pad con velocity |
| **Volumen** | `setVolume` | `value` (0-150) | Volumen maestro |
| | `setSequencerVolume` | `value` (0-150) | Volumen del sequencer |
| | `setLiveVolume` | `value` (0-150) | Volumen de live pads |
| | **`setTrackVolume`** | **`track`, `volume` (0-100)** | **Volumen por track (NUEVO)** |
| | **`getTrackVolume`** | **`track`** | **Obtener volumen de track (NUEVO)** |
| | **`getTrackVolumes`** | **-** | **Obtener todos los volúmenes (NUEVO)** |
| **FX** | `setFilter` | `type` (0-9) | Tipo de filtro global |
| | `setFilterCutoff` | `value` (20-20000) | Frecuencia de corte (Hz) |
| | `setFilterResonance` | `value` (0.1-10.0) | Resonancia del filtro |
| | `setBitCrush` | `value` (1-16) | Bit depth |
| | `setDistortion` | `value` (0.0-1.0) | Distorsión |
| | `setSampleRate` | `value` (1000-44100) | Sample rate reduction |
| | `setTrackFilter` | `track`, `filterType`, `cutoff`, `resonance`, `gain` | Filtro por track |
| | `clearTrackFilter` | `track` | Eliminar filtro de track |
| | `setPadFilter` | `pad`, `filterType`, `cutoff`, `resonance`, `gain` | Filtro por pad |
| | `clearPadFilter` | `pad` | Eliminar filtro de pad |
| **Samples** | `loadSample` | `family`, `filename`, `pad` | Cargar sample en pad |
| **LED** | `setLedMonoMode` | `value` (bool) | Modo mono LED |
| **Sync** | `get_pattern` | `pattern` (opcional) | Solicitar patrón |

---

## Notas Importantes

1. **Sin autenticación** - El protocolo UDP es abierto
2. **Baja latencia** - Ideal para triggers en tiempo real (<10ms)
3. **Sin garantía de entrega** - UDP no garantiza que lleguen todos los paquetes
4. **Sin orden** - Los paquetes pueden llegar desordenados
5. **El MASTER siempre responde** - Confirma recepción con JSON
6. **Tamaño máximo** - 512 bytes por comando, 2048 bytes para `pattern_sync`
7. **Los cambios se propagan** - Todos los clientes WebSocket también reciben los cambios
8. **Timeout recomendado** - 500ms para esperar respuesta del MASTER

---

## Debugging

### En el MASTER (Serial Monitor):
```
[UDP] Received 25 bytes from 192.168.4.2:54321
[UDP] Data: {"cmd":"trigger","pad":0}
► Pattern 0 sent to SLAVE 192.168.4.2
```

### En el SLAVE:
```
→ MASTER: {"cmd":"trigger","pad":0,"vel":127}
← MASTER: {"status":"ok"}
✓ Comando OK
```
