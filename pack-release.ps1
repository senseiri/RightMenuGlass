<#
    Builds a release zip: RightMenuGlass-<Version>-win-x64.zip
    Content: the three binaries, install.cmd / install.ps1 / uninstall.ps1, README and docs.

    Usage:  powershell -ExecutionPolicy Bypass -File pack-release.ps1 -Version v1.0.0
#>
param(
    [string]$Version = 'v1.0.0'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$bin = Join-Path $root 'bin'
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist ("RightMenuGlass-$Version-win-x64")
$zip = $stage + '.zip'

$bins = @('MenuGlass.dll', 'rmgctl.exe', 'rmgcfg.exe')
foreach ($f in $bins) {
    $p = Join-Path $bin $f
    if (-not (Test-Path $p)) { throw "missing $p - run build.ps1 / build-host.ps1 / build-cfg.ps1 first" }
}

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

foreach ($f in $bins) { Copy-Item (Join-Path $bin $f) (Join-Path $stage $f) -Force }
foreach ($f in @('install.ps1', 'install.cmd', 'uninstall.ps1', 'README.md', 'LICENSE')) {
    Copy-Item (Join-Path $root $f) (Join-Path $stage $f) -Force
}
foreach ($d in @('docs', 'plugins-example')) {
    $p = Join-Path $root $d
    if (Test-Path $p) { Copy-Item $p (Join-Path $stage $d) -Recurse -Force }
}

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal

Write-Host ''
Write-Host "zip: $zip"
$len = (Get-Item $zip).Length
Write-Host ('size: {0:N0} bytes' -f $len)
Write-Host ('sha256: ' + (Get-FileHash $zip -Algorithm SHA256).Hash)
Write-Host ''
Write-Host 'contents:'
Get-ChildItem $stage -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($stage.Length + 1)
    Write-Host ('  {0,-42} {1,10:N0}' -f $rel, $_.Length)
}
