#!/bin/sh
# Linux installer entry point for ClassicCars Studio.
#
# Interactive GUI / terminal mode:
#   ./installers/install-linux.sh
#
# Non-interactive CLI mode (install to a specific directory):
#   ./installers/install-linux.sh --dir /path/to/ClassicCars-Studio --yes
#
# Uninstall:
#   ./installers/install-linux.sh --uninstall --dir /path/to/ClassicCars-Studio

set -eu

case "$0" in
  */*) SCRIPT_DIR=${0%/*} ;;
  *) SCRIPT_DIR=. ;;
esac
SCRIPT_DIR=$(cd "$SCRIPT_DIR" && pwd)
TARGET="$SCRIPT_DIR/install.py"

if [ ! -f "$TARGET" ]; then
  echo "Error: installer engine not found at $TARGET" >&2
  exit 1
fi

python_works() {
  "$@" -c "import sys; raise SystemExit(0 if sys.version_info[0] >= 3 else 1)" >/dev/null 2>&1
}

# Remove Conda's lib directory from LD_LIBRARY_PATH so system Python / Tk / GTK
# load the host's native shared libraries cleanly.
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

if [ -x /usr/bin/python3 ] && python_works /usr/bin/python3; then
  exec /usr/bin/python3 "$TARGET" "$@"
elif python_works python3; then
  exec python3 "$TARGET" "$@"
elif python_works python; then
  exec python "$TARGET" "$@"
fi

echo "Error: Python 3 is required to run the ClassicCars Studio installer." >&2
exit 1
