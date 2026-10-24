$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
cmake -S $root -B "$root/build" -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build "$root/build" --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir "$root/build" -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
Write-Host "EXE: $root/build/Release/PortableMixer.exe"
Write-Host 'Build/core tests do not validate WASAPI or Discord. Follow docs/WINDOWS_TEST_PLAN.md.'
