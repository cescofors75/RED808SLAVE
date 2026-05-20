param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$workspace  = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$dest       = Join-Path $workspace ".tools\mediapipe"
$wasmDest   = Join-Path $dest "wasm"

New-Item -ItemType Directory -Path $dest     -Force | Out-Null
New-Item -ItemType Directory -Path $wasmDest -Force | Out-Null

$version  = "0.10.14"
$cdn      = "https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@$version"
$modelUrl = "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"

$files = @(
    @{ Url = "$cdn/vision_bundle.mjs";                        Dest = Join-Path $dest     "vision_bundle.mjs" }
    @{ Url = "$cdn/wasm/vision_wasm_internal.js";             Dest = Join-Path $wasmDest "vision_wasm_internal.js" }
    @{ Url = "$cdn/wasm/vision_wasm_internal.wasm";           Dest = Join-Path $wasmDest "vision_wasm_internal.wasm" }
    @{ Url = "$cdn/wasm/vision_wasm_nosimd_internal.js";      Dest = Join-Path $wasmDest "vision_wasm_nosimd_internal.js" }
    @{ Url = "$cdn/wasm/vision_wasm_nosimd_internal.wasm";    Dest = Join-Path $wasmDest "vision_wasm_nosimd_internal.wasm" }
    @{ Url = $modelUrl;                                       Dest = Join-Path $dest     "hand_landmarker.task" }
)

$total = $files.Count
$done  = 0

foreach ($f in $files) {
    if (-not $Force -and (Test-Path $f.Dest)) {
        $size = (Get-Item $f.Dest).Length
        Write-Host "SKIP (ya existe, $([math]::Round($size/1KB,0)) KB): $(Split-Path $f.Dest -Leaf)"
        $done++
        continue
    }
    Write-Host "Descargando ($($done+1)/$total): $(Split-Path $f.Dest -Leaf) ..."
    try {
        Invoke-WebRequest -Uri $f.Url -OutFile $f.Dest -UseBasicParsing
        $size = (Get-Item $f.Dest).Length
        Write-Host "  OK: $([math]::Round($size/1KB,0)) KB"
    } catch {
        Write-Error "FALLÓ: $($f.Url)`n$_"
    }
    $done++
}

Write-Host ""
Write-Host "MediaPipe listo en: $dest"
Write-Host "Ahora puedes usar gesture-pro sin internet."
