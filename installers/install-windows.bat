@echo off
setlocal EnableExtensions

rem Windows entry-point installer for ClassicCars Studio.
rem
rem Interactive GUI mode (double-click or run without arguments):
rem   installers\install-windows.bat
rem
rem Non-interactive CLI mode (install to a specific directory):
rem   installers\install-windows.bat -InstallDir "C:\Apps\ClassicCars Studio" -Yes
rem
rem Uninstall:
rem   installers\install-windows.bat -Uninstall -InstallDir "C:\Apps\ClassicCars Studio"

set "SCRIPT_DIR=%~dp0"
set "SETUP_EXE_ROOT=%SCRIPT_DIR%..\ClassicCarsStudio-Setup.exe"
set "SETUP_EXE_LOCAL=%SCRIPT_DIR%ClassicCarsStudio-Setup.exe"
set "PS_SCRIPT=%SCRIPT_DIR%install-windows.ps1"
set "PY_SCRIPT=%SCRIPT_DIR%install.py"

if "%~1"=="" (
  if exist "%SETUP_EXE_ROOT%" (
    start "" "%SETUP_EXE_ROOT%"
    exit /b 0
  )
  if exist "%SETUP_EXE_LOCAL%" (
    start "" "%SETUP_EXE_LOCAL%"
    exit /b 0
  )
)

where powershell >nul 2>&1
if not errorlevel 1 (
  if exist "%PS_SCRIPT%" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
    exit /b %ERRORLEVEL%
  )
)

where python >nul 2>&1
if not errorlevel 1 (
  if exist "%PY_SCRIPT%" (
    python "%PY_SCRIPT%" %*
    exit /b %ERRORLEVEL%
  )
)

where py >nul 2>&1
if not errorlevel 1 (
  if exist "%PY_SCRIPT%" (
    py -3 "%PY_SCRIPT%" %*
    exit /b %ERRORLEVEL%
  )
)

>&2 echo Error: Neither PowerShell nor Python 3 was found to run the ClassicCars Studio installer.
exit /b 1
