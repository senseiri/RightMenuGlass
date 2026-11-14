# Build the settings UI (rmgcfg.exe) with the .NET Framework 4.8 compiler.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path $csc)) { throw "csc.exe not found: $csc" }

$out = Join-Path $root 'bin\rmgcfg.exe'
$src = @(
    (Join-Path $root 'src\cfg\RmgJson.cs'),
    (Join-Path $root 'src\cfg\MainForm.cs')
)
foreach ($f in $src) { if (-not (Test-Path $f)) { throw "missing source: $f" } }

$args = @(
    '/nologo',
    '/target:winexe',
    '/platform:anycpu',
    '/optimize+',
    ('/out:' + $out),
    '/r:System.dll',
    '/r:System.Drawing.dll',
    '/r:System.Windows.Forms.dll'
) + $src

& $csc @args
if ($LASTEXITCODE -ne 0) { throw "csc failed with exit code $LASTEXITCODE" }
Get-Item $out | Select-Object Name, Length, LastWriteTime | Format-Table | Out-String
Write-Host ("OK: " + $out)
