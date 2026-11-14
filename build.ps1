param(
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSCommandPath
$src = Join-Path $root 'src\dll'
$out = Join-Path $root 'bin'
$obj = Join-Path $root 'build\obj'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if ($Clean) { Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $obj | Out-Null

$gxx = $null
$candidates = @(
    "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe",
    'C:\mingw64\bin\g++.exe'
)
foreach ($c in $candidates) { if (Test-Path $c) { $gxx = $c; break } }
if (-not $gxx) {
    $cmd = Get-Command g++ -ErrorAction SilentlyContinue
    if ($cmd) { $gxx = $cmd.Source }
}
if (-not $gxx) { throw 'g++ not found' }
Write-Host "Using $gxx"

$files = Get-ChildItem $src -Filter *.cpp | Select-Object -ExpandProperty FullName
$defines = @('-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-D_WIN32_WINNT=0x0A00')
$include = @('-I' + $src)
$cxxflags = @('-std=c++17', '-O2', '-Wall', '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-municode')
$linkflags = @('-shared', '-static-libgcc', '-static-libstdc++')
$libs = @('-luser32', '-lgdi32', '-lshell32', '-lshlwapi', '-lole32', '-loleaut32', '-luuid', '-lmsimg32', '-lpsapi', '-lshcore', '-ladvapi32', '-lcomctl32')

$objs = @()
foreach ($f in $files) {
    $o = Join-Path $obj ((Split-Path $f -Leaf) + '.o')
    Write-Host "CC $f"
    & $gxx @cxxflags @defines @include -c $f -o $o
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $f" }
    $objs += $o
}
$dll = Join-Path $out 'MenuGlass.dll'
# The DLL may still be mapped into explorer.exe; an image can be renamed but not deleted.
if (Test-Path $dll) {
    try {
        Remove-Item $dll -Force -ErrorAction Stop
    } catch {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $bak = Join-Path $out ('MenuGlass.' + $stamp + '.dll.old')
        try {
            Move-Item $dll $bak -Force -ErrorAction Stop
            Write-Host "Locked DLL moved to $bak"
        } catch {
            Write-Host "WARN: output DLL is locked and cannot be replaced: $_"
        }
    }
}
Write-Host "LINK $dll"
$implib = Join-Path $out 'libMenuGlass.a'
$linkImplib = '-Wl,--out-implib,' + $implib
& $gxx @linkflags $objs -o $dll $linkImplib @libs
if ($LASTEXITCODE -ne 0) { throw 'link failed' }
Write-Host "OK: $dll"
Get-Item $dll | Select-Object FullName, Length, LastWriteTime
