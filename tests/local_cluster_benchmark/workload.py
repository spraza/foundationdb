#!/usr/bin/env python3
"""
workload.py – simple 10 : 1 read/write stress for your *local* FDB build
-----------------------------------------------------------------------

• Uses only the wrapper you compiled (no system packages).
• API version read from your build’s fdb/apiversion.py (e.g. 740).
• `fdb.open()` implicitly starts the network thread.
• Low‑level retry loop; prints live OPS/sec.

Typical run
-----------
$ python3 workload.py \
                --cluster_file /tmp/fdblocal/conf/fdb.cluster \
                --fdb-src  ~/cnd_build_output/bindings/python \
                --lib-dir  ~/cnd_build_output/lib/libfdb_c.so \
                --threads 64
"""

from __future__ import annotations
import argparse, importlib, os, random, string, sys, threading, time
from collections import Counter
from pathlib import Path

# ───────────── CLI --------------------------------------------------------
p = argparse.ArgumentParser()
p.add_argument("--cluster_file", required=True, type=Path)
p.add_argument("--fdb-src",      required=True, type=Path,
               help="your build’s bindings/python dir")
p.add_argument("--lib-dir",      required=True, type=Path,
               help="dir that contains libfdb_c.so")
p.add_argument("--threads",      default=4,  type=int)
p.add_argument("--duration",     default=60, type=int)
args = p.parse_args()

# ───────────── load *only* your build ------------------------------------
sys.path.insert(0, str(args.fdb_src.resolve()))
os.environ["FDB_LIB_PATH"] = str(Path(args.lib_dir) / "libfdb_c.so")

import fdb
api_ver = importlib.import_module("fdb.apiversion").LATEST_API_VERSION
fdb.api_version(api_ver)
print(f"Using API version {api_ver}")

# ───────────── workload constants ----------------------------------------
PREFIX, KEY_COUNT, VAL_SIZE = b"wl"+os.urandom(2), 200_000, 256
RANGE_LEN, OPS_PER_TX = 50, 12
rk = lambda: PREFIX + f"{random.randint(0,KEY_COUNT-1):08d}".encode()
rv = lambda: "".join(random.choice(string.ascii_letters)
                     for _ in range(VAL_SIZE)).encode()

# ───────────── worker -----------------------------------------------------
def worker(db: "fdb.Database", agg: Counter, stop: threading.Event):
    while not stop.is_set():
        while True:                     # full retry loop
            tr, local = db.create_transaction(), Counter()
            for _ in range(OPS_PER_TX):  # populate
                r = random.random()
                if r < .66:  
                    tr.get(rk()).wait(); local["get"] += 1
                elif r < 0.75:                # RANGE GET
                    s = rk()
                    for _ in tr.get_range(s, s + b"\xff", limit=RANGE_LEN):
                        pass                  # consume the iterator
                    local["range_get"] += 1
                else:
                    w = random.random()
                    if w < .4:   tr.set(rk(), rv()); local["set"] += 1
                    elif w < .7: tr.clear(rk());     local["clear"] += 1
                    else:        s=rk(); tr.clear_range(s, s+b"\xff"); local["range_clear"] += 1
            try:
                tr.commit().wait()
                break                  # success
            except fdb.FDBError as e:
                tr = tr.on_error(e).wait()
        agg.update(local); agg["tx"] += 1; agg["ops"] += OPS_PER_TX

# ───────────── reporter ---------------------------------------------------
def reporter(agg: Counter, stop: threading.Event):
    last=0
    while not stop.wait(1):
        cur=agg["ops"]; print(f"{cur-last:7} ops/s  TX:{agg['tx']}",flush=True); last=cur

# ───────────── main -------------------------------------------------------
def main():
    db   = fdb.open(str(args.cluster_file))  # auto‑starts network
    agg  = Counter(); stop = threading.Event()
    thrs = [threading.Thread(target=worker, args=(db, agg, stop))
            for _ in range(args.threads)]
    for t in thrs: t.start()
    threading.Thread(target=reporter, args=(agg, stop), daemon=True).start()

    try:   time.sleep(args.duration)
    finally:
        stop.set(); [t.join() for t in thrs]
        print("\nFinal totals:", agg)

if __name__ == "__main__":
    main()
