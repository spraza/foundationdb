#!/usr/bin/env python3
"""
measure.py – interactive “top”‑style monitor for a local FoundationDB cluster

▸ Cluster headline: generation + recovery state
▸ Per‑process: DC ▸ addr ▸ roles ▸ CPU % ▸ RSS MB
▸ Keys:  ↑ / ↓ or Ctrl‑P / Ctrl‑N  – move highlight
          q                        – quit

prereqs:   pip install psutil rich textual
usage:     python3 measure.py --cluster_file /tmp/fdblocal/conf/fdb.cluster \
                              --fdb_cli      ~/cnd_build_output/bin/fdbcli
"""
import argparse, json, shlex, subprocess, time
from pathlib import Path
from typing import Dict, List, Tuple

import psutil
from rich.table import Table
from textual.app import App, ComposeResult
from textual.widgets import DataTable, Static

# ------------------------------ helpers ------------------------------------ #
def run(cmd: str) -> str:
    return subprocess.check_output(shlex.split(cmd), text=True)

def status_json(cluster: Path, fdbcli: Path) -> dict:
    return json.loads(run(f"{fdbcli} -C {cluster} --exec 'status json'"))

def port(addr: str) -> int:                    # handles :tls suffix
    for part in reversed(addr.split(":")):
        if part.isdigit():
            return int(part)
    raise ValueError

def pid_map() -> Dict[int, int]:
    mp: Dict[int, int] = {}
    for p in psutil.process_iter(["pid", "name"]):
        if p.info["name"] != "fdbserver":
            continue
        for c in p.net_connections(kind="inet"):
            if c.status == psutil.CONN_LISTEN:
                mp[c.laddr.port] = p.pid
    return mp

# ------------------------------ TUI class ---------------------------------- #
class FDBTop(App):
    CSS = """
    Screen { layout: vertical; }
    Static { height: 1; content-align: center middle; }
    DataTable { height: 1fr; width: 100%; }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("up", "up", "Prev"),
        ("down", "down", "Next"),
        ("ctrl+p", "up", None),
        ("ctrl+n", "down", None),
    ]

    def __init__(self, cl_file: Path, fdbcli: Path, interval: float):
        super().__init__()
        self.cluster, self.fdbcli, self.interval = cl_file, fdbcli, interval
        self.table: DataTable
        self.row = 0
        self.proc_cache: Dict[int, psutil.Process] = {}

    def compose(self) -> ComposeResult:
        self.header = Static("")   # <- cluster title widget
        self.table  = DataTable(zebra_stripes=True, show_header=True, show_cursor=True)
        self.table.add_columns("DC", "Addr", "Roles", "CPU%", "RSS MB")
        yield self.header
        yield self.table

    async def on_mount(self):
        self.set_interval(self.interval, self.refresh_table)

    async def refresh_table(self):
        try:
            s = status_json(self.cluster, self.fdbcli)
        except subprocess.CalledProcessError as e:
            self.table.title = f"[red]fdbcli error: {e.output}"
            return

        rec = s["cluster"]["recovery_state"]
        gen, rec_name = rec["active_generations"], rec["name"]        
        self.header.update(f"FoundationDB – generation {gen}, {rec_name}")

        port_pid = pid_map()
        rows: List[Tuple[str, ...]] = []

        for key, info in s["cluster"]["processes"].items():
            addr = info.get("address", key)
            if ":" not in addr:
                continue
            try:
                prt = port(addr)
            except ValueError:
                continue
            pid = port_pid.get(prt)
            dc   = info["locality"].get("dcid", "?")
            roles= ",".join(r["role"] for r in info["roles"])

            if pid:
                proc = self.proc_cache.setdefault(pid, psutil.Process(pid))
                cpu  = proc.cpu_percent(None)      # delta since last refresh
                rss  = proc.memory_info().rss / 2**20
                cpu_s, rss_s = f"{cpu:4.1f}", f"{rss:6.1f}"
            else:
                cpu_s = rss_s = "n/a"

            rows.append((dc, addr, roles, cpu_s, rss_s))

        rows.sort(key=lambda r: r[1])
        sel_max = len(rows) - 1
        self.row = max(0, min(self.row, sel_max))

        # redraw table
        self.table.clear()
        for r in rows:
            self.table.add_row(*r)
        self.table.cursor_coordinate = (self.row, 0)        

    # ---------- key actions ----------
    def action_up(self):
        if self.row > 0:
            self.row -= 1

    def action_down(self):
        self.row += 1

# --------------------------- CLI / main ------------------------------------ #
def parse_args():
    ap = argparse.ArgumentParser(description="Interactive FDB process monitor")
    ap.add_argument("--cluster_file", required=True, type=Path, metavar="PATH")
    ap.add_argument("--fdb_cli",     required=True, type=Path, metavar="BIN")
    ap.add_argument("--interval", default=2.0, type=float, metavar="SEC")
    return ap.parse_args()

if __name__ == "__main__":
    a = parse_args()
    FDBTop(a.cluster_file, a.fdb_cli, a.interval).run()
