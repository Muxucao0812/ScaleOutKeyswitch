#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import pathlib
import re
from typing import Dict, List

CLOCK_ROW_RE = re.compile(r"\|\s*ap_clk\s*\|\s*([0-9.]+)\s*ns\|\s*([0-9.]+)\s*ns\|")
LATENCY_ROW_RE = re.compile(
    r"\|\s*([0-9?]+)\|\s*([0-9?]+)\|\s*([0-9?.]+)\|\s*([0-9?.]+)\|\s*([0-9?]+)\|\s*([0-9?]+)\|"
)
TOTAL_RES_RE = re.compile(
    r"\|\s*Total\s*\|\s*([0-9~]+)\|\s*([0-9~]+)\|\s*([0-9~]+)\|\s*([0-9~]+)\|\s*([0-9~]+)\|"
)


def parse_csynth_report(path: pathlib.Path) -> Dict[str, str]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    kernel = path.name.replace("_csynth.rpt", "")
    if kernel.startswith("cinnamon_"):
        kernel = kernel

    target_clock_ns = ""
    estimated_clock_ns = ""
    m = CLOCK_ROW_RE.search(text)
    if m:
        target_clock_ns = m.group(1)
        estimated_clock_ns = m.group(2)

    latency_cycles_min = ""
    latency_cycles_max = ""
    ii_min = ""
    ii_max = ""
    lat_section = text.split("+ Latency:", 1)
    if len(lat_section) == 2:
        lm = LATENCY_ROW_RE.search(lat_section[1])
        if lm:
            latency_cycles_min = lm.group(1)
            latency_cycles_max = lm.group(2)
            ii_min = lm.group(5)
            ii_max = lm.group(6)

    bram = dsp = ff = lut = uram = ""
    um = TOTAL_RES_RE.search(text)
    if um:
        bram, dsp, ff, lut, uram = um.groups()

    return {
        "kernel": kernel,
        "report": str(path),
        "target_clock_ns": target_clock_ns,
        "estimated_clock_ns": estimated_clock_ns,
        "latency_cycles_min": latency_cycles_min,
        "latency_cycles_max": latency_cycles_max,
        "ii_min": ii_min,
        "ii_max": ii_max,
        "bram_18k": bram,
        "dsp": dsp,
        "ff": ff,
        "lut": lut,
        "uram": uram,
    }


def write_md(rows: List[Dict[str, str]], out_path: pathlib.Path) -> None:
    headers = [
        "kernel",
        "target_clock_ns",
        "estimated_clock_ns",
        "ii_min",
        "ii_max",
        "latency_cycles_min",
        "latency_cycles_max",
        "dsp",
        "bram_18k",
        "lut",
        "ff",
        "uram",
    ]
    lines = ["# Kernel CSynth Summary", "", "| " + " | ".join(headers) + " |", "|" + "|".join(["---"] * len(headers)) + "|"]
    for row in rows:
        lines.append("| " + " | ".join(row.get(h, "") for h in headers) + " |")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize Vitis HLS csynth reports")
    parser.add_argument(
        "--build-dir",
        type=pathlib.Path,
        default=pathlib.Path("CinnamonHLS/build/hw"),
    )
    parser.add_argument(
        "--out-dir",
        type=pathlib.Path,
        default=pathlib.Path("CinnamonHLS/build/reports"),
    )
    args = parser.parse_args()

    reports = sorted(args.build_dir.glob("_tmp_compile_*/reports/*/hls_reports/*_csynth.rpt"))
    if not reports:
        reports = sorted(args.build_dir.glob("_tmp_compile/reports/*/hls_reports/*_csynth.rpt"))

    rows = [parse_csynth_report(path) for path in reports]
    rows.sort(key=lambda x: x["kernel"])

    args.out_dir.mkdir(parents=True, exist_ok=True)
    json_path = args.out_dir / "kernel_csynth_summary.json"
    csv_path = args.out_dir / "kernel_csynth_summary.csv"
    md_path = args.out_dir / "kernel_csynth_summary.md"

    json_path.write_text(json.dumps(rows, indent=2), encoding="utf-8")

    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "kernel",
                "report",
                "target_clock_ns",
                "estimated_clock_ns",
                "ii_min",
                "ii_max",
                "latency_cycles_min",
                "latency_cycles_max",
                "dsp",
                "bram_18k",
                "lut",
                "ff",
                "uram",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    write_md(rows, md_path)
    print(f"Wrote: {json_path}")
    print(f"Wrote: {csv_path}")
    print(f"Wrote: {md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
