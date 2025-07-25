#!/usr/bin/env bash
#
# setup.sh – spin up a two‑region FoundationDB test cluster under fdbmonitor
#
# Required flags (no defaults):
#   --fdb-monitor   /path/to/fdbmonitor
#   --fdb-server    /path/to/fdbserver
#   --fdb-cli       /path/to/fdbcli
#   --base-dir      /abs/work/dir               # holds conf/, data/, logs/
#   --port-start    4500                        # first TCP port
#   --main-logs     4                           # logs per main DC
#   --sat-logs      2                           # logs per satellite DC
#   --main-stores   4                           # storage servers per main DC
#   --stateless     4                           # stateless procs per main DC
#   --cluster-name  local74                     # prefix inside .cluster file
#
set -euo pipefail

usage() {
  cat <<EOF
Usage: $0 --fdb-monitor BIN --fdb-server BIN --fdb-cli BIN --base-dir DIR \\
          --port-start N --main-logs N --sat-logs N --main-stores N \\
          --stateless N --cluster-name NAME
EOF
  exit 1
}

# ----------- parse CLI flags -----------
[[ $# -eq 0 ]] && usage
while [[ $# -gt 0 ]]; do
  case "$1" in
    --fdb-monitor)  FDB_MONITOR="$2";  shift 2 ;;
    --fdb-server)   FDB_SERVER="$2";   shift 2 ;;
    --fdb-cli)      FDB_CLI="$2";      shift 2 ;;
    --base-dir)     BASE_DIR="$2";     shift 2 ;;
    --port-start)   PORT_START="$2";   shift 2 ;;
    --main-logs)    MAIN_LOGS="$2";    shift 2 ;;
    --sat-logs)     SAT_LOGS="$2";     shift 2 ;;
    --main-stores)  MAIN_STORES="$2";  shift 2 ;;
    --stateless)    STATELESS="$2";    shift 2 ;;
    --cluster-name) CLUSTER_NAME="$2"; shift 2 ;;
    -h|--help)      usage ;;
    *) echo "❌  Unknown flag: $1"; usage ;;
  esac
done

# ----------- sanity checks -------------
vars=(FDB_MONITOR FDB_SERVER FDB_CLI BASE_DIR PORT_START MAIN_LOGS SAT_LOGS MAIN_STORES STATELESS CLUSTER_NAME)
for v in "${vars[@]}"; do
  [[ -z "${!v:-}" ]] && { echo "❌  $v missing"; usage; }
done

mkdir -p "$BASE_DIR"/{conf,data,logs}

CFG="$BASE_DIR/conf/fdbmonitor.conf"
CLUSTER="$BASE_DIR/conf/fdb.cluster"
REGIONS_JSON="$BASE_DIR/conf/regions.json"

# ----------- helpers -------------------
port() { echo $((PORT_START + $1)); }

add_server() {
  local name=$1 dc=$2 klass=$3 p=$4
  local dir="$BASE_DIR/data/$name"
  mkdir -p "$dir"
  cat >>"$CFG" <<EOF

[[fdbserver.$name]]
command       = $FDB_SERVER
cluster_file  = $CLUSTER
public_address= 127.0.0.1:$(port $p)
listen_address= public
datadir       = $dir
logdir        = $BASE_DIR/logs
class         = $klass
locality_dcid = $dc
EOF
}

# ----------- fdbmonitor conf -----------
cat >"$CFG" <<EOF
[fdbmonitor]
user = $USER

[general]
cluster_file = $CLUSTER
restart_delay = 15
EOF

# region 1 (dc1) + satellites dc1s1 / dc1s2
proc=0
for dc in dc1; do
  for ((i=0;i<MAIN_STORES;i++));  do add_server "dc1_store_$i" $dc storage     $((proc++)); done
  for ((i=0;i<MAIN_LOGS;i++));    do add_server "dc1_log_$i"   $dc transaction $((proc++)); done
  for ((i=0;i<STATELESS;i++));    do add_server "dc1_stateless_$i" $dc stateless $((proc++)); done
done
for s in dc1s1 dc1s2; do
  for ((i=0;i<SAT_LOGS;i++)); do add_server "${s}_log_$i" $s transaction $((proc++)); done
done

# region 2 (dc2) + satellites dc2s1 / dc2s2
for dc in dc2; do
  for ((i=0;i<MAIN_STORES;i++));  do add_server "dc2_store_$i" $dc storage     $((proc++)); done
  for ((i=0;i<MAIN_LOGS;i++));    do add_server "dc2_log_$i"   $dc transaction $((proc++)); done
  for ((i=0;i<STATELESS;i++));    do add_server "dc2_stateless_$i" $dc stateless $((proc++)); done
done
for s in dc2s1 dc2s2; do
  for ((i=0;i<SAT_LOGS;i++)); do add_server "${s}_log_$i" $s transaction $((proc++)); done
done

# ----------- cluster file --------------
COORDS=$(printf ",127.0.0.1:%s" $(seq $PORT_START $((PORT_START+4))))
echo "$CLUSTER_NAME:$(uuidgen | tr -d -)@${COORDS#,}" >"$CLUSTER"

# ----------- regions.json --------------
cat >"$REGIONS_JSON" <<JSON
[
  {
    "priority": 1,
    "satellite_redundancy_mode": "two_satellite_fast",
    "satellite_logs": $SAT_LOGS,
    "datacenters": [
      { "id": "dc1",  "priority": 1, "satellite": 0 },
      { "id": "dc1s1","priority": 2, "satellite": 1 },
      { "id": "dc1s2","priority": 1, "satellite": 1 }
    ]
  },
  {
    "priority": 0,
    "satellite_redundancy_mode": "two_satellite_fast",
    "satellite_logs": $SAT_LOGS,
    "datacenters": [
      { "id": "dc2",  "priority": 1, "satellite": 0 },
      { "id": "dc2s1","priority": 2, "satellite": 1 },
      { "id": "dc2s2","priority": 1, "satellite": 1 }
    ]
  }
]
JSON

# ----------- launch monitor ------------
echo "Starting fdbmonitor …"
"$FDB_MONITOR" --conffile "$CFG" &
MON_PID=$!

# # ----------- initialise database -------
# echo "Initialising database …"
# "$FDB_CLI" -C "$CLUSTER" --exec "configure new double ssd logs=$MAIN_LOGS usable_regions=2"  
# echo "Basic configure done …"
# "$FDB_CLI" -C "$CLUSTER" --exec "coordinators auto"
# echo "Coordinators auto done …"
# "$FDB_CLI" -C "$CLUSTER" --exec "fileconfigure $REGIONS_JSON"
# echo "Region configure done …"

# # ----------- wait for healthy ----------
# echo "Waiting for cluster to become healthy …"
# until "$FDB_CLI" -C "$CLUSTER" --exec 'status minimal' | grep -q 'Healthy'; do
#   sleep 2
# done

# echo -e "\n✅  Cluster is up in $BASE_DIR"
# echo "   kill $MON_PID    # to tear down"
