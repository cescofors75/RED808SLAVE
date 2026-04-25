# BlueSlaveP4 - tareas propuestas de auditoria

Revision: 2026-04-25
Estado de build: `platformio run --project-dir BlueSlaveP4 --environment esp32p4` OK. RAM 15.9%, Flash 22.8%.
Estado release: `platformio run --project-dir BlueSlaveP4 --environment esp32p4-release` OK. RAM 15.9%, Flash 22.7%.

## Prioridad alta - posibles bugs funcionales

- [x] Corregir reintento de sincronizacion UDP con Master.
  - En `BlueSlaveP4/src/udp_handler.cpp`, `udp_request_master_sync()` deja `syncRequested = true`, pero `udp_handler_process()` solo reintenta si `!syncRequested && !masterAlive`. Si el Master no responde o hay timeout, el P4 puede quedarse sin volver a pedir `hello`, `get_pattern` y `getTrackVolumes`.
  - Implementar reintento periodico mientras `udpStarted && !masterAlive`, o convertir `syncRequested` en estado con timeout real.

- [x] Usar el validador UART estricto en recepcion basica.
  - `include/uart_protocol.h` define `uart_validate_packet()` con rangos por tipo/id, pero `src/uart_handler.cpp` llama solo a `uart_validate_basic()`.
  - Cambiar la recepcion para rechazar paquetes validos de checksum pero fuera de protocolo.

- [x] Separar parser RX para USB CDC y UART, o deshabilitar UART RX cuando USB esta conectado.
  - `src/uart_handler.cpp` usa un unico `rxBuf/rxHead` para bytes de UART y USB. Si ambos transportes entregan bytes en la misma vuelta de loop, se pueden mezclar tramas.
  - Crear dos instancias de parser pequenas (`ParserState`) o escoger explicitamente un transporte activo.

- [x] Hacer segura la cola de pads ante overflow.
  - `src/ui/ui_screens.cpp` usa un ring de 32 eventos y `enqueue_pad_event()` avanza `head` sin comprobar si pisa `tail`.
  - Anadir deteccion de lleno, contador de drops y politica clara: descartar nuevo evento o sobrescribir el mas viejo.

- [ ] Revisar data races entre tareas/core sobre `p4`, `p4sd` y espectro DSP.
  - `P4State p4` se escribe desde UDP/UART/loop y se lee desde la tarea LVGL; varias arrays se actualizan sin seccion critica ni snapshot.
  - [x] `dsp_get_spectrum()` ya copia un snapshot protegido; queda pendiente extender el mismo patron a `p4`/`p4sd` si aparecen glitches.
  - Introducir snapshots atomicos/critical sections cortas para UI, o una cola/event bus de estado.

- [x] Proteger el ring buffer USB CDC con atomicos o critical section.
  - `src/usb_cdc_handler.cpp` modifica `usbRxHead` en callback y `usbRxTail` en loop usando `volatile int`; esto no garantiza atomicidad ni orden de memoria.
  - Usar `std::atomic<uint16_t>` o `portENTER_CRITICAL`, y anadir contador de overflow.

## Prioridad media - estabilidad y robustez

- [x] Manejar fallos de creacion de semaforos/tareas LVGL/touch/DSP.
  - `lvgl_port_init()` y `dsp_task_init()` no verifican retorno de `xSemaphoreCreate*` ni `xTaskCreatePinnedToCore()`.
  - En hardware real, un fallo de memoria quedaria como cuelgue dificil de diagnosticar.

- [ ] No reiniciar FX globales en cada handshake sin confirmar intencion.
  - `udp_request_master_sync()` manda varios `set*Active=0` y `setFilter=0` al reconectar.
  - Puede apagar efectos que el Master o S3 consideraban activos. Definir si P4 debe restaurar estado local o solo pedir sync.

- [x] Sustituir credenciales WiFi hardcodeadas por configuracion.
  - `WIFI_SSID` y `WIFI_PASS` viven en `src/udp_handler.cpp`.
  - Mover a `config.h`, `secrets.h` ignorado por git, NVS o provisioning simple.

- [x] Validar y acotar valores JSON recibidos desde Master.
  - `processJson()` acepta `pattern`, `track`, volumenes, BPM y FX sin clamp consistente.
  - Anadir clamps para proteger UI, arrays y comandos reenviados a S3.

- [ ] Decidir una unica autoridad de reloj o reconciliacion con Master.
  - P4 ignora `step_update/step_sync` y corre reloj local. Es rapido para UI, pero puede derivar si el Master real tiene jitter o tempo distinto.
  - Opcion: usar Master como fuente cuando esta conectado y local como fallback.

- [ ] Limpiar pantalla Performance o retirar navegacion a pantalla nula.
  - `create_performance_screen()` es stub y `scr_performance = NULL`.
  - Crear diagnostico real de FPS/heap/link o eliminar referencias para evitar botones muertos futuros.

## Prioridad media - rendimiento

- [x] Crear entorno release P4 separado de debug.
  - `BlueSlaveP4/platformio.ini` usa `-Og` y `CORE_DEBUG_LEVEL=3`; compila, pero no es ideal para latencia/render final.
  - Proponer `env:esp32p4-release` con `-O2` o `-Os`, `CORE_DEBUG_LEVEL=1/0` y logs desactivados.

- [ ] Reducir trabajo de JSON en caliente.
  - `udp_handler.cpp` formatea muchos JSON con `snprintf` y parsea con `JsonDocument` en stack en cada paquete.
  - Para rafagas de `setStep`, estudiar batch JSON o comandos compactos aceptados por Master.

- [ ] Reducir coste de refresco en FX screen.
  - `update_fx_screen()` actualiza arcos/labels de las 6 celdas cada ciclo aunque no cambien.
  - Cachear valores previos como en live/sequencer para reducir invalidaciones LVGL.

- [ ] Revisar frecuencia de polling touch 200 Hz.
  - 5 ms da baja latencia, pero compite con LVGL en Core 0.
  - Medir si 8-10 ms mantiene sensacion MPC con menos carga y menos contencion I2C.

## Prioridad baja - mantenimiento y calidad

- [ ] Extraer transport/protocol compartido con BlueSlaveV2.
  - UART protocol esta sincronizado, pero hay logica duplicada entre P4 y S3.
  - Crear `lib/RedShared/` para validadores, empaquetado de patrones y constantes comunes.

- [ ] Crear pruebas de parser MIDI y protocolo UART.
  - Testear VLQ invalido, running status, overflow de eventos, pattern packing/unpacking y validacion de paquetes.

- [ ] Anadir CI o tarea de verificacion local.
  - Build P4 + S3 + script `check_uart_protocol_sync.ps1` + inspeccion de MIDIs.
  - Evita drift del protocolo y regresiones de compilacion.

- [x] Normalizar nombres FX Flanger/Chorus.
  - El protocolo conserva `ENC_FLANGER`, pero varias pantallas/comentarios ya tratan encoder 0 como Chorus.
  - Mantener binario si hace falta, pero renombrar UI/comentarios para que no confunda.

- [ ] Revisar codigo muerto o deshabilitado.
  - `ripple_spawn()` contiene animacion completa detras de un `return` inmediato.
  - Eliminarla o dejarla como opcion compilable (`P4_ENABLE_RIPPLE`) si se quiere conservar.

## Verificaciones recomendadas despues de implementar

- [x] `platformio run --project-dir BlueSlaveP4 --environment esp32p4`
- [x] `platformio run --project-dir BlueSlaveV2 --environment esp32s3`
- [x] `scripts/check_uart_protocol_sync.ps1`
- [ ] Prueba real: boot sin Master, Master aparece tarde, Master se cae y vuelve.
- [ ] Prueba real: USB CDC conectado + UART fallback conectado para confirmar que no se mezclan tramas.
- [ ] Prueba real: multitouch rapido + note repeat + 16 levels durante rafaga UDP de `pattern_sync`.
