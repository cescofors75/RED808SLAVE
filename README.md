# XboxBLE Workspace

Este workspace contiene varios restos de scaffolding, pero el firmware activo que debe compilarse y subirse es BlueSlaveV2.

## Requisitos

- [Visual Studio Code](https://code.visualstudio.com/)
- [Extensión PlatformIO IDE](https://platformio.org/install/ide?install=vscode)

## Proyecto activo

- Firmware principal: BlueSlaveV2
- Entrada principal: BlueSlaveV2/src/main.cpp
- Configuración PlatformIO usada desde la raíz: platformio.ini
- Placa objetivo: Waveshare ESP32-S3-Touch-LCD-7B

## Estructura relevante

```
XboxBLE/
├── BlueSlaveV2/
│   ├── include/
│   ├── lib/
│   ├── src/
│   ├── test/
│   └── platformio.ini
├── boards/
├── platformio.ini
└── .vscode/
```

## Nota importante

No uses como referencia el src/main.cpp de la raíz del workspace. La configuración de la raíz ahora redirige la compilación a BlueSlaveV2 para evitar cargar por error una UI distinta.

## Compilar el Proyecto

### Desde VS Code:
1. Abre la paleta de comandos (Ctrl+Shift+P)
2. Busca "PlatformIO: Build"
3. O usa el ícono de ✓ en la barra inferior

### Desde terminal:
```bash
platformio run
```

## Subir el Código al ESP32

### Desde VS Code:
1. Conecta tu ESP32 al puerto USB
2. Abre la paleta de comandos (Ctrl+Shift+P)
3. Busca "PlatformIO: Upload"
4. O usa el ícono de → en la barra inferior

### Desde terminal:
```bash
platformio run --target upload
```

## Monitor Serial

Para ver la salida del ESP32:

### Desde VS Code:
- Usa el ícono de 🔌 en la barra inferior

### Desde terminal:
```bash
platformio device monitor
```

## Estado

Las tareas de VS Code y la compilación lanzada desde la raíz deben producir el firmware BlueSlaveV2.

## Solución de Problemas

### El ESP32 no se detecta
- Verifica que el cable USB transmita datos (no solo carga)
- Instala los drivers USB-Serial (CP210x o CH340)
- Verifica el puerto COM en el administrador de dispositivos

### Error de compilación
- Limpia el proyecto: `platformio run --target clean`
- Elimina la carpeta `.pio` y vuelve a compilar

## Recursos Adicionales

- [Documentación PlatformIO](https://docs.platformio.org/)
- [Referencia ESP32 Arduino](https://docs.espressif.com/projects/arduino-esp32/)
- [Ejemplos ESP32](https://github.com/espressif/arduino-esp32/tree/master/libraries)
