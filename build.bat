@echo off
setlocal EnableExtensions

rem Build ClassicCars Studio and increment the patch version.
rem Linux, macOS, and Git Bash: ./build.sh
rem Windows Command Prompt or PowerShell: build.bat
rem Both run build.py. Flags: --rebuild, --no-bump.
set "SCRIPT_DIR=%~dp0"
set "TARGET=%SCRIPT_DIR%build.py"

if not exist "%TARGET%" (
  >&2 echo Error: build script not found at "%TARGET%"
  >&2 echo Next: run build.bat from the ClassicCars Studio directory.
  exit /b 1
)

call :python_works python3
if not errorlevel 1 goto :run_python3

call :python_works python
if not errorlevel 1 goto :run_python

call :python_works py -3
if not errorlevel 1 goto :run_py

>&2 echo Error: Python 3 did not start. Tried python3, then python, then py -3.
>&2 echo Next: install Python 3, then run build.bat
>&2 echo   or py -3 build.py
exit /b 1

:run_python3
echo Using interpreter: python3
set "PY_RUN=python3"
goto :run_build

:run_python
echo Using interpreter: python
set "PY_RUN=python"
goto :run_build

:run_py
echo Using interpreter: py -3
set "PY_RUN=py -3"
goto :run_build

:run_build
set "PYTHONUNBUFFERED=1"
call %PY_RUN% "%TARGET%" %*
exit /b %ERRORLEVEL%

rem Accept only a command that actually runs Python 3.
rem version_info[0] // 3 avoids a ^> character, which redirects in cmd.
:python_works
call "%~1" %~2 -c "import sys; raise SystemExit(0 if sys.version_info[0] // 3 else 1)" >nul 2>&1
if errorlevel 1 exit /b 1
exit /b 0
