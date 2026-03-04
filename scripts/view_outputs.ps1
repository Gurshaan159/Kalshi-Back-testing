param(
  [string]$OutDir = "out",
  [string]$Log = "logs/run.log"
)

$ErrorActionPreference = "Stop"

Write-Host "=== Metrics ==="
if (Test-Path "$OutDir/metrics.json") {
  Get-Content "$OutDir/metrics.json"
} else {
  Write-Host "metrics.json not found"
}

Write-Host ""
Write-Host "=== Last 20 tick log lines ==="
if (Test-Path $Log) {
  Get-Content $Log | Select-Object -Last 20
} else {
  Write-Host "log file not found"
}

Write-Host ""
Write-Host "=== Last 10 equity rows ==="
if (Test-Path "$OutDir/equity.csv") {
  Get-Content "$OutDir/equity.csv" | Select-Object -Last 10
} else {
  Write-Host "equity.csv not found"
}

Write-Host ""
Write-Host "=== Last 10 trades rows ==="
if (Test-Path "$OutDir/trades.csv") {
  Get-Content "$OutDir/trades.csv" | Select-Object -Last 10
} else {
  Write-Host "trades.csv not found"
}
