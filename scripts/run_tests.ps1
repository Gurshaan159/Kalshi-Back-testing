param(
  [switch]$CleanBuild
)

$ErrorActionPreference = "Stop"

function Get-CMakePath {
  $cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
  if ($cmakeCmd) { return $cmakeCmd.Source }
  $fallback = "C:\Program Files\CMake\bin\cmake.exe"
  if (Test-Path $fallback) { return $fallback }
  throw "cmake is not installed or not on PATH."
}

function Get-CtestPath {
  $ctestCmd = Get-Command ctest -ErrorAction SilentlyContinue
  if ($ctestCmd) { return $ctestCmd.Source }
  $fallback = "C:\Program Files\CMake\bin\ctest.exe"
  if (Test-Path $fallback) { return $fallback }
  throw "ctest is not installed or not on PATH."
}

function Get-LlvmMingwBin {
  $base = "C:\Users\$env:USERNAME\AppData\Local\Microsoft\WinGet\Packages"
  if (!(Test-Path $base)) { return $null }
  $dirs = Get-ChildItem -Path $base -Directory -Filter "MartinStorsjo.LLVM-MinGW.UCRT_*"
  if (-not $dirs) { return $null }
  $bin = Get-ChildItem -Path $dirs[0].FullName -Directory -Recurse -Filter "bin" |
    Where-Object { Test-Path (Join-Path $_.FullName "g++.exe") } |
    Select-Object -First 1
  if ($bin) { return $bin.FullName }
  return $null
}

$cmake = Get-CMakePath
$ctest = Get-CtestPath
$llvmMingwBin = Get-LlvmMingwBin

if ($llvmMingwBin) {
  $env:Path = "$llvmMingwBin;$env:Path"
}

if ($CleanBuild -and (Test-Path ".\build")) {
  Remove-Item -Recurse -Force ".\build"
}

if ($llvmMingwBin -and (Test-Path (Join-Path $llvmMingwBin "g++.exe")) -and (Test-Path (Join-Path $llvmMingwBin "mingw32-make.exe"))) {
  & $cmake -S . -B build -G "MinGW Makefiles" `
    -DCMAKE_CXX_COMPILER="$(Join-Path $llvmMingwBin 'g++.exe')" `
    -DCMAKE_MAKE_PROGRAM="$(Join-Path $llvmMingwBin 'mingw32-make.exe')" `
    -DCMAKE_BUILD_TYPE=Release
} else {
  & $cmake -S . -B build
}

& $cmake --build build --config Release
& $ctest --test-dir build --output-on-failure
