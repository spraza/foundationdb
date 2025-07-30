#!/usr/bin/env python3

"""
usage:
  $ ~/src/foundationdb/tests/local_cluster_benchmark/plot.py /tmp/fdblocal/logs/trace*4500* --width 200 --height 25 --bucket 0.5
"""


from __future__ import annotations
import argparse, gzip, re, sys, datetime as dt
from pathlib import Path
from typing import List, Tuple

# ─── regex grabs both epoch Time and ISO DateTime ─────────────────────────
# RX = re.compile(
#     r'Time="(?P<time>[\d.]+)".*?DateTime="(?P<iso>[^"]+)".*?Type="MemoryMetrics".*?TotalMemory96="(?P<mem>\d+)"'
# )
RX = re.compile(
    r'Time="(?P<time>[\d.]+)".*?DateTime="(?P<iso>[^"]+)".*?ResidentMemory="(?P<mem>\d+)"',
    re.DOTALL
)

def parse_file(path: Path) -> List[Tuple[float, int, str]]:
    opener = gzip.open if path.suffix == ".gz" else open
    out: List[Tuple[float, int, str]] = []
    with opener(path, "rt", errors="ignore") as fh:
        for line in fh:
            if "ProcessMetrics" not in line:
                continue
            m = RX.search(line)
            if m:
                out.append((float(m["time"]), int(m["mem"]), m["iso"]))
    return out

# ─── choose friendly unit (B / KiB / MiB) ─────────────────────────────────
def pretty_unit(max_bytes: int) -> Tuple[float, str]:
    if max_bytes >= 1 << 20:
        return (1 << 20, "MiB")
    if max_bytes >= 1 << 10:
        return (1 << 10, "KiB")
    return (1.0, "B")

def bin_and_scale(samples, bucket, width, height):
    if not samples:
        sys.exit("no MemoryMetrics found")

    samples.sort()
    t0 = samples[0][0]
    binned: List[int] = [0] * width
    for t, v, _ in samples:
        idx = int((t - t0) // bucket)
        if idx < width:
            binned[idx] = max(binned[idx], v)

    vmax = max(binned) or 1
    scaled = [int(v * (height - 1) / vmax) for v in binned]

    grid = [[False] * width for _ in range(height)]
    for x, y in enumerate(scaled):
        for row in range(y + 1):
            grid[height - 1 - row][x] = True
    return grid, vmax, t0, samples[0][2], samples[-1][2]

def draw(grid, vmax, unit_div, unit_lbl, iso_start, iso_end, bucket):
    height, width = len(grid), len(grid[0])
    step = vmax / (height - 1)

    # rows with y-axis labels
    for row_idx, row in enumerate(grid):
        if row_idx % 4 == 0:                       # label every 4 rows
            label_val = vmax - row_idx * step
            label = f"{label_val/unit_div:6.1f} {unit_lbl} | "
        else:
            label = " " * 12 + "| "
        line = "".join("█" if cell else " " for cell in row)
        print(label + line)

    # x-axis
    print(" " * 12 + "+" + "─" * width)
    # human-readable timestamps (first & last sample DateTime)
    print(iso_start.ljust(width // 2 + 6) + iso_end.rjust(width // 2 + 6))

# ─── main ────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="ASCII plot of TotalMemory96")
    ap.add_argument("paths", nargs="+", help="trace XML files or directories")
    ap.add_argument("--width",  type=int, default=80)
    ap.add_argument("--height", type=int, default=20)
    ap.add_argument("--bucket", type=float, default=1.0,
                    help="bin size in seconds (default 1)")
    args = ap.parse_args()

    files: List[Path] = []
    for p in args.paths:
        path = Path(p)
        if path.is_dir():
            files += sorted(path.glob("trace*.xml*"))
        else:
            files.append(path)
    if not files:
        sys.exit("no trace files found")

    samples: List[Tuple[float, int, str]] = []
    for f in files:
        samples += parse_file(f)

    grid, vmax, t0, iso_start, iso_end = bin_and_scale(
        samples, args.bucket, args.width, args.height
    )
    unit_div, unit_lbl = pretty_unit(vmax)
    draw(grid, vmax, unit_div, unit_lbl, iso_start, iso_end, args.bucket)

if __name__ == "__main__":
    main()
