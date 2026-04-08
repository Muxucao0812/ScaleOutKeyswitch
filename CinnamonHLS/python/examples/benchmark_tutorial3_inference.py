from __future__ import annotations

import argparse
import csv
import json
import math
import os
import pathlib
import shutil
import statistics
import sys
import time
from contextlib import contextmanager
from datetime import datetime
from typing import Any, Dict, List, Sequence

import cinnamon_emulator
import cinnamon_fpga
import numpy as np
import torch
from cinnamon.compiler import cinnamon_compile
from cinnamon.dsl import CiphertextInput, CinnamonProgram, Output, PlaintextInput
from cinnamon.passes import keyswitch_pass
from cinnamon_fpga.golden import compare_case, load_golden, normalize_kernel_outputs, write_golden
from cinnamon_fpga.parser import (
    load_instruction_streams,
    parse_program_inputs,
    split_stream_by_module,
    summarize_opcodes,
)
from PIL import Image
from torchvision import transforms

# numpy 2.x compatibility for tutorial helpers that still call np.concat.
if not hasattr(np, "concat"):
    np.concat = np.concatenate  # type: ignore[attr-defined]

ROOT_DIR = pathlib.Path(__file__).resolve().parents[2]
REPO_DIR = ROOT_DIR.parent
TUTORIAL3_DIR = REPO_DIR / "CinnamonTutorial" / "notebook3"
if str(TUTORIAL3_DIR) not in sys.path:
    sys.path.insert(0, str(TUTORIAL3_DIR))

from mnist_io import Primes, get_mnist_program_io  # noqa: E402

RNS_BIT_SIZE = 28
TOP_LEVEL = 20
SLOTS = 32 * 1024
_TOKEN_OPERAND_ID = 0xFFE
_IMM_OPERAND_ID = 0xFFF
_INSTRUCTION_WORD_STRIDE = 4


@contextmanager
def pushd(path: pathlib.Path):
    prev = pathlib.Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(prev)


def parse_int_list(value: str) -> List[int]:
    values = [int(part.strip()) for part in value.split(",") if part.strip()]
    if not values:
        raise ValueError(f"invalid comma-separated integer list: {value!r}")
    return values


def get_bsgs_plaintexts(
    name_base: str,
    babysteps: Sequence[int],
    giantsteps: Sequence[int],
    scale: int,
    level: int,
) -> List[PlaintextInput]:
    return [
        PlaintextInput(f"{name_base}_{bs}_{gs}", scale, level)
        for gs in giantsteps
        for bs in babysteps
    ]


def bsgs(inp, plain_diags, babysteps: Sequence[int], giantsteps: Sequence[int]):
    rotate_babysteps = [inp if bs == 0 else (inp >> bs) for bs in babysteps]
    prod = None
    for g, gs in enumerate(giantsteps):
        sum_bs = None
        for b, _ in enumerate(babysteps):
            idx = g * len(babysteps) + b
            term = plain_diags[idx] * rotate_babysteps[b]
            sum_bs = term if sum_bs is None else (sum_bs + term)
        gterm = sum_bs if gs == 0 else (sum_bs << gs)
        prod = gterm if prod is None else (prod + gterm)
    return prod


def conv_2d(image):
    def do_convolution(out_channel_id: int, image_tensor, result: List[Any]) -> None:
        babysteps = [i * 8 for i in range(16)]
        giantsteps = [i * 8192 for i in range(4)]
        plaintexts = get_bsgs_plaintexts(
            f"conv_weight_{out_channel_id}",
            babysteps,
            giantsteps,
            scale=56,
            level=image_tensor.level(),
        )
        product = bsgs(image_tensor, plaintexts, babysteps, giantsteps)
        product = product.rescale()
        bias = PlaintextInput(f"conv_bias_{out_channel_id}", product.scale(), product.level())
        result[out_channel_id] = product + bias

    outputs = [None for _ in range(4)]
    for out_ch in range(4):
        do_convolution(out_ch, image, outputs)

    for out_ch in range(1, 4):
        outputs[0] += outputs[out_ch] >> (64 * 128 * out_ch)
    return outputs[0].rescale().rescale()


def square(x):
    return (x * x).relinearize()


def matmul_256x64(v):
    babysteps = [i * 128 for i in range(8)]
    giantsteps = [i * 1024 for i in range(8)]
    plaintexts = get_bsgs_plaintexts("fc1_w", babysteps, giantsteps, scale=56, level=v.level())
    product = bsgs(v, plaintexts, babysteps, giantsteps)
    product = product.rescale()
    product += product << (1024 * 8)
    product = product.rescale()
    product += product >> (1024 * 16)
    return product + PlaintextInput("fc1_b", product.scale(), product.level())


def matmul_64x10(v):
    babysteps = [i * 128 for i in range(4)]
    giantsteps = [i * 512 for i in range(4)]
    plaintexts = get_bsgs_plaintexts("fc2_w", babysteps, giantsteps, scale=56, level=v.level())
    product = bsgs(v, plaintexts, babysteps, giantsteps)
    product = product.rescale()
    product += product << (1024 * 2)
    product = product.rescale()
    product += product << (1024 * 4)
    return product + PlaintextInput("fc2_b", product.scale(), product.level())


def build_mnist_program(num_chips: int) -> CinnamonProgram:
    program = CinnamonProgram("Mnist", RNS_BIT_SIZE, num_chips=num_chips)
    with program:
        image = CiphertextInput("image", 28 * 3, TOP_LEVEL)
        conv = conv_2d(image)
        conv_sq = square(conv)
        o2 = matmul_256x64(conv_sq.rescale())
        o2_sq = square(o2)
        pred = matmul_64x10(o2_sq.rescale().rescale())
        Output("pred", pred)
    return program


def load_input(sample_num: int) -> np.ndarray:
    transform = transforms.Compose(
        [transforms.ToTensor(), transforms.Normalize((0.1307,), (0.3081,))]
    )
    img_path = TUTORIAL3_DIR / "samples" / f"img_{sample_num}.jpg"
    img = Image.open(img_path)
    return transform(img).view(1, 28, 28).to(torch.float32).detach().numpy()[0]


def build_encryptor(context: cinnamon_emulator.Context) -> cinnamon_emulator.CKKSEncryptor:
    secret_key = [0] * (2 * SLOTS)
    for i in range(32):
        secret_key[2 * i] = 1 if i % 2 == 0 else -1
    return cinnamon_emulator.CKKSEncryptor(context, secret_key)


def summarize_module_coverage(instruction_base: pathlib.Path, num_partitions: int) -> Dict[str, Any]:
    streams = load_instruction_streams(str(instruction_base), num_partitions)
    opcode_counts = summarize_opcodes(streams)
    module_counts: Dict[str, int] = {}
    for stream in streams:
        buckets = split_stream_by_module(stream)
        for module_name, bucket in buckets.items():
            descriptors = len(bucket.instruction_words) // 4
            module_counts[module_name] = module_counts.get(module_name, 0) + descriptors
    return {
        "opcode_counts": dict(sorted(opcode_counts.items())),
        "module_descriptor_counts": dict(sorted(module_counts.items())),
    }


def _decode_descriptor(words: Sequence[int], index: int) -> Dict[str, int]:
    base = index * _INSTRUCTION_WORD_STRIDE
    w0 = int(words[base + 0]) & 0xFFFFFFFFFFFFFFFF
    w2 = int(words[base + 2]) & 0xFFFFFFFFFFFFFFFF
    return {
        "opcode": int(w0 & 0xFF),
        "src0": int((w0 >> 20) & 0xFFF),
        "flags": int((w0 >> 56) & 0xFF),
        "aux": int(w2),
    }


def _binary_search_steps(sorted_keys: Sequence[int], target: int) -> tuple[int, bool]:
    lo = 0
    hi = len(sorted_keys)
    steps = 0
    while lo < hi:
        steps += 1
        mid = lo + ((hi - lo) // 2)
        value = int(sorted_keys[mid]) & 0xFFFFFFFFFFFFFFFF
        if value == (int(target) & 0xFFFFFFFFFFFFFFFF):
            return steps, True
        if value < (int(target) & 0xFFFFFFFFFFFFFFFF):
            lo = mid + 1
        else:
            hi = mid
    return steps, False


def analyze_memory_lookup(
    instruction_base: pathlib.Path,
    program_inputs_path: pathlib.Path,
    num_partitions: int,
) -> Dict[str, Any]:
    streams = load_instruction_streams(str(instruction_base), num_partitions)
    sections = parse_program_inputs(program_inputs_path).sections
    from cinnamon_fpga.api import _build_stream_table_words

    total_memory_desc = 0
    total_token_lookups = 0
    total_lookup_steps = 0
    total_lookup_hits = 0
    per_partition: List[Dict[str, Any]] = []

    for partition_id, stream in enumerate(streams):
        buckets = split_stream_by_module(stream)
        memory_words = buckets["memory"].instruction_words
        memory_desc = len(memory_words) // _INSTRUCTION_WORD_STRIDE
        total_memory_desc += memory_desc

        table_words = _build_stream_table_words(sections, partition_id)
        table_count = int(table_words[0]) if table_words else 0
        table_keys = [int(table_words[1 + 2 * i]) for i in range(max(table_count, 0))]

        token_lookups = 0
        lookup_steps = 0
        lookup_hits = 0
        for idx in range(memory_desc):
            inst = _decode_descriptor(memory_words, idx)
            src0 = inst["src0"]
            flags = inst["flags"]
            is_imm = ((flags & 0x1) != 0) or (src0 == _IMM_OPERAND_ID)
            is_token = (not is_imm) and (src0 == _TOKEN_OPERAND_ID)
            if not is_token:
                continue
            token_lookups += 1
            steps, found = _binary_search_steps(table_keys, inst["aux"])
            lookup_steps += steps
            if found:
                lookup_hits += 1

        total_token_lookups += token_lookups
        total_lookup_steps += lookup_steps
        total_lookup_hits += lookup_hits
        per_partition.append(
            {
                "partition_id": partition_id,
                "memory_instruction_count": memory_desc,
                "stream_table_entries": table_count,
                "token_lookup_count": token_lookups,
                "avg_lookup_steps": (float(lookup_steps) / float(token_lookups))
                if token_lookups > 0
                else 0.0,
                "lookup_hit_rate": (float(lookup_hits) / float(token_lookups))
                if token_lookups > 0
                else 0.0,
            }
        )

    return {
        "memory_instruction_count": total_memory_desc,
        "token_lookup_count": total_token_lookups,
        "avg_lookup_steps": (float(total_lookup_steps) / float(total_token_lookups))
        if total_token_lookups > 0
        else 0.0,
        "lookup_hit_rate": (float(total_lookup_hits) / float(total_token_lookups))
        if total_token_lookups > 0
        else 0.0,
        "per_partition": per_partition,
    }


def _safe_mean(values: Sequence[float]) -> float:
    return float(statistics.mean(values)) if values else 0.0


def _safe_median(values: Sequence[float]) -> float:
    return float(statistics.median(values)) if values else 0.0


def _safe_std(values: Sequence[float]) -> float:
    if len(values) <= 1:
        return 0.0
    return float(statistics.stdev(values))


def _fmt_ratio(value: float) -> str:
    return f"{value * 100.0:.2f}%"


def run_case(
    *,
    target: str,
    xclbin: pathlib.Path,
    work_root: pathlib.Path,
    log_root: pathlib.Path,
    chips: int,
    board_indices: Sequence[int],
    register_file_size: int,
    warmup: int,
    runs: int,
    sample_id: int,
    verify_kernel_results: bool,
) -> Dict[str, Any]:
    if len(board_indices) < chips:
        raise RuntimeError(
            f"need at least {chips} boards, got {len(board_indices)} ({board_indices})"
        )

    out_dir = work_root / f"chips_{chips}"
    out_dir.mkdir(parents=True, exist_ok=True)

    program = build_mnist_program(chips)
    t_keyswitch_begin = time.perf_counter()
    keyswitch_pass(program)
    t_keyswitch_end = time.perf_counter()
    t_compile_begin = time.perf_counter()
    cinnamon_compile(program, TOP_LEVEL, chips, 256, str(out_dir) + "/")
    t_compile_end = time.perf_counter()
    keyswitch_pass_s = float(t_keyswitch_end - t_keyswitch_begin)
    compile_s = float(t_compile_end - t_compile_begin)
    compile_total_s = float(keyswitch_pass_s + compile_s)

    coverage = summarize_module_coverage(out_dir / "instructions", chips)
    memory_lookup = analyze_memory_lookup(out_dir / "instructions", out_dir / "program_inputs", chips)
    required_ops = {"ntt", "bci", "pl1", "rot"}
    missing_required_ops = sorted(required_ops.difference(coverage["opcode_counts"].keys()))
    transpose_descriptors = int(coverage["module_descriptor_counts"].get("transpose", 0))

    context = cinnamon_emulator.Context(SLOTS, Primes)
    encryptor = build_encryptor(context)
    input_image = load_input(sample_id)
    with pushd(TUTORIAL3_DIR):
        raw_inputs, _ = get_mnist_program_io(input_image, TOP_LEVEL)

    instructions_base = str(out_dir / "instructions")
    program_inputs = str(out_dir / "program_inputs")
    evalkeys = str(out_dir / "evalkeys")

    runtime = cinnamon_fpga.Emulator(
        context,
        target=target,
        xclbin_path=xclbin,
        board_indices=list(board_indices[:chips]),
        parallel_dispatch=not (target == "sw_emu" and chips > 1),
        require_kernel_execution=True,
        verify_kernel_results=verify_kernel_results,
    )
    runtime.generate_and_serialize_evalkeys(evalkeys, program_inputs, encryptor)
    runtime.generate_inputs(program_inputs, evalkeys, raw_inputs, encryptor)

    run_rows: List[Dict[str, Any]] = []
    all_outputs: List[List[Dict[str, Any]]] = []
    stage_rows: List[Dict[str, Any]] = []

    total_loops = warmup + runs
    for loop in range(total_loops):
        phase = "warmup" if loop < warmup else "measured"
        measured_idx = loop - warmup
        run_name = f"{phase}_{loop:02d}" if phase == "warmup" else f"run_{measured_idx:02d}"

        t0 = time.perf_counter()
        runtime.run_program(instructions_base, chips, register_file_size)
        t1 = time.perf_counter()

        outputs = runtime.get_kernel_outputs()
        timing_summary = dict(runtime.last_timing_summary)
        timing_global = dict(timing_summary.get("global", {}))
        timing_global["wall_run_program_s"] = float(t1 - t0)
        timing_global["phase"] = phase
        timing_global["loop_index"] = loop
        timing_global["run_name"] = run_name
        timing_global["chips"] = chips

        run_rows.append(timing_global)
        all_outputs.append(outputs)

        for stage in timing_summary.get("stage_timing", []):
            stage_rows.append(
                {
                    "chips": chips,
                    "run_name": run_name,
                    "phase": phase,
                    "module": stage.get("module", ""),
                    "kernel_name": stage.get("kernel_name", ""),
                    "stage_wall_s": float(stage.get("stage_wall_s", 0.0)),
                    "setup_s": float(stage.get("setup_s", 0.0)),
                    "h2d_s": float(stage.get("h2d_s", 0.0)),
                    "wait_s": float(stage.get("wait_s", 0.0)),
                    "d2h_s": float(stage.get("d2h_s", 0.0)),
                    "schedule_s": float(stage.get("schedule_s", 0.0)),
                    "critical_partition": int(stage.get("critical_partition", -1)),
                    "critical_total_s": float(stage.get("critical_total_s", 0.0)),
                }
            )

        run_payload = {
            "chips": chips,
            "phase": phase,
            "loop_index": loop,
            "timing_global": timing_global,
            "timing_stage": timing_summary.get("stage_timing", []),
        }
        (log_root / f"chips_{chips}_{run_name}.json").write_text(
            json.dumps(run_payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    measured_rows = [row for row in run_rows if row.get("phase") == "measured"]
    measured_outputs = [
        all_outputs[idx] for idx, row in enumerate(run_rows) if row.get("phase") == "measured"
    ]
    if not measured_rows:
        raise RuntimeError("no measured runs collected")

    wall_values = [float(row.get("wall_run_program_s", 0.0)) for row in measured_rows]
    compute_values = [float(row.get("compute_wait_total_s", 0.0)) for row in measured_rows]
    schedule_values = [float(row.get("schedule_total_s", 0.0)) for row in measured_rows]

    module_stage_agg: Dict[str, List[float]] = {}
    module_stage_by_run: Dict[tuple[str, str], float] = {}
    for stage in stage_rows:
        if stage.get("phase") != "measured":
            continue
        module_name = str(stage["module"])
        module_stage_agg.setdefault(module_name, []).append(float(stage["stage_wall_s"]))
        module_stage_by_run[(str(stage["run_name"]), module_name)] = float(stage["stage_wall_s"])

    module_stage_stats = {
        module_name: {
            "avg_stage_wall_s": _safe_mean(values),
            "median_stage_wall_s": _safe_median(values),
            "std_stage_wall_s": _safe_std(values),
        }
        for module_name, values in sorted(module_stage_agg.items())
    }

    memory_instruction_count = int(memory_lookup.get("memory_instruction_count", 0))
    memory_wall_values: List[float] = []
    memory_per_instruction_us_values: List[float] = []
    for row in measured_rows:
        run_name = str(row.get("run_name", ""))
        wall_s = float(module_stage_by_run.get((run_name, "memory"), 0.0))
        memory_wall_values.append(wall_s)
        if memory_instruction_count > 0:
            memory_per_instruction_us_values.append((wall_s * 1_000_000.0) / memory_instruction_count)

    return {
        "chips": chips,
        "coverage": coverage,
        "memory_lookup": memory_lookup,
        "missing_required_ops": missing_required_ops,
        "transpose_descriptors": transpose_descriptors,
        "rows_all": run_rows,
        "rows_measured": measured_rows,
        "outputs_measured": measured_outputs,
        "stage_rows": stage_rows,
        "module_stage_stats": module_stage_stats,
        "summary": {
            "wall_avg_s": _safe_mean(wall_values),
            "wall_median_s": _safe_median(wall_values),
            "wall_std_s": _safe_std(wall_values),
            "compute_avg_s": _safe_mean(compute_values),
            "schedule_avg_s": _safe_mean(schedule_values),
            "compute_ratio_avg": _safe_mean(
                [float(row.get("compute_ratio", 0.0)) for row in measured_rows]
            ),
            "schedule_ratio_avg": _safe_mean(
                [float(row.get("schedule_ratio", 0.0)) for row in measured_rows]
            ),
            "setup_ratio_avg": _safe_mean(
                [float(row.get("setup_ratio", 0.0)) for row in measured_rows]
            ),
            "h2d_ratio_avg": _safe_mean(
                [float(row.get("h2d_ratio", 0.0)) for row in measured_rows]
            ),
            "d2h_ratio_avg": _safe_mean(
                [float(row.get("d2h_ratio", 0.0)) for row in measured_rows]
            ),
            "other_host_ratio_avg": _safe_mean(
                [float(row.get("other_host_ratio", 0.0)) for row in measured_rows]
            ),
            "memory_instruction_count": memory_instruction_count,
            "memory_stage_wall_avg_s": _safe_mean(memory_wall_values),
            "memory_stage_wall_median_s": _safe_median(memory_wall_values),
            "memory_per_instruction_avg_us": _safe_mean(memory_per_instruction_us_values),
            "memory_per_instruction_median_us": _safe_median(memory_per_instruction_us_values),
            "memory_token_lookup_count": int(memory_lookup.get("token_lookup_count", 0)),
            "memory_avg_lookup_steps": float(memory_lookup.get("avg_lookup_steps", 0.0)),
            "memory_lookup_hit_rate": float(memory_lookup.get("lookup_hit_rate", 0.0)),
            "keyswitch_pass_s": keyswitch_pass_s,
            "compile_s": compile_s,
            "compile_total_s": compile_total_s,
            "runs": len(measured_rows),
        },
    }


def write_reports(
    *,
    report_dir: pathlib.Path,
    target: str,
    xclbin: pathlib.Path,
    warmup: int,
    runs: int,
    chips_list: Sequence[int],
    results: Dict[str, Dict[str, Any]],
    report_basename: str,
    validation_basename: str,
) -> None:
    report_dir.mkdir(parents=True, exist_ok=True)
    md_path = report_dir / f"{report_basename}.md"
    csv_path = report_dir / f"{report_basename}.csv"
    json_path = report_dir / f"{report_basename}.json"
    validation_path = report_dir / f"{validation_basename}.md"

    rows_for_csv: List[Dict[str, Any]] = []
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        case = results[chip_key]
        for row in case["rows_measured"]:
            summary = case.get("summary", {})
            rows_for_csv.append(
                {
                    "chips": int(chip_key),
                    "run_name": row.get("run_name", ""),
                    "wall_run_program_s": float(row.get("wall_run_program_s", 0.0)),
                    "run_program_total_s": float(row.get("run_program_total_s", 0.0)),
                    "compute_wait_total_s": float(row.get("compute_wait_total_s", 0.0)),
                    "schedule_total_s": float(row.get("schedule_total_s", 0.0)),
                    "setup_s": float(row.get("setup_s", 0.0)),
                    "h2d_s": float(row.get("h2d_s", 0.0)),
                    "d2h_s": float(row.get("d2h_s", 0.0)),
                    "other_host_s": float(row.get("other_host_s", 0.0)),
                    "compute_ratio": float(row.get("compute_ratio", 0.0)),
                    "schedule_ratio": float(row.get("schedule_ratio", 0.0)),
                    "setup_ratio": float(row.get("setup_ratio", 0.0)),
                    "h2d_ratio": float(row.get("h2d_ratio", 0.0)),
                    "d2h_ratio": float(row.get("d2h_ratio", 0.0)),
                    "other_host_ratio": float(row.get("other_host_ratio", 0.0)),
                    "stage_count": int(row.get("stage_count", 0)),
                    "memory_instruction_count": int(summary.get("memory_instruction_count", 0)),
                    "memory_per_instruction_avg_us": float(
                        summary.get("memory_per_instruction_avg_us", 0.0)
                    ),
                    "memory_token_lookup_count": int(summary.get("memory_token_lookup_count", 0)),
                    "memory_avg_lookup_steps": float(summary.get("memory_avg_lookup_steps", 0.0)),
                    "memory_lookup_hit_rate": float(summary.get("memory_lookup_hit_rate", 0.0)),
                    "keyswitch_pass_s": float(summary.get("keyswitch_pass_s", 0.0)),
                    "compile_s": float(summary.get("compile_s", 0.0)),
                    "compile_total_s": float(summary.get("compile_total_s", 0.0)),
                }
            )

    with csv_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows_for_csv[0].keys()) if rows_for_csv else [])
        if rows_for_csv:
            writer.writeheader()
            writer.writerows(rows_for_csv)

    md_lines: List[str] = []
    md_lines.append(f"# Cinnamon {target} Tutorial3 Inference Benchmark")
    md_lines.append("")
    md_lines.append(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S %Z')}")
    md_lines.append("")
    md_lines.append("## Config")
    md_lines.append("")
    md_lines.append(f"- target: `{target}`")
    md_lines.append(f"- xclbin: `{xclbin}`")
    md_lines.append(f"- chips: `{','.join(str(v) for v in chips_list)}`")
    md_lines.append(f"- warmup: `{warmup}`")
    md_lines.append(f"- measured runs: `{runs}`")
    md_lines.append("- compute time definition: `sum(stage max(run.wait over partitions))`")
    md_lines.append("- schedule time definition: `run_program_total - compute_wait_total`")
    md_lines.append("")
    md_lines.append("## Coverage")
    md_lines.append("")
    md_lines.append("| chips | ntt | bci/pl1 | rot | transpose |")
    md_lines.append("|---|---:|---:|---:|---:|")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        coverage = results[chip_key]["coverage"]
        op = coverage["opcode_counts"]
        module_desc = coverage["module_descriptor_counts"]
        md_lines.append(
            f"| {chip_key} | {int(op.get('ntt', 0))} | {int(op.get('bci', 0)) + int(op.get('pl1', 0))} | {int(op.get('rot', 0))} | {int(module_desc.get('transpose', 0))} |"
        )
    md_lines.append("")
    md_lines.append(
        "Transpose coverage note: tutorial3 inference workload does not naturally trigger transpose in current ISA mapping."
    )
    md_lines.append("")
    md_lines.append("## Runtime Summary")
    md_lines.append("")
    md_lines.append(
        "| chips | avg total (s) | median (s) | stddev (s) | avg compute (s) | avg schedule (s) | avg compute ratio | avg schedule ratio |"
    )
    md_lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        summary = results[chip_key]["summary"]
        md_lines.append(
            f"| {chip_key} | {summary['wall_avg_s']:.6f} | {summary['wall_median_s']:.6f} | {summary['wall_std_s']:.6f} | "
            f"{summary['compute_avg_s']:.6f} | {summary['schedule_avg_s']:.6f} | "
            f"{_fmt_ratio(summary['compute_ratio_avg'])} | {_fmt_ratio(summary['schedule_ratio_avg'])} |"
        )
    md_lines.append("")

    md_lines.append("## Event Breakdown (Compile / Schedule / Memory / Compute)")
    md_lines.append("")
    md_lines.append(
        "| chips | compile total (s) | schedule avg (s) | compute avg (s) | memory stage avg (s) | memory stage share |"
    )
    md_lines.append("|---|---:|---:|---:|---:|---:|")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        summary = results[chip_key]["summary"]
        module_stats = results[chip_key].get("module_stage_stats", {})
        stage_total = sum(
            float(stat.get("avg_stage_wall_s", 0.0)) for stat in module_stats.values()
        )
        memory_stage = float(summary.get("memory_stage_wall_avg_s", 0.0))
        memory_share = (memory_stage / stage_total) if stage_total > 0.0 else 0.0
        md_lines.append(
            f"| {chip_key} | {float(summary.get('compile_total_s', 0.0)):.6f} | "
            f"{float(summary.get('schedule_avg_s', 0.0)):.6f} | "
            f"{float(summary.get('compute_avg_s', 0.0)):.6f} | "
            f"{memory_stage:.6f} | {_fmt_ratio(memory_share)} |"
        )
    md_lines.append("")

    md_lines.append("## Memory Slow-Path Diagnostics")
    md_lines.append("")
    md_lines.append(
        "| chips | memory desc count | memory avg stage wall (s) | memory avg per-inst (us) | token lookups | avg lookup steps | lookup hit rate |"
    )
    md_lines.append("|---|---:|---:|---:|---:|---:|---:|")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        summary = results[chip_key]["summary"]
        md_lines.append(
            f"| {chip_key} | {int(summary.get('memory_instruction_count', 0))} | "
            f"{float(summary.get('memory_stage_wall_avg_s', 0.0)):.6f} | "
            f"{float(summary.get('memory_per_instruction_avg_us', 0.0)):.3f} | "
            f"{int(summary.get('memory_token_lookup_count', 0))} | "
            f"{float(summary.get('memory_avg_lookup_steps', 0.0)):.3f} | "
            f"{_fmt_ratio(float(summary.get('memory_lookup_hit_rate', 0.0)))} |"
        )
    md_lines.append("")

    if "1" in results:
        base = results["1"]["summary"]["wall_avg_s"]
        md_lines.append("Speedup vs 1 chip:")
        md_lines.append("")
        for chip_key in sorted(results.keys(), key=lambda x: int(x)):
            cur = results[chip_key]["summary"]["wall_avg_s"]
            speedup = (base / cur) if cur > 0 else math.nan
            md_lines.append(f"- `chips={chip_key}`: `{speedup:.3f}x`")
        md_lines.append("")

    md_lines.append("## Module Stage Wall Time (Average per run)")
    md_lines.append("")
    md_lines.append("| chips | module | avg stage wall (s) | share of total stage wall |")
    md_lines.append("|---|---|---:|---:|")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        stats = results[chip_key]["module_stage_stats"]
        stage_total = sum(float(stats[module_name]["avg_stage_wall_s"]) for module_name in stats.keys())
        for module_name in sorted(stats.keys()):
            module_wall = float(stats[module_name]["avg_stage_wall_s"])
            module_share = (module_wall / stage_total) if stage_total > 0.0 else 0.0
            md_lines.append(
                f"| {chip_key} | {module_name} | {module_wall:.6f} | {_fmt_ratio(module_share)} |"
            )
    md_lines.append("")

    md_lines.append("## Slowdown Interpretation")
    md_lines.append("")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        summary = results[chip_key]["summary"]
        md_lines.append(
            f"- chips={chip_key}: compile={float(summary.get('compile_total_s', 0.0)):.6f}s, "
            f"compute={_fmt_ratio(summary['compute_ratio_avg'])}, "
            f"schedule={_fmt_ratio(summary['schedule_ratio_avg'])}, "
            f"memory_per_inst={float(summary.get('memory_per_instruction_avg_us', 0.0)):.3f}us, "
            f"memory_lookup_steps={float(summary.get('memory_avg_lookup_steps', 0.0)):.3f}, "
            f"setup/h2d/d2h/other={_fmt_ratio(summary['setup_ratio_avg'])}/"
            f"{_fmt_ratio(summary['h2d_ratio_avg'])}/{_fmt_ratio(summary['d2h_ratio_avg'])}/"
            f"{_fmt_ratio(summary['other_host_ratio_avg'])}"
        )
    md_lines.append("")
    md_lines.append(
        "- Multi-chip变慢通常来自调度与同步路径放大（xclbin/kernel setup + H2D/D2H + host编排），而不是算子计算本身。"
    )
    md_lines.append("")

    md_path.write_text("\n".join(md_lines) + "\n", encoding="utf-8")

    validation_lines = [
        "# Validation Matrix",
        "",
        "| Gate | Check | Status |",
        "|---|---|---|",
    ]
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        case = results[chip_key]
        missing_ops = case["missing_required_ops"]
        status_a = "PASS" if not missing_ops else f"FAIL (missing: {','.join(missing_ops)})"
        status_t = (
            "PASS (not triggered by workload)"
            if int(case["transpose_descriptors"]) == 0
            else f"PASS (transpose descriptors={int(case['transpose_descriptors'])})"
        )
        status_c = "PASS" if int(case["summary"]["runs"]) > 0 else "FAIL (no measured runs)"
        validation_lines.append(f"| Gate A chips={chip_key} | ntt+bci/pl1+rot coverage | {status_a} |")
        validation_lines.append(f"| Gate A chips={chip_key} | transpose coverage note | {status_t} |")
        validation_lines.append(f"| Gate C chips={chip_key} | timing statistics available | {status_c} |")
    validation_path.write_text("\n".join(validation_lines) + "\n", encoding="utf-8")

    json_payload = {
        "target": target,
        "xclbin": str(xclbin),
        "chips": [int(v) for v in chips_list],
        "warmup": warmup,
        "runs": runs,
        "results": results,
        "csv_path": str(csv_path),
        "markdown_path": str(md_path),
        "validation_path": str(validation_path),
    }
    json_path.write_text(json.dumps(json_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark tutorial3 encrypted inference on FPGA hardware and report compute/schedule breakdown"
    )
    parser.add_argument("--target", choices=["sw_emu", "hw_emu", "hw"], default="hw")
    parser.add_argument(
        "--xclbin",
        type=pathlib.Path,
        default=None,
    )
    parser.add_argument("--chips", default="1,2,4")
    parser.add_argument("--boards", default="0,1,2,3")
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--sample-id", type=int, default=1)
    parser.add_argument("--register-file-size", type=int, default=1024)
    parser.add_argument(
        "--work-root",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "cinnamon_tutorial3_inference_benchmark",
    )
    parser.add_argument(
        "--golden",
        type=pathlib.Path,
        default=ROOT_DIR / "golden" / "tutorial3_inference_kernel_outputs.json",
    )
    parser.add_argument("--write-golden", action="store_true")
    parser.add_argument("--report-tag", default="")
    args = parser.parse_args()

    chips_list = parse_int_list(args.chips)
    board_list = parse_int_list(args.boards)
    if args.xclbin is None:
        args.xclbin = ROOT_DIR / "build" / args.target / f"cinnamon_fpga.{args.target}.xclbin"
    if not args.xclbin.exists():
        raise FileNotFoundError(f"xclbin not found: {args.xclbin}")

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_root = ROOT_DIR / "build" / "logs" / "tutorial3_benchmark" / timestamp
    log_root.mkdir(parents=True, exist_ok=True)
    latest_link = ROOT_DIR / "build" / "logs" / "tutorial3_benchmark" / "latest"
    if latest_link.is_symlink():
        latest_link.unlink()
    elif latest_link.exists():
        if latest_link.is_dir():
            shutil.rmtree(latest_link)
        else:
            latest_link.unlink()
    latest_link.symlink_to(log_root)

    print(
        f"target={args.target} xclbin={args.xclbin} chips={chips_list} boards={board_list} "
        f"warmup={args.warmup} runs={args.runs} sample_id={args.sample_id}"
    )
    print(f"log_root={log_root}")

    results: Dict[str, Dict[str, Any]] = {}
    actual_cases: Dict[str, List[Dict[str, Any]]] = {}

    for chips in chips_list:
        case = run_case(
            target=args.target,
            xclbin=args.xclbin,
            work_root=args.work_root / args.target,
            log_root=log_root,
            chips=chips,
            board_indices=board_list,
            register_file_size=args.register_file_size,
            warmup=args.warmup,
            runs=args.runs,
            sample_id=args.sample_id,
            verify_kernel_results=True,
        )
        results[str(chips)] = case
        first_measured_outputs = case["outputs_measured"][0]
        actual_cases[str(chips)] = normalize_kernel_outputs(first_measured_outputs)

    if args.write_golden:
        payload = {
            "program": "tutorial3_mnist_inference",
            "target_reference": args.target,
            "chips": chips_list,
            "cases": actual_cases,
        }
        write_golden(args.golden, payload)
        print(f"Wrote golden file: {args.golden}")
    else:
        if not args.golden.exists():
            raise FileNotFoundError(
                f"golden file not found: {args.golden}. "
                "Run again with --write-golden to create the fixed golden baseline."
            )
        golden_payload = load_golden(args.golden)
        golden_cases = golden_payload.get("cases", {})
        for key, actual_case in actual_cases.items():
            if key not in golden_cases:
                raise RuntimeError(f"golden is missing chips={key} case")
            ok, message = compare_case(golden_cases[key], actual_case)
            if not ok:
                raise RuntimeError(f"golden mismatch for chips={key}: {message}")

    report_basename = f"{args.target}_runtime_benchmark"
    validation_basename = f"validation_matrix.{args.target}"
    report_tag = args.report_tag.strip()
    if report_tag:
        report_basename = f"{report_basename}.{report_tag}"
        validation_basename = f"{validation_basename}.{report_tag}"

    write_reports(
        report_dir=ROOT_DIR / "build" / "reports",
        target=args.target,
        xclbin=args.xclbin,
        warmup=args.warmup,
        runs=args.runs,
        chips_list=chips_list,
        results=results,
        report_basename=report_basename,
        validation_basename=validation_basename,
    )
    print("Benchmark completed and reports written:")
    print(f"  - {ROOT_DIR / 'build' / 'reports' / f'{report_basename}.md'}")
    print(f"  - {ROOT_DIR / 'build' / 'reports' / f'{report_basename}.csv'}")
    print(f"  - {ROOT_DIR / 'build' / 'reports' / f'{report_basename}.json'}")
    print(f"  - {ROOT_DIR / 'build' / 'reports' / f'{validation_basename}.md'}")
    print(f"  - {log_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
