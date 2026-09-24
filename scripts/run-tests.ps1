# Run SpaceWM automated tests with minimal disruption to the host session.
# Tests only create short-lived windows in the test process and touch a few
# system query APIs. They do NOT leave windows cloaked on exit.
#
# Usage:
#   .\scripts\run-tests.ps1
#   .\scripts\run-tests.ps1 -BuildDir D:\workspace\SpaceWM\build

param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path $BuildDir).Path

Write-Host "Build dir: $BuildDir"
if (-not (Test-Path (Join-Path $BuildDir "CTestTestfile.cmake"))) {
    Write-Error "No CTest files in $BuildDir — configure/build first."
    exit 1
}

# Prefer already-deployed Qt next to tests
$testDir = Join-Path $BuildDir "tests"
if (Test-Path $testDir) {
    $env:PATH = "$testDir;$env:PATH"
}

Push-Location $BuildDir
try {
    ctest --output-on-failure --timeout 60
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($code -ne 0) {
    Write-Error "Tests failed with exit code $code"
    exit $code
}
Write-Host "All tests passed."
