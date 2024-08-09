#!/bin/bash

pkill -9 fdbserver
rm -rf /root/tmp/repro-issue-5871
mkdir -p /root/tmp/repro-issue-5871
cd /root/tmp/repro-issue-5871
echo "path: /root/tmp/repro-issue-5871"

/root/src/foundationdb/tests/loopback_cluster/run_custom_cluster.sh /root/build_output/ --stateless_count 1 --logs_count 1 --storage_count 4 --replication_count 2

/root/build_output/bin/fdbcli -C /root/tmp/repro-issue-5871/loopback-cluster/fdb.cluster --exec "writemode on; praza_experiment;"