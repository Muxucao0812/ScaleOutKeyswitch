#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import re
from typing import Dict, Optional

TIMING_PAIR_RE = re.compile(r"\bWNS\(ns\)\s+TNS\(ns\)\b", re.IGNORECASE)
FLOAT_RE = re.compile(r"-?[0-9]+\.[0-9]+")
STATE_RE = re.compile(r'"state"\s*:\s*"([^"]+)"')
INT_TOKEN_RE = re.compile(r"^[0-9,]+$")


def _find_one(root: pathlib.Path, pattern: str) -> Optional[pathlib.Path]:
    matches = sorted(root.glob(pattern))
    return matches[0] if matches else None


def _find_latest(root: pathlib.Path, pattern: str) -> Optional[pathlib.Path]:
    matches = list(root.glob(pattern))
    if not matches:
        return None
    return max(matches, key=lambda p: p.stat().st_mtime)


def parse_timing(path: pathlib.Path) -> Dict[str, str]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    lines = text.splitlines()
    wns = ""
    tns = ""
    for i, line in enumerate(lines):
      if TIMING_PAIR_RE.search(line):
          if i + 1 < len(lines):
              vals = FLOAT_RE.findall(lines[i + 1])
              if len(vals) >= 2:
                  wns, tns = vals[0], vals[1]
                  break
    if not wns:
        # fallback: first two floats in file
        vals = FLOAT_RE.findall(text)
        if len(vals) >= 2:
            wns, tns = vals[0], vals[1]
    return {"timing_report": str(path), "wns_ns": wns, "tns_ns": tns}


def parse_link_state(path: pathlib.Path) -> Dict[str, str]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    states = STATE_RE.findall(text)
    state = states[-1] if states else ""
    return {"link_summary": str(path), "link_state": state}


def parse_util(path: pathlib.Path) -> Dict[str, str]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    out = {
        "util_report": str(path),
        "lut_used": "",
        "lut_available": "",
        "ff_used": "",
        "ff_available": "",
        "bram_used": "",
        "bram_available": "",
        "dsp_used": "",
        "dsp_available": "",
    }
    names = {
        "clb luts*": ("lut_used", "lut_available"),
        "clb registers": ("ff_used", "ff_available"),
        "block ram tile": ("bram_used", "bram_available"),
        "dsps": ("dsp_used", "dsp_available"),
    }
    for line in text.splitlines():
        if "|" not in line:
            continue
        cols = [c.strip() for c in line.split("|")[1:-1]]
        if len(cols) < 3:
            continue
        name = cols[0].lower()
        if name not in names:
            continue
        numbers = [tok for tok in cols[1:] if INT_TOKEN_RE.match(tok)]
        if len(numbers) < 2:
            continue
        used = numbers[0]
        avail = numbers[-1]
        used_key, avail_key = names[name]
        if not out[used_key]:
            out[used_key] = used
            out[avail_key] = avail
    return out


def write_md(summary: Dict[str, str], out_path: pathlib.Path) -> None:
    lines = [
        "# HW Link Summary",
        "",
        "| Metric | Value |",
        "|---|---|",
    ]
    for key in [
        "link_summary",
        "link_state",
        "timing_report",
        "wns_ns",
        "tns_ns",
        "util_report",
        "lut_used",
        "lut_available",
        "ff_used",
        "ff_available",
        "bram_used",
        "bram_available",
        "dsp_used",
        "dsp_available",
    ]:
        lines.append(f"| {key} | {summary.get(key, '')} |")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize Vitis HW link timing/utilization")
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

    args.out_dir.mkdir(parents=True, exist_ok=True)

    summary: Dict[str, str] = {}

    link_summary = _find_latest(args.build_dir, "*.link_summary")
    if link_summary:
        summary.update(parse_link_state(link_summary))

    timing_report = _find_one(args.build_dir, "_tmp_link/reports/link/imp/*timing*summary*.rpt")
    if timing_report is None:
        timing_report = _find_one(args.build_dir, "_tmp_link/link/vivado/vpl/prj/prj.runs/impl_1/*timing*summary*.rpt")
    if timing_report is None:
        timing_report = _find_one(args.build_dir, "_tmp_link/link/vivado/vpl/prj/prj.runs/impl_1/*timing*.rpt")
    if timing_report:
        summary.update(parse_timing(timing_report))

    util_report = _find_one(args.build_dir, "_tmp_link/reports/link/imp/*full_util*placed.rpt")
    if util_report is None:
        util_report = _find_one(args.build_dir, "_tmp_link/link/vivado/vpl/prj/prj.runs/impl_1/*full_util*placed.rpt")
    if util_report is None:
        util_report = _find_one(args.build_dir, "_tmp_link/link/vivado/vpl/prj/prj.runs/impl_1/*full_util*synthed.rpt")
    if util_report:
        summary.update(parse_util(util_report))

    json_path = args.out_dir / "hw_link_summary.json"
    md_path = args.out_dir / "hw_link_summary.md"
    json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    write_md(summary, md_path)
    print(f"Wrote: {json_path}")
    print(f"Wrote: {md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
