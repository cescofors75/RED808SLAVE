# RED808 Command Contract

Este es el contrato unico vigente entre BlueSlaveP4, BlueSlaveV2 y RedMaster ESP32-S3. Daisy Seed no participa en este contrato JSON: Daisy recibe comandos binarios por SPI real desde RedMaster.

## Topologia

| Enlace | Transporte | Propietario | Formato |
|---|---|---|---|
| BlueSlaveV2 -> BlueSlaveP4 | USB CDC/UART | V2/P4 | Binario `uart_protocol.h` |
| BlueSlaveP4 -> RedMaster ESP32-S3 | WiFi UDP `8888` | P4 gateway | JSON UTF-8 |
| BlueSlaveV2 -> RedMaster ESP32-S3 | WiFi UDP `8888` opcional | V2 fallback | JSON UTF-8 |
| RedMaster ESP32-S3 -> Daisy Seed | SPI real | RedMaster | Binario `protocol.h` |

La fuente de verdad musical es RedMaster ESP32-S3. P4/S3 pueden editar o pedir estado, pero deben aceptar `pattern_sync` y `state_sync` como confirmacion autoritativa del Master.

## Reglas JSON

- Campo obligatorio: `cmd`.
- Indices base cero: patrones `0..15`, tracks `0..15`, steps `0..63`.
- Volumen master/seq/live: `0..150`. Volumen por track: `0..150` en UDP; si cruza UART hacia S3 se limita a `0..100` por compatibilidad del paquete binario.
- Velocity: `1..127`; `0` se reserva para apagado cuando el comando lo soporte explicitamente.
- UDP maximo operativo: 4096 bytes. `pattern_sync` completo usa `data` plano por compatibilidad P4/S3.

## Comandos P4/S3 -> Master

### Patrones

`selectPattern`

```json
{"cmd":"selectPattern","index":0}
```

Selecciona el patron actual en Master. El Master responde con `state_sync` y puede enviar `pattern_sync` si el cliente lo pide.

`get_pattern`

```json
{"cmd":"get_pattern","pattern":0}
```

Solicita el patron indicado. Si `pattern` falta, el Master responde con el actual.

`setStep`

```json
{"cmd":"setStep","pattern":0,"track":0,"step":0,"active":true,"noteLen":1,"silent":false}
```

`pattern` es opcional; sin el se escribe el patron actual. `silent:true` evita broadcasts WebSocket durante importaciones masivas.

`setStepVelocity`

```json
{"cmd":"setStepVelocity","pattern":0,"track":0,"step":0,"velocity":110,"silent":false}
```

Acepta steps `0..63`.

### Mixer

`mute`

```json
{"cmd":"mute","track":0,"value":true}
```

`solo`

```json
{"cmd":"solo","track":0,"value":true}
```

Alias aceptado por Master: `setTrackSolo`.

`setTrackVolume`

```json
{"cmd":"setTrackVolume","track":0,"volume":100}
```

`getTrackVolumes`

```json
{"cmd":"getTrackVolumes"}
```

### FX Master

Comandos canonicos:

```json
{"cmd":"setFilter","type":1}
{"cmd":"setFilterCutoff","value":12000}
{"cmd":"setFilterResonance","value":2.4}
{"cmd":"setDistortion","value":0.35}
{"cmd":"setBitCrush","value":12}
{"cmd":"setSampleRate","value":24000}
{"cmd":"setDelayActive","value":true}
{"cmd":"setDelayMix","value":45}
{"cmd":"setReverbActive","value":true}
{"cmd":"setReverbMix","value":0.35}
{"cmd":"setChorusActive","value":true}
{"cmd":"setChorusMix","value":0.30}
```

Los campos `value` de mix pueden llegar como `0..1` o `0..100`; Master normaliza segun el comando existente.

### FX Per-Track

```json
{"cmd":"setTrackReverbSend","track":0,"value":40}
{"cmd":"setTrackDelaySend","track":0,"value":25}
{"cmd":"setTrackChorusSend","track":0,"value":30}
{"cmd":"setTrackPan","track":0,"value":-20}
{"cmd":"setTrackEcho","track":0,"active":true,"time":120,"feedback":35,"mix":30}
{"cmd":"setTrackFlanger","track":0,"active":true,"rate":60,"depth":50,"feedback":25}
{"cmd":"setTrackCompressor","track":0,"active":true,"threshold":-20,"ratio":4}
```

## Comandos Master -> P4/S3

### `pattern_sync`

```json
{
  "cmd":"pattern_sync",
  "pattern":0,
  "stepCount":64,
  "data":[[1,0,0,0],[0,0,1,0]]
}
```

`data` contiene 16 arrays de track. El Master puede enviar 64 steps; P4 renderiza/aplica su vista actual de 16 steps mientras S3 conserva hasta 64.

### `state_sync`

Snapshot compacto del estado autoritativo del Master hacia P4/S3.

```json
{
  "cmd":"state_sync",
  "pattern":0,
  "playing":false,
  "tempo":120.0,
  "stepCount":64,
  "masterVolume":100,
  "sequencerVolume":100,
  "liveVolume":100,
  "mute":[false],
  "solo":[false],
  "trackVolumes":[100],
  "fx":{
    "filterType":1,
    "delayActive":true,
    "reverbActive":false,
    "chorusActive":false,
    "trackReverbSend":[0],
    "trackDelaySend":[0],
    "trackChorusSend":[0],
    "trackEcho":[false],
    "trackFlanger":[false],
    "trackCompressor":[false]
  },
  "kit":"RED 808 KARZ",
  "samples":[{"pad":0,"loaded":true,"name":"BD.WAV"}]
}
```

El Master envia `state_sync` al saludar (`hello`), al recibir `get_state`, periodicamente a clientes UDP vivos y tras cambios de patron/mixer/FX/kit relevantes.

## Daisy Seed

Daisy no usa UART legacy para el enlace RedMaster. El enlace operativo es SPI real:

- Master: `RedMaster_ESP32S3/src/SPIMaster.*` y `RedMaster_ESP32S3/src/protocol.h`.
- Slave: `RedMaster_DaisySeed64MB/DaisySeed/main.cpp`.
- Paquete: header binario con magic/command/length/seq/crc y payload DSQ/audio.

No documentar ni implementar nuevos comandos hacia Daisy como JSON UDP o UART. Si un control viene de P4/S3, primero entra al contrato JSON del Master y luego RedMaster traduce a SPI.