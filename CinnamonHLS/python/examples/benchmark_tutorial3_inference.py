from __future__ import annotations

import argparse
import json
import os
import pathlib
import random
import sys
import time
from contextlib import contextmanager
from datetime import datetime
from typing import Any, Dict, List, Optional, Sequence

import cinnamon_emulator
import cinnamon_fpga
import numpy as np
import torch
from cinnamon.compiler import cinnamon_compile
from cinnamon.dsl import CiphertextInput, CinnamonProgram, Output, PlaintextInput
from cinnamon.passes import keyswitch_pass
from PIL import Image
from torchvision import transforms

ROOT_DIR = pathlib.Path(__file__).resolve().parents[2]
REPO_DIR = ROOT_DIR.parent
TUTORIAL3_DIR = REPO_DIR / "CinnamonTutorial" / "notebook3"
if str(TUTORIAL3_DIR) not in sys.path:
    sys.path.insert(0, str(TUTORIAL3_DIR))

from mnist_io import Primes, get_mnist_program_io  # noqa: E402
from cinnamon_fpga.tutorial3_decode import (  # noqa: E402
    DEFAULT_PRED_DECODE_MODE as _TUTORIAL3_DEFAULT_PRED_DECODE_MODE,
    SUPPORTED_PRED_DECODE_MODES as _TUTORIAL3_SUPPORTED_PRED_DECODE_MODES,
    decode_scores_with_mode as _decode_scores_with_mode,
)

RNS_BIT_SIZE = 28
TOP_LEVEL = 20
SLOTS = 32 * 1024

_SUPPORTED_PRED_DECODE_MODES = _TUTORIAL3_SUPPORTED_PRED_DECODE_MODES
_DEFAULT_PRED_DECODE_MODE = _TUTORIAL3_DEFAULT_PRED_DECODE_MODE
_DEBUG_PREVIEW_WORDS = 16

# For HE-vs-HE intermediate checks, compare only a small prefix of slots.
_LAYER_COMPARE_PLAN: Dict[str, int] = {
    "conv": 256,
    "conv_sq": 256,
    "o2": 64,
    "o2_sq": 64,
    "pred": 10,
}


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

        Output("conv", conv)
        Output("conv_sq", conv_sq)
        Output("o2", o2)
        Output("o2_sq", o2_sq)
        Output("pred", pred)

    return program


def load_input(sample_num: int) -> np.ndarray:
    transform = transforms.Compose(
        [transforms.ToTensor(), transforms.Normalize((0.1307,), (0.3081,))]
    )
    img_path = TUTORIAL3_DIR / "samples" / f"img_{sample_num}.jpg"
    img = Image.open(img_path)
    tensor = transform(img).view(1, 28, 28).to(torch.float32).detach().numpy()[0]
    return tensor


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


def run_one_sample_plain_model(
    *,
    sample_id: int,
    model_path: Optional[pathlib.Path] = None,
) -> Dict[str, Any]:
    labels = load_labels()
    if sample_id < 1 or sample_id > len(labels):
        raise ValueError(f"sample_id={sample_id} is out of label range 1..{len(labels)}")
    label = int(labels[sample_id - 1])

    target_model_path = model_path or (TUTORIAL3_DIR / "mnist.pth")
    model = _get_plain_model(target_model_path)
    image = load_input(sample_id)
    image_tensor = torch.from_numpy(image).to(torch.float32).view(1, 1, 28, 28)

    t0 = time.perf_counter()
    with torch.no_grad():
        logits = model(image_tensor).detach().cpu().numpy().reshape(-1)
    t1 = time.perf_counter()

    logits_list = [float(v) for v in logits.tolist()]
    pred_label = int(np.argmax(logits)) if logits.size > 0 else -1
    top3 = _topk_indices(logits_list, 3)
    return {
        "sample_id": int(sample_id),
        "label": int(label),
        "pred_label": int(pred_label),
        "pred_valid": bool(logits.size >= 10),
        "is_correct": bool(pred_label == label),
        "logits": logits_list[:10],
        "top3_labels": [int(v) for v in top3],
        "top1_margin": _top1_margin(logits_list[:10]),
        "wall_run_program_s": float(t1 - t0),
        "model_path": str(target_model_path),
    }


def build_encryptor(context: cinnamon_emulator.Context) -> cinnamon_emulator.CKKSEncryptor:
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


def _to_real_values(values: Sequence[Any]) -> List[float]:
    out: List[float] = []
    for item in values:
        if isinstance(item, complex):
            out.append(float(item.real))
        else:
            out.append(float(item))
    return out


def _preview_numeric(values: Sequence[Any], limit: int = _DEBUG_PREVIEW_WORDS) -> List[float]:
    return _to_real_values(list(values)[:limit])


def _topk_indices(values: Sequence[float], k: int) -> List[int]:
    if not values:
        return []
    return sorted(range(len(values)), key=lambda idx: float(values[idx]), reverse=True)[:k]


def _top1_margin(values: Sequence[float]) -> float:
    top2 = _topk_indices(values, 2)
    if len(top2) < 2:
        return 0.0
    return float(values[top2[0]] - values[top2[1]])


def _notebook_pred_indices(class_count: int = 10) -> List[int]:
    return [int(cls * 128) for cls in range(max(class_count, 0))]


def _values_at_indices(values: Sequence[float], indices: Sequence[int]) -> List[float]:
    out: List[float] = []
    for idx in indices:
        if 0 <= int(idx) < len(values):
            out.append(float(values[int(idx)]))
        else:
            out.append(0.0)
    return out


def _max_abs_diff(a: Sequence[Any], b: Sequence[Any], limit: Optional[int] = None) -> float:
    aa = _to_real_values(a)
    bb = _to_real_values(b)
    n = min(len(aa), len(bb))
    if limit is not None:
        n = min(n, int(limit))
    if n <= 0:
        return 0.0
    return max(abs(float(aa[i]) - float(bb[i])) for i in range(n))


def decode_pred(
    pred_values: Sequence[Any],
    *,
    decode_mode: str,
) -> Dict[str, Any]:
    if not pred_values:
        return {
            "pred_values_real": [],
            "scores": [],
            "decode_mode": "empty",
            "index_map": {"stride": 0, "repeats": 0, "class_count": 0},
            "pred_label": None,
            "pred_valid": False,
            "top3_labels": [],
            "top1_margin": 0.0,
            "pred_key_indices": _notebook_pred_indices(10),
            "pred_key_values": [0.0] * 10,
        }

    real_values = _to_real_values(pred_values)
    scores, normalized_mode, index_map, _ = _decode_scores_with_mode(real_values, decode_mode)

    pred_valid = len(scores) >= 10
    pred_label = int(max(range(10), key=lambda idx: scores[idx])) if pred_valid else None
    top3 = _topk_indices(scores[:10], 3) if pred_valid else []
    top1_margin = _top1_margin(scores[:10]) if pred_valid else 0.0

    notebook_indices = _notebook_pred_indices(10)
    notebook_values = _values_at_indices(real_values, notebook_indices)

    return {
        "pred_values_real": real_values,
        "scores": [float(v) for v in scores[:10]],
        "decode_mode": normalized_mode,
        "index_map": index_map,
        "pred_label": pred_label,
        "pred_valid": bool(pred_valid),
        "top3_labels": [int(v) for v in top3],
        "top1_margin": float(top1_margin),
        "pred_key_indices": notebook_indices,
        "pred_key_values": notebook_values,
    }


def summarize_kernel_execution(outputs: Sequence[Dict[str, Any]]) -> Dict[str, Any]:
    issues: List[str] = []
    checked_modules = 0

    for partition in outputs:
        partition_id = int(partition.get("partition_id", -1))
        module_results = list(partition.get("module_results", []))
        for module_result in module_results:
            checked_modules += 1
            module_name = str(module_result.get("module", ""))
            status = module_result.get("status")
            if status is None:
                issues.append(f"p{partition_id}:{module_name}:missing_status")
                continue
            if int(status) != 0:
                issues.append(f"p{partition_id}:{module_name}:status={int(status)}")

    return {
        "ok": len(issues) == 0,
        "checked_modules": int(checked_modules),
        "issues": issues[:32],
    }


def summarize_kernel_outputs(
    outputs: Sequence[Dict[str, Any]],
    *,
    preview_words: int = _DEBUG_PREVIEW_WORDS,
) -> List[Dict[str, Any]]:
    partitions: List[Dict[str, Any]] = []
    for partition in outputs:
        partition_summary: Dict[str, Any] = {
            "partition_id": int(partition.get("partition_id", -1)),
            "board_index": int(partition.get("board_index", -1)),
            "input_count": int(partition.get("input_count", 0)),
            "instruction_count": int(partition.get("instruction_count", 0)),
            "modules": [],
        }
        for module in partition.get("module_results", []):
            partition_summary["modules"].append(
                {
                    "module": str(module.get("module", "")),
                    "kernel_name": str(module.get("kernel_name", "")),
                    "status": int(module.get("status", -1)) if module.get("status") is not None else None,
                    "executed": int(module.get("executed", 0)),
                    "register_count": int(module.get("register_count", 0)),
                    "trace_acc": int(module.get("trace_acc", 0)),
                    "output_preview": [int(v) for v in list(module.get("output_words", []))[:preview_words]],
                }
            )
        partitions.append(partition_summary)
    return partitions


def prepare_artifacts(
    *,
    chips: int,
    work_root: pathlib.Path,
    reuse_compiled_artifacts: bool,
) -> Dict[str, Any]:
    out_dir = work_root / f"chips_{chips}"
    out_dir.mkdir(parents=True, exist_ok=True)

    has_instructions = (out_dir / "instructions").exists() or bool(list(out_dir.glob("instructions*")))
    has_program_inputs = (out_dir / "program_inputs").exists()
    should_compile = not (reuse_compiled_artifacts and has_instructions and has_program_inputs)

    compile_info: Dict[str, Any] = {
        "out_dir": out_dir,
        "compiled": False,
        "keyswitch_pass_s": 0.0,
        "compile_s": 0.0,
        "compile_total_s": 0.0,
    }

    if should_compile:
        program = build_mnist_program(chips)

        t0 = time.perf_counter()
        keyswitch_pass(program)
        t1 = time.perf_counter()

        cinnamon_compile(program, TOP_LEVEL, chips, 256, str(out_dir) + "/")
        t2 = time.perf_counter()

        compile_info["compiled"] = True
        compile_info["keyswitch_pass_s"] = float(t1 - t0)
        compile_info["compile_s"] = float(t2 - t1)
        compile_info["compile_total_s"] = float(t2 - t0)

    compile_info["instruction_base"] = out_dir / "instructions"
    compile_info["program_inputs"] = out_dir / "program_inputs"
    compile_info["evalkeys"] = out_dir / "evalkeys"
    return compile_info


def compare_he_intermediates(
    *,
    reference_outputs: Dict[str, Any],
    test_outputs: Dict[str, Any],
) -> Dict[str, Any]:
    result: Dict[str, Any] = {}
    first_bad_layer: Optional[str] = None

    for layer_name, take_n in _LAYER_COMPARE_PLAN.items():
        ref_vals = _to_real_values(reference_outputs.get(layer_name, []))[:take_n]
        test_vals = _to_real_values(test_outputs.get(layer_name, []))[:take_n]

        diff = _max_abs_diff(ref_vals, test_vals, limit=take_n)
        compare_count = min(len(ref_vals), len(test_vals))

        result[layer_name] = {
            "compare_count": int(compare_count),
            "max_abs_diff": float(diff),
            "reference_preview": ref_vals[:16],
            "test_preview": test_vals[:16],
        }

        if first_bad_layer is None and diff > 1e-4:
            first_bad_layer = layer_name

    result["first_bad_layer"] = first_bad_layer
    return result


def run_one_sample_fpga_only(
    *,
    target: str,
    xclbin: pathlib.Path,
    chips: int,
    boards: Sequence[int],
    register_file_size: int,
    sample_id: int,
    decode_mode: str,
    work_root: pathlib.Path,
    reuse_compiled_artifacts: bool,
    dump_full_kernel_outputs: bool,
) -> Dict[str, Any]:
    if len(boards) < chips:
        raise RuntimeError(f"need at least {chips} boards, got {len(boards)} ({boards})")

    artifact_info = prepare_artifacts(
        chips=chips,
        work_root=work_root,
        reuse_compiled_artifacts=reuse_compiled_artifacts,
    )

    context = cinnamon_emulator.Context(SLOTS, Primes)
    encryptor = build_encryptor(context)

    input_image = load_input(sample_id)
    with pushd(TUTORIAL3_DIR):
        raw_inputs, output_scales = get_mnist_program_io(input_image, TOP_LEVEL)

    labels = load_labels()
    if sample_id < 1 or sample_id > len(labels):
        raise ValueError(f"sample_id={sample_id} is out of label range 1..{len(labels)}")
    label = int(labels[sample_id - 1])

    instruction_base = str(artifact_info["instruction_base"])
    program_inputs = str(artifact_info["program_inputs"])
    evalkeys = str(artifact_info["evalkeys"])

    runtime = cinnamon_fpga.Emulator(
        context,
        target=target,
        xclbin_path=xclbin,
        board_indices=list(boards[:chips]),
        parallel_dispatch=not (target == "sw_emu" and chips > 1),
        require_kernel_execution=True,
        verify_kernel_results=True,
    )

    runtime.generate_and_serialize_evalkeys(evalkeys, program_inputs, encryptor)
    runtime.generate_inputs(program_inputs, evalkeys, raw_inputs, encryptor)

    t0 = time.perf_counter()
    runtime.run_program(instruction_base, chips, register_file_size)
    t1 = time.perf_counter()

    outputs = runtime.get_kernel_outputs()
    kernel_exec = summarize_kernel_execution(outputs)
    kernel_outputs_summary = summarize_kernel_outputs(outputs)
    timing_summary = dict(runtime.last_timing_summary)

    decrypt_ok = False
    decrypt_error = ""
    decrypted_outputs: Dict[str, Any] = {}
    pred_values: List[Any] = []

    try:
        decrypted_outputs = dict(runtime.get_decrypted_outputs(encryptor, output_scales))
        pred_values = list(decrypted_outputs.get("pred", []))
        decrypt_ok = True
    except Exception as exc:
        decrypt_ok = False
        decrypt_error = str(exc)

    decoded = decode_pred(pred_values, decode_mode=decode_mode)
    pred_label = decoded["pred_label"]
    pred_valid = bool(decrypt_ok and decoded["pred_valid"] and pred_label is not None)
    is_correct = bool(pred_valid and int(pred_label) == label)

    result = {
        "target": target,
        "chips": int(chips),
        "sample_id": int(sample_id),
        "label": int(label),
        "pred_label": int(pred_label) if pred_label is not None else None,
        "pred_valid": bool(pred_valid),
        "is_correct": bool(is_correct),
        "decrypt_ok": bool(decrypt_ok),
        "decrypt_error": str(decrypt_error),
        "decode_mode": str(decoded["decode_mode"]),
        "index_map": decoded["index_map"],
        "scores": decoded["scores"],
        "top3_labels": decoded["top3_labels"],
        "top1_margin": decoded["top1_margin"],
        "pred_key_indices": decoded["pred_key_indices"],
        "pred_key_values": decoded["pred_key_values"],
        "pred_values_preview": _preview_numeric(pred_values),
        "decrypted_output_names": sorted(str(k) for k in decrypted_outputs.keys()),
        "decrypted_outputs": {
            name: _to_real_values(values)
            for name, values in decrypted_outputs.items()
        } if decrypt_ok else {},
        "kernel_execution": kernel_exec,
        "kernel_outputs_summary": kernel_outputs_summary,
        "runtime_global": dict(timing_summary.get("global", {})),
        "runtime_stage": list(timing_summary.get("stage_timing", [])),
        "wall_run_program_s": float(t1 - t0),
        "artifacts": {
            "out_dir": str(artifact_info["out_dir"]),
            "instruction_base": instruction_base,
            "program_inputs": program_inputs,
            "evalkeys": evalkeys,
            "xclbin": str(xclbin),
        },
        "compile_info": {
            "compiled": bool(artifact_info["compiled"]),
            "keyswitch_pass_s": float(artifact_info["keyswitch_pass_s"]),
            "compile_s": float(artifact_info["compile_s"]),
            "compile_total_s": float(artifact_info["compile_total_s"]),
        },
    }
    if dump_full_kernel_outputs:
        result["kernel_outputs_full"] = outputs
    return result


def run_one_sample_cpu_compiled(
    *,
    chips: int,
    register_file_size: int,
    sample_id: int,
    decode_mode: str,
    work_root: pathlib.Path,
    reuse_compiled_artifacts: bool,
) -> Dict[str, Any]:
    artifact_info = prepare_artifacts(
        chips=chips,
        work_root=work_root,
        reuse_compiled_artifacts=reuse_compiled_artifacts,
    )

    context = cinnamon_emulator.Context(SLOTS, Primes)
    encryptor = build_encryptor(context)

    input_image = load_input(sample_id)
    with pushd(TUTORIAL3_DIR):
        raw_inputs, output_scales = get_mnist_program_io(input_image, TOP_LEVEL)

    labels = load_labels()
    if sample_id < 1 or sample_id > len(labels):
        raise ValueError(f"sample_id={sample_id} is out of label range 1..{len(labels)}")
    label = int(labels[sample_id - 1])

    instruction_base = str(artifact_info["instruction_base"])
    program_inputs = str(artifact_info["program_inputs"])
    evalkeys = str(artifact_info["evalkeys"])

    cpu = cinnamon_emulator.Emulator(context)
    cpu.generate_and_serialize_evalkeys(evalkeys, program_inputs, encryptor)
    cpu.generate_inputs(program_inputs, evalkeys, raw_inputs, encryptor)

    t0 = time.perf_counter()
    cpu.run_program(instruction_base, chips, register_file_size)
    t1 = time.perf_counter()

    decrypted_outputs = dict(cpu.get_decrypted_outputs(encryptor, output_scales))
    pred_values = list(decrypted_outputs.get("pred", []))
    decoded = decode_pred(pred_values, decode_mode=decode_mode)
    pred_label = decoded["pred_label"]
    pred_valid = bool(decoded["pred_valid"] and pred_label is not None)
    is_correct = bool(pred_valid and int(pred_label) == label)

    return {
        "sample_id": int(sample_id),
        "label": int(label),
        "pred_label": int(pred_label) if pred_label is not None else None,
        "pred_valid": bool(pred_valid),
        "is_correct": bool(is_correct),
        "decode_mode": str(decoded["decode_mode"]),
        "index_map": decoded["index_map"],
        "scores": decoded["scores"],
        "top3_labels": decoded["top3_labels"],
        "top1_margin": decoded["top1_margin"],
        "pred_key_indices": decoded["pred_key_indices"],
        "pred_key_values": decoded["pred_key_values"],
        "pred_values_preview": _preview_numeric(pred_values),
        "decrypted_output_names": sorted(str(k) for k in decrypted_outputs.keys()),
        "decrypted_outputs": {
            name: _to_real_values(values)
            for name, values in decrypted_outputs.items()
        },
        "wall_run_program_s": float(t1 - t0),
        "artifacts": {
            "out_dir": str(artifact_info["out_dir"]),
            "instruction_base": instruction_base,
            "program_inputs": program_inputs,
            "evalkeys": evalkeys,
        },
    }


def build_verdict(
    fpga_result: Dict[str, Any],
    cpu_result: Optional[Dict[str, Any]],
    plain_result: Optional[Dict[str, Any]],
    he_compare: Optional[Dict[str, Any]],
) -> Dict[str, Any]:
    if not fpga_result.get("kernel_execution", {}).get("ok", False):
        return {
            "category": "kernel_runtime_issue",
            "reason": "One or more FPGA kernels reported non-zero status.",
        }

    if not bool(fpga_result.get("decrypt_ok", False)):
        return {
            "category": "decrypt_issue",
            "reason": f"FPGA output decryption failed: {fpga_result.get('decrypt_error', '')}",
        }

    if cpu_result is not None and he_compare is not None:
        first_bad_layer = he_compare.get("first_bad_layer")
        if first_bad_layer is not None:
            return {
                "category": "fpga_vs_cpu_he_mismatch",
                "reason": f"FPGA and CPU-compiled HE diverge first at layer {first_bad_layer}.",
            }

    if plain_result is not None:
        plain_pred = plain_result.get("pred_label")
        label = fpga_result.get("label")

        if plain_pred == label:
            if not fpga_result.get("is_correct", False):
                return {
                    "category": "he_program_or_decode_issue",
                    "reason": "Plain model is correct but HE path is wrong.",
                }
        else:
            return {
                "category": "model_or_label_issue",
                "reason": "Plain model itself disagrees with label.",
            }

    if cpu_result is None:
        if bool(fpga_result.get("is_correct", False)):
            return {
                "category": "pass",
                "reason": "FPGA decrypted prediction matches label.",
            }
        return {
            "category": "fpga_wrong_no_cpu_baseline",
            "reason": "FPGA prediction wrong and no CPU baseline available.",
        }

    fpga_pred = fpga_result.get("pred_label")
    cpu_pred = cpu_result.get("pred_label")
    same_pred = (
        fpga_pred is not None and cpu_pred is not None and int(fpga_pred) == int(cpu_pred)
    )

    if same_pred and fpga_result.get("is_correct", False):
        return {
            "category": "pass",
            "reason": "FPGA and CPU compiled HE match and are correct.",
        }

    if same_pred and not fpga_result.get("is_correct", False):
        return {
            "category": "program_or_decode_issue",
            "reason": "FPGA and CPU compiled HE match each other but both disagree with the label.",
        }

    return {
        "category": "fpga_vs_cpu_mismatch",
        "reason": "FPGA and CPU compiled HE final predictions differ.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="CPU encrypt -> FPGA compute -> CPU decrypt -> pred vs label"
    )
    parser.add_argument("--target", choices=["sw_emu", "hw_emu", "hw"], default="sw_emu")
    parser.add_argument("--xclbin", type=pathlib.Path, default=None)
    parser.add_argument("--chips", default="1")
    parser.add_argument("--boards", default="0,1,2,3")
    parser.add_argument("--sample-id", type=int, default=10)
    parser.add_argument("--sample-start", type=int, default=None)
    parser.add_argument("--sample-end", type=int, default=None)
    parser.add_argument("--register-file-size", type=int, default=1024)
    parser.add_argument(
        "--pred-decode-mode",
        choices=list(_SUPPORTED_PRED_DECODE_MODES),
        default=_DEFAULT_PRED_DECODE_MODE,
    )
    parser.add_argument(
        "--work-root",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "cinnamon_tutorial3_fpga_only",
    )
    parser.add_argument(
        "--report-dir",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "reports",
    )
    parser.add_argument("--report-tag", default="")
    parser.add_argument(
        "--force-recompile",
        action="store_true",
        help="Do not reuse existing compiled instructions/program_inputs/evalkeys",
    )
    parser.add_argument(
        "--compare-cpu-compiled",
        action="store_true",
        help="Also run the same compiled instructions on cinnamon_emulator for comparison.",
    )
    parser.add_argument(
        "--run-plain-model",
        action="store_true",
        help="Also run the plain PyTorch MNIST model on the same sample for diagnosis.",
    )
    parser.add_argument(
        "--dump-full-kernel-outputs",
        action="store_true",
        help="Store full kernel_outputs in the JSON report (can be large).",
    )
    args = parser.parse_args()

    chips_list = parse_int_list(args.chips)
    board_list = parse_int_list(args.boards)

    labels = load_labels()
    sample_start = int(args.sample_start) if args.sample_start is not None else int(args.sample_id)
    sample_end = int(args.sample_end) if args.sample_end is not None else sample_start

    if sample_start < 1:
        raise ValueError(f"sample-start must be >= 1, got {sample_start}")
    if sample_end < sample_start:
        raise ValueError(f"sample-end must be >= sample-start, got {sample_end} < {sample_start}")
    if sample_end > len(labels):
        raise ValueError(f"sample-end={sample_end} exceeds labels range 1..{len(labels)}")

    if args.xclbin is None:
        args.xclbin = ROOT_DIR / "build" / args.target / f"cinnamon_fpga.{args.target}.xclbin"
    if not args.xclbin.exists():
        raise FileNotFoundError(f"xclbin not found: {args.xclbin}")

    sample_ids = list(range(sample_start, sample_end + 1))
    all_results: List[Dict[str, Any]] = []

    print(
        f"target={args.target} xclbin={args.xclbin} chips={chips_list} boards={board_list} "
        f"sample_range={sample_start}..{sample_end} decode_mode={args.pred_decode_mode} "
        f"compare_cpu_compiled={bool(args.compare_cpu_compiled)} run_plain_model={bool(args.run_plain_model)}"
    )

    for chips in chips_list:
        for sample_id in sample_ids:
            print(f"\n=== chips={chips} sample={sample_id} ===")

            fpga_result = run_one_sample_fpga_only(
                target=str(args.target),
                xclbin=args.xclbin,
                chips=int(chips),
                boards=board_list,
                register_file_size=int(args.register_file_size),
                sample_id=int(sample_id),
                decode_mode=str(args.pred_decode_mode),
                work_root=args.work_root / args.target,
                reuse_compiled_artifacts=not bool(args.force_recompile),
                dump_full_kernel_outputs=bool(args.dump_full_kernel_outputs),
            )

            cpu_result: Optional[Dict[str, Any]] = None
            he_compare: Optional[Dict[str, Any]] = None
            if args.compare_cpu_compiled:
                cpu_result = run_one_sample_cpu_compiled(
                    chips=int(chips),
                    register_file_size=int(args.register_file_size),
                    sample_id=int(sample_id),
                    decode_mode=str(args.pred_decode_mode),
                    work_root=args.work_root / args.target,
                    reuse_compiled_artifacts=True,
                )
                he_compare = compare_he_intermediates(
                    reference_outputs=cpu_result["decrypted_outputs"],
                    test_outputs=fpga_result["decrypted_outputs"],
                )

            plain_result: Optional[Dict[str, Any]] = None
            if args.run_plain_model:
                plain_result = run_one_sample_plain_model(sample_id=int(sample_id))

            verdict = build_verdict(fpga_result, cpu_result, plain_result, he_compare)

            result = {
                "fpga": fpga_result,
                "cpu_compiled": cpu_result,
                "plain_model": plain_result,
                "he_intermediate_compare": he_compare,
                "verdict": verdict,
            }
            all_results.append(result)

            print(
                f"label={fpga_result['label']} "
                f"fpga_pred={fpga_result['pred_label']} "
                f"fpga_correct={fpga_result['is_correct']} "
                f"decrypt_ok={fpga_result['decrypt_ok']} "
                f"kernel_ok={fpga_result['kernel_execution']['ok']} "
                f"wall_run_program_s={fpga_result['wall_run_program_s']:.6f}"
            )
            print(f"fpga_pred_key_values={fpga_result['pred_key_values']}")

            if fpga_result["decrypt_error"]:
                print(f"decrypt_error={fpga_result['decrypt_error']}")
            if not fpga_result["kernel_execution"]["ok"]:
                print(f"kernel_issues={fpga_result['kernel_execution']['issues']}")

            if cpu_result is not None:
                print(
                    f"cpu_pred={cpu_result['pred_label']} "
                    f"cpu_correct={cpu_result['is_correct']} "
                    f"cpu_wall_s={cpu_result['wall_run_program_s']:.6f}"
                )
                print(f"cpu_pred_key_values={cpu_result['pred_key_values']}")
                print(
                    "compare:"
                    f" pred_key_max_abs_diff={_max_abs_diff(fpga_result['pred_key_values'], cpu_result['pred_key_values']):.6e},"
                    f" score_max_abs_diff={_max_abs_diff(fpga_result['scores'], cpu_result['scores']):.6e}"
                )
                if he_compare is not None:
                    print(f"he_first_bad_layer={he_compare['first_bad_layer']}")
                    for layer_name in ["conv", "conv_sq", "o2", "o2_sq", "pred"]:
                        info = he_compare[layer_name]
                        print(
                            f"{layer_name}: "
                            f"max_abs_diff={info['max_abs_diff']:.6e} "
                            f"count={info['compare_count']}"
                        )

            if plain_result is not None:
                print(
                    f"plain_pred={plain_result['pred_label']} "
                    f"plain_correct={plain_result['is_correct']} "
                    f"plain_wall_s={plain_result['wall_run_program_s']:.6f}"
                )
                print(f"plain_logits={plain_result['logits']}")

            print(f"verdict={verdict['category']} reason={verdict['reason']}")

    total = len(all_results)
    correct = sum(1 for r in all_results if bool(r["fpga"]["is_correct"]))
    decrypt_ok_count = sum(1 for r in all_results if bool(r["fpga"]["decrypt_ok"]))
    kernel_ok_count = sum(1 for r in all_results if bool(r["fpga"]["kernel_execution"]["ok"]))
    cpu_match_count = sum(
        1
        for r in all_results
        if r["cpu_compiled"] is not None
        and r["fpga"].get("pred_label") is not None
        and r["cpu_compiled"].get("pred_label") is not None
        and int(r["fpga"]["pred_label"]) == int(r["cpu_compiled"]["pred_label"])
    )
    plain_correct_count = sum(
        1 for r in all_results if r.get("plain_model") is not None and bool(r["plain_model"].get("is_correct", False))
    )
    plain_match_fpga_count = sum(
        1
        for r in all_results
        if r.get("plain_model") is not None
        and r["fpga"].get("pred_label") is not None
        and r["plain_model"].get("pred_label") is not None
        and int(r["fpga"]["pred_label"]) == int(r["plain_model"]["pred_label"])
    )

    summary = {
        "generated_at": datetime.now().isoformat(),
        "target": str(args.target),
        "xclbin": str(args.xclbin),
        "chips": [int(v) for v in chips_list],
        "boards": [int(v) for v in board_list],
        "sample_start": int(sample_start),
        "sample_end": int(sample_end),
        "sample_ids": [int(v) for v in sample_ids],
        "decode_mode": str(args.pred_decode_mode),
        "compare_cpu_compiled": bool(args.compare_cpu_compiled),
        "run_plain_model": bool(args.run_plain_model),
        "total": int(total),
        "correct": int(correct),
        "accuracy": (float(correct) / float(total)) if total > 0 else 0.0,
        "decrypt_ok_count": int(decrypt_ok_count),
        "kernel_ok_count": int(kernel_ok_count),
        "cpu_match_count": int(cpu_match_count),
        "plain_correct_count": int(plain_correct_count),
        "plain_match_fpga_count": int(plain_match_fpga_count),
        "results": all_results,
    }

    args.report_dir.mkdir(parents=True, exist_ok=True)
    tag = f".{args.report_tag.strip()}" if args.report_tag.strip() else ""
    out_json = args.report_dir / f"{args.target}_fpga_only_pred_vs_label{tag}.json"
    out_json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("\n=== summary ===")
    print(f"accuracy={summary['accuracy']:.4f} ({correct}/{total})")
    print(f"decrypt_ok={decrypt_ok_count}/{total}")
    print(f"kernel_ok={kernel_ok_count}/{total}")
    if args.compare_cpu_compiled:
        print(f"cpu_match={cpu_match_count}/{total}")
    if args.run_plain_model:
        print(f"plain_correct={plain_correct_count}/{total}")
        print(f"plain_match_fpga={plain_match_fpga_count}/{total}")
    print(f"report={out_json}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())