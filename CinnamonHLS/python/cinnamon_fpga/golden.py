from __future__ import annotations

import json
import pathlib
from typing import Any, Dict, List, Sequence, Tuple


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


def compare_case(
    expected_outputs: Sequence[Dict[str, Any]],
    actual_outputs: Sequence[Dict[str, Any]],
) -> Tuple[bool, str]:
    expected_norm = normalize_kernel_outputs(expected_outputs)
    actual_norm = normalize_kernel_outputs(actual_outputs)
    if expected_norm == actual_norm:
        return True, ""

    return (
        False,
        "Expected and actual kernel outputs differ.\n"
        + "expected="
        + json.dumps(expected_norm, sort_keys=True)
        + "\nactual="
        + json.dumps(actual_norm, sort_keys=True),
    )
