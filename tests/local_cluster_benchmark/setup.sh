#!/usr/bin/env bash
#
# setup.sh – spin up a two‑region FoundationDB test cluster under fdbmonitor
#
# Required flags (NO defaults):
#   --fdb-monitor   /path/to/fdbmonitor
#   --fdb-server    /path/to/fdbserver
#   --fdb-cli       /path/to/fdbcli
#   --base-dir      /abs/work/dir
#   --port-start    4500
#   --main-logs     4
#   --sat-logs      2
#   --main-stores   4
#   --stateless     4
#   --cluster-name  local74
#
set -euo pipefail

############################  arg‑parsing  ####################################
usage() {
  cat <<EOF
Usage: $0 --fdb-monitor BIN --fdb-server BIN --fdb-cli BIN --base-dir DIR \\
          --port-start N --main-logs N --sat-logs N --main-stores N \\
          --stateless N --cluster-name NAME
EOF
  exit 1
}

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

vars=(FDB_MONITOR FDB_SERVER FDB_CLI BASE_DIR PORT_START MAIN_LOGS SAT_LOGS MAIN_STORES STATELESS CLUSTER_NAME)
for v in "${vars[@]}"; do
  [[ -z "${!v:-}" ]] && { echo "❌  $v missing"; usage; }
done

############################  paths / helpers  ################################
mkdir -p "$BASE_DIR"/{conf,data,logs}

CFG="$BASE_DIR/conf/fdbmonitor.conf"
CLUSTER="$BASE_DIR/conf/fdb.cluster"
REGIONS_JSON="$BASE_DIR/conf/regions.json"

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

############################  trap for cleanup  ###############################
clean_up() {
  if [[ -n "${MON_PID:-}" ]] && kill -0 "$MON_PID" 2>/dev/null; then
    echo -e "\nCleaning up – stopping fdbmonitor …"
    kill "$MON_PID"
    wait "$MON_PID" 2>/dev/null || true
  fi
}
trap clean_up INT TERM

############################  write configs  ##################################
cat >"$CFG" <<EOF
[fdbmonitor]
user = $USER

[general]
cluster_file = $CLUSTER
restart_delay = 15
EOF

proc=0
# region 1 (dc1) + satellites
for dc in dc1; do
  for ((i=0;i<MAIN_STORES;i++));  do add_server "dc1_store_$i" $dc storage     $((proc++)); done
  for ((i=0;i<MAIN_LOGS;i++));    do add_server "dc1_log_$i"   $dc transaction $((proc++)); done
  for ((i=0;i<STATELESS;i++));    do add_server "dc1_stateless_$i" $dc stateless $((proc++)); done
done
for s in dc1s1 dc1s2; do
  for ((i=0;i<SAT_LOGS;i++)); do add_server "${s}_log_$i" $s transaction $((proc++)); done
done

# region 2 (dc2) + satellites
for dc in dc2; do
  for ((i=0;i<MAIN_STORES;i++));  do add_server "dc2_store_$i" $dc storage     $((proc++)); done
  for ((i=0;i<MAIN_LOGS;i++));    do add_server "dc2_log_$i"   $dc transaction $((proc++)); done
  for ((i=0;i<STATELESS;i++));    do add_server "dc2_stateless_$i" $dc stateless $((proc++)); done
done
for s in dc2s1 dc2s2; do
  for ((i=0;i<SAT_LOGS;i++)); do add_server "${s}_log_$i" $s transaction $((proc++)); done
done

COORDS=$(printf ",127.0.0.1:%s" $(seq $PORT_START $((PORT_START+4))))
echo "$CLUSTER_NAME:$(uuidgen | tr -d -)@${COORDS#,}" >"$CLUSTER"

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

############################  launch & configure  #############################
echo "Starting fdbmonitor …"
"$FDB_MONITOR" --conffile "$CFG" &
MON_PID=$!

echo "(BEGIN) Initialising database (single region) …"
"$FDB_CLI" -C "$CLUSTER" --exec "configure new single ssd-2 logs=$MAIN_LOGS"
echo "(END) Initialising database (single region) …"

# "$FDB_CLI" -C "$CLUSTER" --exec "coordinators auto"
# "$FDB_CLI" -C "$CLUSTER" --exec "fileconfigure $REGIONS_JSON"
# "$FDB_CLI" -C "$CLUSTER" --exec "configure usable_regions=2 logs=$MAIN_LOGS"

# echo "Waiting for cluster to become healthy …"
# until "$FDB_CLI" -C "$CLUSTER" --exec 'status minimal' | grep -q 'Healthy'; do
#   sleep 1
# done

echo -e "\n✅  Cluster is up in $BASE_DIR"
echo "   (script will clean up fdbmonitor if you Ctrl‑C)"
wait "$MON_PID"  
