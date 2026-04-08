from __future__ import annotations

import copy
import dataclasses
import os
import pathlib
import re
import time
from typing import Any, Dict, List, Optional, Sequence

from .parser import (
    encode_stream_token,
    expected_host_comm_token,
    host_managed_module_name,
    load_instruction_streams,
    module_kernels,
    module_order,
    parse_program_inputs,
    split_stream_by_module,
    summarize_opcodes,
)
from .pyxrt_runner import (
    DispatchConfig,
    PartitionDispatchResult,
    XRTUnavailableError,
    run_dispatches,
)

_INPUT_MAGIC = 0x43494E4E414D4F4E  # "CINNAMON"
_DEFAULT_MODULUS = 268042241
_OUTPUT_HEADER_WORDS = 6
_MAX_REGISTERS = 2048
_MODULE_IDS = {
    "memory": 1,
    "arithmetic": 2,
    "montgomery": 3,
    "ntt": 4,
    "base_conv": 5,
    "automorphism": 6,
    "transpose": 7,
}
_PROGRAM_INPUT_ENTRY_RE = re.compile(
    r"^\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*\[([^\]]*)\]\s*$"
)


def _maybe_import_emulator():
    try:
        import cinnamon_emulator  # type: ignore

        return cinnamon_emulator
    except Exception:
        return None


def _resolve_board_indices(num_partitions: int, board_indices: Optional[Sequence[int]]) -> List[int]:
    if board_indices is not None:
        if len(board_indices) < num_partitions:
            raise ValueError(
                f"Requested {num_partitions} partitions but only {len(board_indices)} board indices were provided"
            )
        return list(board_indices)

    env = os.getenv("CINNAMON_FPGA_BOARDS", "").strip()
    if env:
        parsed = [int(x.strip()) for x in env.split(",") if x.strip()]
        if len(parsed) < num_partitions:
            raise ValueError(
                "CINNAMON_FPGA_BOARDS does not contain enough boards for requested partitions"
            )
        return parsed

    return list(range(num_partitions))


def _bounded_register_count(register_file_size: int) -> int:
    return min(max(int(register_file_size), 0), _MAX_REGISTERS)


def _build_stream_table_words(
    program_input_sections: Dict[str, List[str]],
    partition_id: int,
) -> List[int]:
    section_ids = {
        "ciphertext": 1,
        "plaintext": 2,
        "scalar": 3,
        "output": 4,
        "evalkey": 5,
    }

    token_map: Dict[int, int] = {}
    for section_name in ("ciphertext", "plaintext", "scalar", "output", "evalkey"):
        entries = program_input_sections.get(section_name, [])
        section_id = section_ids[section_name]
        for entry_idx, line in enumerate(entries):
            match = _PROGRAM_INPUT_ENTRY_RE.match(line)
            if not match:
                continue
            stream_name = match.group(1).strip()
            rns_raw = match.group(3).strip()
            rns_ids = [int(token.strip()) for token in rns_raw.split(",") if token.strip()]
            if not rns_ids:
                rns_ids = [0]
            for limb_index in rns_ids:
                key = encode_stream_token(f"{stream_name}({limb_index})")
                free_key = encode_stream_token(f"{stream_name}({limb_index}){{F}}")
                value = (
                    ((section_id & 0xFF) << 56)
                    | ((entry_idx & 0xFFFF) << 32)
                    | ((int(limb_index) & 0xFFFF) << 16)
                    | (int(partition_id) & 0xFFFF)
                )
                token_map[key] = value
                token_map[free_key] = value

    words: List[int] = [len(token_map)]
    for key, value in sorted(token_map.items()):
        words.append(int(key) & 0xFFFFFFFFFFFFFFFF)
        words.append(int(value) & 0xFFFFFFFFFFFFFFFF)
    return words


def _build_partition_input_words(
    register_file_size: int,
    program_input_sections: Dict[str, List[str]],
    partition_id: int,
    modulus: int,
) -> List[int]:
    register_count = _bounded_register_count(register_file_size)
    mod = int(modulus) if int(modulus) != 0 else _DEFAULT_MODULUS
    state = [0] * register_count
    stream_table_words = _build_stream_table_words(program_input_sections, partition_id)
    return [_INPUT_MAGIC, register_count, mod, *state, *stream_table_words]


def _slice_state_from_output(
    output_words: Sequence[int],
    expected_register_count: int,
    partition_id: int,
    module_name: str,
) -> List[int]:
    if len(output_words) < _OUTPUT_HEADER_WORDS:
        raise RuntimeError(
            f"Kernel output for module={module_name}, partition={partition_id} "
            f"is shorter than header words ({len(output_words)} < {_OUTPUT_HEADER_WORDS})"
        )

    status = int(output_words[0])
    returned_register_count = int(output_words[2])
    returned_partition_id = int(output_words[4])

    if status != 0:
        raise RuntimeError(
            f"Kernel returned non-zero status for module={module_name}, partition={partition_id}: status={status}"
        )
    if returned_partition_id != partition_id:
        raise RuntimeError(
            f"Kernel partition mismatch for module={module_name}: expected {partition_id}, got {returned_partition_id}"
        )

    bounded = min(max(expected_register_count, 0), _MAX_REGISTERS)
    if returned_register_count != bounded:
        raise RuntimeError(
            f"Kernel register count mismatch for module={module_name}, partition={partition_id}: "
            f"expected {bounded}, got {returned_register_count}"
        )

    state_end = _OUTPUT_HEADER_WORDS + bounded
    if len(output_words) < state_end:
        raise RuntimeError(
            f"Kernel output for module={module_name}, partition={partition_id} does not include full state: "
            f"need {state_end} words, got {len(output_words)}"
        )
    return [int(word) for word in output_words[_OUTPUT_HEADER_WORDS:state_end]]


def _first_word_mismatch(a: Sequence[int], b: Sequence[int]) -> int:
    limit = min(len(a), len(b))
    for i in range(limit):
        if int(a[i]) != int(b[i]):
            return i
    if len(a) != len(b):
        return limit
    return -1


@dataclasses.dataclass
class RuntimeConfig:
    target: str = "sw_emu"
    xclbin_path: Optional[pathlib.Path] = None
    kernel_name: str = "cinnamon_dispatch"
    kernel_names: Optional[Dict[str, str]] = None
    board_indices: Optional[Sequence[int]] = None
    output_word_count: int = 16
    parallel_dispatch: bool = True
    dispatch_pool: str = "thread"  # thread | process
    require_kernel_execution: bool = True
    verify_kernel_results: bool = True
    modulus: int = _DEFAULT_MODULUS


class Emulator:
    """FPGA runtime with kernel-only execution path.

    Methods match CinnamonEmulator:
    - generate_and_serialize_evalkeys
    - generate_inputs
    - run_program
    - get_kernel_outputs
    """

    def __init__(self, context: Any, **kwargs: Any) -> None:
        self.context = context
        self.config = RuntimeConfig(**kwargs)
        if self.config.xclbin_path is not None and not isinstance(
            self.config.xclbin_path, pathlib.Path
        ):
            self.config.xclbin_path = pathlib.Path(self.config.xclbin_path)
        self.last_dispatch: List[PartitionDispatchResult] = []
        self.last_opcode_summary: Dict[str, int] = {}
        self.last_kernel_outputs: List[Dict[str, Any]] = []
        self.last_timing_summary: Dict[str, Any] = {}

        emulator_mod = _maybe_import_emulator()
        self._emulator = None
        if emulator_mod is not None:
            self._emulator = emulator_mod.Emulator(context)

    def _set_emulation_env(self) -> None:
        if self.config.target in ("sw_emu", "hw_emu"):
            os.environ["XCL_EMULATION_MODE"] = self.config.target
            if self.config.xclbin_path is not None:
                os.environ["EMCONFIG_PATH"] = str(self.config.xclbin_path.resolve().parent)

    def generate_and_serialize_evalkeys(
        self,
        output_file_name: str,
        program_inputs_file_name: str,
        encryptor: Any,
    ) -> None:
        if self._emulator is None:
            raise RuntimeError(
                "generate_and_serialize_evalkeys requires cinnamon_emulator. "
                "Input preparation is not yet FPGA-native in this phase."
            )
        self._emulator.generate_and_serialize_evalkeys(
            output_file_name, program_inputs_file_name, encryptor
        )

    def generate_inputs(
        self,
        inputs_file_name: str,
        evalkeys_file_name: str,
        raw_inputs: Dict[str, Any],
        encryptor: Any,
    ) -> None:
        if self._emulator is None:
            raise RuntimeError(
                "generate_inputs requires cinnamon_emulator. "
                "Input preparation is not yet FPGA-native in this phase."
            )
        self._emulator.generate_inputs(inputs_file_name, evalkeys_file_name, raw_inputs, encryptor)

    def run_program(
        self,
        instruction_file_base: str,
        num_partitions: int,
        register_file_size: int,
    ) -> None:
        run_start_s = time.perf_counter()
        instruction_streams = load_instruction_streams(instruction_file_base, num_partitions)
        self.last_opcode_summary = summarize_opcodes(instruction_streams)
        buckets_per_partition = [split_stream_by_module(stream) for stream in instruction_streams]

        if self.config.xclbin_path is None:
            raise RuntimeError(
                "run_program requires xclbin_path; runtime CPU fallback is disabled"
            )

        self._set_emulation_env()
        board_indices = _resolve_board_indices(num_partitions, self.config.board_indices)
        kernel_map = module_kernels()
        if self.config.kernel_names:
            kernel_map.update(self.config.kernel_names)

        can_parallel = self.config.parallel_dispatch and not (
            self.config.target == "sw_emu" and num_partitions > 1
        )
        pool_mode = str(self.config.dispatch_pool).strip().lower()
        dispatch_pool = "process" if pool_mode == "process" else "thread"

        program_input_sections: Dict[str, List[str]] = {
            "ciphertext": [],
            "plaintext": [],
            "scalar": [],
            "output": [],
            "evalkey": [],
        }
        program_inputs_path = pathlib.Path(instruction_file_base).with_name("program_inputs")
        if program_inputs_path.exists():
            parsed_inputs = parse_program_inputs(program_inputs_path)
            program_input_sections = parsed_inputs.sections

        bounded_register_count = _bounded_register_count(register_file_size)
        per_partition_inputs = [
            _build_partition_input_words(
                register_file_size=register_file_size,
                program_input_sections=program_input_sections,
                partition_id=partition_id,
                modulus=self.config.modulus,
            )
            for partition_id in range(num_partitions)
        ]

        module_output_words = max(
            int(self.config.output_word_count),
            _OUTPUT_HEADER_WORDS + bounded_register_count,
        )

        partition_records: List[Dict[str, Any]] = [
            {
                "partition_id": idx,
                "board_index": board_indices[idx],
                "instruction_count": len(stream.lines),
                "input_count": len(per_partition_inputs[idx]),
                "opcode_counts": dict(stream.opcodes),
                "module_results": [],
            }
            for idx, stream in enumerate(instruction_streams)
        ]

        self.last_dispatch = []
        stage_timing_records: List[Dict[str, Any]] = []
        for module_name in module_order():
            kernel_name = kernel_map[module_name]
            partition_words = [
                buckets_per_partition[partition_id][module_name].instruction_words
                for partition_id in range(num_partitions)
            ]
            if all(len(words) == 0 for words in partition_words):
                continue

            stage_start_s = time.perf_counter()
            dispatch_cfg = DispatchConfig(
                xclbin_path=self.config.xclbin_path,
                kernel_name=kernel_name,
                output_word_count=module_output_words,
                word_alignment=8 if module_name == "memory" else 1,
            )
            prev_pool = os.environ.get("CINNAMON_FPGA_DISPATCH_POOL")
            os.environ["CINNAMON_FPGA_DISPATCH_POOL"] = dispatch_pool
            try:
                module_dispatches = run_dispatches(
                    dispatch_cfg, board_indices=board_indices, partition_words=partition_words,
                    input_words=per_partition_inputs, parallel=can_parallel
                )
            finally:
                if prev_pool is None:
                    os.environ.pop("CINNAMON_FPGA_DISPATCH_POOL", None)
                else:
                    os.environ["CINNAMON_FPGA_DISPATCH_POOL"] = prev_pool
            stage_end_s = time.perf_counter()
            self.last_dispatch.extend(module_dispatches)

            critical_dispatch = max(
                module_dispatches,
                key=lambda d: float(d.total_s),
                default=None,
            )
            if critical_dispatch is None:
                stage_setup_s = 0.0
                stage_h2d_s = 0.0
                stage_wait_s = 0.0
                stage_d2h_s = 0.0
                critical_partition = -1
                critical_total_s = 0.0
            else:
                stage_setup_s = float(critical_dispatch.setup_s)
                stage_h2d_s = float(critical_dispatch.h2d_s)
                stage_wait_s = float(critical_dispatch.wait_s)
                stage_d2h_s = float(critical_dispatch.d2h_s)
                critical_partition = int(critical_dispatch.partition_id)
                critical_total_s = float(critical_dispatch.total_s)
            stage_wall_s = float(stage_end_s - stage_start_s)
            stage_schedule_s = max(stage_wall_s - stage_wait_s, 0.0)

            stage_timing_records.append(
                {
                    "module": module_name,
                    "kernel_name": kernel_name,
                    "partition_count": len(module_dispatches),
                    "stage_wall_s": stage_wall_s,
                    "setup_s": stage_setup_s,
                    "h2d_s": stage_h2d_s,
                    "wait_s": stage_wait_s,
                    "d2h_s": stage_d2h_s,
                    "schedule_s": stage_schedule_s,
                    "parallel_dispatch": bool(can_parallel and len(module_dispatches) > 1),
                    "dispatch_pool": dispatch_pool,
                    "critical_partition": critical_partition,
                    "critical_total_s": critical_total_s,
                }
            )

            for partition_id, dispatch in enumerate(module_dispatches):
                bucket = buckets_per_partition[partition_id][module_name]
                current_input_words = per_partition_inputs[partition_id]
                if self.config.verify_kernel_results:
                    self._verify_module_dispatch(
                        module_name=module_name,
                        dispatch=dispatch,
                        instruction_words=bucket.instruction_words,
                        input_words=current_input_words,
                        output_count=module_output_words,
                    )

                next_state = _slice_state_from_output(
                    dispatch.output_words,
                    expected_register_count=bounded_register_count,
                    partition_id=partition_id,
                    module_name=module_name,
                )
                io_tail = current_input_words[3 + bounded_register_count :]
                per_partition_inputs[partition_id] = [
                    _INPUT_MAGIC,
                    bounded_register_count,
                    int(self.config.modulus) if int(self.config.modulus) != 0 else _DEFAULT_MODULUS,
                    *next_state,
                    *io_tail,
                ]

                partition_records[partition_id]["module_results"].append(
                    {
                        "module": module_name,
                        "kernel_name": kernel_name,
                        "opcode_counts": dict(bucket.opcode_counts),
                        "status": int(dispatch.output_words[0]) if len(dispatch.output_words) > 0 else None,
                        "executed": int(dispatch.output_words[1]) if len(dispatch.output_words) > 1 else None,
                        "register_count": int(dispatch.output_words[2]) if len(dispatch.output_words) > 2 else None,
                        "module_id": int(dispatch.output_words[3]) if len(dispatch.output_words) > 3 else None,
                        "partition_id": int(dispatch.output_words[4]) if len(dispatch.output_words) > 4 else None,
                        "trace_acc": int(dispatch.output_words[5]) if len(dispatch.output_words) > 5 else None,
                        "timing": {
                            "setup_s": float(dispatch.setup_s),
                            "h2d_s": float(dispatch.h2d_s),
                            "wait_s": float(dispatch.wait_s),
                            "d2h_s": float(dispatch.d2h_s),
                            "partition_total_s": float(dispatch.total_s),
                        },
                        "output_words": list(dispatch.output_words),
                    }
                )

        run_end_s = time.perf_counter()
        run_program_total_s = float(run_end_s - run_start_s)
        compute_wait_total_s = sum(float(stage["wait_s"]) for stage in stage_timing_records)
        stage_wall_total_s = sum(float(stage["stage_wall_s"]) for stage in stage_timing_records)
        setup_total_s = sum(float(stage["setup_s"]) for stage in stage_timing_records)
        h2d_total_s = sum(float(stage["h2d_s"]) for stage in stage_timing_records)
        d2h_total_s = sum(float(stage["d2h_s"]) for stage in stage_timing_records)
        schedule_total_s = max(run_program_total_s - compute_wait_total_s, 0.0)
        transfer_breakdown_total_s = setup_total_s + h2d_total_s + d2h_total_s
        other_host_total_s = max(schedule_total_s - transfer_breakdown_total_s, 0.0)

        compute_ratio = (compute_wait_total_s / run_program_total_s) if run_program_total_s > 0 else 0.0
        schedule_ratio = (schedule_total_s / run_program_total_s) if run_program_total_s > 0 else 0.0
        setup_ratio = (setup_total_s / run_program_total_s) if run_program_total_s > 0 else 0.0
        h2d_ratio = (h2d_total_s / run_program_total_s) if run_program_total_s > 0 else 0.0
        d2h_ratio = (d2h_total_s / run_program_total_s) if run_program_total_s > 0 else 0.0
        other_host_ratio = (other_host_total_s / run_program_total_s) if run_program_total_s > 0 else 0.0

        host_module_name = host_managed_module_name()
        for partition_id in range(num_partitions):
            host_bucket = buckets_per_partition[partition_id][host_module_name]
            comm_token = expected_host_comm_token(
                partition_id=partition_id,
                opcode_counts=host_bucket.opcode_counts,
                num_partitions=num_partitions,
            )
            partition_records[partition_id]["host_sync"] = {
                "module": host_module_name,
                "opcode_counts": dict(host_bucket.opcode_counts),
                "comm_token": comm_token,
            }

            partition_module_timings = [
                {
                    "module": str(entry["module"]),
                    "kernel_name": str(entry["kernel_name"]),
                    "setup_s": float(entry.get("timing", {}).get("setup_s", 0.0)),
                    "h2d_s": float(entry.get("timing", {}).get("h2d_s", 0.0)),
                    "wait_s": float(entry.get("timing", {}).get("wait_s", 0.0)),
                    "d2h_s": float(entry.get("timing", {}).get("d2h_s", 0.0)),
                    "partition_total_s": float(entry.get("timing", {}).get("partition_total_s", 0.0)),
                }
                for entry in partition_records[partition_id]["module_results"]
            ]
            partition_records[partition_id]["timing_summary"] = {
                "partition_id": partition_id,
                "module_timings": partition_module_timings,
                "partition_totals": {
                    "setup_s": sum(item["setup_s"] for item in partition_module_timings),
                    "h2d_s": sum(item["h2d_s"] for item in partition_module_timings),
                    "wait_s": sum(item["wait_s"] for item in partition_module_timings),
                    "d2h_s": sum(item["d2h_s"] for item in partition_module_timings),
                    "partition_total_s": sum(item["partition_total_s"] for item in partition_module_timings),
                },
            }
            partition_records[partition_id]["timing_breakdown"] = {
                "run_program_total_s": run_program_total_s,
                "compute_wait_total_s": compute_wait_total_s,
                "schedule_total_s": schedule_total_s,
                "setup_s": setup_total_s,
                "h2d_s": h2d_total_s,
                "d2h_s": d2h_total_s,
                "other_host_s": other_host_total_s,
                "compute_ratio": compute_ratio,
                "schedule_ratio": schedule_ratio,
                "setup_ratio": setup_ratio,
                "h2d_ratio": h2d_ratio,
                "d2h_ratio": d2h_ratio,
                "other_host_ratio": other_host_ratio,
                "stage_wall_total_s": stage_wall_total_s,
                "stage_count": len(stage_timing_records),
            }

        self.last_kernel_outputs = partition_records
        self.last_timing_summary = {
            "global": {
                "run_program_total_s": run_program_total_s,
                "compute_wait_total_s": compute_wait_total_s,
                "schedule_total_s": schedule_total_s,
                "setup_s": setup_total_s,
                "h2d_s": h2d_total_s,
                "d2h_s": d2h_total_s,
                "other_host_s": other_host_total_s,
                "compute_ratio": compute_ratio,
                "schedule_ratio": schedule_ratio,
                "setup_ratio": setup_ratio,
                "h2d_ratio": h2d_ratio,
                "d2h_ratio": d2h_ratio,
                "other_host_ratio": other_host_ratio,
                "stage_wall_total_s": stage_wall_total_s,
                "stage_count": len(stage_timing_records),
            },
            "stage_timing": stage_timing_records,
            "num_partitions": num_partitions,
            "target": self.config.target,
        }

    def get_kernel_outputs(self) -> List[Dict[str, Any]]:
        return copy.deepcopy(self.last_kernel_outputs)

    def _verify_module_dispatch(
        self,
        module_name: str,
        dispatch: PartitionDispatchResult,
        instruction_words: Sequence[int],
        input_words: Sequence[int],
        output_count: int,
    ) -> None:
        if len(dispatch.output_words) < _OUTPUT_HEADER_WORDS:
            raise RuntimeError(
                f"Kernel output too short for kernel={dispatch.kernel_name}, module={module_name}, "
                f"partition={dispatch.partition_id}: {len(dispatch.output_words)} words"
            )

        status = int(dispatch.output_words[0])
        executed = int(dispatch.output_words[1])
        register_count = int(dispatch.output_words[2])
        module_id = int(dispatch.output_words[3])
        kernel_partition = int(dispatch.output_words[4])

        if status != 0:
            raise RuntimeError(
                f"Kernel returned non-zero status for kernel={dispatch.kernel_name}, module={module_name}, "
                f"partition={dispatch.partition_id}: status={status}"
            )
        if kernel_partition != dispatch.partition_id:
            raise RuntimeError(
                f"Kernel partition mismatch for kernel={dispatch.kernel_name}, module={module_name}: "
                f"expected={dispatch.partition_id}, got={kernel_partition}"
            )
        expected_module_id = _MODULE_IDS.get(module_name)
        if expected_module_id is not None and module_id != expected_module_id:
            raise RuntimeError(
                f"Kernel module id mismatch for kernel={dispatch.kernel_name}, module={module_name}: "
                f"expected={expected_module_id}, got={module_id}"
            )
        if register_count < 0 or register_count > _MAX_REGISTERS:
            raise RuntimeError(
                f"Kernel register count out of range for kernel={dispatch.kernel_name}, module={module_name}: "
                f"register_count={register_count}"
            )
        if executed > (len(instruction_words) // 4):
            raise RuntimeError(
                f"Kernel executed count exceeds descriptor count for kernel={dispatch.kernel_name}, module={module_name}: "
                f"executed={executed}, descriptors={len(instruction_words) // 4}"
            )
        required_words = _OUTPUT_HEADER_WORDS + register_count
        if len(dispatch.output_words) < required_words:
            raise RuntimeError(
                f"Kernel output does not include advertised state for kernel={dispatch.kernel_name}, module={module_name}: "
                f"need {required_words} words, got {len(dispatch.output_words)}"
            )


CinnamonFPGAEmulator = Emulator


def describe_last_dispatch(runtime: Emulator) -> List[Dict[str, Any]]:
    entries: List[Dict[str, Any]] = []
    for item in runtime.last_dispatch:
        entries.append(
            {
                "kernel_name": item.kernel_name,
                "partition_id": item.partition_id,
                "board_index": item.board_index,
                "instruction_count": item.instruction_count,
                "input_count": item.input_count,
                "status": item.output_words[0] if len(item.output_words) > 0 else None,
                "executed": item.output_words[1] if len(item.output_words) > 1 else None,
                "register_count": item.output_words[2] if len(item.output_words) > 2 else None,
                "module_id": item.output_words[3] if len(item.output_words) > 3 else None,
                "kernel_partition_id": item.output_words[4] if len(item.output_words) > 4 else None,
                "trace_acc": item.output_words[5] if len(item.output_words) > 5 else None,
                "setup_s": float(item.setup_s),
                "h2d_s": float(item.h2d_s),
                "wait_s": float(item.wait_s),
                "d2h_s": float(item.d2h_s),
                "total_s": float(item.total_s),
            }
        )
    return entries


__all__ = [
    "CinnamonFPGAEmulator",
    "Emulator",
    "RuntimeConfig",
    "XRTUnavailableError",
    "describe_last_dispatch",
]
