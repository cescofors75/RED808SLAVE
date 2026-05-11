# SlavePico (ESP32-P4)

Nodo esclavo sin pantalla para RED808.

## Objetivo Fase 1
- Conectarse por WiFi al master (AP `RED808`) igual que P4.
- Publicar eventos UDP JSON al master (`192.168.4.1:8888`).
- Leer dispositivos por hub I2C (PCA9548A) con canales fijos.
- Dejar preparada la arquitectura para fase 2 (rotaries analogicos por interrupciones).

## Hardware previsto
- 1x ESP32-P4-Pico (Waveshare)
- 1x PCA9548A (hub I2C)
- 4x DFRobot Rotary Infinite Ring LED (SEN0502)
- 2x M5Stack 8Encoder LED (addr 0x41)
- 2x M5Stack ByteButton (addr 0x47)
- 1x M5Stack Unit Fader

## Estructura
- `include/config.h`: pines, red, canales hub y temporizaciones.
- `include/device_map.h`: IDs logicos de controles.
- `src/drivers/i2c_driver.*`: mutex + seleccion de canal del hub.
- `src/drivers/input_manager.*`: polling de entradas (scaffold fase 1).
- `src/comm/udp_handler.*`: transporte UDP JSON al master.
- `src/main.cpp`: setup, tareas y loop principal.

## Build
Desde esta carpeta:

```powershell
$env:PLATFORMIO_CORE_DIR="$env:USERPROFILE\.platformio"
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
& $pio run
```
