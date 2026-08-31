#!/usr/bin/env python3
"""Plot the per-network samples written by `flightnet bench --csv`.

    flightnet bench --networks 10000 --csv build/bench.csv
    python scripts/plot_bench.py build/bench.csv

Produces build/bench_plots.png with three panels:
  1. solve time against network size, for each algorithm
  2. the distribution of the per-network Ford-Fulkerson / Dinic speedup
  3. augmenting paths (Ford-Fulkerson) against phases (Dinic)

matplotlib is optional; without it the script still prints a text summary, so
the project has no hard Python dependency.
"""

import csv
import statistics
import sys
from collections import defaultdict


def load(path):
    rows = []
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            try:
                rows.append(
                    {
                        "airports": int(row["airports"]),
                        "flights": int(row["flights"]),
                        "max_flow": int(row["max_flow"]),
                        "ff_ms": float(row["ff_ms"]),
                        "ek_ms": float(row["ek_ms"]),
                        "dinic_ms": float(row["dinic_ms"]),
                        "ff_aug": int(row["ff_augmentations"]),
                        "dinic_phases": int(row["dinic_phases"]),
                    }
                )
            except (KeyError, ValueError):
                continue  # skip a malformed row rather than aborting the run
    return rows


def summarize(rows):
    if not rows:
        print("no usable rows found")
        return

    ff = sum(r["ff_ms"] for r in rows)
    dn = sum(r["dinic_ms"] for r in rows)
    ratios = [r["ff_ms"] / r["dinic_ms"] for r in rows if r["dinic_ms"] > 0]

    print("networks              : %d" % len(rows))
    print("airports  (mean)      : %.1f" % statistics.mean(r["airports"] for r in rows))
    print("flights   (mean)      : %.1f" % statistics.mean(r["flights"] for r in rows))
    print("Ford-Fulkerson total  : %.1f ms" % ff)
    print("Dinic total           : %.1f ms" % dn)
    if dn > 0:
        print("aggregate speedup     : %.2fx" % (ff / dn))
    if ratios:
        print("median speedup        : %.2fx" % statistics.median(ratios))
    print(
        "FF augmenting paths   : %d" % sum(r["ff_aug"] for r in rows),
    )
    print("Dinic phases          : %d" % sum(r["dinic_phases"] for r in rows))

    # Speedup by size bucket, which is where the trend actually shows.
    buckets = defaultdict(list)
    for r in rows:
        if r["dinic_ms"] > 0:
            buckets[r["airports"] // 10 * 10].append(r["ff_ms"] / r["dinic_ms"])
    print("\nmedian speedup by airport count:")
    for size in sorted(buckets):
        vals = buckets[size]
        print("  %3d-%3d  n=%-6d  %.2fx" % (size, size + 9, len(vals), statistics.median(vals)))


def plot(rows, out_path):
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n(matplotlib not installed - skipping plots)")
        return

    fig, axes = plt.subplots(1, 3, figsize=(16, 4.6))

    # 1. time vs size
    ax = axes[0]
    ax.scatter([r["flights"] for r in rows], [r["ff_ms"] for r in rows],
               s=4, alpha=0.25, label="Ford-Fulkerson")
    ax.scatter([r["flights"] for r in rows], [r["dinic_ms"] for r in rows],
               s=4, alpha=0.25, label="Dinic")
    ax.set_xlabel("flight legs (E)")
    ax.set_ylabel("solve time (ms)")
    ax.set_yscale("log")
    ax.set_title("Max-flow time vs network size")
    ax.legend(markerscale=3)

    # 2. speedup distribution
    ax = axes[1]
    ratios = [r["ff_ms"] / r["dinic_ms"] for r in rows if r["dinic_ms"] > 0]
    if ratios:
        ax.hist(ratios, bins=60)
        ax.axvline(statistics.median(ratios), linestyle="--",
                   label="median %.2fx" % statistics.median(ratios))
        ax.legend()
    ax.set_xlabel("Ford-Fulkerson time / Dinic time")
    ax.set_ylabel("networks")
    ax.set_title("Per-network speedup")

    # 3. work done
    ax = axes[2]
    ax.scatter([r["dinic_phases"] for r in rows], [r["ff_aug"] for r in rows],
               s=4, alpha=0.25)
    ax.set_xlabel("Dinic phases")
    ax.set_ylabel("Ford-Fulkerson augmenting paths")
    ax.set_title("Work per solve")

    fig.tight_layout()
    fig.savefig(out_path, dpi=130)
    print("\nwrote %s" % out_path)


def main(argv):
    path = argv[1] if len(argv) > 1 else "build/bench.csv"
    try:
        rows = load(path)
    except IOError:
        print("cannot read %s\nrun: flightnet bench --networks 10000 --csv %s" % (path, path))
        return 1

    summarize(rows)
    plot(rows, path.rsplit(".", 1)[0] + "_plots.png")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
