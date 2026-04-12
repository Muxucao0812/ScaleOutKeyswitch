#!/usr/bin/env python3
from __future__ import annotations

import argparse
import collections
import json
import pathlib
from typing import Dict, List

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


STACK_PARTS = [
    ("setup_s", "Setup", "#c44e52"),
    ("h2d_s", "H2D", "#4c72b0"),
    ("compute_wait_total_s", "FPGA wait", "#55a868"),
    ("d2h_s", "D2H", "#8172b2"),
    ("other_host_s", "Other host", "#dd8452"),
]

MODULE_NAMES = ["memory", "arithmetic", "montgomery", "ntt", "base_conv", "automorphism"]
MODULE_COLORS = {
    "memory": "#c44e52",
    "arithmetic": "#4c72b0",
    "montgomery": "#55a868",
    "ntt": "#8172b2",
    "base_conv": "#dd8452",
    "automorphism": "#64b5cd",
}


def _mean(values: List[float]) -> float:
    return float(sum(values) / len(values)) if values else 0.0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot aggregated accuracy and timing breakdown from tutorial3 benchmark JSON report."
    )
    parser.add_argument("report", type=pathlib.Path)
    parser.add_argument(
        "--out-prefix",
        type=pathlib.Path,
        default=None,
        help="Output path prefix without extension; defaults next to report.",
    )
    args = parser.parse_args()

    data = json.loads(args.report.read_text(encoding="utf-8"))
    grouped: Dict[int, List[Dict[str, object]]] = collections.defaultdict(list)
    for item in data.get("results", []):
        fpga = dict(item.get("fpga", {}))
        grouped[int(fpga["chips"])].append(fpga)

    chips = sorted(grouped)
    if not chips:
        raise RuntimeError(f"report has no results: {args.report}")

    labels = [f"{c} chip" if c == 1 else f"{c} chips" for c in chips]
    x = np.arange(len(chips))

    accuracy_values: List[float] = []
    accuracy_available: List[bool] = []
    run_totals: List[float] = []
    stack_values = {name: [] for name, _, _ in STACK_PARTS}
    module_values = {name: [] for name in MODULE_NAMES}

    for chip in chips:
        rows = grouped[chip]
        pred_rows = [r for r in rows if bool(r.get("pred_compare_available", True))]
        accuracy_available.append(len(pred_rows) == len(rows) and len(rows) > 0)
        if pred_rows:
            acc = sum(1 for r in pred_rows if bool(r.get("is_correct", False))) / len(pred_rows)
            accuracy_values.append(float(acc))
        else:
            accuracy_values.append(0.0)

        run_global_rows = [dict(r.get("runtime_global", {})) for r in rows]
        run_totals.append(_mean([float(g.get("run_program_total_s", 0.0)) for g in run_global_rows]))
        for key, _, _ in STACK_PARTS:
            stack_values[key].append(_mean([float(g.get(key, 0.0)) for g in run_global_rows]))

        per_module = collections.defaultdict(list)
        for r in rows:
            for stage in r.get("runtime_stage", []):
                per_module[str(stage["module"])].append(float(stage["stage_wall_s"]))
        for module in MODULE_NAMES:
            module_values[module].append(_mean(per_module[module]))

    if args.out_prefix is None:
        out_prefix = args.report.with_suffix("")
    else:
        out_prefix = args.out_prefix
    out_prefix.parent.mkdir(parents=True, exist_ok=True)
    png_path = out_prefix.with_suffix(".png")
    svg_path = out_prefix.with_suffix(".svg")

    plt.rcParams.update(
        {
            "font.size": 11,
            "axes.titlesize": 14,
            "axes.labelsize": 11,
            "figure.titlesize": 18,
        }
    )

    fig = plt.figure(figsize=(16, 11), constrained_layout=True)
    gs = fig.add_gridspec(2, 2, height_ratios=[1.0, 1.15])
    ax0 = fig.add_subplot(gs[0, 0])
    ax1 = fig.add_subplot(gs[0, 1])
    ax2 = fig.add_subplot(gs[1, :])

    bars = ax0.bar(
        x,
        [v * 100.0 for v in accuracy_values],
        color=["#55a868" if ok else "#cfcfcf" for ok in accuracy_available],
        width=0.6,
    )
    ax0.set_xticks(x, labels)
    ax0.set_ylim(0, 100)
    ax0.set_ylabel("accuracy (%)")
    ax0.set_title("Accuracy by chip count")
    ax0.grid(axis="y", linestyle="--", alpha=0.25)
    for bar, ok, value in zip(bars, accuracy_available, accuracy_values):
        text = f"{value * 100.0:.1f}%" if ok else "N/A"
        ax0.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 2.0, text, ha="center", va="bottom")

    bottom = np.zeros(len(chips))
    for key, label, color in STACK_PARTS:
        values = np.array(stack_values[key], dtype=float)
        ax1.bar(x, values, width=0.65, bottom=bottom, color=color, label=label)
        bottom += values
    for xi, total in zip(x, run_totals):
        ax1.text(xi, total + max(run_totals) * 0.015, f"{total:.2f}s", ha="center", va="bottom")
    ax1.set_xticks(x, labels)
    ax1.set_ylabel("seconds")
    ax1.set_title("Average run_program breakdown")
    ax1.legend(frameon=False, loc="upper left")
    ax1.grid(axis="y", linestyle="--", alpha=0.25)

    for module in MODULE_NAMES:
        values = np.array(module_values[module], dtype=float)
        ax2.plot(chips, values, marker="o", linewidth=2.2, label=module, color=MODULE_COLORS[module])
        for cx, cy in zip(chips, values):
            ax2.text(cx, cy + max(values.max() * 0.02, 0.03), f"{cy:.2f}", ha="center", va="bottom", fontsize=9)
    ax2.set_xticks(chips, labels)
    ax2.set_ylabel("seconds")
    ax2.set_title("Average stage wall time by module")
    ax2.legend(frameon=False, loc="upper left", ncol=3)
    ax2.grid(True, linestyle="--", alpha=0.25)

    fig.suptitle(f"Tutorial3 Benchmark Summary\n{args.report.name}")
    fig.text(
        0.5,
        0.01,
        "If accuracy is shown as N/A, the report was generated with --disable-emulator-mirror and prediction comparison is unavailable.",
        ha="center",
        fontsize=10,
        color="#444444",
    )

    fig.savefig(png_path, dpi=180, bbox_inches="tight")
    fig.savefig(svg_path, bbox_inches="tight")
    print(png_path)
    print(svg_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
