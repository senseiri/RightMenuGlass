@echo off
setlocal
cd /d "%~dp0"
echo RightMenuGlass installer
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
set RC=%ERRORLEVEL%
echo.
if not "%RC%"=="0" echo install failed with code %RC%
pause
exit /b %RC%
