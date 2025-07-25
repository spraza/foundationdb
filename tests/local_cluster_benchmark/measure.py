#!/usr/bin/env python3
"""
measure.py – interactive “top”‑style monitor for a local FoundationDB cluster

▸ Header shows generation + recovery state
▸ Table: DC ▸ addr ▸ roles ▸ CPU % ▸ RSS MB
▸ Keys:  ↑ / ↓   or   Ctrl‑K / Ctrl‑J   – move highlight      q – quit

Sorting:  --sort {dc,addr,cpu,rss}   (default addr)
Example : python3 measure.py --cluster_file /tmp/fdblocal/conf/fdb.cluster \
                             --fdb_cli      ~/cnd_build_output/bin/fdbcli     \
                             --sort cpu
"""
from __future__ import annotations
import argparse, json, shlex, subprocess
from pathlib import Path
from typing import Dict, List, Tuple

import psutil
from textual.app import App, ComposeResult
from textual.widgets import DataTable, Static

# ── helpers ────────────────────────────────────────────────────────────
def run(cmd: str) -> str:
    return subprocess.check_output(shlex.split(cmd), text=True)

def status_json(cluster: Path, fdbcli: Path) -> dict:
    return json.loads(run(f"{fdbcli} -C {cluster} --exec 'status json'"))

def port(addr: str) -> int:                       # handles :tls suffix
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

def scroll_row(tbl: DataTable, row: int) -> None:   # support old Textual
    if hasattr(tbl, "scroll_to_row"):
        tbl.scroll_to_row(row)
    elif hasattr(tbl, "scroll_to_cell"):
        tbl.scroll_to_cell(row, 0)

# ── TUI application ────────────────────────────────────────────────────
class FDBTop(App):
    CSS = """
    Screen   { layout: vertical; }
    Static   { height: 1; content-align: center middle; }
    DataTable{ height: 1fr; width: 100%; }
    """
    BINDINGS = [
        ("q", "quit", ""),
        ("up", "row_up", ""),
        ("down", "row_down", ""),
        ("ctrl+k", "row_up", ""),
        ("ctrl+j", "row_down", ""),
    ]

    def __init__(self, cluster: Path, fdbcli: Path, interval: float, sort_key: str):
        super().__init__()
        self.cluster, self.fdbcli, self.interval = cluster, fdbcli, interval
        self.sort_key = sort_key
        self.header: Static
        self.table:  DataTable
        self.row = 0
        self.rows: List[Tuple[str, ...]] = []
        self.sel_port: int | None = None
        self.proc_cache: Dict[int, psutil.Process] = {}
        self.port_pid:   Dict[int, int]           = {}
        self.tick = 0

    # layout
    def compose(self) -> ComposeResult:
        self.header = Static("")
        self.table  = DataTable(zebra_stripes=True, show_header=True, show_cursor=True)
        self.table.add_columns("DC", "Addr", "Roles", "CPU%", "RSS MB")
        yield self.header
        yield self.table

    async def on_mount(self):
        self.set_interval(self.interval, self._update)

    # update loop
    async def _update(self):
        stat = status_json(self.cluster, self.fdbcli)
        rec  = stat["cluster"]["recovery_state"]
        self.header.update(
            f"FoundationDB – generation {rec['active_generations']}, {rec['name']}"
        )

        if self.tick % 5 == 0:                     # refresh port→pid every ~10 s
            self.port_pid = pid_map()
        self.tick += 1
        port_pid = self.port_pid

        rows: List[Tuple[str, ...]] = []
        for key, info in stat["cluster"]["processes"].items():
            addr = info.get("address", key)
            if ":" not in addr:
                continue
            try:
                pnum = port(addr)
            except ValueError:
                continue

            pid   = port_pid.get(pnum)
            dc    = info["locality"].get("dcid", "?")
            roles = ",".join(r["role"] for r in info["roles"])

            if pid:
                proc  = self.proc_cache.setdefault(pid, psutil.Process(pid))
                cpu_s = f"{proc.cpu_percent(None):4.1f}"
                rss_s = f"{proc.memory_info().rss / 2**20:6.1f}"
            else:
                cpu_s = rss_s = "n/a"

            rows.append((dc, addr, roles, cpu_s, rss_s, pnum))

        # sort
        key_map = {
            "dc"  : lambda r: r[0],
            "addr": lambda r: r[1],
            "cpu" : lambda r: float(r[3] if r[3] != "n/a" else -1.0),
            "rss" : lambda r: float(r[4] if r[4] != "n/a" else -1.0),
        }
        rows.sort(key=key_map[self.sort_key], reverse=self.sort_key in {"cpu", "rss"})

        # keep highlight on same port if still present
        if self.sel_port is not None:
            for idx, *_, pnum in rows:
                if pnum == self.sel_port:
                    self.row = idx
                    break
        self.row = min(self.row, max(0, len(rows) - 1))

        # redraw
        self.table.clear()
        for dc, addr, roles, cpu, rss, _ in rows:
            self.table.add_row(dc, addr, roles, cpu, rss)
        self.table.cursor_coordinate = (self.row, 0)
        scroll_row(self.table, self.row)
        self.rows = rows

    # key handlers
    def action_row_up(self):
        if self.row > 0:
            self.row -= 1
            self.sel_port = port(self.rows[self.row][1])
            self.table.cursor_coordinate = (self.row, 0)
            scroll_row(self.table, self.row)

    def action_row_down(self):
        if self.row < len(self.rows) - 1:
            self.row += 1
            self.sel_port = port(self.rows[self.row][1])
            self.table.cursor_coordinate = (self.row, 0)
            scroll_row(self.table, self.row)

# CLI
def parse_args():
    p = argparse.ArgumentParser(description="Interactive FDB process monitor")
    p.add_argument("--cluster_file", required=True, type=Path)
    p.add_argument("--fdb_cli",     required=True, type=Path)
    p.add_argument("--interval",    default=2.0, type=float)
    p.add_argument(
        "--sort", choices=["dc", "addr", "cpu", "rss"], default="addr",
        help="column to sort by (default addr)",
    )
    return p.parse_args()

if __name__ == "__main__":
    a = parse_args()
    FDBTop(a.cluster_file, a.fdb_cli, a.interval, a.sort).run()
