#!/bin/sh
# Same entry as launcher.sh. ClassicCars-Studio.desktop Exec is launcher.sh.
# This name remains so an older desktop file still starts the versioned launcher.

case "$0" in
  */*) SCRIPT_DIR=${0%/*} ;;
  *) SCRIPT_DIR=. ;;
esac

exec "$SCRIPT_DIR/launcher.sh" "$@"
