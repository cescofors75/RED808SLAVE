param(
    [Parameter(Mandatory=$true)][ValidateSet('p4','s3')][string]$Device,
    [Parameter(Mandatory=$true)][ValidateSet('upload','uploadfs')][string]$Target,
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [string]$Environment
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
    Write-Host "ERROR: Dispositivo $Device no encontrado. Conectalo y vuelve a intentar." -ForegroundColor Red
    Write-Host "Puertos disponibles:" -ForegroundColor Yellow
    $devs | ForEach-Object { Write-Host ("  " + $_.port + "  " + $_.description) }
    exit 1
}

Write-Host ("Detectado en " + $found.port + " (" + $found.description + ")") -ForegroundColor Green

$args = @('run', '--project-dir', $ProjectDir, '--target', $Target, '--upload-port', $found.port)
if ($Environment) { $args += @('--environment', $Environment) }

& $pio @args
exit $LASTEXITCODE
