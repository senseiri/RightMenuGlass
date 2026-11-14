<#
    RightMenuGlass uninstaller.
    Removes autostart, stops the watcher, restarts explorer.exe without the DLL,
    then deletes the install directory.

    Usage:  powershell -ExecutionPolicy Bypass -File uninstall.ps1
            powershell -ExecutionPolicy Bypass -File uninstall.ps1 -PurgeConfig
#>
param(
    [string]$Target = (Join-Path $env:LOCALAPPDATA 'Programs\RightMenuGlass'),
    [switch]$PurgeConfig,
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
$ctl = Join-Path $Target 'rmgctl.exe'

if (-not (Test-Path $ctl)) {
    Write-Host "no installation found at $Target"
} else {
    Write-Host 'stopping watcher'
    & $ctl stop | Out-Null
    Write-Host 'removing autostart entry'
    & $ctl uninstall | Out-Null
    if (-not $KeepRunning) {
        Write-Host 'restarting explorer.exe without the DLL'
        & $ctl restart --no-inject | Out-Null
        Start-Sleep -Milliseconds 1200
    }
}

if (-not $KeepRunning) {
    Start-Sleep -Milliseconds 800
    Remove-Item $Target -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $Target) {
        Write-Host "could not fully delete $Target (files still loaded); sign out and delete it manually."
    } else {
        Write-Host "removed $Target"
    }
}

if ($PurgeConfig) {
    $cfgDir = Join-Path $env:LOCALAPPDATA 'RightMenuGlass'
    Remove-Item $cfgDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "removed $cfgDir"
} else {
    $cfgDir = Join-Path $env:LOCALAPPDATA 'RightMenuGlass'
    Write-Host "kept configuration and plugins in $cfgDir (use -PurgeConfig to delete them)"
}

Write-Host 'uninstall finished.'
