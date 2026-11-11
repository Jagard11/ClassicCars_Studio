@echo off
setlocal EnableExtensions

rem Start ClassicCars Studio in its own window.
rem Linux, macOS, and Git Bash: ./launcher.sh
rem Windows Command Prompt or PowerShell: launcher.bat
rem If classiccars-studio.exe is missing, build.bat runs first.
rem Extra arguments are forwarded to the server.
set "SCRIPT_DIR=%~dp0"
set "TARGET=%SCRIPT_DIR%launcher.py"

if not exist "%TARGET%" (
  >&2 echo Error: launcher not found at "%TARGET%"
  >&2 echo Next: run launcher.bat from the ClassicCars Studio directory.
  exit /b 1
)

call :python_works python3
if not errorlevel 1 goto :run_python3

call :python_works python
if not errorlevel 1 goto :run_python

call :python_works py -3
if not errorlevel 1 goto :run_py

>&2 echo Error: Python 3 did not start. Tried python3, then python, then py -3.
>&2 echo Next: install Python 3, then run launcher.bat
>&2 echo   or py -3 launcher.py
exit /b 1

:run_python3
echo Using interpreter: python3
set "PY_RUN=python3"
goto :run_launcher

:run_python
echo Using interpreter: python
set "PY_RUN=python"
goto :run_launcher

:run_py
echo Using interpreter: py -3
set "PY_RUN=py -3"
goto :run_launcher

:run_launcher
set "PYTHONUNBUFFERED=1"
call %PY_RUN% "%TARGET%" %*
exit /b %ERRORLEVEL%

rem Accept only a command that actually runs Python 3.
rem version_info[0] // 3 avoids a ^> character, which redirects in cmd.
:python_works
call "%~1" %~2 -c "import sys; raise SystemExit(0 if sys.version_info[0] // 3 else 1)" >nul 2>&1
if errorlevel 1 exit /b 1
exit /b 0
