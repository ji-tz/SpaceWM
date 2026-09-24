# Enable Windows Sandbox (requires elevated PowerShell)
# Usage: run PowerShell as Administrator, then:
#   .\enable-windows-sandbox.ps1
#
# After enabling, reboot if prompted, then launch "Windows Sandbox" from Start.

$ErrorActionPreference = 'Stop'

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
    Write-Error "Please run this script as Administrator (right-click PowerShell -> Run as administrator)."
    exit 1
}

Write-Host "Enabling Windows Sandbox (Containers-DisposableClientVM)..."
$feat = Get-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM
Write-Host ("Current state: {0}" -f $feat.State)

if ($feat.State -ne 'Enabled') {
    Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All -NoRestart
    Write-Host "Feature enabled. A reboot may be required."
} else {
    Write-Host "Already enabled."
}

# Hyper-V may be required on some SKUs
$hyperv = Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All -ErrorAction SilentlyContinue
if ($hyperv -and $hyperv.State -ne 'Enabled') {
    Write-Host "Note: Hyper-V feature is present but not enabled: $($hyperv.State)"
}

Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Reboot if Windows asks you to."
Write-Host "  2. Start menu -> 'Windows Sandbox'."
Write-Host "  3. Copy build\SpaceWM.exe + lib\ DLLs into the sandbox (shared folders or clipboard)."
Write-Host "  4. Run automated tests inside the sandbox:"
Write-Host "       ctest --output-on-failure"
Write-Host ""
Write-Host "Alternatives if Sandbox is unavailable:"
Write-Host "  - Separate Windows user session for manual QA"
Write-Host "  - A disposable VM (Hyper-V / VirtualBox)"
Write-Host "  - Keep automated tests on host (they only create short-lived own windows)"
