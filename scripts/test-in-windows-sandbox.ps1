# Stage the current build and run it on an isolated Windows Sandbox desktop.
# Usage: .\scripts\test-in-windows-sandbox.ps1 [-PrepareOnly]
# No build, feature installation, host shutdown or reboot is performed here.
[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildDir = (Resolve-Path $BuildDir).Path
foreach ($required in @('SpaceWM.exe', 'tests\test_overview_spaces.exe', 'tests\Qt6Test.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $required))) {
        throw "Build/deploy first; missing: $required"
    }
}
$ctest = (Get-Command ctest.exe -ErrorAction Stop).Source
$run = Join-Path $repo ('build\sandbox-runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$inputDir = Join-Path $run 'input'
$resultDir = Join-Path $run 'results'
$project = Join-Path $inputDir 'project'
$testDir = Join-Path $project 'build\tests'
New-Item -ItemType Directory -Path $testDir, $resultDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'AGENTS.md'), (Join-Path $repo 'CMakeLists.txt') -Destination $project
New-Item -ItemType Directory -Path (Join-Path $project 'src') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'scripts') -Destination $project -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'lib') -Destination $project -Recurse
# Copy runtime children explicitly, including the Qt plugin directories.
Get-ChildItem -LiteralPath (Join-Path $repo 'lib') | Copy-Item -Destination (Join-Path $project 'build') -Recurse
Copy-Item -LiteralPath (Join-Path $BuildDir 'SpaceWM.exe') -Destination (Join-Path $project 'build')
$sourceTests = Join-Path $BuildDir 'tests'
foreach ($file in (Get-ChildItem -LiteralPath $sourceTests -File -Recurse | Where-Object { $_.Extension -in '.exe', '.dll' })) {
    $relative = $file.FullName.Substring($sourceTests.Length).TrimStart('\')
    $destination = Join-Path $testDir $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}
Copy-Item -LiteralPath $ctest -Destination (Join-Path $inputDir 'ctest.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'windows-sandbox-guest.ps1') -Destination $inputDir

# Re-register the entire suite at its guest location. Build-generated CTest
# files contain host-absolute paths, including the source path in __FILE__.
$cmakeText = Get-Content -LiteralPath (Join-Path $repo 'tests\CMakeLists.txt') -Raw
$match = [regex]::Match($cmakeText, '(?s)set\(SPACEWM_TEST_NAMES\s+(.*?)\)')
if (-not $match.Success) { throw 'Cannot find SPACEWM_TEST_NAMES' }
$names = @([regex]::Matches($match.Groups[1].Value, '\btest_[a-z_]+\b') | ForEach-Object { $_.Value })
if ($names.Count -eq 0) { throw 'No tests registered' }
$guestRepo = 'D:/workspace/SpaceWM'
$manifest = foreach ($name in $names) {
    'add_test({0} "{1}/build/tests/{0}.exe" "-o" "{0}.out.txt,txt")' -f $name, $guestRepo
    $timeout = 60
    if ($name -eq 'test_package') { $timeout = 180 }
    if ($name -eq 'test_integration_flow') { $timeout = 300 }
    'set_tests_properties({0} PROPERTIES TIMEOUT {1})' -f $name, $timeout
}
$manifest | Set-Content -LiteralPath (Join-Path $testDir 'CTestTestfile.cmake') -Encoding ASCII
'subdirs("tests")' | Set-Content -LiteralPath (Join-Path $project 'build\CTestTestfile.cmake') -Encoding ASCII
@{
    created = (Get-Date).ToString('o')
    source = $repo
    build = $BuildDir
    tests = $names
    executableSha256 = (Get-FileHash -LiteralPath (Join-Path $BuildDir 'SpaceWM.exe') -Algorithm SHA256).Hash
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $inputDir 'manifest.json') -Encoding UTF8

function Escape-Xml([string]$Value) { [System.Security.SecurityElement]::Escape($Value) }
$configuration = @"
<Configuration>
  <VGpu>Enable</VGpu>
  <Networking>Disable</Networking>
  <ClipboardRedirection>Disable</ClipboardRedirection>
  <AudioInput>Disable</AudioInput>
  <VideoInput>Disable</VideoInput>
  <PrinterRedirection>Disable</PrinterRedirection>
  <MemoryInMB>6144</MemoryInMB>
  <MappedFolders>
    <MappedFolder>
      <HostFolder>$(Escape-Xml $inputDir)</HostFolder>
      <SandboxFolder>C:\SpaceWM-Input</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$(Escape-Xml $resultDir)</HostFolder>
      <SandboxFolder>C:\SpaceWM-Results</SandboxFolder>
      <ReadOnly>false</ReadOnly>
    </MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\SpaceWM-Input\windows-sandbox-guest.ps1</Command>
  </LogonCommand>
</Configuration>
"@
$configPath = Join-Path $run 'SpaceWM-tests.wsb'
$configuration | Set-Content -LiteralPath $configPath -Encoding UTF8
Write-Output "Configuration: $configPath"
Write-Output "Results: $resultDir"
Write-Output "Registered tests: $($names.Count)"
if (-not $PrepareOnly) {
    # A visible interactive desktop is required for native UI and screen capture.
    Start-Process -FilePath "$env:WINDIR\System32\WindowsSandbox.exe" -ArgumentList ('"' + $configPath + '"')
}
