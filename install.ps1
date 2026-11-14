<#
    RightMenuGlass installer.
    Copies the binaries to %LOCALAPPDATA%\Programs\RightMenuGlass, registers autostart,
    then restarts explorer.exe so the DLL is loaded immediately.

    Usage:  powershell -ExecutionPolicy Bypass -File install.ps1
            powershell -ExecutionPolicy Bypass -File install.ps1 -NoRestart
            powershell -ExecutionPolicy Bypass -File install.ps1 -NoAutostart
#>
param(
    [string]$Target = (Join-Path $env:LOCALAPPDATA 'Programs\RightMenuGlass'),
    [switch]$NoRestart,
    [switch]$NoAutostart
)

$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
# In the repo the binaries live in bin\; a release zip has them next to this script.
$srcBin = if (Test-Path (Join-Path $src 'bin')) { Join-Path $src 'bin' } else { $src }

foreach ($f in @('MenuGlass.dll', 'rmgctl.exe', 'rmgcfg.exe')) {
    $p = Join-Path $srcBin $f
    if (-not (Test-Path $p)) { throw "missing $p - build the project first (build.ps1 / build-host.ps1 / build-cfg.ps1)" }
}

Write-Host "installing to $Target"
New-Item -ItemType Directory -Force -Path $Target | Out-Null

# Stop a previous watch process before replacing binaries.
$oldCtl = Join-Path $Target 'rmgctl.exe'
if (Test-Path $oldCtl) {
    & $oldCtl stop --silent | Out-Null
    & $oldCtl uninstall --silent | Out-Null
}

foreach ($f in @('MenuGlass.dll', 'rmgctl.exe', 'rmgcfg.exe')) {
    Copy-Item (Join-Path $srcBin $f) (Join-Path $Target $f) -Force
}
$docSrc = Join-Path $src 'docs'
if (Test-Path $docSrc) {
    Copy-Item $docSrc (Join-Path $Target 'docs') -Recurse -Force
}
$exSrc = Join-Path $src 'plugins-example'
if (Test-Path $exSrc) {
    Copy-Item $exSrc (Join-Path $Target 'plugins-example') -Recurse -Force
}

$ctl = Join-Path $Target 'rmgctl.exe'

# First run seeds the config file; the plugin folder is created next to it.
$cfgDir = Join-Path $env:LOCALAPPDATA 'RightMenuGlass'
$cfgFile = Join-Path $cfgDir 'config.json'
$pluginDir = Join-Path $cfgDir 'plugins'
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
if (-not (Test-Path $cfgFile)) {
    if (-not $NoRestart) {
        # Restarting explorer once is the simplest way to let the DLL write defaults.
        & $ctl restart | Out-Null
    }
}
if ((Get-ChildItem $pluginDir -Filter *.json -ErrorAction SilentlyContinue | Measure-Object).Count -eq 0) {
    $exPlugins = Join-Path $Target 'plugins-example'
    if (Test-Path $exPlugins) {
        Copy-Item (Join-Path $exPlugins '*.json') $pluginDir -Force
        Write-Host 'example plugins copied to the plugin folder'
    }
}

if (-not $NoAutostart) {
    & $ctl install
}

if (-not $NoRestart) {
    Write-Host 'restarting explorer.exe and injecting the DLL...'
    & $ctl restart
    # Also start the watcher now so folders opened later get injected too.
    & $ctl stop --silent | Out-Null
    Start-Process $ctl -ArgumentList 'watch', '--silent' -WindowStyle Hidden
} else {
    Write-Host 'skipped explorer restart; run:' ('"' + $ctl + '" restart')
}

Write-Host ''
Write-Host 'done. right-click any file or the desktop to try it.'
Write-Host ('settings: ' + (Join-Path $Target 'rmgcfg.exe'))
Write-Host ('config:   ' + $cfgFile)
