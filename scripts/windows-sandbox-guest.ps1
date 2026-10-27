# Windows PowerShell 5.1 compatible. Runs only inside the disposable guest.
$ErrorActionPreference = 'Stop'
$results = 'C:\SpaceWM-Results'
$guestRepo = 'D:\workspace\SpaceWM'
$summary = [ordered]@{ state = 'starting'; regressionExitCode = $null; integrationExitCode = $null }
function Save-Summary {
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $results 'summary.json') -Encoding UTF8
}
Start-Transcript -Path (Join-Path $results 'guest.log') -Force | Out-Null
try {
    Save-Summary
    # Preserve source paths compiled into tests (notably test_package/__FILE__).
    New-Item -ItemType Directory -Path 'C:\SpaceWM-Drive\workspace' -Force | Out-Null
    & subst.exe D: 'C:\SpaceWM-Drive'
    if ($LASTEXITCODE -ne 0) { throw 'Cannot map guest D: drive' }
    Copy-Item -LiteralPath 'C:\SpaceWM-Input\project' -Destination $guestRepo -Recurse
    $env:PATH = "$guestRepo\build\tests;$guestRepo\build;$env:PATH"
    $env:SPACEWM_IT = $null
    Add-Type -AssemblyName System.Windows.Forms
    $summary.monitors = @([System.Windows.Forms.Screen]::AllScreens | ForEach-Object {
        @{ device = $_.DeviceName; bounds = $_.Bounds.ToString(); primary = $_.Primary }
    })
    $summary.multiMonitorCoverage = (@([System.Windows.Forms.Screen]::AllScreens).Count -ge 2)
    $summary.state = 'regression'
    Save-Summary
    Push-Location "$guestRepo\build"
    try {
        & 'C:\SpaceWM-Input\ctest.exe' --output-on-failure 2>&1 |
            Tee-Object -FilePath (Join-Path $results 'ctest.txt')
        $summary.regressionExitCode = $LASTEXITCODE
        Save-Summary
        New-Item -ItemType Directory -Path (Join-Path $results 'regression') -Force | Out-Null
        Get-ChildItem -LiteralPath "$guestRepo\build\tests" -Filter '*.out.txt' |
            Copy-Item -Destination (Join-Path $results 'regression')

        # Windows 11 24H2+ Sandbox can omit Notepad. Do not substitute a fake
        # app or report a skipped real-app flow as a successful integration run.
        $notepad = Get-Command notepad.exe -ErrorAction SilentlyContinue
        if ($notepad) {
            $summary.state = 'integration'
            Save-Summary
            $env:SPACEWM_IT = '1'
            & 'C:\SpaceWM-Input\ctest.exe' -R '^test_integration_flow$' --output-on-failure 2>&1 |
                Tee-Object -FilePath (Join-Path $results 'integration.txt')
            $summary.integrationExitCode = $LASTEXITCODE
            Copy-Item -LiteralPath "$guestRepo\build\tests\test_integration_flow.out.txt" -Destination $results
        } else {
            $summary.integrationNotRun = 'Notepad is not installed in this Windows Sandbox image'
        }
        $env:SPACEWM_IT = $null
    } finally {
        Pop-Location
    }
    # Capture the sandbox desktop after testing for environment diagnostics.
    Add-Type -AssemblyName System.Drawing
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $bitmap.Save((Join-Path $results 'desktop.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
    foreach ($dir in @('TR', 'EH')) {
        if (Test-Path -LiteralPath (Join-Path $guestRepo $dir)) {
            Copy-Item -LiteralPath (Join-Path $guestRepo $dir) -Destination $results -Recurse -Force
        }
    }
    $summary.state = 'completed'
} catch {
    $summary.state = 'error'
    $summary.error = $_.Exception.Message
    Write-Output $_.Exception.ToString()
} finally {
    Save-Summary
    Stop-Transcript | Out-Null
}
