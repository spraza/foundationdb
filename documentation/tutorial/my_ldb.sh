#!/bin/bash

if [ -z "$1" ]; then
  echo "Usage: $0 /path/to/db"
  exit 1
fi

db_path="$1"

if [ ! -d "$db_path" ]; then
  echo "The provided path does not exist or is not a directory."
  exit 1
fi

# REPL
while true; do
  read -p "ldb> " cmd
  if [ "$cmd" = "exit" ]; then
    break
  fi
  /root/build_output/fdbserver/rocksdb-prefix/src/rocksdb-build/tools/ldb --db="$db_path" $cmd
done
