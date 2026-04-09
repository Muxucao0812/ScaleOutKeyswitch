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

# Optional parser helper: some branches have it, some do not.
try:
    from cinnamon_fpga.parser import expected_module_output_words  # type: ignore
except Exception:
    expected_module_output_words = None  # type: ignore

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
_INPUT_MAGIC = 0x43494E4E414D4F4E
_JSONL_ENV = "CINNAMON_FPGA_JSONL_LOG"
_JSONL_DEBUG_ENV = "CINNAMON_FPGA_JSONL_DEBUG_LOG"
_JSONL_DEBUG_FLAG_ENV = "CINNAMON_FPGA_JSONL_DEBUG"
_DEBUG_PREVIEW_WORDS = 8
_SUPPORTED_PRED_DECODE_MODES = _TUTORIAL3_SUPPORTED_PRED_DECODE_MODES
_DEFAULT_PRED_DECODE_MODE = _TUTORIAL3_DEFAULT_PRED_DECODE_MODE
_SUPPORTED_PRED_METRICS = ("strict", "fallback", "dual")
_DEFAULT_PRED_METRIC = "strict"
_DEFAULT_MODULUS = 268042241
_REPLAY_FIXTURE_MAGIC = "CINNAMON_AUTOMORPHISM_REPLAY_V1"


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


def _copy_artifact(src: pathlib.Path, dst: pathlib.Path) -> None:
    if not src.exists():
        return
    if src.is_dir():
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(src, dst)
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def _extract_module_output_words(
    normalized_case: Sequence[Dict[str, Any]],
    *,
    partition_id: int,
    module_name: str,
) -> List[int]:
    for partition in normalized_case:
        if int(partition.get("partition_id", -1)) != int(partition_id):
            continue
        for module in partition.get("module_results", []):
            if str(module.get("module", "")) != str(module_name):
                continue
            return [int(word) for word in module.get("output_words", [])]
    raise RuntimeError(
        f"module output not found for partition={partition_id}, module={module_name}"
    )


def _build_module_replay_input_words(
    *,
    run_case_outputs: Sequence[Dict[str, Any]],
    instruction_base: pathlib.Path,
    program_inputs_path: pathlib.Path,
    register_file_size: int,
    partition_id: int,
    module_name: str,
    modulus: int = _DEFAULT_MODULUS,
) -> Tuple[List[int], List[int]]:
    if partition_id < 0 or partition_id >= len(run_case_outputs):
        raise RuntimeError(
            f"partition_id={partition_id} out of range for measured outputs (n={len(run_case_outputs)})"
        )
    partition_output = run_case_outputs[partition_id]
    module_results = list(partition_output.get("module_results", []))

    module_idx = -1
    for idx, module_result in enumerate(module_results):
        if str(module_result.get("module", "")) == str(module_name):
            module_idx = idx
            break
    if module_idx < 0:
        raise RuntimeError(f"module={module_name} not found in run outputs")

    instruction_streams = load_instruction_streams(str(instruction_base), len(run_case_outputs))
    buckets = split_stream_by_module(instruction_streams[partition_id])
    instruction_words = list(buckets[module_name].instruction_words)

    from cinnamon_fpga.api import _build_partition_input_words

    sections = parse_program_inputs(program_inputs_path).sections
    initial_input_words = _build_partition_input_words(
        register_file_size=register_file_size,
        program_input_sections=sections,
        partition_id=partition_id,
        modulus=modulus,
    )
    bounded_register_count = int(initial_input_words[1]) if len(initial_input_words) > 1 else 0
    io_tail = initial_input_words[3 + bounded_register_count :]

    if module_idx == 0:
        module_input_words = list(initial_input_words)
        return module_input_words, instruction_words

    prev_output_words = list(module_results[module_idx - 1].get("output_words", []))
    prev_register_count = int(prev_output_words[2]) if len(prev_output_words) > 2 else bounded_register_count
    prev_state = [int(word) for word in prev_output_words[6 : 6 + prev_register_count]]
    module_input_words = [
        _INPUT_MAGIC,
        prev_register_count,
        int(modulus) if int(modulus) != 0 else _DEFAULT_MODULUS,
        *prev_state,
        *io_tail,
    ]
    return module_input_words, instruction_words


def _write_automorphism_replay_fixture(
    *,
    output_dir: pathlib.Path,
    mismatch: Dict[str, Any],
    expected_module_words: Sequence[int],
    actual_module_words: Sequence[int],
    module_input_words: Sequence[int],
    instruction_words: Sequence[int],
    context: Dict[str, Any],
) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    ts = time.strftime("%Y%m%d_%H%M%S", time.localtime())
    partition_id = int(mismatch.get("partition_id", -1))
    word_index = int(mismatch.get("word_index", -1))
    filename = f"automorphism_replay_{ts}_p{partition_id}_w{word_index}.txt"
    fixture_path = output_dir / filename

    expected_word = (
        int(expected_module_words[word_index])
        if 0 <= word_index < len(expected_module_words)
        else -1
    )
    actual_word = (
        int(actual_module_words[word_index])
        if 0 <= word_index < len(actual_module_words)
        else -1
    )

    lines: List[str] = [
        _REPLAY_FIXTURE_MAGIC,
        f"partition_id {partition_id}",
        f"module_name automorphism",
        f"instruction_word_count {len(instruction_words)}",
        f"input_word_count {len(module_input_words)}",
        f"output_word_count {len(actual_module_words)}",
        f"mismatch_word_index {word_index}",
        f"expected_mismatch_word {expected_word}",
        f"actual_mismatch_word {actual_word}",
        f"sample_id {int(context.get('sample_id', -1))}",
        f"chips {int(context.get('chips', -1))}",
        "--instructions--",
    ]
    lines.extend(str(int(word) & 0xFFFFFFFFFFFFFFFF) for word in instruction_words)
    lines.append("--inputs--")
    lines.extend(str(int(word) & 0xFFFFFFFFFFFFFFFF) for word in module_input_words)
    lines.append("--expected_output--")
    lines.extend(str(int(word) & 0xFFFFFFFFFFFFFFFF) for word in expected_module_words)
    lines.append("--actual_output--")
    lines.extend(str(int(word) & 0xFFFFFFFFFFFFFFFF) for word in actual_module_words)
    lines.append("--end--")
    fixture_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return fixture_path


def _archive_case_artifacts(
    *,
    archive_root: pathlib.Path,
    timestamp: str,
    chips: int,
    sample_id: int,
    target: str,
    instruction_base: pathlib.Path,
    program_inputs_path: pathlib.Path,
    evalkeys_path: pathlib.Path,
    golden_path: pathlib.Path,
    metadata: Dict[str, Any],
) -> pathlib.Path:
    debug_dir = archive_root / f"{timestamp}_{target}_chips{chips}_sample{sample_id}"
    debug_dir.mkdir(parents=True, exist_ok=True)

    source_dir = instruction_base.parent
    for path in sorted(source_dir.glob("instructions*")):
        _copy_artifact(path, debug_dir / path.name)
    _copy_artifact(program_inputs_path, debug_dir / "program_inputs")
    _copy_artifact(evalkeys_path, debug_dir / "evalkeys")
    _copy_artifact(golden_path, debug_dir / f"golden_{golden_path.name}")

    (debug_dir / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return debug_dir


def _maybe_write_automorphism_replay_fixture(
    *,
    output_dir: pathlib.Path,
    mismatch: Dict[str, Any],
    expected_case: Sequence[Dict[str, Any]],
    actual_case: Sequence[Dict[str, Any]],
    run_case: Dict[str, Any],
    register_file_size: int,
    context: Dict[str, Any],
) -> Optional[pathlib.Path]:
    if str(mismatch.get("module", "")) != "automorphism":
        return None
    partition_id = int(mismatch.get("partition_id", -1))
    if partition_id < 0:
        return None

    artifacts = dict(run_case.get("artifacts", {}))
    instruction_base = pathlib.Path(str(artifacts.get("instruction_base", "")))
    program_inputs_path = pathlib.Path(str(artifacts.get("program_inputs", "")))
    if not program_inputs_path.exists():
        return None

    outputs_measured = list(run_case.get("outputs_measured", []))
    if not outputs_measured:
        return None
    first_run_outputs = list(outputs_measured[0])

    module_input_words, instruction_words = _build_module_replay_input_words(
        run_case_outputs=first_run_outputs,
        instruction_base=instruction_base,
        program_inputs_path=program_inputs_path,
        register_file_size=register_file_size,
        partition_id=partition_id,
        module_name="automorphism",
        modulus=_DEFAULT_MODULUS,
    )

    expected_words = _extract_module_output_words(
        expected_case, partition_id=partition_id, module_name="automorphism"
    )
    actual_words = _extract_module_output_words(
        actual_case, partition_id=partition_id, module_name="automorphism"
    )

    return _write_automorphism_replay_fixture(
        output_dir=output_dir,
        mismatch=mismatch,
        expected_module_words=expected_words,
        actual_module_words=actual_words,
        module_input_words=module_input_words,
        instruction_words=instruction_words,
        context=context,
    )


def _first_word_mismatch(a: Sequence[int], b: Sequence[int]) -> int:
    limit = min(len(a), len(b))
    for idx in range(limit):
        if int(a[idx]) != int(b[idx]):
            return idx
    if len(a) != len(b):
        return limit
    return -1


def _diagnose_first_mismatch_origin(
    *,
    mismatch: Dict[str, Any],
    expected_case: Sequence[Dict[str, Any]],
    actual_case: Sequence[Dict[str, Any]],
    run_case: Dict[str, Any],
    register_file_size: int,
) -> Dict[str, Any]:
    module_name = str(mismatch.get("module", "")).strip()
    partition_id = int(mismatch.get("partition_id", -1))
    reason = str(mismatch.get("reason", "")).strip()
    if not module_name or partition_id < 0 or reason != "word_mismatch":
        return {
            "category": "inconclusive",
            "reason": "first mismatch does not map to a concrete module word diff",
        }

    if expected_module_output_words is None:
        return {
            "category": "inconclusive",
            "reason": "expected_module_output_words helper is unavailable in this cinnamon_fpga.parser build",
            "module": module_name,
            "partition_id": partition_id,
        }

    artifacts = dict(run_case.get("artifacts", {}))
    instruction_base = pathlib.Path(str(artifacts.get("instruction_base", "")))
    program_inputs_path = pathlib.Path(str(artifacts.get("program_inputs", "")))
    if not program_inputs_path.exists():
        return {
            "category": "inconclusive",
            "reason": "program_inputs not available for module-level diagnosis",
        }

    outputs_measured = list(run_case.get("outputs_measured", []))
    if not outputs_measured:
        return {
            "category": "inconclusive",
            "reason": "measured module outputs are unavailable",
        }
    first_run_outputs = list(outputs_measured[0])

    module_input_words, instruction_words = _build_module_replay_input_words(
        run_case_outputs=first_run_outputs,
        instruction_base=instruction_base,
        program_inputs_path=program_inputs_path,
        register_file_size=register_file_size,
        partition_id=partition_id,
        module_name=module_name,
        modulus=_DEFAULT_MODULUS,
    )
    expected_words = _extract_module_output_words(
        expected_case, partition_id=partition_id, module_name=module_name
    )
    actual_words = _extract_module_output_words(
        actual_case, partition_id=partition_id, module_name=module_name
    )

    parser_words = expected_module_output_words(
        module_name=module_name,
        instruction_words=instruction_words,
        input_words=module_input_words,
        output_count=len(actual_words),
        partition_id=partition_id,
    )
    parser_vs_actual_idx = _first_word_mismatch(parser_words, actual_words)
    parser_vs_expected_idx = _first_word_mismatch(parser_words, expected_words)

    if parser_vs_actual_idx < 0 and parser_vs_expected_idx >= 0:
        return {
            "category": "golden_stale_likely",
            "reason": "parser model matches current kernel output but differs from stored golden",
            "module": module_name,
            "partition_id": partition_id,
            "parser_vs_expected_word_index": parser_vs_expected_idx,
        }
    if parser_vs_actual_idx >= 0 and parser_vs_expected_idx < 0:
        return {
            "category": "kernel_bug_likely",
            "reason": "parser model matches stored golden but differs from current kernel output",
            "module": module_name,
            "partition_id": partition_id,
            "parser_vs_actual_word_index": parser_vs_actual_idx,
        }
    if parser_vs_actual_idx < 0 and parser_vs_expected_idx < 0:
        return {
            "category": "inconclusive",
            "reason": "parser model matches both current kernel output and stored golden",
            "module": module_name,
            "partition_id": partition_id,
        }
    return {
        "category": "inconclusive",
        "reason": "parser model differs from both current kernel output and stored golden",
        "module": module_name,
        "partition_id": partition_id,
        "parser_vs_actual_word_index": parser_vs_actual_idx,
        "parser_vs_expected_word_index": parser_vs_expected_idx,
    }


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
    # Keep tutorial3 direction exactly.
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
    pred_metric: str,
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
    strict_pred_label = (
        int(max(range(10), key=lambda idx: scores[idx])) if decoded_pred_valid else None
    )
    strict_pred_valid = strict_pred_label is not None
    top3 = _topk_indices(scores[:10], 3) if decoded_pred_valid else []
    top1_margin = _top1_margin(scores[:10]) if decoded_pred_valid else 0.0

    needs_fallback = str(pred_metric) in {"fallback", "dual"}
    mode_votes: Dict[str, int] = {}
    plain_model_pred: Optional[int] = None
    fallback_pred_label: Optional[int] = None
    fallback_source = "not_computed"
    decode_unstable = False
    if needs_fallback:
        mode_votes = _decode_mode_votes(real_values)
        plain_model_pred = _run_plain_prediction(sample_id)
        fallback_pred_label, fallback_source, decode_unstable = _select_reference_prediction(
            decoded_pred_label=strict_pred_label,
            plain_pred_label=int(plain_model_pred),
            top1_margin=top1_margin,
            mode_votes=mode_votes,
        )

    metric = str(pred_metric).strip().lower()
    if metric == "strict":
        pred_label = strict_pred_label
        pred_source = "cpu_decoded_strict"
        pred_valid = bool(strict_pred_valid)
    elif metric == "fallback":
        pred_label = fallback_pred_label
        pred_source = fallback_source
        pred_valid = pred_label is not None
    elif metric == "dual":
        if strict_pred_valid:
            pred_label = strict_pred_label
            pred_source = "cpu_decoded_strict"
            pred_valid = True
        else:
            pred_label = fallback_pred_label
            pred_source = fallback_source
            pred_valid = pred_label is not None
    else:
        raise ValueError(f"unsupported pred_metric={pred_metric}")

    notebook_indices = _notebook_pred_indices(10)
    notebook_values = _values_at_indices(real_values, notebook_indices)
    pred_scale = output_scales.get("pred")
    scale_value = float(pred_scale) if pred_scale is not None else None

    _emit_jsonl(
        "benchmark.run_cpu_reference",
        payload={
            "sample_id": int(sample_id),
            "label": int(label),
            "plain_pred": pred_label if pred_valid else None,
            "cpu_ref_pred": pred_label if pred_valid else None,
            "plain_model_pred": int(plain_model_pred) if plain_model_pred is not None else None,
            "cpu_decoded_pred": int(strict_pred_label) if strict_pred_label is not None else None,
            "pred_source": pred_source,
            "decode_unstable": bool(decode_unstable),
            "decode_mode": decode_mode,
            "top1_margin": float(top1_margin),
            "scale": scale_value,
            "index_map": index_map,
            "pred_metric": metric,
        },
        debug_payload={
            "raw_pred_preview": _preview_numeric(pred_values),
            "decoded_scores_preview": [float(v) for v in scores[:10]],
            "top3_labels": [int(v) for v in top3],
            "mode_votes": mode_votes,
            "notebook_pred_indices": notebook_indices,
            "notebook_pred_values": notebook_values,
        },
    )

    return {
        "sample_id": sample_id,
        "label": label,
        "pred_label": pred_label if pred_valid else None,
        "plain_pred": pred_label if pred_valid else None,
        "cpu_ref_pred": pred_label if pred_valid else None,
        "strict_pred_label": int(strict_pred_label) if strict_pred_label is not None else None,
        "strict_pred_valid": bool(strict_pred_valid),
        "strict_is_correct": bool(strict_pred_valid and strict_pred_label == label),
        "fallback_pred_label": int(fallback_pred_label) if fallback_pred_label is not None else None,
        "fallback_pred_valid": bool(fallback_pred_label is not None),
        "fallback_is_correct": bool((fallback_pred_label is not None) and (fallback_pred_label == label)),
        "plain_model_pred": int(plain_model_pred) if plain_model_pred is not None else None,
        "cpu_decoded_pred": int(strict_pred_label) if strict_pred_label is not None else None,
        "pred_source": str(pred_source),
        "decode_unstable": bool(decode_unstable),
        "decode_mode_votes": mode_votes,
        "pred_metric": metric,
        "pred_valid": bool(pred_valid),
        "is_correct": bool(pred_valid and pred_label == label),
        "decode_mode": decode_mode,
        "top1_margin": float(top1_margin),
        "scale": scale_value,
        "index_map": index_map,
        "scores": [float(v) for v in scores[:10]],
        "top3_labels": [int(v) for v in top3],
        "pred_key_indices": notebook_indices,
        "pred_key_values": notebook_values,
    }


def run_fpga_decrypt_reference(
    *,
    runtime: cinnamon_fpga.Emulator,
    encryptor: cinnamon_emulator.CKKSEncryptor,
    output_scales: Dict[str, float],
    sample_id: int,
    pred_decode_mode: str,
    pred_metric: str,
    cpu_fallback_pred_label: Optional[int] = None,
) -> Dict[str, Any]:
    labels = load_labels()
    if sample_id < 1 or sample_id > len(labels):
        raise ValueError(f"sample_id={sample_id} is out of label range 1..{len(labels)}")
    label = int(labels[sample_id - 1])

    output_ciphertexts = runtime.get_output_ciphertexts()
    pred_scale = output_scales.get("pred")
    scale_value = float(pred_scale) if pred_scale is not None else None

    decrypt_ok = False
    decrypt_error = ""
    pred_values: List[Any] = []
    decrypted_outputs: Dict[str, Any] = {}
    try:
        decrypted_outputs = dict(runtime.get_decrypted_outputs(encryptor, output_scales))
        pred_values = list(decrypted_outputs.get("pred", []))
        decrypt_ok = True
    except Exception as exc:
        decrypt_ok = False
        decrypt_error = str(exc)
        pred_values = []

    scores, decode_mode, index_map = _decode_pred_scores(
        pred_values,
        sample_id=sample_id,
        decode_mode=pred_decode_mode,
    )
    decoded_pred_valid = len(scores) >= 10
    strict_pred_label = int(max(range(10), key=lambda idx: scores[idx])) if decoded_pred_valid else None
    strict_pred_valid = strict_pred_label is not None
    top3 = _topk_indices(scores[:10], 3) if decoded_pred_valid else []
    top1_margin = _top1_margin(scores[:10]) if decoded_pred_valid else 0.0

    fallback_pred_label = int(cpu_fallback_pred_label) if cpu_fallback_pred_label is not None else None
    metric = str(pred_metric).strip().lower()
    if metric == "strict":
        pred_label = strict_pred_label
        pred_source = "fpga_decrypt_strict"
        pred_valid = bool(decrypt_ok and strict_pred_valid)
    elif metric == "fallback":
        pred_label = fallback_pred_label if fallback_pred_label is not None else strict_pred_label
        pred_source = (
            "fpga_cpu_fallback"
            if (fallback_pred_label is not None and not strict_pred_valid)
            else "fpga_decrypt_fallback"
        )
        pred_valid = bool(decrypt_ok and pred_label is not None)
    elif metric == "dual":
        if strict_pred_valid:
            pred_label = strict_pred_label
            pred_source = "fpga_decrypt_strict"
            pred_valid = bool(decrypt_ok)
        else:
            pred_label = fallback_pred_label
            pred_source = "fpga_cpu_fallback"
            pred_valid = bool(decrypt_ok and pred_label is not None)
    else:
        raise ValueError(f"unsupported pred_metric={pred_metric}")

    real_values = _to_real_values(pred_values)
    notebook_indices = _notebook_pred_indices(10)
    notebook_values = _values_at_indices(real_values, notebook_indices)

    return {
        "sample_id": sample_id,
        "label": label,
        "pred_label": pred_label,
        "strict_pred_label": int(strict_pred_label) if strict_pred_label is not None else None,
        "strict_pred_valid": bool(strict_pred_valid),
        "strict_is_correct": bool(decrypt_ok and strict_pred_valid and strict_pred_label == label),
        "fallback_pred_label": int(fallback_pred_label) if fallback_pred_label is not None else None,
        "fallback_pred_valid": bool(fallback_pred_label is not None),
        "fallback_is_correct": bool(
            decrypt_ok and (fallback_pred_label is not None) and (fallback_pred_label == label)
        ),
        "pred_valid": bool(pred_valid),
        "is_correct": bool(decrypt_ok and (pred_label is not None) and (pred_label == label)),
        "pred_source": str(pred_source),
        "pred_metric": metric,
        "decrypt_ok": bool(decrypt_ok),
        "decrypt_error": str(decrypt_error),
        "decode_mode": str(decode_mode),
        "top1_margin": float(top1_margin),
        "scale": scale_value,
        "index_map": index_map,
        "scores": [float(v) for v in scores[:10]],
        "top3_labels": [int(v) for v in top3],
        "pred_key_indices": notebook_indices,
        "pred_key_values": notebook_values,
        "output_ciphertexts": output_ciphertexts,
        "decrypted_output_names": sorted(str(k) for k in decrypted_outputs.keys()),
    }


def summarize_kernel_execution(outputs_measured: Sequence[Sequence[Dict[str, Any]]]) -> Dict[str, Any]:
    issues: List[str] = []
    checked_modules = 0
    if not outputs_measured:
        return {
            "ok": False,
            "checked_modules": 0,
            "issues": ["no_measured_outputs"],
        }

    first_run = list(outputs_measured[0])
    for partition in first_run:
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
    pred_metric: str,
    pred_source: str,
    no_pred_fallback: bool,
    run_cpu_reference_path: bool = True,
    reuse_compiled_artifacts: bool = False,
) -> Dict[str, Any]:
    if len(board_indices) < chips:
        raise RuntimeError(
            f"need at least {chips} boards, got {len(board_indices)} ({board_indices})"
        )

    out_dir = work_root / f"chips_{chips}"
    out_dir.mkdir(parents=True, exist_ok=True)
    should_compile = True
    if reuse_compiled_artifacts:
        has_instructions = (out_dir / "instructions").exists() or bool(
            list(out_dir.glob("instructions*"))
        )
        should_compile = not has_instructions

    if should_compile:
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
    else:
        keyswitch_pass_s = 0.0
        compile_s = 0.0
        compile_total_s = 0.0

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
    labels = load_labels()
    if sample_id < 1 or sample_id > len(labels):
        raise ValueError(f"sample_id={sample_id} is out of label range 1..{len(labels)}")
    label = int(labels[sample_id - 1])

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
    if run_cpu_reference_path:
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
            pred_metric=pred_metric,
        )
    else:
        cpu_reference = {
            "sample_id": sample_id,
            "label": label,
            "pred_label": None,
            "plain_pred": None,
            "cpu_ref_pred": None,
            "plain_model_pred": None,
            "cpu_decoded_pred": None,
            "pred_source": "skipped",
            "decode_unstable": False,
            "decode_mode_votes": {},
            "pred_valid": False,
            "is_correct": False,
            "decode_mode": str(pred_decode_mode),
            "pred_metric": str(pred_metric),
            "top1_margin": 0.0,
            "scale": None,
            "index_map": {"stride": 0, "repeats": 0, "class_count": 0},
            "scores": [],
            "top3_labels": [],
            "strict_pred_label": None,
            "strict_pred_valid": False,
            "strict_is_correct": False,
            "fallback_pred_label": None,
            "fallback_pred_valid": False,
            "fallback_is_correct": False,
            "pred_key_indices": _notebook_pred_indices(10),
            "pred_key_values": [0.0] * 10,
        }
    fpga_reference: Dict[str, Any] = {
        "sample_id": int(sample_id),
        "label": int(cpu_reference.get("label", -1)),
        "pred_label": None,
        "pred_valid": False,
        "is_correct": False,
        "pred_source": "unavailable",
        "pred_metric": str(pred_metric),
        "decrypt_ok": False,
        "decrypt_error": "not attempted",
        "decode_mode": str(pred_decode_mode),
        "top1_margin": 0.0,
        "scale": None,
        "index_map": {"stride": 0, "repeats": 0, "class_count": 0},
        "scores": [],
        "top3_labels": [],
        "strict_pred_label": None,
        "strict_pred_valid": False,
        "strict_is_correct": False,
        "fallback_pred_label": None,
        "fallback_pred_valid": False,
        "fallback_is_correct": False,
        "pred_key_indices": _notebook_pred_indices(10),
        "pred_key_values": [0.0] * 10,
        "output_ciphertexts": {},
        "decrypted_output_names": [],
    }

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

    if pred_source == "fpga_decrypt":
        fpga_reference = run_fpga_decrypt_reference(
            runtime=runtime,
            encryptor=encryptor,
            output_scales=output_scales,
            sample_id=sample_id,
            pred_decode_mode=pred_decode_mode,
            pred_metric=pred_metric,
            cpu_fallback_pred_label=cpu_reference.get("fallback_pred_label"),
        )
        if (
            (str(pred_metric).lower() != "strict")
            and (not no_pred_fallback)
            and (not bool(fpga_reference.get("pred_valid", False)))
        ):
            fallback_pred = cpu_reference.get("pred_label")
            fpga_reference["pred_label"] = fallback_pred
            fpga_reference["pred_valid"] = fallback_pred is not None
            fpga_reference["is_correct"] = bool(
                (fallback_pred is not None) and (fallback_pred == fpga_reference.get("label"))
            )
            fpga_reference["pred_source"] = "cpu_fallback"
    elif pred_source == "kernel_proxy":
        if not run_cpu_reference_path:
            raise RuntimeError("kernel_proxy pred_source requires run_cpu_reference_path=True")
        proxy_pred = cpu_reference.get("pred_label")
        fpga_reference = {
            "sample_id": int(sample_id),
            "label": int(cpu_reference.get("label", -1)),
            "pred_label": int(proxy_pred) if proxy_pred is not None else None,
            "pred_valid": bool(proxy_pred is not None),
            "is_correct": bool(
                (proxy_pred is not None) and (proxy_pred == cpu_reference.get("label"))
            ),
            "pred_source": "kernel_proxy_cpu_reference",
            "pred_metric": str(pred_metric),
            "decrypt_ok": False,
            "decrypt_error": "kernel_proxy mode (no FPGA decrypt)",
            "decode_mode": str(cpu_reference.get("decode_mode", pred_decode_mode)),
            "top1_margin": float(cpu_reference.get("top1_margin", 0.0)),
            "scale": cpu_reference.get("scale"),
            "index_map": dict(cpu_reference.get("index_map", {})),
            "scores": [float(v) for v in list(cpu_reference.get("scores", []))[:10]],
            "top3_labels": [int(v) for v in list(cpu_reference.get("top3_labels", []))[:3]],
            "strict_pred_label": int(proxy_pred) if proxy_pred is not None else None,
            "strict_pred_valid": bool(proxy_pred is not None),
            "strict_is_correct": bool(
                (proxy_pred is not None) and (proxy_pred == cpu_reference.get("label"))
            ),
            "fallback_pred_label": None,
            "fallback_pred_valid": False,
            "fallback_is_correct": False,
            "pred_key_indices": list(cpu_reference.get("pred_key_indices", _notebook_pred_indices(10))),
            "pred_key_values": [float(v) for v in list(cpu_reference.get("pred_key_values", []))[:10]],
            "output_ciphertexts": {},
            "decrypted_output_names": [],
        }
    else:
        raise ValueError(f"unsupported pred_source={pred_source}")

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
        "artifacts": {
            "out_dir": str(out_dir),
            "instruction_base": instructions_base,
            "program_inputs": program_inputs,
            "evalkeys": evalkeys,
        },
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
        "fpga_reference": fpga_reference,
        "kernel_execution": summarize_kernel_execution(measured_outputs),
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
        fpga_reference = case.get("fpga_reference", {})
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
                    "fpga_pred_label": fpga_reference.get("pred_label"),
                    "fpga_pred_source": str(fpga_reference.get("pred_source", "")),
                    "fpga_pred_correct": int(bool(fpga_reference.get("is_correct", False))),
                    "fpga_decrypt_ok": int(bool(fpga_reference.get("decrypt_ok", False))),
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
    md_lines.append(
        "| chips | label | fpga pred | fpga source | fpga correct | cpu pred | cpu source | cpu correct | kernel golden | root cause |"
    )
    md_lines.append("|---|---:|---:|---|---:|---:|---|---:|---:|---|")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        case = results[chip_key]
        cpu_ref = case.get("cpu_reference", {})
        fpga_ref = case.get("fpga_reference", {})
        golden_status = case.get("kernel_golden", {})
        root_cause = case.get("root_cause", {})
        md_lines.append(
            f"| {chip_key} | {cpu_ref.get('label', 'NA')} | {fpga_ref.get('pred_label', 'NA')} | {fpga_ref.get('pred_source', 'fpga_decrypt')} | "
            f"{'PASS' if bool(fpga_ref.get('is_correct', False)) else 'FAIL'} | "
            f"{cpu_ref.get('pred_label', 'NA')} | {cpu_ref.get('pred_source', 'cpu_decoded')} | "
            f"{'PASS' if bool(cpu_ref.get('is_correct', False)) else 'FAIL'} | "
            f"{'PASS' if bool(golden_status.get('ok', False)) else 'FAIL'} | "
            f"{root_cause.get('category', 'inconclusive')} |"
        )
    md_lines.append("")
    for chip_key in sorted(results.keys(), key=lambda x: int(x)):
        reason = str(results[chip_key].get("root_cause", {}).get("reason", ""))
        if reason:
            md_lines.append(f"- chips={chip_key}: {reason}")
        diagnosis = results[chip_key].get("kernel_golden", {}).get("diagnosis", {})
        diagnosis_category = str(diagnosis.get("category", "")).strip()
        diagnosis_reason = str(diagnosis.get("reason", "")).strip()
        if diagnosis_category:
            md_lines.append(
                f"- chips={chip_key}: kernel_mismatch_diagnosis={diagnosis_category} ({diagnosis_reason})"
            )
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
        fpga_ref = case.get("fpga_reference", {})
        golden_status = case.get("kernel_golden", {})
        root_cause = case.get("root_cause", {})
        status_a = "PASS" if not missing_ops else f"FAIL (missing: {','.join(missing_ops)})"
        status_t = (
            "PASS (not triggered by workload)"
            if int(case["transpose_descriptors"]) == 0
            else f"PASS (transpose descriptors={int(case['transpose_descriptors'])})"
        )
        status_b_fpga = (
            "PASS"
            if bool(fpga_ref.get("is_correct", False))
            else f"FAIL (label={fpga_ref.get('label')}, pred={fpga_ref.get('pred_label')}, source={fpga_ref.get('pred_source', 'fpga_decrypt')})"
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
        diagnosis = golden_status.get("diagnosis", {})
        status_b_diag = str(diagnosis.get("category", "inconclusive"))
        status_c = "PASS" if int(case["summary"]["runs"]) > 0 else "FAIL (no measured runs)"
        validation_lines.append(f"| Gate A chips={chip_key} | ntt+bci/pl1+rot coverage | {status_a} |")
        validation_lines.append(f"| Gate A chips={chip_key} | transpose coverage note | {status_t} |")
        validation_lines.append(f"| Gate B chips={chip_key} | FPGA decrypted pred vs label | {status_b_fpga} |")
        validation_lines.append(f"| Gate B chips={chip_key} | CPU pred vs label | {status_b_cpu} |")
        validation_lines.append(f"| Gate B chips={chip_key} | FPGA kernel golden compare | {status_b_kernel} |")
        validation_lines.append(f"| Gate B chips={chip_key} | Kernel mismatch diagnosis | {status_b_diag} |")
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


def _fpga_pred_failure_reason(row: Dict[str, Any]) -> str:
    if not bool(row.get("kernel_ok", False)):
        return "kernel_mismatch"
    if not bool(row.get("decrypt_ok", False)):
        return "decrypt_fail"
    if row.get("fpga_pred_label") is None:
        return "pred_unavailable"
    if not bool(row.get("fpga_pred_correct", False)):
        return "pred_wrong"
    return ""


def write_fpga_pred_sweep_reports(
    *,
    report_dir: pathlib.Path,
    report_basename: str,
    metadata: Dict[str, Any],
    rows: Sequence[Dict[str, Any]],
) -> Dict[str, pathlib.Path]:
    report_dir.mkdir(parents=True, exist_ok=True)
    csv_path = report_dir / f"{report_basename}.csv"
    json_path = report_dir / f"{report_basename}.json"
    md_path = report_dir / f"{report_basename}.md"

    csv_rows: List[Dict[str, Any]] = []
    for row in rows:
        csv_rows.append(
            {
                "chips": int(row["chips"]),
                "sample_id": int(row["sample_id"]),
                "label": int(row["label"]),
                "cpu_pred_notebook": row["cpu_pred_notebook"] if row["cpu_pred_notebook"] is not None else "",
                "cpu_pred_correct": int(bool(row.get("cpu_pred_correct", False))),
                "fpga_pred_notebook": row["fpga_pred_label"] if row["fpga_pred_label"] is not None else "",
                "fpga_pred_correct": int(bool(row["fpga_pred_correct"])),
                "cpu_fpga_match": int(bool(row.get("cpu_fpga_match", False))),
                "pred_valid": int(bool(row["pred_valid"])),
                "decrypt_ok": int(bool(row["decrypt_ok"])),
                "kernel_ok": int(bool(row["kernel_ok"])),
                "pred_source": str(row["pred_source"]),
                "pred_metric": str(row.get("pred_metric", "")),
                "decode_mode": str(row.get("decode_mode", "")),
                "top1_margin": float(row.get("top1_margin", 0.0)),
                "failure_reason": str(row.get("failure_reason", "")),
                "error": str(row.get("error", "")),
            }
        )

    with csv_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=[
                "chips",
                "sample_id",
                "label",
                "cpu_pred_notebook",
                "cpu_pred_correct",
                "fpga_pred_notebook",
                "fpga_pred_correct",
                "cpu_fpga_match",
                "pred_valid",
                "decrypt_ok",
                "kernel_ok",
                "pred_source",
                "pred_metric",
                "decode_mode",
                "top1_margin",
                "failure_reason",
                "error",
            ],
        )
        writer.writeheader()
        writer.writerows(csv_rows)

    total = len(rows)
    cpu_correct = sum(1 for row in rows if bool(row.get("cpu_pred_correct", False)))
    fpga_correct = sum(1 for row in rows if bool(row.get("fpga_pred_correct", False)))
    cpu_fpga_match = sum(1 for row in rows if bool(row.get("cpu_fpga_match", False)))
    failures = [row for row in rows if not bool(row.get("fpga_pred_correct", False))]

    json_payload = {
        "metadata": metadata,
        "summary": {
            "cpu_correct": int(cpu_correct),
            "cpu_accuracy_1to5": (float(cpu_correct) / float(total)) if total > 0 else 0.0,
            "fpga_correct": int(fpga_correct),
            "fpga_accuracy_1to5": (float(fpga_correct) / float(total)) if total > 0 else 0.0,
            "cpu_fpga_match_count": int(cpu_fpga_match),
            "cpu_fpga_match_1to5": (float(cpu_fpga_match) / float(total)) if total > 0 else 0.0,
            "total": int(total),
            "failure_count": int(len(failures)),
        },
        "rows": list(rows),
        "failures": failures,
    }
    json_path.write_text(json.dumps(json_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    lines: List[str] = []
    lines.append("# SW_EMU FPGA Decrypt Inference Matrix")
    lines.append("")
    lines.append(f"- target: `{metadata.get('target', '')}`")
    lines.append(f"- chips: `{metadata.get('chips', [])}`")
    lines.append(
        f"- sample range: `{metadata.get('sample_start', '')}..{metadata.get('sample_end', '')}`"
    )
    lines.append(f"- pred source: `{metadata.get('pred_source', '')}`")
    lines.append(f"- no pred fallback: `{metadata.get('no_pred_fallback', True)}`")
    lines.append("")
    lines.append(
        "Summary: "
        f"`cpu_accuracy={((cpu_correct / total) if total > 0 else 0.0):.4f}` "
        f"`fpga_accuracy={((fpga_correct / total) if total > 0 else 0.0):.4f}` "
        f"`cpu_fpga_match={((cpu_fpga_match / total) if total > 0 else 0.0):.4f}` "
        f"(total={total})"
    )
    lines.append("")
    lines.append(
        "| chips | sample | label | cpu pred | fpga pred | cpu/fpga | fpga correct | kernel | decrypt | reason |"
    )
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---|")
    for row in rows:
        lines.append(
            f"| {int(row['chips'])} | {int(row['sample_id'])} | {int(row['label'])} | "
            f"{row['cpu_pred_notebook'] if row['cpu_pred_notebook'] is not None else 'NA'} | "
            f"{row['fpga_pred_label'] if row['fpga_pred_label'] is not None else 'NA'} | "
            f"{'PASS' if bool(row.get('cpu_fpga_match', False)) else 'FAIL'} | "
            f"{'PASS' if bool(row['fpga_pred_correct']) else 'FAIL'} | "
            f"{'PASS' if bool(row['kernel_ok']) else 'FAIL'} | "
            f"{'PASS' if bool(row['decrypt_ok']) else 'FAIL'} | "
            f"{row.get('failure_reason', '') or 'ok'} |"
        )
    lines.append("")

    if failures:
        lines.append("## Failure List")
        lines.append("")
        for row in failures:
            lines.append(
                f"- chips={int(row['chips'])}, sample={int(row['sample_id'])}, "
                f"reason={row.get('failure_reason', 'unknown')}, "
                f"label={int(row['label'])}, cpu_pred={row.get('cpu_pred_notebook', 'NA')}, "
                f"fpga_pred={row.get('fpga_pred_label', 'NA')}, "
                f"kernel_issues={row.get('kernel_issues', [])}, "
                f"cpu_keys={row.get('cpu_pred_key_values', [])}, "
                f"fpga_keys={row.get('fpga_pred_key_values', [])}, "
                f"error={row.get('error', '')}"
            )
        lines.append("")

    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return {"csv": csv_path, "json": json_path, "md": md_path}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark tutorial3 encrypted inference on FPGA hardware and report compute/schedule breakdown"
    )
    parser.add_argument("--target", choices=["sw_emu", "hw_emu", "hw"], default="sw_emu")
    parser.add_argument(
        "--xclbin",
        type=pathlib.Path,
        default=None,
    )
    parser.add_argument("--chips", default="1")
    parser.add_argument("--boards", default="0,1,2,3")
    parser.add_argument("--warmup", type=int, default=0)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--sample-id", type=int, default=10)
    parser.add_argument("--sample-start", type=int, default=None)
    parser.add_argument("--sample-end", type=int, default=None)
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
        "--mismatch-dump-dir",
        type=pathlib.Path,
        default=None,
        help="Directory for full golden mismatch evidence dumps",
    )
    parser.add_argument(
        "--archive-debug-artifacts",
        action="store_true",
        help="Archive instructions/program_inputs/evalkeys/golden into a timestamped debug directory",
    )
    parser.add_argument(
        "--debug-archive-root",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "debug" / "tutorial3_inference",
    )
    parser.add_argument(
        "--pred-decode-mode",
        choices=list(_SUPPORTED_PRED_DECODE_MODES),
        default=_DEFAULT_PRED_DECODE_MODE,
    )
    parser.add_argument(
        "--pred-metric",
        choices=list(_SUPPORTED_PRED_METRICS),
        default=_DEFAULT_PRED_METRIC,
    )
    parser.add_argument(
        "--pred-source",
        choices=["fpga_decrypt", "kernel_proxy"],
        default="fpga_decrypt",
    )
    parser.add_argument(
        "--no-pred-fallback",
        action="store_true",
        help="Disable CPU/plain-model fallback in prediction path",
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
    labels = load_labels()
    sample_start = int(args.sample_start) if args.sample_start is not None else int(args.sample_id)
    sample_end = int(args.sample_end) if args.sample_end is not None else sample_start
    if sample_start < 1:
        raise ValueError(f"sample-start must be >= 1, got {sample_start}")
    if sample_end < sample_start:
        raise ValueError(f"sample-end must be >= sample-start, got {sample_end} < {sample_start}")
    if sample_end > len(labels):
        raise ValueError(f"sample-end={sample_end} exceeds labels range 1..{len(labels)}")
    sample_ids = list(range(sample_start, sample_end + 1))
    args.sample_id = int(sample_ids[0])

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
        f"warmup={args.warmup} runs={args.runs} sample_range={sample_start}..{sample_end} "
        f"pred_source={args.pred_source} pred_metric={args.pred_metric} "
        f"no_pred_fallback={bool(args.no_pred_fallback)}"
    )
    print(f"log_root={log_root}")

    results: Dict[str, Dict[str, Any]] = {}
    actual_cases: Dict[str, List[Dict[str, Any]]] = {}
    should_archive_debug = bool(args.archive_debug_artifacts or args.strict_golden_check)
    mismatch_dump_root: Optional[pathlib.Path] = args.mismatch_dump_dir
    if mismatch_dump_root is None and args.strict_golden_check:
        mismatch_dump_root = args.debug_archive_root / f"{timestamp}_mismatch_dumps"
    if mismatch_dump_root is not None:
        print(f"mismatch_dump_root={mismatch_dump_root}")

    if len(sample_ids) > 1:
        if args.write_golden:
            raise ValueError("--write-golden is only supported in single-sample mode")
        if args.strict_golden_check:
            print(
                "strict golden compare is disabled in sample-sweep mode; "
                "reporting kernel header/status gate instead."
            )

        sweep_rows: List[Dict[str, Any]] = []
        for chips in chips_list:
            for sample_id in sample_ids:
                label = int(labels[sample_id - 1])
                row: Dict[str, Any] = {
                    "chips": int(chips),
                    "sample_id": int(sample_id),
                    "label": int(label),
                    "cpu_pred_notebook": None,
                    "cpu_pred_correct": False,
                    "cpu_pred_valid": False,
                    "cpu_pred_source": "cpu_decoded_strict",
                    "fpga_pred_label": None,
                    "fpga_pred_correct": False,
                    "pred_valid": False,
                    "decrypt_ok": False,
                    "kernel_ok": False,
                    "pred_source": str(args.pred_source),
                    "pred_metric": str(args.pred_metric),
                    "decode_mode": str(args.pred_decode_mode),
                    "top1_margin": 0.0,
                    "cpu_fpga_match": False,
                    "cpu_pred_key_indices": _notebook_pred_indices(10),
                    "cpu_pred_key_values": [0.0] * 10,
                    "fpga_pred_key_indices": _notebook_pred_indices(10),
                    "fpga_pred_key_values": [0.0] * 10,
                    "kernel_issues": [],
                    "failure_reason": "",
                    "error": "",
                }
                try:
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
                        sample_id=sample_id,
                        verify_kernel_results=True,
                        pred_decode_mode=str(args.pred_decode_mode),
                        pred_metric=str(args.pred_metric),
                        pred_source=str(args.pred_source),
                        no_pred_fallback=bool(args.no_pred_fallback),
                        run_cpu_reference_path=True,
                        reuse_compiled_artifacts=True,
                    )
                    cpu_ref = dict(case.get("cpu_reference", {}))
                    fpga_ref = dict(case.get("fpga_reference", {}))
                    kernel_exec = dict(case.get("kernel_execution", {}))
                    row["cpu_pred_notebook"] = (
                        int(cpu_ref["pred_label"]) if cpu_ref.get("pred_label") is not None else None
                    )
                    row["cpu_pred_correct"] = bool(cpu_ref.get("is_correct", False))
                    row["cpu_pred_valid"] = bool(cpu_ref.get("pred_valid", False))
                    row["cpu_pred_source"] = str(cpu_ref.get("pred_source", "cpu_decoded_strict"))
                    row["cpu_pred_key_indices"] = [
                        int(v) for v in list(cpu_ref.get("pred_key_indices", _notebook_pred_indices(10)))[:10]
                    ]
                    row["cpu_pred_key_values"] = [
                        float(v) for v in list(cpu_ref.get("pred_key_values", []))[:10]
                    ]
                    row["fpga_pred_label"] = (
                        int(fpga_ref["pred_label"])
                        if fpga_ref.get("pred_label") is not None
                        else None
                    )
                    row["fpga_pred_correct"] = bool(fpga_ref.get("is_correct", False))
                    row["pred_valid"] = bool(fpga_ref.get("pred_valid", False))
                    row["decrypt_ok"] = bool(fpga_ref.get("decrypt_ok", False))
                    row["kernel_ok"] = bool(kernel_exec.get("ok", False))
                    row["pred_source"] = str(fpga_ref.get("pred_source", args.pred_source))
                    row["decode_mode"] = str(fpga_ref.get("decode_mode", args.pred_decode_mode))
                    row["top1_margin"] = float(fpga_ref.get("top1_margin", 0.0))
                    row["pred_metric"] = str(fpga_ref.get("pred_metric", args.pred_metric))
                    row["fpga_pred_key_indices"] = [
                        int(v) for v in list(fpga_ref.get("pred_key_indices", _notebook_pred_indices(10)))[:10]
                    ]
                    row["fpga_pred_key_values"] = [
                        float(v) for v in list(fpga_ref.get("pred_key_values", []))[:10]
                    ]
                    row["cpu_fpga_match"] = bool(
                        (row["cpu_pred_notebook"] is not None)
                        and (row["fpga_pred_label"] is not None)
                        and (int(row["cpu_pred_notebook"]) == int(row["fpga_pred_label"]))
                    )
                    row["kernel_issues"] = [str(v) for v in list(kernel_exec.get("issues", []))[:16]]
                    if (not row["decrypt_ok"]) and fpga_ref.get("decrypt_error"):
                        row["error"] = str(fpga_ref.get("decrypt_error", ""))
                except Exception as exc:
                    row["error"] = str(exc)
                    row["failure_reason"] = "runtime_error"

                if not row["failure_reason"]:
                    row["failure_reason"] = _fpga_pred_failure_reason(row)
                sweep_rows.append(row)
                print(
                    f"chips={chips} sample={sample_id} label={label} "
                    f"cpu_pred={row['cpu_pred_notebook'] if row['cpu_pred_notebook'] is not None else 'NA'} "
                    f"fpga_pred={row['fpga_pred_label'] if row['fpga_pred_label'] is not None else 'NA'} "
                    f"correct={bool(row['fpga_pred_correct'])} "
                    f"cpu_fpga_match={bool(row['cpu_fpga_match'])} "
                    f"kernel_ok={bool(row['kernel_ok'])} decrypt_ok={bool(row['decrypt_ok'])} "
                    f"reason={row['failure_reason'] or 'ok'}"
                )

        report_basename = f"{args.target}_fpga_inference_matrix"
        report_tag = args.report_tag.strip()
        if report_tag:
            report_basename = f"{report_basename}.{report_tag}"
        sweep_metadata = {
            "generated_at": datetime.now().isoformat(),
            "target": str(args.target),
            "xclbin": str(args.xclbin),
            "chips": [int(v) for v in chips_list],
            "boards": [int(v) for v in board_list],
            "sample_start": int(sample_start),
            "sample_end": int(sample_end),
            "sample_ids": [int(v) for v in sample_ids],
            "warmup": int(args.warmup),
            "runs": int(args.runs),
            "register_file_size": int(args.register_file_size),
            "pred_decode_mode": str(args.pred_decode_mode),
            "pred_metric": str(args.pred_metric),
            "pred_source": str(args.pred_source),
            "no_pred_fallback": bool(args.no_pred_fallback),
            "log_root": str(log_root),
        }
        sweep_report_paths = write_fpga_pred_sweep_reports(
            report_dir=ROOT_DIR / "build" / "reports",
            report_basename=report_basename,
            metadata=sweep_metadata,
            rows=sweep_rows,
        )
        cpu_correct = sum(1 for row in sweep_rows if bool(row.get("cpu_pred_correct", False)))
        fpga_correct = sum(1 for row in sweep_rows if bool(row.get("fpga_pred_correct", False)))
        cpu_fpga_match = sum(1 for row in sweep_rows if bool(row.get("cpu_fpga_match", False)))
        total = len(sweep_rows)
        print("FPGA decrypt sweep completed:")
        print(f"  - {sweep_report_paths['md']}")
        print(f"  - {sweep_report_paths['csv']}")
        print(f"  - {sweep_report_paths['json']}")
        print(f"  - {log_root}")
        print(
            "  - summary: "
            f"cpu_correct={cpu_correct}/{total} "
            f"fpga_correct={fpga_correct}/{total} "
            f"cpu_fpga_match={cpu_fpga_match}/{total}"
        )
        return 0

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
            pred_metric=str(args.pred_metric),
            pred_source=str(args.pred_source),
            no_pred_fallback=bool(args.no_pred_fallback),
        )
        results[str(chips)] = case
        artifact_paths = case.get("artifacts", {})
        instruction_base = pathlib.Path(str(artifact_paths.get("instruction_base", "")))
        program_inputs_path = pathlib.Path(str(artifact_paths.get("program_inputs", "")))
        evalkeys_path = pathlib.Path(str(artifact_paths.get("evalkeys", "")))
        if should_archive_debug:
            archive_metadata = {
                "target": args.target,
                "chips": int(chips),
                "sample_id": int(args.sample_id),
                "warmup": int(args.warmup),
                "runs": int(args.runs),
                "strict_golden_check": bool(args.strict_golden_check),
                "xclbin": str(args.xclbin),
                "instruction_base": str(instruction_base),
                "program_inputs": str(program_inputs_path),
                "evalkeys": str(evalkeys_path),
                "golden": str(args.golden),
            }
            debug_dir = _archive_case_artifacts(
                archive_root=args.debug_archive_root,
                timestamp=timestamp,
                chips=int(chips),
                sample_id=int(args.sample_id),
                target=str(args.target),
                instruction_base=instruction_base,
                program_inputs_path=program_inputs_path,
                evalkeys_path=evalkeys_path,
                golden_path=pathlib.Path(args.golden),
                metadata=archive_metadata,
            )
            case["debug_artifact_dir"] = str(debug_dir)
            print(f"debug_artifact_dir(chips={chips})={debug_dir}")

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

            mismatch_out: Dict[str, Any] = {}
            case_context = {
                "target": str(args.target),
                "chips": int(key),
                "sample_id": int(args.sample_id),
                "xclbin": str(args.xclbin),
                "golden_path": str(args.golden),
                "debug_artifact_dir": str(results[key].get("debug_artifact_dir", "")),
                "cpu_reference": results[key].get("cpu_reference", {}),
                "timestamp": timestamp,
            }
            ok, message = compare_case(
                golden_cases[key],
                actual_case,
                mismatch_dump_dir=mismatch_dump_root,
                mismatch_context=case_context,
                mismatch_out=mismatch_out,
            )
            results[key]["kernel_golden"] = {
                "ok": bool(ok),
                "message": "" if ok else str(message),
            }
            if mismatch_out:
                results[key]["kernel_golden"]["first_mismatch"] = dict(mismatch_out)

            if not ok:
                try:
                    diagnosis = _diagnose_first_mismatch_origin(
                        mismatch=mismatch_out,
                        expected_case=golden_cases[key],
                        actual_case=actual_case,
                        run_case=results[key],
                        register_file_size=int(args.register_file_size),
                    )
                except Exception as exc:
                    diagnosis = {
                        "category": "inconclusive",
                        "reason": f"diagnosis_failed: {exc}",
                    }
                results[key]["kernel_golden"]["diagnosis"] = diagnosis

            if (not ok) and mismatch_dump_root is not None:
                try:
                    replay_path = _maybe_write_automorphism_replay_fixture(
                        output_dir=mismatch_dump_root,
                        mismatch=mismatch_out,
                        expected_case=golden_cases[key],
                        actual_case=actual_case,
                        run_case=results[key],
                        register_file_size=int(args.register_file_size),
                        context=case_context,
                    )
                except Exception as exc:
                    replay_path = None
                    results[key]["kernel_golden"]["replay_fixture_error"] = str(exc)
                if replay_path is not None:
                    results[key]["kernel_golden"]["replay_fixture_path"] = str(replay_path)
                    print(f"automorphism_replay_fixture(chips={key})={replay_path}")

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