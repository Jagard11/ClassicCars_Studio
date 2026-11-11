#!/bin/sh
# Build ClassicCars Studio and increment the patch version.
# Linux, macOS, and Git Bash: ./build.sh
# Windows Command Prompt or PowerShell: build.bat
# Both run build.py. Flags: --rebuild, --no-bump.

case "$0" in
  */*) SCRIPT_DIR=${0%/*} ;;
  *) SCRIPT_DIR=. ;;
esac
TARGET=$SCRIPT_DIR/build.py

if [ ! -f "$TARGET" ]; then
  echo "Error: build script not found at $TARGET" >&2
  printf '%s\n' 'Next: run ./build.sh or build.bat from the ClassicCars Studio directory.' >&2
  exit 1
fi

python_works() {
  "$@" -c "import sys; raise SystemExit(0 if sys.version_info[0] >= 3 else 1)" >/dev/null 2>&1
}

export PYTHONUNBUFFERED=1

if python_works python3; then
  echo "Using interpreter: python3"
  exec python3 "$TARGET" "$@"
elif python_works python; then
  echo "Using interpreter: python"
  exec python "$TARGET" "$@"
elif python_works py -3; then
  echo "Using interpreter: py -3"
  exec py -3 "$TARGET" "$@"
fi

echo "Error: Python 3 did not start. Tried python3, then python, then py -3." >&2
echo "Next: install Python 3, then run ./build.sh or build.bat." >&2
exit 1
