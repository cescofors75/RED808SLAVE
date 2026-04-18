# =============================================================================
# Verifies that BlueSlaveV2/include/uart_protocol.h and
# BlueSlaveP4/include/uart_protocol.h are byte-identical.
# Exits non-zero (and prints the diff) if they drift.
# Run from workspace root:
#   pwsh -File scripts/check_uart_protocol_sync.ps1
# =============================================================================
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$v2   = Join-Path $root "BlueSlaveV2\include\uart_protocol.h"
$p4   = Join-Path $root "BlueSlaveP4\include\uart_protocol.h"

if (-not (Test-Path $v2)) { Write-Error "Missing $v2"; exit 2 }
if (-not (Test-Path $p4)) { Write-Error "Missing $p4"; exit 2 }

$h1 = (Get-FileHash $v2 -Algorithm MD5).Hash
$h2 = (Get-FileHash $p4 -Algorithm MD5).Hash

if ($h1 -ne $h2) {
    Write-Host "uart_protocol.h DESINCRONIZADO" -ForegroundColor Red
    Write-Host "  V2: $h1"
    Write-Host "  P4: $h2"
    Write-Host "Diff:" -ForegroundColor Yellow
    git --no-pager diff --no-index --color=always $v2 $p4
    exit 1
}

Write-Host "uart_protocol.h sincronizado ($h1)" -ForegroundColor Green
exit 0
