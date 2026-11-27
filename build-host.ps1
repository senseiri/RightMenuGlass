param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSCommandPath
$out = Join-Path $root 'bin'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$gxx = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe"
if (-not (Test-Path $gxx)) { throw 'g++ not found' }
$defines = @('-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-D_WIN32_WINNT=0x0A00')
$cxxflags = @('-std=c++17', '-O2', '-municode')
$libs = @('-luser32', '-ladvapi32', '-lpsapi')
$exe = Join-Path $out 'rmgctl.exe'
$src = Join-Path $root 'src\host\rmgctl.cpp'
# -static keeps libstdc++/libgcc/libwinpthread inside the exe so the release
# runs on machines without MinGW installed.
& $gxx @cxxflags @defines $src -o $exe -static @libs
if ($LASTEXITCODE -ne 0) { throw 'host build failed' }
Write-Host "OK: $exe"
