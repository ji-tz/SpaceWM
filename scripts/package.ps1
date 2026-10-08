# Stage and package SpaceWM for distribution.
#   dist/SpaceWM-win64/          staged portable run dir (exe + lib/ runtime)
#   dist/SpaceWM-win64.zip       portable zip (CI artifact / Release asset)
#   dist/SpaceWM-Setup-x64.exe   installer (with -Installer; needs Inno Setup 6)
#
# Usage:
#   .\scripts\package.ps1
#   .\scripts\package.ps1 -Installer
#   .\scripts\package.ps1 -OutDir D:\out -IsccPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

param(
    [string]$BuildDir  = (Join-Path $PSScriptRoot "..\build"),
    [string]$LibDir    = (Join-Path $PSScriptRoot "..\lib"),
    [string]$OutDir    = (Join-Path $PSScriptRoot "..\dist"),
    [switch]$Installer,
    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not (Test-Path $BuildDir)) {
    Write-Error "Build dir not found: '$BuildDir' - build SpaceWM first."
    exit 1
}
if (-not (Test-Path $LibDir)) {
    Write-Error "Runtime dir not found: '$LibDir'."
    exit 1
}
$BuildDir = (Resolve-Path $BuildDir).Path
$LibDir   = (Resolve-Path $LibDir).Path
$OutDir   = [System.IO.Path]::GetFullPath($OutDir)

$exe = Join-Path $BuildDir "SpaceWM.exe"
if (-not (Test-Path $exe)) {
    Write-Error "SpaceWM.exe not found in '$BuildDir' - build first."
    exit 1
}
if (-not (Test-Path (Join-Path $LibDir "Qt6Core.dll"))) {
    Write-Error "Portable runtime incomplete in '$LibDir' (Qt6Core.dll missing)."
    exit 1
}

# ---- Stage ------------------------------------------------------------------
$stage = Join-Path $OutDir "SpaceWM-win64"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -LiteralPath $exe -Destination $stage
Copy-Item (Join-Path $LibDir "*") $stage -Recurse

# ---- Zip + self-check -------------------------------------------------------
$zip = Join-Path $OutDir "SpaceWM-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
# Windows' ZipArchive uses '\' as entry separator - normalize before comparing.
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
$entries = @($archive.Entries | ForEach-Object { $_.FullName -replace '\\', '/' })
$archive.Dispose()
$required = @(
    "SpaceWM.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "vcruntime140.dll",
    "platforms/qwindows.dll"
)
$missing = $required | Where-Object { $_ -notin $entries }
if ($missing) {
    Write-Error "Zip verification failed, missing entries: $($missing -join ', ')"
    exit 1
}
Write-Host ("Zip: {0} ({1:N0} bytes, {2} entries)" -f $zip, (Get-Item $zip).Length, $entries.Count)

# ---- Installer (optional) ---------------------------------------------------
if (-not $Installer) { exit 0 }

function Find-Iscc {
    param([string]$Explicit)
    $candidates = @()
    if ($Explicit) { $candidates += $Explicit }
    $cmd = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($cmd) { $candidates += $cmd.Source }
    $candidates += @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path }
    }
    return $null
}

$iscc = Find-Iscc -Explicit $IsccPath
if (-not $iscc) {
    Write-Error "ISCC.exe not found. Install Inno Setup 6, e.g.: winget install JRSoftware.InnoSetup"
    exit 2
}

$cmakeText = Get-Content (Join-Path $repoRoot "CMakeLists.txt") -Raw
if ($cmakeText -notmatch 'project\(\s*SpaceWM\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    Write-Error "Cannot parse SpaceWM version from CMakeLists.txt"
    exit 1
}
$version = $Matches[1]

$setup = Join-Path $OutDir "SpaceWM-Setup-x64.exe"
if (Test-Path $setup) { Remove-Item $setup -Force }

& $iscc "/DAppVersion=$version" "/DStageDir=$stage" "/DOutputDir=$OutDir" `
    (Join-Path $repoRoot "installer\SpaceWM.iss")
if ($LASTEXITCODE -ne 0) {
    Write-Error "ISCC failed with exit code $LASTEXITCODE"
    exit 1
}
if (-not (Test-Path $setup)) {
    Write-Error "Installer not produced at '$setup'"
    exit 1
}
Write-Host ("Installer: {0} ({1:N0} bytes, version {2})" -f $setup, (Get-Item $setup).Length, $version)
