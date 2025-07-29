#!/usr/bin/env python3
"""
fault.py – partition DCs with iptables if NET_ADMIN is available,
           otherwise pause remote‑DC processes with SIGSTOP/SIGCONT.
"""

from __future__ import annotations
import argparse, json, os, signal, subprocess, sys, time
from pathlib import Path
import psutil

# ---------- helpers --------------------------------------------------------
def run(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, text=True, stderr=subprocess.PIPE)

def listening_ports(cluster: Path, fdbcli: str, dcid: str) -> set[int]:
    js = json.loads(run([fdbcli, "-C", cluster, "--exec", "status json"]))
    ports = set()
    for info in js["cluster"]["processes"].values():
        if info["locality"].get("dcid") != dcid:
            continue
        addr = info.get("address", "")
        if ":" in addr:
            try: ports.add(int(addr.split(":")[1].split(":")[0]))
            except ValueError: pass
    return ports

def real_pids(cluster: Path, fdbcli: str, dcid: str) -> list[int]:
    tgt_ports = listening_ports(cluster, fdbcli, dcid)
    pids = []
    for p in psutil.process_iter(["pid", "name"]):
        if p.info["name"] != "fdbserver": continue
        try:
            for c in p.connections(kind="inet"):
                if c.status == psutil.CONN_LISTEN and c.laddr.port in tgt_ports:
                    pids.append(p.pid); break
        except (psutil.AccessDenied, psutil.ZombieProcess): continue
    return pids

def cluster_healthy(cluster: Path, fdbcli: str) -> bool:
    rec = json.loads(
        run([fdbcli, "-C", cluster, "--exec", "status json"])
    )["cluster"]["recovery_state"]
    return rec["name"] == "fully_recovered" and rec.get("lag_seconds", 0) == 0

# ---------- iptables capability test --------------------------------------
def iptables_works(sudo: list[str]) -> bool:
    try:
        subprocess.check_call(sudo + ["iptables", "-L", "-n", "-w"],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
        return True
    except subprocess.CalledProcessError:
        return False

def safe_iptables(rule: list[str], sudo: list[str]):
    try:  subprocess.check_call(sudo + rule,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError: pass  # ignore EEXIST etc.

# ---------- main -----------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cluster_file", required=True, type=Path)
    ap.add_argument("--fdb_cli",      required=True)
    ap.add_argument("--primary_dc",   default="dc1")
    ap.add_argument("--remote_dc",    default="dc2")
    ap.add_argument("--duration",     type=int, default=300)
    args = ap.parse_args()

    sudo = [] if os.geteuid() == 0 else ["sudo"]

    if iptables_works(sudo):
        # ----- network partition path ------------------------------------
        prim = listening_ports(args.cluster_file, args.fdb_cli, args.primary_dc)
        rem  = listening_ports(args.cluster_file, args.fdb_cli, args.remote_dc)
        if not prim or not rem:
            sys.exit("Could not discover listen ports for both DCs")
        rules = [(s, d) for s in prim for d in rem] + \
                [(d, s) for s in prim for d in rem]
        def ipt(action, s, d): return [
            "iptables", action, "OUTPUT",
            "-p", "tcp", "--sport", str(s), "--dport", str(d),
            "-j", "DROP", "-m", "comment", "--comment", "fdb-fault"]
        print(f"🔌  iptables partition {args.primary_dc} ↔ {args.remote_dc} "
              f"for {args.duration}s")
        for s, d in rules: safe_iptables(ipt("-I", s, d), sudo)
        try: time.sleep(args.duration)
        finally:
            print("🩹  healing iptables rules")
            for s, d in reversed(rules): safe_iptables(ipt("-D", s, d), sudo)

    else:
        # ----- fallback: SIGSTOP remote processes -------------------------
        pids = real_pids(args.cluster_file, args.fdb_cli, args.remote_dc)
        if not pids: sys.exit("No remote‑DC PIDs found; aborting")
        print(f"🔌  SIGSTOP {len(pids)} processes in {args.remote_dc} "
              f"for {args.duration}s (no NET_ADMIN)")
        for pid in pids: os.kill(pid, signal.SIGSTOP)
        try: time.sleep(args.duration)
        finally:
            print("🩹  SIGCONT processes")
            for pid in pids: os.kill(pid, signal.SIGCONT)

    # ---------- wait for recovery ----------------------------------------
    print("⏳  waiting for fully_recovered + lag 0 …")
    while not cluster_healthy(args.cluster_file, args.fdb_cli):
        time.sleep(2)
    print("✅  cluster healthy again")

if __name__ == "__main__":
    main()
