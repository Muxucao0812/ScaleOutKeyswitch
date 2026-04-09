from __future__ import annotations

import dataclasses
import json
import os
import pathlib
import struct
import threading
import time
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from typing import Any, Dict, List, Sequence, Tuple


class XRTUnavailableError(RuntimeError):
    pass


@dataclasses.dataclass(frozen=True)
class PartitionDispatchResult:
    kernel_name: str
    partition_id: int
    board_index: int
    instruction_count: int
    input_count: int
    output_words: List[int]
    setup_s: float = 0.0
    h2d_s: float = 0.0
    wait_s: float = 0.0
    d2h_s: float = 0.0
    total_s: float = 0.0


@dataclasses.dataclass(frozen=True)
class DispatchConfig:
    xclbin_path: pathlib.Path
    kernel_name: str
    output_word_count: int = 16
    word_alignment: int = 1


@dataclasses.dataclass
class _KernelHandle:
    pyxrt: Any
    device: Any
    kernel: Any


@dataclasses.dataclass
class _DeviceHandle:
    pyxrt: Any
    device: Any
    uuid: Any


@dataclasses.dataclass
class _BoBundle:
    inst_bo: Any
    inp_bo: Any
    out_bo: Any
    inst_size: int
    inp_size: int
    out_size: int
    last_inst_payload: bytes = b""
    last_inp_payload: bytes = b""


_KERNEL_CACHE_LOCK = threading.Lock()
_DEVICE_CACHE: Dict[Tuple[int, str], _DeviceHandle] = {}
_KERNEL_CACHE: Dict[Tuple[int, str, str], _KernelHandle] = {}
_BO_CACHE: Dict[Tuple[int, str, str, int], _BoBundle] = {}
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

def _load_pyxrt():
    try:
        import pyxrt  # type: ignore

        return pyxrt
    except Exception as exc:  # pragma: no cover - environment dependent
        raise XRTUnavailableError(
            "Failed to import pyxrt. Ensure Python 3.10 and "
            "PYTHONPATH=/opt/xilinx/xrt/python are set."
        ) from exc


def _pack_words(
    words: Sequence[int],
    minimum_words: int = 1,
    alignment_words: int = 1,
) -> bytes:
    normalized = [int(w) & 0xFFFFFFFFFFFFFFFF for w in words]
    target_words = max(len(normalized), int(minimum_words), 1)
    align = max(int(alignment_words), 1)
    if align > 1:
        remainder = target_words % align
        if remainder != 0:
            target_words += align - remainder
    if len(normalized) < target_words:
        normalized.extend([0] * (target_words - len(normalized)))
    return struct.pack(f"<{len(normalized)}Q", *normalized)


def _unpack_words(payload: bytes) -> List[int]:
    if not payload:
        return []
    if len(payload) % 8 != 0:
        raise ValueError("payload length must be a multiple of 8")
    return list(struct.unpack(f"<{len(payload) // 8}Q", payload))


def _resolve_device_handle(config: DispatchConfig, board_index: int) -> _DeviceHandle:
    pyxrt = _load_pyxrt()
    xclbin_path = str(config.xclbin_path.resolve())
    cache_key = (int(board_index), xclbin_path)

    with _KERNEL_CACHE_LOCK:
        cached = _DEVICE_CACHE.get(cache_key)
    if cached is not None:
        return cached

    if not config.xclbin_path.exists():
        raise FileNotFoundError(f"xclbin not found: {config.xclbin_path}")

    device = pyxrt.device(board_index)
    xbin = pyxrt.xclbin(xclbin_path)
    uuid = device.load_xclbin(xbin)
    handle = _DeviceHandle(pyxrt=pyxrt, device=device, uuid=uuid)

    with _KERNEL_CACHE_LOCK:
        _DEVICE_CACHE[cache_key] = handle
    return handle


def _resolve_kernel_handle(config: DispatchConfig, board_index: int) -> _KernelHandle:
    xclbin_path = str(config.xclbin_path.resolve())
    cache_key = (int(board_index), xclbin_path, str(config.kernel_name))

    with _KERNEL_CACHE_LOCK:
        cached = _KERNEL_CACHE.get(cache_key)
    if cached is not None:
        return cached

    device_handle = _resolve_device_handle(config, board_index)
    pyxrt = device_handle.pyxrt
    kernel = pyxrt.kernel(
        device_handle.device,
        device_handle.uuid,
        config.kernel_name,
        pyxrt.kernel.shared,
    )
    handle = _KernelHandle(pyxrt=pyxrt, device=device_handle.device, kernel=kernel)

    with _KERNEL_CACHE_LOCK:
        _KERNEL_CACHE[cache_key] = handle
    return handle


def _resolve_bo_bundle(
    *,
    handle: _KernelHandle,
    board_index: int,
    config: DispatchConfig,
    partition_id: int,
    inst_size: int,
    inp_size: int,
    out_size: int,
) -> _BoBundle:
    cache_key = (
        int(board_index),
        str(config.xclbin_path.resolve()),
        str(config.kernel_name),
        int(partition_id),
    )
    with _KERNEL_CACHE_LOCK:
        bundle = _BO_CACHE.get(cache_key)

    needs_realloc = (
        bundle is None
        or bundle.inst_size != inst_size
        or bundle.inp_size != inp_size
        or bundle.out_size != out_size
    )
    if needs_realloc:
        kernel = handle.kernel
        pyxrt = handle.pyxrt
        bundle = _BoBundle(
            inst_bo=pyxrt.bo(handle.device, inst_size, pyxrt.bo.normal, kernel.group_id(0)),
            inp_bo=pyxrt.bo(handle.device, inp_size, pyxrt.bo.normal, kernel.group_id(1)),
            out_bo=pyxrt.bo(handle.device, out_size, pyxrt.bo.normal, kernel.group_id(2)),
            inst_size=inst_size,
            inp_size=inp_size,
            out_size=out_size,
        )
        with _KERNEL_CACHE_LOCK:
            _BO_CACHE[cache_key] = bundle
    return bundle


def _write_if_changed(
    *,
    bo: Any,
    payload: bytes,
    previous: bytes,
    pyxrt: Any,
) -> bytes:
    if payload == previous:
        return previous

    total_size = len(payload)
    bo.write(payload, 0)
    bo.sync(
        pyxrt.xclBOSyncDirection.XCL_BO_SYNC_BO_TO_DEVICE,
        total_size,
        0,
    )
    return payload


def run_partition_dispatch(
    config: DispatchConfig,
    board_index: int,
    partition_id: int,
    instruction_words: Sequence[int],
    input_words: Sequence[int],
) -> PartitionDispatchResult:
    t_begin = time.perf_counter()
    handle = _resolve_kernel_handle(config, board_index)
    pyxrt = handle.pyxrt
    kernel = handle.kernel
    t_setup_done = time.perf_counter()

    word_alignment = max(int(config.word_alignment), 1)
    instructions_packed = _pack_words(instruction_words, alignment_words=word_alignment)
    inputs_packed = _pack_words(input_words, alignment_words=word_alignment)
    output_words_count = max(config.output_word_count, 1)
    padded_output_words = output_words_count
    if word_alignment > 1:
        remainder = padded_output_words % word_alignment
        if remainder != 0:
            padded_output_words += word_alignment - remainder
    outputs_packed_size = padded_output_words * 8
    bundle = _resolve_bo_bundle(
        handle=handle,
        board_index=board_index,
        config=config,
        partition_id=partition_id,
        inst_size=len(instructions_packed),
        inp_size=len(inputs_packed),
        out_size=outputs_packed_size,
    )

    bundle.last_inst_payload = _write_if_changed(
        bo=bundle.inst_bo,
        payload=instructions_packed,
        previous=bundle.last_inst_payload,
        pyxrt=pyxrt,
    )
    bundle.last_inp_payload = _write_if_changed(
        bo=bundle.inp_bo,
        payload=inputs_packed,
        previous=bundle.last_inp_payload,
        pyxrt=pyxrt,
    )
    t_h2d_done = time.perf_counter()

    run = kernel(
        bundle.inst_bo,
        bundle.inp_bo,
        bundle.out_bo,
        len(instruction_words),
        len(input_words),
        output_words_count,
        partition_id,
    )
    t_wait_begin = time.perf_counter()
    run.wait()
    t_wait_done = time.perf_counter()

    bundle.out_bo.sync(
        pyxrt.xclBOSyncDirection.XCL_BO_SYNC_BO_FROM_DEVICE,
        outputs_packed_size,
        0,
    )

    mapped = bundle.out_bo.map()
    output_raw = bytes(mapped[:outputs_packed_size])
    output_words = _unpack_words(output_raw)[:output_words_count]
    t_done = time.perf_counter()

    _emit_jsonl(
        "pyxrt.run_partition_dispatch",
        payload={
            "card_id": int(board_index),
            "partition_id": int(partition_id),
            "kernel_name": str(config.kernel_name),
            "instruction_count": int(len(instruction_words)),
            "input_count": int(len(input_words)),
            "output_count": int(output_words_count),
            "kernel_status": int(output_words[0]) if len(output_words) > 0 else None,
            "kernel_executed": int(output_words[1]) if len(output_words) > 1 else None,
            "kernel_register_count": int(output_words[2]) if len(output_words) > 2 else None,
            "kernel_module_id": int(output_words[3]) if len(output_words) > 3 else None,
            "kernel_partition_id": int(output_words[4]) if len(output_words) > 4 else None,
            "kernel_trace_acc": int(output_words[5]) if len(output_words) > 5 else None,
            "setup_s": float(t_setup_done - t_begin),
            "h2d_s": float(t_h2d_done - t_setup_done),
            "wait_s": float(t_wait_done - t_wait_begin),
            "d2h_s": float(t_done - t_wait_done),
            "partition_total_s": float(t_done - t_begin),
        },
        debug_payload={
            "raw_instruction_preview": [
                int(word) for word in instruction_words[:_DEBUG_PREVIEW_WORDS]
            ],
            "raw_input_preview": [int(word) for word in input_words[:_DEBUG_PREVIEW_WORDS]],
            "raw_output_preview": [int(word) for word in output_words[:_DEBUG_PREVIEW_WORDS]],
        },
    )

    return PartitionDispatchResult(
        kernel_name=config.kernel_name,
        partition_id=partition_id,
        board_index=board_index,
        instruction_count=len(instruction_words),
        input_count=len(input_words),
        output_words=output_words,
        setup_s=t_setup_done - t_begin,
        h2d_s=t_h2d_done - t_setup_done,
        wait_s=t_wait_done - t_wait_begin,
        d2h_s=t_done - t_wait_done,
        total_s=t_done - t_begin,
    )


def _run_partition_dispatch_proxy(
    args: Tuple[DispatchConfig, int, int, Sequence[int], Sequence[int]]
) -> PartitionDispatchResult:
    return run_partition_dispatch(*args)


def run_dispatches(
    config: DispatchConfig,
    board_indices: Sequence[int],
    partition_words: Sequence[Sequence[int]],
    input_words: Sequence[int] | Sequence[Sequence[int]],
    parallel: bool = True,
) -> List[PartitionDispatchResult]:
    if len(board_indices) < len(partition_words):
        raise ValueError(
            f"board_indices has {len(board_indices)} entries but {len(partition_words)} partitions were requested"
        )

    partition_inputs: List[List[int]]
    if len(partition_words) == 0:
        partition_inputs = []
    elif len(input_words) == 0:
        partition_inputs = [[] for _ in range(len(partition_words))]
    else:
        first = input_words[0]  # type: ignore[index]
        if isinstance(first, Sequence):
            nested_words = input_words  # type: ignore[assignment]
            if len(nested_words) < len(partition_words):
                raise ValueError(
                    f"input_words has {len(nested_words)} entries but {len(partition_words)} partitions were requested"
                )
            partition_inputs = [
                [int(word) for word in nested_words[partition_id]]  # type: ignore[index]
                for partition_id in range(len(partition_words))
            ]
        else:
            shared_words = [int(word) for word in input_words]  # type: ignore[arg-type]
            partition_inputs = [shared_words for _ in range(len(partition_words))]

    args = [
        (
            config,
            board_indices[partition_id],
            partition_id,
            words,
            partition_inputs[partition_id],
        )
        for partition_id, words in enumerate(partition_words)
    ]

    if parallel and len(args) > 1:
        pool_mode = os.getenv("CINNAMON_FPGA_DISPATCH_POOL", "thread").strip().lower()
        if pool_mode == "process":
            with ProcessPoolExecutor(max_workers=len(args)) as pool:
                futures = [pool.submit(_run_partition_dispatch_proxy, arg) for arg in args]
                return [f.result() for f in futures]
        with ThreadPoolExecutor(max_workers=len(args)) as pool:
            futures = [pool.submit(run_partition_dispatch, *arg) for arg in args]
            return [f.result() for f in futures]

    return [run_partition_dispatch(*arg) for arg in args]
