#!/bin/bash

pkill -9 fdbserver
rm -rf /root/tmp/repro-issue-5871
mkdir /root/tmp/repro-issue-5871
cd /root/tmp/repro-issue-5871
echo "path: /root/tmp/repro-issue-5871"

/root/src/foundationdb/tests/loopback_cluster/run_custom_cluster.sh /root/build_output/

for i in {1..1000000}
    do 
        random_value=$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 1024 | head -n 1)
        /root/build_output/bin/fdbcli -C /root/tmp/repro-issue-5871/loopback-cluster/fdb.cluster --exec "writemode on; set key$i $random_value;"
done
