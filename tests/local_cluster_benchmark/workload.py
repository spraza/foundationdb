#!/usr/bin/env python3
"""
workload.py- mixed read/write stress for a local FDB cluster

 • 10:1 read: write mix (gets, range-gets, set, clear, clear-range)
 • Runs N worker threads; prints live OPS every second.

Binding options
---------------
  --fdb-src   /path/to/foundationdb/bindings/python
              (adds that directory to PYTHONPATH before importing fdb)

  --lib-dir   /path/containing/libfdb_c.so
              (sets LD_LIBRARY_PATH / DYLD_LIBRARY_PATH and FDB_LIB_PATH)

Examples
--------
  # use in-tree bindings + lib
  python3 workload.py \
      --cluster_file /tmp/fdblocal/conf/fdb.cluster \
      --fdb-src ~/fdb/build/bindings/python \
      --lib-dir ~/fdb/build \
      --threads 16

  # system-installed bindings
  python3 workload.py --cluster_file /tmp/fdblocal/conf/fdb.cluster
"""
import argparse, os, random, string, sys, threading, time
from collections import Counter
from pathlib import Path

# ── argument parsing ────────────────────────────────────────────────────
ap = argparse.ArgumentParser()
ap.add_argument("--cluster_file", required=True, type=Path)
ap.add_argument("--threads",      default=4, type=int, help="worker threads")
ap.add_argument("--duration",     default=60, type=int, help="seconds to run")
ap.add_argument("--fdb-src",      type=Path, help="Path to bindings/python dir")
ap.add_argument("--lib-dir",      type=Path, help="Dir containing libfdb_c.so")
args = ap.parse_args()

# ── optional source/binary override before importing fdb ────────────────
if args.fdb_src:
    sys.path.insert(0, str(args.fdb_src.resolve()))
if args.lib_dir:
    lib_path = args.lib_dir.resolve()
    os.environ["FDB_LIB_PATH"] = str(lib_path / "libfdb_c.so")
    # also help the dynamic linker
    ld_var = "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
    os.environ[ld_var] = f"{lib_path}:{os.environ.get(ld_var,'')}"

import fdb                                     # noqa  (after path tweaks)
fdb.api_version(710)

# ── constants ───────────────────────────────────────────────────────────
KEY_SPACE  = os.urandom(2)
KEY_COUNT  = 200_000
VAL_SIZE   = 256
RANGE_LEN  = 50
OPS_PER_TX = 12

# ── helpers ─────────────────────────────────────────────────────────────
def rk():
    n = random.randint(0, KEY_COUNT - 1)
    return KEY_SPACE + f"{n:08d}".encode()

def rv():
    return "".join(random.choice(string.ascii_letters) for _ in range(VAL_SIZE)).encode()

# ── worker / reporter ───────────────────────────────────────────────────
def worker(db, stats: Counter, stop: threading.Event):
    while not stop.is_set():

        while True:                                # outer retry loop
            tr = db.create_transaction()
            local = Counter()

            try:
                # -------------- populate ---------------------------------
                for _ in range(OPS_PER_TX):
                    r = random.random()

                    if r < 0.66:                   # simple GET
                        tr.get(rk()).wait()
                        local["get"] += 1

                    elif r < 0.75:                 # range GET
                        start = rk()
                        for _ in tr.get_range(start, start + b"\xff",
                                              limit=RANGE_LEN).wait():
                            pass
                        local["range_get"] += 1

                    else:                          # write side
                        w = random.random()
                        if w < 0.4:
                            tr[rk()] = rv()
                            local["set"] += 1
                        elif w < 0.7:
                            tr.clear(rk())
                            local["clear"] += 1
                        else:
                            s = rk()
                            tr.clear_range(s, s + b"\xff")
                            local["range_clear"] += 1

                # -------------- commit -----------------------------------
                tr.commit().wait()                # may raise FDBError
                break                             # success → exit retry loop

            except fdb.FDBError as e:
                tr = tr.on_error(e).wait()        # back‑off
                continue                          # rebuild txn

        # success: merge counts
        stats.update(local)
        stats["tx"]  += 1
        stats["ops"] += OPS_PER_TX

def reporter(stats: Counter, stop: threading.Event):
    last = 0
    while not stop.wait(1):
        now = stats["ops"]
        print(f"{now-last:>7} ops/s   "
              f"G:{stats['get']} RG:{stats['range_get']} "
              f"S:{stats['set']} C:{stats['clear']} RC:{stats['range_clear']} "
              f"TX:{stats['tx']}", flush=True)
        last = now

# ── main ────────────────────────────────────────────────────────────────
def main():
    db     = fdb.open(str(args.cluster_file))
    stats  = Counter()
    stop   = threading.Event()

    threads = [threading.Thread(target=worker, args=(db, stats, stop))
               for _ in range(args.threads)]
    for t in threads: t.start()
    threading.Thread(target=reporter, args=(stats, stop), daemon=True).start()

    try:
        time.sleep(args.duration)
    finally:
        stop.set()
        for t in threads: t.join()

    print("\nFinal totals:", stats)

if __name__ == "__main__":
    main()
