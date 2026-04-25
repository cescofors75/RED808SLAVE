# BlueSlaveV2 - auditoria y tareas

Revision: 2026-04-25
Arquitectura actual: S3 como superficie/controlador auxiliar por USB CDC hacia P4. WiFi directo del S3 esta desactivado por defecto (`S3_WIFI_ENABLED=0`) y queda como modo opcional.

Estado de build: `platformio run --project-dir BlueSlaveV2 --environment esp32s3` OK. RAM 22.9%, Flash 23.7%.

## Corregido

- [x] Endurecer `i2c_lock()` cuando no existe mutex.
  - Antes devolvia `true` si `i2c_bus_mutex` era nulo, lo que podia permitir accesos Wire sin proteccion si la inicializacion fallaba.
  - Ahora devuelve `false` y `i2c_init()` registra fallo si no puede crear el mutex.

- [x] Validar recursos LVGL antes de usarlos.
  - `lvgl_port_init()` ahora comprueba semaforos, buffers de dibujo/framebuffers y creacion de tarea.
  - `lvgl_port_lock()` y `lvgl_port_unlock()` toleran mutex nulo.

- [x] Proteger cola de triggers de pads entre tareas.
  - `pendingLivePadTriggerMask` ahora usa `std::atomic<uint32_t>` con `fetch_or()` y `exchange()`.
  - Evita perder o pisar triggers cuando la tarea de pads, la navegacion UI y el loop limpian/leen la mascara.

- [x] Proteger flag de refresco visual live pads.
  - `livePadsVisualDirty` ahora usa `std::atomic<bool>` y LVGL la consume con `exchange(false)`.

- [x] Validar paquetes basicos P4->S3 con validador estricto.
  - `uart_bridge_receive()` ahora usa `uart_validate_packet()` en vez de solo checksum basico.
  - Rechaza comandos checksum-validos pero fuera de rango antes de tocar estado.

- [x] Comprobar creacion de tareas principales.
  - `encoder_task`, `touch_task`, `pad_trigger_task` y `lvgl_task` registran si no pudieron crearse.

## Pendiente recomendado

- [ ] Probar en hardware USB CDC S3<->P4 con multitouch rapido + note repeat.
- [ ] Probar entrada/salida repetida de pantalla LIVE para confirmar que no quedan triggers fantasma.
- [ ] Medir I2C bajo carga: GT911 + M5 encoders + DFRobot + ByteButtons simultaneos.
- [ ] Revisar si `currentScreen`, `currentStep`, `isPlaying` y otros estados compartidos deben migrar a snapshots/atomicos si aparecen glitches reales.
- [ ] Mejorar feedback de carga MIDI grande: ahora se loguea overflow, pero la UI podria mostrar aviso de patron truncado.
- [ ] Si algun dia se activa `S3_WIFI_ENABLED=1`, auditar de nuevo buffer UDP, reconexion y autoridad de reloj para ese modo.

## Verificaciones

- [x] `platformio run --project-dir BlueSlaveV2 --environment esp32s3`
- [ ] Prueba real: boot con P4 conectado por USB y Master disponible desde P4.
- [ ] Prueba real: desconectar/reconectar USB S3<->P4 durante uso.
- [ ] Prueba real: cargar MIDI desde SD y comprobar patron en S3, P4 y Master.
