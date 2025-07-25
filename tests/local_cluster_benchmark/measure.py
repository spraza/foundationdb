#!/usr/bin/env python3
"""
measure.py – live “top”‑style view of FoundationDB processes

  • app  : shows   active_generations  (status json)
  • system: shows  CPU % and RSS MB    (psutil)

Grouping key:  dcid / ip:port / roles

pre-reqs (once): 
    $ pip3 install psutil rich

usage:
    $ python3 measure.py /tmp/fdblocal/conf/fdb.cluster
"""

import argparse, json, os, re, shlex, subprocess, time
from collections import defaultdict
from pathlib import Path

import psutil
from rich.console import Console
from rich.live import Live
from rich.table import Table

################################################################################
def run(cmd: str) -> str:
    """Run shell cmd, return stdout (raise on non‑zero)."""
    return subprocess.check_output(shlex.split(cmd), text=True)

def get_status_json(cluster_file: Path, fdbcli: str) -> dict:
    out = run(f'{fdbcli} -C {cluster_file} --exec "status json"')
    return json.loads(out)

def port_from_address(addr: str) -> int:
    """Extract numeric port from 'IP:PORT' or 'IP:PORT:tls'."""
    for part in reversed(addr.split(":")):
        if part.isdigit():
            return int(part)
    raise ValueError(f"no port in {addr}")

def map_port_to_pid() -> dict[int, int]:
    """
    Return {LISTEN_port: pid} for every local fdbserver.
    Works on all psutil versions (no attrs= trick).
    """
    mapping = {}
    for p in psutil.process_iter(attrs=["pid", "name"]):
        if p.info["name"] != "fdbserver":
            continue
        try:            
            conns = p.net_connections(kind="inet")
        except (psutil.AccessDenied, psutil.ZombieProcess):
            continue
        for c in conns:
            if c.status == psutil.CONN_LISTEN:
                mapping[c.laddr.port] = p.pid
    return mapping

################################################################################
def make_table(rows: list[tuple]):
    tbl = Table(title="FoundationDB local cluster – live view", expand=True)
    tbl.add_column("DC")
    tbl.add_column("Addr")
    tbl.add_column("Roles")
    tbl.add_column("Gen", justify="right")
    tbl.add_column("CPU%", justify="right")
    tbl.add_column("RSS MB", justify="right")
    for r in rows:
        tbl.add_row(*r)
    return tbl

def parse_args():
    parser = argparse.ArgumentParser(
        description="Live CPU/RSS + active_generations monitor for a local FDB cluster",
        prog="measure.py",
    )
    parser.add_argument(
        "--cluster_file", required=True, type=Path, metavar="PATH",
        help="path to fdb.cluster (e.g. /tmp/fdblocal/conf/fdb.cluster)",
    )
    parser.add_argument(
        "--fdb_cli", required=True, metavar="BIN",
        help="path to fdbcli binary (e.g. ~/cnd_build_output/bin/fdbcli)",
    )
    parser.add_argument(
        "--interval", default=2.0, type=float, metavar="SEC",
        help="refresh period (default: 2 s)",
    )
    return parser.parse_args()    

def main():
    args = parse_args()
    console = Console()
    with Live(console=console, refresh_per_second=4) as live:
        while True:
            try:
                status = get_status_json(args.cluster_file, args.fdb_cli)
            except subprocess.CalledProcessError as e:
                live.update(f"[red]fdbcli failed:\n{e.output}")
                time.sleep(args.interval)
                continue

            # build dictionaries
            port_pid   = map_port_to_pid()
            proc_info  = status["cluster"]["processes"]

            rows = []
            for key, info in proc_info.items():
                addr = info.get("address", key)     # hex‑key → use its address field
                if ":" not in addr:                 # still no port? skip
                    continue

                try:
                    port = port_from_address(addr)
                except ValueError:
                    continue

                pid = port_pid.get(port)
                dc    = info["locality"].get("dcid", "?")
                roles = ",".join(sorted(r["role"] for r in info["roles"]))
                gen   = str(info.get("active_generations", "?"))

                if pid:
                    p   = psutil.Process(pid)
                    cpu = f"{p.cpu_percent(None):4.1f}"
                    rss = f"{p.memory_info().rss / (1024**2):6.1f}"
                else:
                    cpu = rss = "n/a"

                rows.append((dc, addr, roles, gen, cpu, rss))

            rows.sort(key=lambda r: r[1])  # sort by address            
            live.update(make_table(rows))
            time.sleep(args.interval)

################################################################################
if __name__ == "__main__":
    main()
