$ErrorActionPreference = "SilentlyContinue"

Get-Process -Name caddy | Stop-Process -Force
Write-Host "Proxy HTTPS detenido (procesos caddy finalizados)."
