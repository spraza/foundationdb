#!/usr/bin/env bash
#
# run.sh – clean slate → spin up test cluster
#

# ------- hard‑wired parameters -------
FDBMON=~/cnd_build_output/bin/fdbmonitor
FDBSRV=~/cnd_build_output/bin/fdbserver
FDBCLI=~/cnd_build_output/bin/fdbcli
BASE_DIR=/tmp/fdblocal
PORT_START=4500
MAIN_LOGS=4
SAT_LOGS=2
MAIN_STORES=4
STATELESS=4
CLUSTER_NAME=prazalocal
# -------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "----- cleanup -----"
bash "$SCRIPT_DIR/cleanup.sh" --base-dir "$BASE_DIR"

echo "----- starting fresh cluster -----"
bash "$SCRIPT_DIR/setup.sh" \
  --fdb-monitor "$FDBMON" \
  --fdb-server  "$FDBSRV" \
  --fdb-cli     "$FDBCLI" \
  --base-dir    "$BASE_DIR" \
  --port-start  "$PORT_START" \
  --main-logs   "$MAIN_LOGS" \
  --sat-logs    "$SAT_LOGS" \
  --main-stores "$MAIN_STORES" \
  --stateless   "$STATELESS" \
  --cluster-name "$CLUSTER_NAME"
