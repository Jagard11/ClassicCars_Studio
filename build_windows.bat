@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Build classiccars-studio.exe with MinGW gcc or MSVC cl.
rem From Linux, use: make windows

if not exist "src\version.h" (
  if exist "build.py" (
    echo src\version.h is missing. Run build.bat or: py -3 build.py --no-bump
  )
  >&2 echo Error: src\version.h was not found.
  exit /b 1
)

set "SRC=src\main.c src\http.c src\router.c src\ssi.c src\proxy.c src\settings.c src\config.c src\app_config.c src\project_macros.c src\json.c src\gallery.c src\preview_draft.c src\project_search.c src\git_scm.c src\terminal.c src\fs_rename.c src\fs_ops.c src\runtime_versions.c src\preflight.c src\updater.c src\code_engine.c src\api.c src\window.c"
set "OUT=classiccars-studio.exe"

where gcc >nul 2>&1
if not errorlevel 1 (
  echo Compiling %OUT% with gcc
  gcc -std=c11 -Wall -Wextra -O2 -D_WIN32_WINNT=0x0600 -mwindows -Wl,--subsystem,windows -Wl,-e,mainCRTStartup -o "%OUT%" %SRC% -static -lws2_32 -lgdi32 -luser32 -lole32 -loleaut32 -luuid -lshlwapi -ladvapi32
  exit /b %ERRORLEVEL%
)

where cl >nul 2>&1
if not errorlevel 1 (
  echo Compiling %OUT% with cl
  cl /nologo /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0600 /Fe:"%OUT%" %SRC% ws2_32.lib gdi32.lib user32.lib ole32.lib oleaut32.lib uuid.lib shlwapi.lib advapi32.lib /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
  exit /b %ERRORLEVEL%
)

>&2 echo No gcc or cl compiler found.
>&2 echo Install MinGW-w64 and run this script again,
>&2 echo or from Linux run: make windows
exit /b 1
