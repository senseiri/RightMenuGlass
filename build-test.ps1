param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSCommandPath
$src = Join-Path $root 'src'
$out = Join-Path $root 'bin'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$gxx = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe"
if (-not (Test-Path $gxx)) { throw 'g++ not found' }
$defines = @('-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-D_WIN32_WINNT=0x0A00')
$cxxflags = @('-std=c++17', '-O1', '-g', '-municode')
$libs = @('-luser32', '-lgdi32', '-lshell32', '-lshlwapi', '-lole32', '-loleaut32', '-luuid', '-lmsimg32', '-lpsapi', '-lshcore', '-ladvapi32', '-lcomctl32')
$files = @(
  (Join-Path $src 'test\test_menu.cpp'),
  (Join-Path $src 'dll\config.cpp'),
  (Join-Path $src 'dll\draw.cpp'),
  (Join-Path $src 'dll\hook.cpp'),
  (Join-Path $src 'dll\menu_ui.cpp'),
  (Join-Path $src 'dll\plugin.cpp'),
  (Join-Path $src 'dll\provider.cpp'),
  (Join-Path $src 'dll\util.cpp')
)
$exe = Join-Path $out 'rmgtest.exe'
Write-Host "LINK $exe"
& $gxx @cxxflags @defines ('-I' + (Join-Path $src 'dll')) @files -o $exe @libs
if ($LASTEXITCODE -ne 0) { throw 'test link failed' }
Write-Host 'OK'
