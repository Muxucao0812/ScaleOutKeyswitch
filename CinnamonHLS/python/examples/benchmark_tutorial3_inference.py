from __future__ import annotations

import argparse
import csv
import json
import math
import os
import pathlib
import random
import shutil
import statistics
import sys
import time
from contextlib import contextmanager
from datetime import datetime
from typing import Any, Dict, List, Optional, Sequence, Tuple

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
from cinnamon_fpga.tutorial3_decode import (
    DEFAULT_PRED_DECODE_MODE as _TUTORIAL3_DEFAULT_PRED_DECODE_MODE,
    SUPPORTED_PRED_DECODE_MODES as _TUTORIAL3_SUPPORTED_PRED_DECODE_MODES,
    decode_mode_votes as _decode_mode_votes,
    decode_scores_with_mode as _decode_scores_with_mode,
    select_reference_prediction as _select_reference_prediction,
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
_JSONL_ENV = "CINNAMON_FPGA_JSONL_LOG"
_JSONL_DEBUG_ENV = "CINNAMON_FPGA_JSONL_DEBUG_LOG"
_JSONL_DEBUG_FLAG_ENV = "CINNAMON_FPGA_JSONL_DEBUG"
_DEBUG_PREVIEW_WORDS = 8
_SUPPORTED_PRED_DECODE_MODES = _TUTORIAL3_SUPPORTED_PRED_DECODE_MODES
_DEFAULT_PRED_DECODE_MODE = _TUTORIAL3_DEFAULT_PRED_DECODE_MODE


class MNIST_CNN(torch.nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.conv2d = torch.nn.Conv2d(
            in_channels=1, out_channels=4, kernel_size=(7, 7), stride=(3, 3), bias=True
        )
        self.fc1 = torch.nn.Linear(in_features=256, out_features=64, bias=True)
        self.fc2 = torch.nn.Linear(in_features=64, out_features=10, bias=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        conv = self.conv2d(x)
        conv_sq = conv * conv
        conv_sq = conv_sq.reshape(1, -1)
        o2 = self.fc1(conv_sq)
        o2_sq = o2 * o2
        o3 = self.fc2(o2_sq)
        return o3


_PLAIN_MODEL_CACHE: Optional[MNIST_CNN] = None
_PLAIN_MODEL_PATH: Optional[pathlib.Path] = None


def _env_flag(name: str) -> bool:
    value = os.getenv(name, "").strip().lower()
    return value in {"1", "true", "yes", "on"}


def _append_jsonl(path: str, payload: Dict[str, Any]) -> None:
    target = pathlib.Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    line = json.dumps(payload, sort_keys=True, default=str) + "\n"
    fd = os.open(str(target), os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
    try:
        os.write(fd, line.encode("utf-8"))
    finally:
        os.close(fd)


def _emit_jsonl(
    event: str,
    *,
    payload: Dict[str, Any],
    debug_payload: Optional[Dict[str, Any]] = None,
) -> None:
    always_path = os.getenv(_JSONL_ENV, "").strip()
    debug_enabled = _env_flag(_JSONL_DEBUG_FLAG_ENV)
    debug_path = os.getenv(_JSONL_DEBUG_ENV, "").strip() or always_path
    if not always_path and not (debug_enabled and debug_path):
        return

    base_payload = {
        "event": event,
        "ts_unix_s": time.time(),
        **payload,
    }
    if always_path:
        _append_jsonl(always_path, base_payload)
    if debug_enabled and debug_path:
        debug_entry = dict(base_payload)
        if debug_payload:
            debug_entry.update(debug_payload)
        debug_entry["debug"] = True
        _append_jsonl(debug_path, debug_entry)


def _preview_numeric(values: Sequence[Any], limit: int = _DEBUG_PREVIEW_WORDS) -> List[float]:
    preview: List[float] = []
    for item in values[:limit]:
        if isinstance(item, complex):
            preview.append(float(item.real))
        else:
            preview.append(float(item))
    return preview


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
    tensor = transform(img).view(1, 28, 28).to(torch.float32).detach().numpy()[0]
    _emit_jsonl(
        "benchmark.load_input",
        payload={
            "sample_id": int(sample_num),
            "decode_mode": "mnist_tensor",
            "shape": [int(v) for v in tensor.shape],
        },
        debug_payload={
            "raw_input_preview": _preview_numeric(tensor.reshape(-1).tolist()),
        },
    )
    return tensor


def _load_plain_model(model_path: pathlib.Path) -> MNIST_CNN:
    if not model_path.exists():
        raise FileNotFoundError(f"plain model checkpoint not found: {model_path}")
    model = MNIST_CNN()
    state_dict = torch.load(model_path, map_location="cpu")
    model.load_state_dict(state_dict)
    model.eval()
    return model


def _get_plain_model(model_path: pathlib.Path) -> MNIST_CNN:
    global _PLAIN_MODEL_CACHE, _PLAIN_MODEL_PATH
    resolved = model_path.resolve()
    if _PLAIN_MODEL_CACHE is None or _PLAIN_MODEL_PATH is None or _PLAIN_MODEL_PATH != resolved:
        _PLAIN_MODEL_CACHE = _load_plain_model(resolved)
        _PLAIN_MODEL_PATH = resolved
    return _PLAIN_MODEL_CACHE


def _run_plain_prediction(sample_id: int, model_path: Optional[pathlib.Path] = None) -> int:
    target_model_path = model_path or (TUTORIAL3_DIR / "mnist.pth")
    model = _get_plain_model(target_model_path)
    image = load_input(sample_id)
    image_tensor = torch.from_numpy(image).to(torch.float32).view(1, 1, 28, 28)
    with torch.no_grad():
        logits = model(image_tensor)
    return int(torch.argmax(logits).item())


def build_encryptor(context: cinnamon_emulator.Context) -> cinnamon_emulator.CKKSEncryptor:
    # Match notebook3 secret-key generation for parity between benchmark and tutorial flow.
    rng = random.Random(10)
    secret_key = [0] * (2 * SLOTS)
    count = 0
    while count < SLOTS:
        pos = rng.randint(0, (2 * SLOTS) - 1)
        if secret_key[pos] != 0:
            continue
        secret_key[pos] = -1 if rng.randint(0, 1) == 0 else 1
        count += 1
    return cinnamon_emulator.CKKSEncryptor(context, secret_key, [0, 1, 2, 3, 4, 5, 6, 7])


def load_labels() -> List[int]:
    labels_path = TUTORIAL3_DIR / "samples" / "answers.txt"
    labels = [
        int(line.strip())
        for line in labels_path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if not labels:
        raise RuntimeError(f"empty labels file: {labels_path}")
    return labels


def _top1_margin(values: Sequence[float]) -> float:
    top2 = _topk_indices(values, 2)
    if len(top2) < 2:
        return 0.0
    return float(values[top2[0]] - values[top2[1]])


def _to_real_values(pred_values: Sequence[Any]) -> List[float]:
    values: List[float] = []
    for item in pred_values:
        if isinstance(item, complex):
            values.append(float(item.real))
        else:
            values.append(float(item))
    return values


def _decode_pred_scores(
    pred_values: Sequence[Any],
    *,
    sample_id: Optional[int] = None,
    decode_mode: str = _DEFAULT_PRED_DECODE_MODE,
) -> Tuple[List[float], str, Dict[str, Any]]:
    if not pred_values:
        _emit_jsonl(
            "benchmark.decode_pred_scores",
            payload={
                "sample_id": sample_id,
                "decode_mode": "empty",
                "plain_pred": None,
                "cpu_ref_pred": None,
                "top1_margin": 0.0,
                "index_map": {"stride": 0, "repeats": 0, "class_count": 0},
            },
        )
        return [], "empty", {"stride": 0, "repeats": 0, "class_count": 0}

    values = _to_real_values(pred_values)
    scores, normalized_mode, index_map, index_map_preview = _decode_scores_with_mode(
        values,
        decode_mode,
    )

    pred_label = (
        int(max(range(len(scores)), key=lambda idx: float(scores[idx]))) if scores else None
    )
    _emit_jsonl(
        "benchmark.decode_pred_scores",
        payload={
            "sample_id": sample_id,
            "decode_mode": normalized_mode,
            "plain_pred": pred_label,
            "cpu_ref_pred": pred_label,
            "top1_margin": _top1_margin(scores),
            "index_map": index_map,
        },
        debug_payload={
            "raw_pred_preview": _preview_numeric(values),
            "decoded_scores_preview": [float(v) for v in scores[:10]],
            "index_map_preview": index_map_preview,
        },
    )
    return scores, normalized_mode, index_map


def _topk_indices(values: Sequence[float], k: int) -> List[int]:
    if not values:
        return []
    return sorted(range(len(values)), key=lambda idx: float(values[idx]), reverse=True)[:k]


def run_cpu_reference(
    *,
    context: cinnamon_emulator.Context,
    encryptor: cinnamon_emulator.CKKSEncryptor,
    instructions_base: str,
    program_inputs: str,
    evalkeys: str,
    raw_inputs: Dict[str, Any],
    output_scales: Dict[str, float],
    chips: int,
    register_file_size: int,
    sample_id: int,
    pred_decode_mode: str,
) -> Dict[str, Any]:
    labels = load_labels()
    if sample_id < 1 or sample_id > len(labels):
        raise ValueError(f"sample_id={sample_id} is out of label range 1..{len(labels)}")
    label = int(labels[sample_id - 1])

    cpu = cinnamon_emulator.Emulator(context)
    cpu.generate_and_serialize_evalkeys(evalkeys, program_inputs, encryptor)
    cpu.generate_inputs(program_inputs, evalkeys, raw_inputs, encryptor)
    cpu.run_program(instructions_base, chips, register_file_size)
    decrypted = cpu.get_decrypted_outputs(encryptor, output_scales)

    pred_values = list(decrypted.get("pred", []))
    real_values = _to_real_values(pred_values)
    scores, decode_mode, index_map = _decode_pred_scores(
        pred_values,
        sample_id=sample_id,
        decode_mode=pred_decode_mode,
    )
    decoded_pred_valid = len(scores) >= 10
    decoded_pred_label = (
        int(max(range(10), key=lambda idx: scores[idx])) if decoded_pred_valid else None
    )
    top3 = _topk_indices(scores[:10], 3) if decoded_pred_valid else []
    top1_margin = _top1_margin(scores[:10]) if decoded_pred_valid else 0.0
    mode_votes = _decode_mode_votes(real_values)
    plain_model_pred = _run_plain_prediction(sample_id)
    pred_label, pred_source, decode_unstable = _select_reference_prediction(
        decoded_pred_label=decoded_pred_label,
        plain_pred_label=plain_model_pred,
        top1_margin=top1_margin,
        mode_votes=mode_votes,
    )
    pred_valid = pred_label is not None
    pred_scale = output_scales.get("pred")
    scale_value = float(pred_scale) if pred_scale is not None else None

    _emit_jsonl(
        "benchmark.run_cpu_reference",
        payload={
            "sample_id": int(sample_id),
            "label": int(label),
            "plain_pred": pred_label if pred_valid else None,
            "cpu_ref_pred": pred_label if pred_valid else None,
            "plain_model_pred": int(plain_model_pred),
            "cpu_decoded_pred": int(decoded_pred_label) if decoded_pred_label is not None else None,
            "pred_source": pred_source,
            "decode_unstable": bool(decode_unstable),
            "decode_mode": decode_mode,
            "top1_margin": float(top1_margin),
            "scale": scale_value,
            "index_map": index_map,
        },
        debug_payload={
            "raw_pred_preview": _preview_numeric(pred_values),
            "decoded_scores_preview": [float(v) for v in scores[:10]],
            "top3_labels": [int(v) for v in top3],
            "mode_votes": mode_votes,
        },
    )

    return {
        "sample_id": sample_id,
        "label": label,
        "pred_label": pred_label if pred_valid else None,
        "plain_pred": pred_label if pred_valid else None,
        "cpu_ref_pred": pred_label if pred_valid else None,
        "plain_model_pred": int(plain_model_pred),
        "cpu_decoded_pred": int(decoded_pred_label) if decoded_pred_label is not None else None,
        "pred_source": str(pred_source),
        "decode_unstable": bool(decode_unstable),
        "decode_mode_votes": mode_votes,
        "pred_valid": bool(pred_valid),
        "is_correct": bool(pred_valid and pred_label == label),
        "decode_mode": decode_mode,
        "top1_margin": float(top1_margin),
        "scale": scale_value,
        "index_map": index_map,
        "scores": [float(v) for v in scores[:10]],
        "top3_labels": [int(v) for v in top3],
    }


def classify_root_cause(cpu_reference: Dict[str, Any], kernel_golden_ok: bool) -> Dict[str, str]:
    if not bool(cpu_reference.get("pred_valid", False)):
        return {
            "category": "inconclusive",
            "reason": "CPU reference prediction is unavailable.",
        }
    if bool(cpu_reference.get("decode_unstable", False)):
        return {
            "category": "reference_chain_likely",
            "reason": "CPU decoded logits are unstable across decode modes; using plain-model fallback for label reference.",
        }
    cpu_correct = bool(cpu_reference.get("is_correct", False))
    if cpu_correct and (not kernel_golden_ok):
        return {
            "category": "hardware_likely",
            "reason": "CPU prediction matches label while FPGA kernel outputs mismatch golden.",
        }
    if not cpu_correct:
        return {
            "category": "software_likely",
            "reason": "CPU reference already disagrees with label.",
        }
    return {
        "category": "inconclusive",
        "reason": "CPU prediction and kernel golden both pass; FPGA decrypted logits are unavailable in this runtime.",
    }


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
    pred_decode_mode: str,
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
        raw_inputs, output_scales = get_mnist_program_io(input_image, TOP_LEVEL)

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
    cpu_reference = run_cpu_reference(
        context=context,
        encryptor=encryptor,
        instructions_base=instructions_base,
        program_inputs=program_inputs,
        evalkeys=evalkeys,
        raw_inputs=raw_inputs,
        output_scales=output_scales,
        chips=chips,
        register_file_size=register_file_size,
        sample_id=sample_id,
        pred_decode_mode=pred_decode_mode,
    )

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

        for partition_output in outputs:
            partition_id = int(partition_output.get("partition_id", -1))
            card_id = int(partition_output.get("board_index", -1))
            module_results = list(partition_output.get("module_results", []))
            first_header = module_results[0] if module_results else {}
            module_headers: List[Dict[str, Any]] = []
            for entry in module_results[:3]:
                module_headers.append(
                    {
                        "module": str(entry.get("module", "")),
                        "kernel_name": str(entry.get("kernel_name", "")),
                        "kernel_status": entry.get("status"),
                        "kernel_executed": entry.get("executed"),
                        "kernel_register_count": entry.get("register_count"),
                        "kernel_module_id": entry.get("module_id"),
                        "kernel_partition_id": entry.get("partition_id"),
                        "kernel_trace_acc": entry.get("trace_acc"),
                        "raw_output_preview": [
                            int(word)
                            for word in entry.get("output_words", [])[:_DEBUG_PREVIEW_WORDS]
                        ],
                    }
                )
            _emit_jsonl(
                "benchmark.run_case_module_outputs",
                payload={
                    "sample_id": int(sample_id),
                    "card_id": card_id,
                    "partition_id": partition_id,
                    "label": cpu_reference.get("label"),
                    "plain_pred": cpu_reference.get("plain_pred"),
                    "cpu_ref_pred": cpu_reference.get("cpu_ref_pred"),
                    "decode_mode": str(cpu_reference.get("decode_mode", "")),
                    "top1_margin": float(cpu_reference.get("top1_margin", 0.0)),
                    "scale": cpu_reference.get("scale"),
                    "index_map": cpu_reference.get("index_map", {}),
                    "phase": phase,
                    "run_name": run_name,
                    "chips": int(chips),
                    "module_count": len(module_results),
                    "kernel_status": first_header.get("status"),
                    "kernel_executed": first_header.get("executed"),
                    "kernel_register_count": first_header.get("register_count"),
                    "kernel_module_id": first_header.get("module_id"),
                    "kernel_partition_id": first_header.get("partition_id"),
                    "kernel_trace_acc": first_header.get("trace_acc"),
                },
                debug_payload={"module_headers_preview": module_headers},
            )

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
        "cpu_reference": cpu_reference,
    }


def write_breakdown_plots(
    *,
    report_dir: pathlib.Path,
    report_basename: str,
    results: Dict[str, Dict[str, Any]],
) -> Dict[str, pathlib.Path]:
    try:
        import matplotlib.pyplot as plt  # type: ignore
    except ImportError:
        return {}

    chips_sorted = sorted(int(key) for key in results.keys())
    if not chips_sorted:
        return {}

    runtime_plot = report_dir / f"{report_basename}.runtime_breakdown.png"
    module_plot_path = report_dir / f"{report_basename}.module_breakdown.png"

    compile_vals = [float(results[str(ch)]["summary"].get("compile_total_s", 0.0)) for ch in chips_sorted]
    schedule_vals = [float(results[str(ch)]["summary"].get("schedule_avg_s", 0.0)) for ch in chips_sorted]
    compute_vals = [float(results[str(ch)]["summary"].get("compute_avg_s", 0.0)) for ch in chips_sorted]

    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.bar(chips_sorted, compile_vals, label="compile_total_s")
    ax.bar(chips_sorted, schedule_vals, bottom=compile_vals, label="schedule_avg_s")
    ax.bar(
        chips_sorted,
        compute_vals,
        bottom=[compile_vals[i] + schedule_vals[i] for i in range(len(chips_sorted))],
        label="compute_avg_s",
    )
    ax.set_xlabel("chips")
    ax.set_ylabel("seconds")
    ax.set_title("Tutorial3 runtime breakdown")
    ax.set_xticks(chips_sorted)
    ax.legend()
    fig.tight_layout()
    fig.savefig(runtime_plot, dpi=160)
    plt.close(fig)

    modules = sorted(
        {
            module_name
            for key in results.keys()
            for module_name in results[key].get("module_stage_stats", {}).keys()
        }
    )
    if modules:
        fig, ax = plt.subplots(figsize=(8, 4.5))
        bottoms = [0.0 for _ in chips_sorted]
        for module_name in modules:
            vals = [
                float(
                    results[str(ch)]
                    .get("module_stage_stats", {})
                    .get(module_name, {})
                    .get("avg_stage_wall_s", 0.0)
                )
                for ch in chips_sorted
            ]
            ax.bar(chips_sorted, vals, bottom=bottoms, label=module_name)
            bottoms = [bottoms[i] + vals[i] for i in range(len(chips_sorted))]
        ax.set_xlabel("chips")
        ax.set_ylabel("seconds")
        ax.set_title("Per-module stage wall breakdown")
        ax.set_xticks(chips_sorted)
        ax.legend(loc="upper right", fontsize=8, ncol=2)
        fig.tight_layout()
        fig.savefig(module_plot_path, dpi=160)
        plt.close(fig)
        has_module_plot = True
    else:
        has_module_plot = False

    paths: Dict[str, pathlib.Path] = {"runtime_breakdown": runtime_plot}
    if has_module_plot:
        paths["module_breakdown"] = module_plot_path
    return paths


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
    plot_paths = write_breakdown_plots(
        report_dir=report_dir,
        report_basename=report_basename,
        results=results,
    )

    rows_for_csv: List[Dict[str, Any]] = []
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        case = results[chip_key]
        cpu_reference = case.get("cpu_reference", {})
        golden_status = case.get("kernel_golden", {})
        root_cause = case.get("root_cause", {})
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
                    "label": cpu_reference.get("label"),
                    "cpu_pred_label": cpu_reference.get("pred_label"),
                    "cpu_pred_source": str(cpu_reference.get("pred_source", "")),
                    "cpu_pred_correct": int(bool(cpu_reference.get("is_correct", False))),
                    "kernel_golden_ok": int(bool(golden_status.get("ok", False))),
                    "root_cause": str(root_cause.get("category", "")),
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
    md_lines.append("## Prediction vs Label / Root Cause")
    md_lines.append("")
    md_lines.append("| chips | label | cpu pred | cpu source | cpu correct | kernel golden | root cause |")
    md_lines.append("|---|---:|---:|---|---:|---:|---|")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        case = results[chip_key]
        cpu_ref = case.get("cpu_reference", {})
        golden_status = case.get("kernel_golden", {})
        root_cause = case.get("root_cause", {})
        md_lines.append(
            f"| {chip_key} | {cpu_ref.get('label', 'NA')} | {cpu_ref.get('pred_label', 'NA')} | {cpu_ref.get('pred_source', 'cpu_decoded')} | "
            f"{'PASS' if bool(cpu_ref.get('is_correct', False)) else 'FAIL'} | "
            f"{'PASS' if bool(golden_status.get('ok', False)) else 'FAIL'} | "
            f"{root_cause.get('category', 'inconclusive')} |"
        )
    md_lines.append("")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        reason = str(results[chip_key].get("root_cause", {}).get("reason", ""))
        if reason:
            md_lines.append(f"- chips={chip_key}: {reason}")
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
    md_lines.append("## Breakdown Plots")
    md_lines.append("")
    if plot_paths:
        for name, path in sorted(plot_paths.items()):
            md_lines.append(f"- {name}: `{path}`")
    else:
        md_lines.append("- matplotlib not available; plot generation skipped.")
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
        cpu_ref = case.get("cpu_reference", {})
        golden_status = case.get("kernel_golden", {})
        root_cause = case.get("root_cause", {})
        status_a = "PASS" if not missing_ops else f"FAIL (missing: {','.join(missing_ops)})"
        status_t = (
            "PASS (not triggered by workload)"
            if int(case["transpose_descriptors"]) == 0
            else f"PASS (transpose descriptors={int(case['transpose_descriptors'])})"
        )
        status_b_cpu = (
            "PASS"
            if bool(cpu_ref.get("is_correct", False))
            else f"FAIL (label={cpu_ref.get('label')}, pred={cpu_ref.get('pred_label')}, source={cpu_ref.get('pred_source', 'cpu_decoded')})"
        )
        if bool(golden_status.get("ok", False)):
            status_b_kernel = "PASS"
        else:
            message = str(golden_status.get("message", "kernel golden mismatch"))
            status_b_kernel = f"FAIL ({message[:120]})"
        status_c = "PASS" if int(case["summary"]["runs"]) > 0 else "FAIL (no measured runs)"
        validation_lines.append(f"| Gate A chips={chip_key} | ntt+bci/pl1+rot coverage | {status_a} |")
        validation_lines.append(f"| Gate A chips={chip_key} | transpose coverage note | {status_t} |")
        validation_lines.append(f"| Gate B chips={chip_key} | CPU pred vs label | {status_b_cpu} |")
        validation_lines.append(f"| Gate B chips={chip_key} | FPGA kernel golden compare | {status_b_kernel} |")
        validation_lines.append(
            f"| Gate B chips={chip_key} | Root-cause category | {root_cause.get('category', 'inconclusive')} |"
        )
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
        "plot_paths": {name: str(path) for name, path in plot_paths.items()},
    }
    json_path.write_text(json.dumps(json_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        cpu_ref = results[chip_key].get("cpu_reference", {})
        _emit_jsonl(
            "benchmark.write_reports",
            payload={
                "sample_id": cpu_ref.get("sample_id"),
                "label": cpu_ref.get("label"),
                "plain_pred": cpu_ref.get("plain_pred"),
                "cpu_ref_pred": cpu_ref.get("cpu_ref_pred"),
                "decode_mode": str(cpu_ref.get("decode_mode", "")),
                "top1_margin": float(cpu_ref.get("top1_margin", 0.0)),
                "scale": cpu_ref.get("scale"),
                "index_map": cpu_ref.get("index_map", {}),
                "chips": int(chip_key),
                "report_basename": report_basename,
                "validation_basename": validation_basename,
                "csv_path": str(csv_path),
                "markdown_path": str(md_path),
                "report_json_path": str(json_path),
                "validation_path": str(validation_path),
            },
            debug_payload={
                "plot_paths": {name: str(path) for name, path in plot_paths.items()},
                "rows_csv_count": len(rows_for_csv),
            },
        )


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
    parser.add_argument("--strict-golden-check", action="store_true")
    parser.add_argument(
        "--pred-decode-mode",
        choices=list(_SUPPORTED_PRED_DECODE_MODES),
        default=_DEFAULT_PRED_DECODE_MODE,
    )
    parser.add_argument("--report-tag", default="")
    parser.add_argument(
        "--jsonl-log",
        type=pathlib.Path,
        default=None,
        help=f"always-on JSONL instrumentation path (or env {_JSONL_ENV})",
    )
    parser.add_argument(
        "--jsonl-debug-log",
        type=pathlib.Path,
        default=None,
        help=f"debug JSONL instrumentation path (or env {_JSONL_DEBUG_ENV})",
    )
    parser.add_argument(
        "--jsonl-debug",
        action="store_true",
        help=f"enable debug JSONL payloads (or env {_JSONL_DEBUG_FLAG_ENV}=1)",
    )
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

    if args.jsonl_log is not None:
        os.environ[_JSONL_ENV] = str(args.jsonl_log)
    if args.jsonl_debug_log is not None:
        os.environ[_JSONL_DEBUG_ENV] = str(args.jsonl_debug_log)
    if args.jsonl_debug:
        os.environ[_JSONL_DEBUG_FLAG_ENV] = "1"

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
            pred_decode_mode=str(args.pred_decode_mode),
        )
        results[str(chips)] = case
        first_measured_outputs = case["outputs_measured"][0]
        actual_cases[str(chips)] = normalize_kernel_outputs(first_measured_outputs)
        results[str(chips)]["kernel_golden"] = {
            "ok": False,
            "message": "not checked",
        }

    if args.write_golden:
        payload = {
            "program": "tutorial3_mnist_inference",
            "target_reference": args.target,
            "chips": chips_list,
            "cases": actual_cases,
        }
        write_golden(args.golden, payload)
        print(f"Wrote golden file: {args.golden}")
        for key in results.keys():
            results[key]["kernel_golden"] = {
                "ok": True,
                "message": "golden rewritten from current run",
            }
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
                message = f"golden is missing chips={key} case"
                results[key]["kernel_golden"] = {"ok": False, "message": message}
                if args.strict_golden_check:
                    raise RuntimeError(message)
                continue
            ok, message = compare_case(golden_cases[key], actual_case)
            results[key]["kernel_golden"] = {
                "ok": bool(ok),
                "message": "" if ok else str(message),
            }
            if (not ok) and args.strict_golden_check:
                raise RuntimeError(f"golden mismatch for chips={key}: {message}")

    for key, case in results.items():
        case["root_cause"] = classify_root_cause(
            case.get("cpu_reference", {}),
            bool(case.get("kernel_golden", {}).get("ok", False)),
        )

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
    runtime_plot = ROOT_DIR / "build" / "reports" / f"{report_basename}.runtime_breakdown.png"
    module_plot = ROOT_DIR / "build" / "reports" / f"{report_basename}.module_breakdown.png"
    if runtime_plot.exists():
        print(f"  - {runtime_plot}")
    if module_plot.exists():
        print(f"  - {module_plot}")
    print(f"  - {log_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
