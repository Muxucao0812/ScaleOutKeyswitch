from __future__ import annotations

import json
import os
import pathlib
import time
from typing import Any, Dict, List, Sequence, Tuple

_JSONL_ENV = "CINNAMON_FPGA_JSONL_LOG"
_JSONL_DEBUG_ENV = "CINNAMON_FPGA_JSONL_DEBUG_LOG"
_JSONL_DEBUG_FLAG_ENV = "CINNAMON_FPGA_JSONL_DEBUG"
_DEBUG_PREVIEW_WORDS = 8


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
    debug_payload: Dict[str, Any] | None = None,
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


def _normalize_int_list(values: Sequence[Any]) -> List[int]:
    return [int(v) for v in values]


def normalize_kernel_outputs(outputs: Sequence[Dict[str, Any]]) -> List[Dict[str, Any]]:
    normalized: List[Dict[str, Any]] = []
    for partition in sorted(outputs, key=lambda item: int(item["partition_id"])):
        modules = sorted(partition.get("module_results", []), key=lambda item: str(item["module"]))
        norm_modules = []
        for module_entry in modules:
            norm_modules.append(
                {
                    "module": str(module_entry["module"]),
                    "kernel_name": str(module_entry["kernel_name"]),
                    "opcode_counts": {
                        str(k): int(v)
                        for k, v in sorted(module_entry.get("opcode_counts", {}).items())
                    },
                    "output_words": _normalize_int_list(module_entry.get("output_words", [])),
                }
            )

        host_sync = partition.get("host_sync", {})
        normalized.append(
            {
                "partition_id": int(partition["partition_id"]),
                "instruction_count": int(partition["instruction_count"]),
                "input_count": int(partition["input_count"]),
                "opcode_counts": {
                    str(k): int(v) for k, v in sorted(partition.get("opcode_counts", {}).items())
                },
                "module_results": norm_modules,
                "host_sync": {
                    "module": str(host_sync.get("module", "")),
                    "opcode_counts": {
                        str(k): int(v) for k, v in sorted(host_sync.get("opcode_counts", {}).items())
                    },
                    "comm_token": int(host_sync.get("comm_token", 0)),
                },
            }
        )
    return normalized


def load_golden(path: str | pathlib.Path) -> Dict[str, Any]:
    file_path = pathlib.Path(path)
    return json.loads(file_path.read_text(encoding="utf-8"))


def write_golden(path: str | pathlib.Path, payload: Dict[str, Any]) -> None:
    file_path = pathlib.Path(path)
    file_path.parent.mkdir(parents=True, exist_ok=True)
    file_path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _first_word_mismatch(expected: Sequence[int], actual: Sequence[int]) -> int:
    limit = min(len(expected), len(actual))
    for idx in range(limit):
        if int(expected[idx]) != int(actual[idx]):
            return idx
    if len(expected) != len(actual):
        return limit
    return -1


def _kernel_header(words: Sequence[int]) -> Dict[str, Any]:
    return {
        "kernel_status": int(words[0]) if len(words) > 0 else None,
        "kernel_executed": int(words[1]) if len(words) > 1 else None,
        "kernel_register_count": int(words[2]) if len(words) > 2 else None,
        "kernel_module_id": int(words[3]) if len(words) > 3 else None,
        "kernel_partition_id": int(words[4]) if len(words) > 4 else None,
        "kernel_trace_acc": int(words[5]) if len(words) > 5 else None,
    }


def _first_mismatch_summary(
    expected_norm: Sequence[Dict[str, Any]],
    actual_norm: Sequence[Dict[str, Any]],
) -> Dict[str, Any]:
    expected_partitions = {int(item["partition_id"]): item for item in expected_norm}
    actual_partitions = {int(item["partition_id"]): item for item in actual_norm}
    all_partition_ids = sorted(set(expected_partitions).union(actual_partitions))

    for partition_id in all_partition_ids:
        expected_partition = expected_partitions.get(partition_id)
        actual_partition = actual_partitions.get(partition_id)
        if expected_partition is None or actual_partition is None:
            return {
                "partition_id": int(partition_id),
                "module": None,
                "word_index": None,
                "reason": "partition_missing",
                "expected_present": expected_partition is not None,
                "actual_present": actual_partition is not None,
            }

        expected_modules = {
            str(item["module"]): item for item in expected_partition.get("module_results", [])
        }
        actual_modules = {
            str(item["module"]): item for item in actual_partition.get("module_results", [])
        }
        all_modules = sorted(set(expected_modules).union(actual_modules))
        for module_name in all_modules:
            expected_module = expected_modules.get(module_name)
            actual_module = actual_modules.get(module_name)
            if expected_module is None or actual_module is None:
                return {
                    "partition_id": int(partition_id),
                    "module": str(module_name),
                    "word_index": None,
                    "reason": "module_missing",
                    "expected_present": expected_module is not None,
                    "actual_present": actual_module is not None,
                }

            expected_words = _normalize_int_list(expected_module.get("output_words", []))
            actual_words = _normalize_int_list(actual_module.get("output_words", []))
            mismatch_idx = _first_word_mismatch(expected_words, actual_words)
            if mismatch_idx >= 0:
                expected_word = (
                    int(expected_words[mismatch_idx]) if mismatch_idx < len(expected_words) else None
                )
                actual_word = int(actual_words[mismatch_idx]) if mismatch_idx < len(actual_words) else None
                return {
                    "partition_id": int(partition_id),
                    "module": str(module_name),
                    "word_index": int(mismatch_idx),
                    "reason": "word_mismatch",
                    "expected_word": expected_word,
                    "actual_word": actual_word,
                    "expected_word_count": len(expected_words),
                    "actual_word_count": len(actual_words),
                    "expected_kernel_header": _kernel_header(expected_words),
                    "actual_kernel_header": _kernel_header(actual_words),
                    "expected_preview": [
                        int(word) for word in expected_words[:_DEBUG_PREVIEW_WORDS]
                    ],
                    "actual_preview": [int(word) for word in actual_words[:_DEBUG_PREVIEW_WORDS]],
                }

    return {
        "partition_id": None,
        "module": None,
        "word_index": None,
        "reason": "normalized_payload_diff",
        "expected_partition_count": len(expected_norm),
        "actual_partition_count": len(actual_norm),
    }


def compare_case(
    expected_outputs: Sequence[Dict[str, Any]],
    actual_outputs: Sequence[Dict[str, Any]],
) -> Tuple[bool, str]:
    expected_norm = normalize_kernel_outputs(expected_outputs)
    actual_norm = normalize_kernel_outputs(actual_outputs)
    if expected_norm == actual_norm:
        return True, ""

    mismatch = _first_mismatch_summary(expected_norm, actual_norm)
    _emit_jsonl(
        "golden.compare_case_mismatch",
        payload={
            "partition_id": mismatch.get("partition_id"),
            "module": mismatch.get("module"),
            "word_index": mismatch.get("word_index"),
            "reason": mismatch.get("reason"),
            "kernel_status": mismatch.get("actual_kernel_header", {}).get("kernel_status")
            if isinstance(mismatch.get("actual_kernel_header"), dict)
            else None,
            "kernel_executed": mismatch.get("actual_kernel_header", {}).get("kernel_executed")
            if isinstance(mismatch.get("actual_kernel_header"), dict)
            else None,
            "kernel_register_count": mismatch.get("actual_kernel_header", {}).get(
                "kernel_register_count"
            )
            if isinstance(mismatch.get("actual_kernel_header"), dict)
            else None,
            "kernel_module_id": mismatch.get("actual_kernel_header", {}).get("kernel_module_id")
            if isinstance(mismatch.get("actual_kernel_header"), dict)
            else None,
            "kernel_partition_id": mismatch.get("actual_kernel_header", {}).get(
                "kernel_partition_id"
            )
            if isinstance(mismatch.get("actual_kernel_header"), dict)
            else None,
            "kernel_trace_acc": mismatch.get("actual_kernel_header", {}).get("kernel_trace_acc")
            if isinstance(mismatch.get("actual_kernel_header"), dict)
            else None,
        },
        debug_payload={
            "mismatch_summary": mismatch,
        },
    )

    return (
        False,
        "Expected and actual kernel outputs differ.\nfirst_mismatch="
        + json.dumps(mismatch, sort_keys=True),
    )
