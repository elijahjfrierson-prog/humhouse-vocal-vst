# HumHouse Vocals — Quick Install Script for Windows
# Run: Right-click → "Run with PowerShell"
# Or: powershell -ExecutionPolicy Bypass -File install.ps1
#
# This copies the VST3 plugin to your system VST3 folder so
# FL Studio (and all DAWs) find it on the next plugin scan.

$ErrorActionPreference = "Stop"

$pluginName = "HumHouse Vocals.vst3"
$vst3Dir    = "$env:CommonProgramFiles\VST3"
$destDir    = Join-Path $vst3Dir $pluginName

# Find the VST3 bundle in the same folder as this script (or in build output)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$source    = Get-ChildItem -Path $scriptDir -Filter $pluginName -Recurse -Directory | Select-Object -First 1

if (-not $source) {
    Write-Host "ERROR: Could not find '$pluginName' folder next to this script." -ForegroundColor Red
    Write-Host "Make sure you extracted the ZIP first, then run this script from inside it."
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host ""
Write-Host "=== HumHouse Vocals Installer ===" -ForegroundColor Magenta
Write-Host ""
Write-Host "Source:      $($source.FullName)"
Write-Host "Destination: $destDir"
Write-Host ""

# Remove old install if present
if (Test-Path $destDir) {
    Write-Host "Removing old version..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $destDir
}

# Copy
Write-Host "Installing..." -ForegroundColor Cyan
Copy-Item -Recurse -Force $source.FullName $destDir

Write-Host ""
Write-Host "Done! HumHouse Vocals installed to:" -ForegroundColor Green
Write-Host "  $destDir"
Write-Host ""
Write-Host "Next: FL Studio -> Options -> Manage Plugins -> Start Scan" -ForegroundColor Yellow
Write-Host ""
Read-Host "Press Enter to exit"
