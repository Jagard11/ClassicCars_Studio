#!/bin/sh
# Start ClassicCars Studio in its own window.
# Linux, macOS, and Git Bash: ./launcher.sh
# Windows Command Prompt or PowerShell: launcher.bat
# If classiccars-studio is missing, ./build.sh runs first.
# Extra arguments are forwarded to the server.

case "$0" in
  */*) SCRIPT_DIR=${0%/*} ;;
  *) SCRIPT_DIR=. ;;
esac
TARGET=$SCRIPT_DIR/launcher.py

if [ ! -f "$TARGET" ]; then
  echo "Error: launcher not found at $TARGET" >&2
  printf '%s\n' 'Next: run ./launcher.sh or launcher.bat from the ClassicCars Studio directory.' >&2
  exit 1
fi

python_works() {
  "$@" -c "import sys; raise SystemExit(0 if sys.version_info[0] >= 3 else 1)" >/dev/null 2>&1
}

# Conda's lib directory on LD_LIBRARY_PATH makes GTK 3 and WebKitGTK load
# Conda's older libglib and libstdc++. Keep the system copies.
scrub_conda_ld_library_path() {
  [ -n "${LD_LIBRARY_PATH:-}" ] || return 0
  _old=$LD_LIBRARY_PATH
  _cleaned=
  while [ -n "$_old" ]; do
    case "$_old" in
      *:*)
        _entry=${_old%%:*}
        _old=${_old#*:}
        ;;
      *)
        _entry=$_old
        _old=
        ;;
    esac
    case "$_entry" in
      ""|*miniconda3/lib*|*miniconda3/*/lib*|*anaconda3/lib*|*anaconda3/*/lib*)
        continue
        ;;
    esac
    if [ -n "$_cleaned" ]; then
      _cleaned="${_cleaned}:${_entry}"
    else
      _cleaned=$_entry
    fi
  done
  if [ -n "$_cleaned" ]; then
    LD_LIBRARY_PATH=$_cleaned
    export LD_LIBRARY_PATH
  else
    unset LD_LIBRARY_PATH
  fi
  unset _old _cleaned _entry
}

scrub_conda_ld_library_path

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
echo "Next: install Python 3, then run ./launcher.sh or launcher.bat." >&2
exit 1
