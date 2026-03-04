$ErrorActionPreference = "Stop"

Write-Host "== Running tests =="
& ".\scripts\run_tests.ps1"

Write-Host ""
Write-Host "== Running sample backtest =="
& ".\scripts\run_backtest.ps1"

Write-Host ""
Write-Host "== Showing outputs =="
& ".\scripts\view_outputs.ps1"
