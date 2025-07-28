#!/usr/bin/env python3
"""
fault.py – freeze all fdbserver processes in a remote DC (SIGSTOP/SIGCONT)
"""

import argparse, json, os, signal, subprocess, sys, time
from pathlib import Path

import psutil                   # pip install psutil

def run(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, text=True)

# --------------------------------------------------------------------------- #
# 1. collect listening *ports* for every process whose locality.dcid == dcid
# 2. walk psutil → map those ports to real kernel PIDs                         #
# --------------------------------------------------------------------------- #
def pids_for_dcid(cluster: Path, fdbcli: str, dcid: str) -> list[int]:
    status = json.loads(run([fdbcli, "-C", cluster, "--exec", "status json"]))

    target_ports: set[int] = set()
    for info in status["cluster"]["processes"].values():
        if info["locality"].get("dcid") != dcid:
            continue
        addr = info.get("address", "")
        if ":" in addr:
            try:
                target_ports.add(int(addr.split(":")[1].split(":")[0]))
            except ValueError:
                pass

    if not target_ports:
        return []

    pids: list[int] = []
    for proc in psutil.process_iter(["pid", "name"]):
        if proc.info["name"] != "fdbserver":
            continue
        try:
            for c in proc.connections(kind="inet"):
                if c.status == psutil.CONN_LISTEN and c.laddr.port in target_ports:
                    pids.append(proc.pid)
                    break
        except (psutil.AccessDenied, psutil.ZombieProcess):
            continue
    return pids

def wait_healthy(cluster: Path, fdbcli: str):
    while True:
        js = json.loads(run([fdbcli, "-C", cluster, "--exec", "status json"]))
        rec = js["cluster"]["recovery_state"]
        lag = rec.get("lag_seconds") or rec.get("remote_recovered", 0)
        if rec["name"] == "fully_recovered" and not lag:
            break
        time.sleep(2)

# --------------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cluster_file", required=True, type=Path)
    ap.add_argument("--fdb_cli",      required=True)
    ap.add_argument("--remote_dc",    default="dc2")
    ap.add_argument("--duration",     type=int, default=300)
    args = ap.parse_args()

    pids = pids_for_dcid(args.cluster_file, args.fdb_cli, args.remote_dc)
    if not pids:
        sys.exit(f"No fdbserver PIDs found for dcid='{args.remote_dc}'")

    print(f"🔌  SIGSTOP {len(pids)} processes in {args.remote_dc} "
          f"for {args.duration}s")
    for pid in pids:
        os.kill(pid, signal.SIGSTOP)

    try:
        time.sleep(args.duration)
    finally:
        print("🩹  SIGCONT processes")
        for pid in pids:
            os.kill(pid, signal.SIGCONT)

    print("⏳  waiting until cluster healthy & replicated …")
    wait_healthy(args.cluster_file, args.fdb_cli)
    print("✅  cluster healthy again")

if __name__ == "__main__":
    main()
