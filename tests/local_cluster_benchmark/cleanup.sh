#!/usr/bin/env bash
#
# cleanup.sh – stop the test cluster and wipe its working directory
#
# Required flag:
#   --base-dir   <same BASE_DIR you gave setup.sh>
#
set -euo pipefail

############ arg parsing ############
[[ $# -eq 0 ]] && { echo "Usage: $0 --base-dir DIR"; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --base-dir) BASE_DIR="$2"; shift 2 ;;
    *)          echo "❌  Unknown flag: $1"; exit 1 ;;
  esac
done

[[ -z "${BASE_DIR:-}" ]] && { echo "❌  --base-dir required"; exit 1; }

CONF_FILE="$BASE_DIR/conf/fdbmonitor.conf"

############ kill procs #############
echo "Stopping any fdbmonitor/fdbserver tied to $BASE_DIR …"

# kill fdbmonitor started by this setup (matches its --conffile arg)
pkill -f "fdbmonitor --conffile $CONF_FILE" 2>/dev/null || true

# kill any fdbserver whose datadir lives under BASE_DIR
pgrep -f "$BASE_DIR/data" 2>/dev/null | xargs -r kill 2>/dev/null || true

############ wipe dir ###############
if [[ -d "$BASE_DIR" ]]; then
  echo "Removing $BASE_DIR …"
  rm -rf "$BASE_DIR"
fi

echo "Cleanup complete."
