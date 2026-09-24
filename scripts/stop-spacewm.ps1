# Graceful stop for SpaceWM before rebuilding the exe.
# Signals the named event so the app runs cloak::showAllHidden(), then waits.
# Force-kills only if it refuses to exit (windows may stay hidden in that case).
#
# Usage:
#   .\scripts\stop-spacewm.ps1
#   .\scripts\stop-spacewm.ps1 -TimeoutMs 5000

param(
    [int]$TimeoutMs = 4000
)

$ErrorActionPreference = 'SilentlyContinue'

$procs = @(Get-Process -Name SpaceWM -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) {
    Write-Host "SpaceWM not running."
    exit 0
}

# Signal graceful quit (same event name as main.cpp kQuitEventName).
try {
    $ev = [System.Threading.EventWaitHandle]::OpenExisting('SpaceWM-quit')
    if ($null -eq $ev) {
        $ev = New-Object System.Threading.EventWaitHandle($true, [System.Threading.EventResetMode]::ManualReset, 'SpaceWM-quit')
    }
    $ev.Set() | Out-Null
    Write-Host "Signaled SpaceWM-quit — waiting for graceful exit..."
}
catch {
    Write-Warning "Could not signal quit event: $_"
}

$deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
while ([DateTime]::UtcNow -lt $deadline) {
    $procs = @(Get-Process -Name SpaceWM -ErrorAction SilentlyContinue)
    if ($procs.Count -eq 0) {
        Write-Host "SpaceWM exited gracefully (windows uncloaked)."
        exit 0
    }
    Start-Sleep -Milliseconds 100
}

$procs = @(Get-Process -Name SpaceWM -ErrorAction SilentlyContinue)
if ($procs.Count -gt 0) {
    Write-Warning "Graceful quit timed out — FORCE KILL (hidden windows may remain)."
    $procs | Stop-Process -Force
    Start-Sleep -Milliseconds 300
}

Write-Host "SpaceWM stopped."
exit 0
