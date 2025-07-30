#!/usr/bin/env python3
"""
saturator.py – push a *single-host* “ssd” FoundationDB cluster to its limits.

• Forks one process per CPU core (override with --procs).
• Each process spawns several threads that keep a configurable pipeline
  of write-heavy transactions in flight.
• Retries on transient errors; treats error 1021 (commit_unknown_result)
  as success because duplicates don’t matter for a stress test.


Usage:

$ python3 saturator.py \
        --cluster_file /tmp/fdblocal/conf/fdb.cluster \
        --fdb-src  ~/cnd_build_output/bindings/python \
        --lib-dir  ~/cnd_build_output/lib \
        --duration 60 \
        --procs 8 \
        --threads 8 \
        --pipeline 8 \
        --val-size 32768 \
        --ops-per-tx 256 \
        --read-frac 0.05

"""

from __future__ import annotations
import argparse, importlib, os, random, sys, time, threading
from multiprocessing import Process, Event, Value, cpu_count
from pathlib import Path
from ctypes import c_ulonglong as c_u64

# ───── CLI ────────────────────────────────────────────────────────────────
p = argparse.ArgumentParser()
p.add_argument("--cluster_file", required=True, type=Path)
p.add_argument("--fdb-src",      required=True, type=Path)
p.add_argument("--lib-dir",      required=True, type=Path)
p.add_argument("--procs",     default=cpu_count(), type=int, help="# forked procs")
p.add_argument("--threads",   default=8,  type=int, help="threads per proc")
p.add_argument("--duration",  default=60, type=int, help="seconds to run")
p.add_argument("--val-size",  default=32768, type=int, help="bytes per set")
p.add_argument("--ops-per-tx",default=256,   type=int, help="ops per txn")
p.add_argument("--pipeline",  default=8,     type=int, help="in-flight commits/thread")
p.add_argument("--read-frac", default=0.05,  type=float, help="fraction of reads [0-1]")
args = p.parse_args()

tot_ops = Value(c_u64, 0, lock=False)      # global counter

# ───── per-process worker ─────────────────────────────────────────────────
def proc_main(proc_idx: int, stop: Event):
    # load *this* build of FDB in isolation
    sys.path.insert(0, str(args.fdb_src.resolve()))
    os.environ["FDB_LIB_PATH"] = str(Path(args.lib_dir) / "libfdb_c.so")

    import fdb
    api = importlib.import_module("fdb.apiversion").LATEST_API_VERSION
    fdb.api_version(api)

    db  = fdb.open(str(args.cluster_file))
    rng = random.Random(os.urandom(8))

    PREFIX = f"wl{proc_idx:02x}".encode() + os.urandom(2)
    key_of  = lambda: PREFIX + f"{rng.randint(0, 999_999_999):012d}".encode()
    gen_val = lambda: os.urandom(args.val_size)

    def one_thread():
        futures: list[tuple["fdb.FutureVoid", "fdb.Transaction"]] = []
        while not stop.is_set():
            # fill pipeline
            while len(futures) < args.pipeline:
                tr = db.create_transaction()
                for _ in range(args.ops_per_tx):
                    if rng.random() < args.read_frac:        # read
                        tr.get(key_of())
                    else:                                    # write / clear
                        if rng.random() < 0.5:
                            tr.set(key_of(), gen_val())
                        else:
                            tr.clear(key_of())
                futures.append((tr.commit(), tr))

            fut, tr = futures.pop(0)
            try:
                fut.wait()
                tot_ops.value += args.ops_per_tx
            except fdb.FDBError as e:
                if stop.is_set():
                    continue                 # shutting down, don't retry
                if e.code == 1021:           # commit_unknown_result ⇒ count & move on
                    tot_ops.value += args.ops_per_tx
                    continue
                try:                         # normal on_error retry
                    tr = tr.on_error(e).wait()
                    futures.insert(0, (tr.commit(), tr))
                except fdb.FDBError:
                    pass                     # give up on this txn

    threads = [threading.Thread(target=one_thread, daemon=True)
               for _ in range(args.threads)]
    for t in threads: t.start()
    for t in threads: t.join()

# ───── reporter ───────────────────────────────────────────────────────────
def reporter(stop: Event):
    last = 0
    while not stop.wait(1):
        cur = tot_ops.value
        print(f"{cur-last:>11,} ops/s  total={cur:,}", flush=True)
        last = cur

# ───── main ───────────────────────────────────────────────────────────────
if __name__ == "__main__":
    stop = Event()
    procs = [Process(target=proc_main, args=(i, stop)) for i in range(args.procs)]
    for p in procs: p.start()

    threading.Thread(target=reporter, args=(stop,), daemon=True).start()
    time.sleep(args.duration)
    stop.set()

    for p in procs: p.join()
    print(f"\nFinished – {tot_ops.value:,} total ops")
