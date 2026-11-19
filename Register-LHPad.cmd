@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-explorer.ps1" -Directory "%~dp0."
set "LHPAD_RESULT=%ERRORLEVEL%"
echo.
if not "%LHPAD_RESULT%"=="0" echo LHPad setup failed or was cancelled. See the message above.
pause
exit /b %LHPAD_RESULT%
