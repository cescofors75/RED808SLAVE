param(
    [Parameter(Mandatory=$true)][ValidateSet('p4','s3')][string]$Device,
    [int]$Baud = 115200
)

$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'

$vidPid = switch ($Device) {
    'p4' { '303A:1001' }
    's3' { '1A86:55D3' }
}

Write-Host "Buscando dispositivo $Device (VID:PID=$vidPid)..." -ForegroundColor Cyan
$json = & $pio device list --json-output
$devs = $json | ConvertFrom-Json
$found = $devs | Where-Object { $_.hwid -match "VID:PID=$vidPid" } | Select-Object -First 1

if (-not $found) {
    Write-Host "ERROR: Dispositivo $Device no encontrado." -ForegroundColor Red
    $devs | ForEach-Object { Write-Host ("  " + $_.port + "  " + $_.description) }
    exit 1
}

Write-Host ("Monitor en " + $found.port + " @ " + $Baud) -ForegroundColor Green
& $pio device monitor --port $found.port --baud $Baud --filter esp32_exception_decoder --filter time
exit $LASTEXITCODE
