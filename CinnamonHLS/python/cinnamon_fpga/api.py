from __future__ import annotations

import copy
import dataclasses
import json
import os
import pathlib
import re
import time
import zlib
from typing import Any, Dict, List, Optional, Sequence

from .parser import (
    encode_stream_token,
    expected_module_output_words,
    expected_host_comm_token,
    extract_opcode,
    host_managed_module_name,
    load_instruction_streams,
    module_kernels,
    module_for_opcode,
    module_order,
    parse_program_inputs,
    segment_stream_by_contiguous_module,
    split_stream_by_module,
    summarize_opcodes,
)
from .pyxrt_runner import (
    DispatchConfig,
    PartitionDispatchResult,
    XRTUnavailableError,
    run_payload_partition_dispatch,
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
_TERM_WITH_RNS_RE = re.compile(r"^(.+)\(([-+]?\d+)\)$")
_JSONL_ENV = "CINNAMON_FPGA_JSONL_LOG"
_JSONL_DEBUG_ENV = "CINNAMON_FPGA_JSONL_DEBUG_LOG"
_JSONL_DEBUG_FLAG_ENV = "CINNAMON_FPGA_JSONL_DEBUG"
_DEBUG_PREVIEW_WORDS = 8
_PAYLOAD_CONTROL_MAGIC = 0x43494E4E5041594C  # "CINNPAYL"
_PAYLOAD_CONTROL_VERSION = 1
_PAYLOAD_FLAG_IS_NTT = 1 << 0
_PAYLOAD_HEADER_WORDS = 9
_PAYLOAD_INVALID_HANDLE = 0
_PAYLOAD_EXTRA_MAGIC = 0x43494E4E58425243  # "CINNXBRC"
_PAYLOAD_EXTRA_VERSION = 1
_PAYLOAD_BCU_UNIT_COUNT = 8
_PAYLOAD_BCU_OUTPUT_CAPACITY = 128
_PAYLOAD_SUPPORTED_MODULES = {
    "memory",
    "arithmetic",
    "montgomery",
    "automorphism",
    "ntt",
    "base_conv",
}
_PAYLOAD_SUPPORTED_OPCODES = {
    "load",
    "loas",
    "store",
    "spill",
    "mov",
    "evg",
    "rec",
    "snd",
    "add",
    "sub",
    "ads",
    "sus",
    "sud",
    "con",
    "neg",
    "mul",
    "mup",
    "mus",
    "rot",
    "ntt",
    "int",
    "bci",
    "pl1",
    "bcw",
}
_LEGACY_COMPAT_MODULES = {"memory", "arithmetic"}
_BCI_LINE_RE = re.compile(
    r"^\s*bci\s+B(\d+)\s*:\s*\[([^\]]*)\]\s*,\s*\[([^\]]*)\]\s*$",
    re.IGNORECASE,
)


@dataclasses.dataclass(frozen=True)
class PayloadControlLayout:
    register_count: int
    coeff_count: int
    rns_table_count: int
    handle_count: int
    handle_capacity: int
    token_count: int
    output_token_count: int
    rns_table_offset: int
    register_handles_offset: int
    handle_meta_offset: int
    token_directory_offset: int
    output_directory_offset: int


@dataclasses.dataclass(frozen=True)
class PayloadBciConfig:
    line_crc: int
    bcu_id: int
    dest_base_ids: List[int]
    source_base_ids: List[int]
    factors: List[int]


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


def _parse_output_stream_descriptors(
    output_entries: Sequence[str],
) -> Dict[str, Dict[str, Any]]:
    descriptors: Dict[str, Dict[str, Any]] = {}
    for line in output_entries:
        match = _PROGRAM_INPUT_ENTRY_RE.match(str(line))
        if not match:
            continue
        stream_name = match.group(1).strip()
        stream_ref = match.group(2).strip()
        rns_raw = match.group(3).strip()
        parts = stream_ref.split(":", 1)
        if len(parts) != 2:
            continue
        output_name = parts[0].strip()
        component = parts[1].strip().lower()
        if component not in {"c0", "c1"}:
            continue
        rns_ids = [int(token.strip()) for token in rns_raw.split(",") if token.strip()]
        entry = descriptors.setdefault(
            output_name,
            {
                "name": output_name,
                "c0_term": "",
                "c1_term": "",
                "rns_base_ids": [],
            },
        )
        entry[f"{component}_term"] = stream_name
        if rns_ids:
            combined = sorted(set(int(v) for v in list(entry["rns_base_ids"]) + rns_ids))
            entry["rns_base_ids"] = combined
    return descriptors


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


def _get_context_coeff_count(context: Any) -> int:
    coeff_count = int(getattr(context, "coeff_count", 0))
    if coeff_count <= 0:
        raise RuntimeError("Context does not expose a positive coeff_count")
    return coeff_count


def _get_context_rns_moduli(context: Any) -> List[int]:
    moduli = [int(value) for value in list(getattr(context, "rns_moduli", []))]
    if not moduli:
        raise RuntimeError(
            "Context does not expose rns_moduli; rebuild cinnamon_emulator with the updated Python bindings"
        )
    return moduli


def _line_crc32(value: str) -> int:
    return zlib.crc32(value.encode("utf-8")) & 0xFFFFFFFF


def _term_rns_base_id(term: str) -> Optional[int]:
    match = _TERM_WITH_RNS_RE.match(term.strip())
    if match is None:
        return None
    return int(match.group(2))


def _parse_uint_list(text: str) -> List[int]:
    values: List[int] = []
    for item in text.split(","):
        token = item.strip()
        if token:
            values.append(int(token))
    return values


def _compute_base_conversion_factors(
    *,
    rns_moduli: Sequence[int],
    source_base_ids: Sequence[int],
    dest_base_ids: Sequence[int],
) -> List[int]:
    input_moduli = [int(rns_moduli[idx]) for idx in source_base_ids]
    factors: List[int] = []
    for src_pos, src_base_id in enumerate(source_base_ids):
        src_modulus = int(rns_moduli[src_base_id])
        punctured_product = 1
        for other_pos, other_modulus in enumerate(input_moduli):
            if other_pos == src_pos:
                continue
            punctured_product *= int(other_modulus)
        inv_punctured = pow(punctured_product % src_modulus, -1, src_modulus)
        for dest_base_id in dest_base_ids:
            dest_modulus = int(rns_moduli[dest_base_id])
            factor = ((punctured_product % dest_modulus) * inv_punctured) % dest_modulus
            factors.append(int(factor))
    return factors


def _collect_payload_bci_configs(
    *,
    rns_moduli: Sequence[int],
    instruction_streams: Sequence[Any],
) -> Dict[int, PayloadBciConfig]:
    configs: Dict[int, PayloadBciConfig] = {}
    for stream in instruction_streams:
        for line in getattr(stream, "lines", []):
            line_text = str(line).strip()
            match = _BCI_LINE_RE.match(line_text)
            if match is None:
                continue
            line_crc = _line_crc32(line_text)
            if line_crc in configs:
                continue
            bcu_id = int(match.group(1))
            dest_base_ids = _parse_uint_list(match.group(2))
            source_base_ids = _parse_uint_list(match.group(3))
            factors = _compute_base_conversion_factors(
                rns_moduli=rns_moduli,
                source_base_ids=source_base_ids,
                dest_base_ids=dest_base_ids,
            )
            configs[line_crc] = PayloadBciConfig(
                line_crc=line_crc,
                bcu_id=bcu_id,
                dest_base_ids=dest_base_ids,
                source_base_ids=source_base_ids,
                factors=factors,
            )
    return configs


def _parse_payload_control_layout(control_words: Sequence[int]) -> PayloadControlLayout:
    if len(control_words) < _PAYLOAD_HEADER_WORDS:
        raise RuntimeError(
            f"Payload control buffer is shorter than header ({len(control_words)} < {_PAYLOAD_HEADER_WORDS})"
        )
    if int(control_words[0]) != _PAYLOAD_CONTROL_MAGIC:
        raise RuntimeError(
            f"Payload control magic mismatch: expected {_PAYLOAD_CONTROL_MAGIC}, got {int(control_words[0])}"
        )
    if int(control_words[1]) != _PAYLOAD_CONTROL_VERSION:
        raise RuntimeError(
            f"Payload control version mismatch: expected {_PAYLOAD_CONTROL_VERSION}, got {int(control_words[1])}"
        )

    register_count = int(control_words[2])
    coeff_count = int(control_words[3])
    rns_table_count = int(control_words[4])
    handle_count = int(control_words[5])
    handle_capacity = int(control_words[6])
    token_count = int(control_words[7])
    output_token_count = int(control_words[8])

    offset = _PAYLOAD_HEADER_WORDS
    rns_table_offset = offset
    offset += rns_table_count * 2
    register_handles_offset = offset
    offset += register_count
    handle_meta_offset = offset
    offset += handle_capacity * 2
    token_directory_offset = offset
    offset += token_count * 2
    output_directory_offset = offset
    offset += output_token_count * 2
    if offset > len(control_words):
        raise RuntimeError(
            f"Payload control layout exceeds available words: need {offset}, got {len(control_words)}"
        )

    return PayloadControlLayout(
        register_count=register_count,
        coeff_count=coeff_count,
        rns_table_count=rns_table_count,
        handle_count=handle_count,
        handle_capacity=handle_capacity,
        token_count=token_count,
        output_token_count=output_token_count,
        rns_table_offset=rns_table_offset,
        register_handles_offset=register_handles_offset,
        handle_meta_offset=handle_meta_offset,
        token_directory_offset=token_directory_offset,
        output_directory_offset=output_directory_offset,
    )


def _sorted_pair_lookup(words: Sequence[int], base: int, pair_count: int, key: int) -> int:
    lo = 0
    hi = int(pair_count)
    while lo < hi:
        mid = lo + ((hi - lo) // 2)
        cursor = int(base + (mid * 2))
        pair_key = int(words[cursor])
        if pair_key == int(key):
            return int(words[cursor + 1])
        if pair_key < int(key):
            lo = mid + 1
        else:
            hi = mid
    return 0


def _handle_to_materialized_limb(
    control_words: Sequence[int],
    payload_words: Sequence[int],
    layout: PayloadControlLayout,
    handle_id: int,
) -> Dict[str, Any]:
    if handle_id <= 0 or handle_id > layout.handle_count:
        raise RuntimeError(f"Invalid payload handle id: {handle_id}")
    meta_cursor = layout.handle_meta_offset + ((handle_id - 1) * 2)
    rns_base_id = int(control_words[meta_cursor])
    flags = int(control_words[meta_cursor + 1])
    coeff_begin = (handle_id - 1) * layout.coeff_count
    coeff_end = coeff_begin + layout.coeff_count
    if coeff_end > len(payload_words):
        raise RuntimeError(
            f"Payload buffer is too short for handle {handle_id}: need {coeff_end}, got {len(payload_words)}"
        )
    return {
        "rns_base_id": rns_base_id,
        "is_ntt_form": bool(flags & _PAYLOAD_FLAG_IS_NTT),
        "coeffs": [int(value) for value in payload_words[coeff_begin:coeff_end]],
    }


def _count_payload_allocations(
    streams: Sequence[Any],
    bci_configs: Optional[Dict[int, PayloadBciConfig]] = None,
) -> int:
    allocating_opcodes = {
        "add",
        "sub",
        "ads",
        "sus",
        "sud",
        "con",
        "neg",
        "mul",
        "mup",
        "mus",
        "rot",
        "ntt",
        "int",
        "pl1",
        "bcw",
    }
    count = sum(
        1
        for stream in streams
        for line in stream.lines
        if extract_opcode(line) in allocating_opcodes
    )
    if bci_configs:
        count += sum(len(cfg.dest_base_ids) for cfg in bci_configs.values())
    return count


def _uses_phase1_payload_backend(streams: Sequence[Any]) -> bool:
    saw_supported = False
    for stream in streams:
        for line in stream.lines:
            opcode = extract_opcode(line)
            module_name = module_for_opcode(opcode)
            if module_name == host_managed_module_name():
                continue
            if opcode not in _PAYLOAD_SUPPORTED_OPCODES or module_name not in _PAYLOAD_SUPPORTED_MODULES:
                return False
            saw_supported = True
    return saw_supported


def _collect_phase1_payload_blockers(streams: Sequence[Any]) -> List[str]:
    blockers: set[str] = set()
    for stream in streams:
        for line in stream.lines:
            opcode = extract_opcode(line)
            module_name = module_for_opcode(opcode)
            if module_name == host_managed_module_name():
                continue
            if opcode in _PAYLOAD_SUPPORTED_OPCODES and module_name in _PAYLOAD_SUPPORTED_MODULES:
                continue
            blockers.add(f"{module_name}:{opcode}")
    return sorted(blockers)


def _build_payload_partition_buffers(
    *,
    context: Any,
    register_file_size: int,
    materialized_memory: Dict[str, Dict[str, Any]],
    output_descriptors: Dict[str, Dict[str, Any]],
    instruction_streams: Sequence[Any],
    bci_configs: Optional[Dict[int, PayloadBciConfig]] = None,
) -> tuple[List[int], List[int]]:
    register_count = _bounded_register_count(register_file_size)
    coeff_count = _get_context_coeff_count(context)
    rns_moduli = _get_context_rns_moduli(context)

    term_items = sorted((str(term), dict(payload)) for term, payload in materialized_memory.items())
    initial_handle_count = len(term_items)
    handle_capacity = (
        initial_handle_count
        + _count_payload_allocations(instruction_streams, bci_configs=bci_configs)
        + 32
    )
    if handle_capacity < initial_handle_count:
        handle_capacity = initial_handle_count

    payload_words = [0] * (handle_capacity * coeff_count)
    handle_meta_words = [0] * (handle_capacity * 2)
    token_map: Dict[int, int] = {}

    for handle_id, (term, payload) in enumerate(term_items, start=1):
        coeffs = [int(value) for value in payload.get("coeffs", [])]
        if len(coeffs) != coeff_count:
            raise RuntimeError(
                f"Materialized term {term} has coeff_count={len(coeffs)} but context expects {coeff_count}"
            )
        coeff_begin = (handle_id - 1) * coeff_count
        payload_words[coeff_begin : coeff_begin + coeff_count] = coeffs
        meta_cursor = (handle_id - 1) * 2
        handle_meta_words[meta_cursor] = int(payload.get("rns_base_id", 0))
        handle_meta_words[meta_cursor + 1] = _PAYLOAD_FLAG_IS_NTT if bool(payload.get("is_ntt_form", True)) else 0
        token_map[int(encode_stream_token(term))] = handle_id
        if term.endswith(")") and not term.endswith("{F}"):
            token_map[int(encode_stream_token(f"{term}{{F}}"))] = handle_id

    output_pairs: List[tuple[int, int]] = []
    seen_output_terms: set[int] = set()
    for descriptor in output_descriptors.values():
        rns_base_ids = [int(v) for v in descriptor.get("rns_base_ids", [])]
        for component_term in (str(descriptor.get("c0_term", "")), str(descriptor.get("c1_term", ""))):
            if not component_term:
                continue
            for rns_base_id in rns_base_ids:
                token_key = int(encode_stream_token(f"{component_term}({rns_base_id})"))
                if token_key in seen_output_terms:
                    continue
                seen_output_terms.add(token_key)
                output_pairs.append((token_key, _PAYLOAD_INVALID_HANDLE))

    token_pairs = sorted(token_map.items(), key=lambda item: item[0])
    output_pairs.sort(key=lambda item: item[0])
    register_handles = [_PAYLOAD_INVALID_HANDLE] * register_count

    rns_table_words: List[int] = []
    for rns_base_id, modulus in enumerate(rns_moduli):
        rns_table_words.append(int(rns_base_id))
        rns_table_words.append(int(modulus))

    control_words = [
        _PAYLOAD_CONTROL_MAGIC,
        _PAYLOAD_CONTROL_VERSION,
        register_count,
        coeff_count,
        len(rns_moduli),
        initial_handle_count,
        handle_capacity,
        len(token_pairs),
        len(output_pairs),
        *rns_table_words,
        *register_handles,
        *handle_meta_words,
    ]
    for token_key, handle_id in token_pairs:
        control_words.extend((int(token_key), int(handle_id)))
    for token_key, handle_id in output_pairs:
        control_words.extend((int(token_key), int(handle_id)))

    active_bcu_config = [0] * _PAYLOAD_BCU_UNIT_COUNT
    bcu_output_handles = [0] * (_PAYLOAD_BCU_UNIT_COUNT * _PAYLOAD_BCU_OUTPUT_CAPACITY)
    control_words.extend(
        [
            _PAYLOAD_EXTRA_MAGIC,
            _PAYLOAD_EXTRA_VERSION,
            _PAYLOAD_BCU_UNIT_COUNT,
            _PAYLOAD_BCU_OUTPUT_CAPACITY,
            _PAYLOAD_BCU_UNIT_COUNT,
            len(bci_configs) if bci_configs is not None else 0,
            *active_bcu_config,
            *bcu_output_handles,
        ]
    )
    if bci_configs:
        for line_crc in sorted(bci_configs.keys()):
            cfg = bci_configs[line_crc]
            control_words.extend(
                [
                    int(cfg.line_crc),
                    int(cfg.bcu_id),
                    len(cfg.source_base_ids),
                    len(cfg.dest_base_ids),
                    *[int(v) for v in cfg.source_base_ids],
                    *[int(v) for v in cfg.dest_base_ids],
                    *[int(v) for v in cfg.factors],
                ]
            )
    return ([int(word) for word in control_words], [int(word) for word in payload_words])


def _materialize_output_terms_from_payload(
    *,
    control_words: Sequence[int],
    payload_words: Sequence[int],
    output_descriptors: Dict[str, Dict[str, Any]],
) -> Dict[str, Dict[str, Any]]:
    layout = _parse_payload_control_layout(control_words)
    materialized_terms: Dict[str, Dict[str, Any]] = {}
    for descriptor in output_descriptors.values():
        rns_base_ids = [int(v) for v in descriptor.get("rns_base_ids", [])]
        for component_term in (str(descriptor.get("c0_term", "")), str(descriptor.get("c1_term", ""))):
            if not component_term:
                continue
            for rns_base_id in rns_base_ids:
                token = int(encode_stream_token(f"{component_term}({rns_base_id})"))
                handle_id = _sorted_pair_lookup(
                    control_words,
                    layout.output_directory_offset,
                    layout.output_token_count,
                    token,
                )
                if handle_id == _PAYLOAD_INVALID_HANDLE:
                    continue
                materialized_terms[f"{component_term}({rns_base_id})"] = _handle_to_materialized_limb(
                    control_words, payload_words, layout, handle_id
                )
    return materialized_terms


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
    - get_output_ciphertexts
    - get_decrypted_outputs
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
        self.last_output_ciphertexts: Dict[str, Dict[str, Any]] = {}
        self._last_materialized_program_memory: Dict[str, Dict[str, Any]] = {}
        self._last_materialized_output_terms: Dict[str, Dict[str, Any]] = {}
        self._payload_backend_active: bool = False
        self._last_instruction_file_base: Optional[str] = None
        self._last_num_partitions: Optional[int] = None
        self._last_register_file_size: Optional[int] = None
        self._run_generation: int = 0
        self._mirror_generation: int = -1

        emulator_mod = _maybe_import_emulator()
        self._emulator = None
        if emulator_mod is not None:
            self._emulator = emulator_mod.Emulator(context)

    def _require_run_context(self) -> None:
        if (
            self._last_instruction_file_base is None
            or self._last_num_partitions is None
            or self._last_register_file_size is None
        ):
            raise RuntimeError("run_program must be called before output/decrypt APIs")

    def _ensure_emulator_mirror_outputs(self) -> None:
        if self._emulator is None:
            raise RuntimeError(
                "get_decrypted_outputs requires cinnamon_emulator to be installed in this environment"
            )
        self._require_run_context()
        if self._mirror_generation == self._run_generation:
            return
        self._emulator.run_program(
            str(self._last_instruction_file_base),
            int(self._last_num_partitions),
            int(self._last_register_file_size),
        )
        self._mirror_generation = self._run_generation

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
        self._last_materialized_program_memory = self.get_materialized_program_memory()
        self._last_materialized_output_terms = {}

    def get_materialized_program_memory(self) -> Dict[str, Dict[str, Any]]:
        if self._emulator is None:
            raise RuntimeError(
                "get_materialized_program_memory requires cinnamon_emulator. "
                "Program input materialization is currently sourced from the CPU emulator."
            )
        raw_memory = dict(self._emulator.get_program_memory())
        materialized: Dict[str, Dict[str, Any]] = {}
        for term, payload in raw_memory.items():
            rns_base_id, is_ntt_form, coeffs = payload
            materialized[str(term)] = {
                "rns_base_id": int(rns_base_id),
                "is_ntt_form": bool(is_ntt_form),
                "coeffs": [int(value) for value in coeffs],
            }
        return materialized

    def set_materialized_program_memory(self, memory: Dict[str, Dict[str, Any]]) -> None:
        if self._emulator is None:
            raise RuntimeError(
                "set_materialized_program_memory requires cinnamon_emulator. "
                "Program output materialization is not yet FPGA-native in this phase."
            )
        serialized: Dict[str, Any] = {}
        for term, payload in memory.items():
            coeffs = [int(value) for value in payload.get("coeffs", [])]
            serialized[str(term)] = (
                int(payload.get("rns_base_id", 0)),
                bool(payload.get("is_ntt_form", True)),
                coeffs,
            )
        self._emulator.set_program_memory(serialized)

    def _run_phase1_payload_program(
        self,
        *,
        instruction_streams: Sequence[Any],
        instruction_file_base: str,
        num_partitions: int,
        register_file_size: int,
        board_indices: Sequence[int],
        kernel_map: Dict[str, str],
        output_descriptors: Dict[str, Dict[str, Any]],
    ) -> None:
        if self._emulator is None:
            raise RuntimeError(
                "Phase-1 payload backend requires cinnamon_emulator for input materialization and decrypt"
            )

        bounded_register_count = _bounded_register_count(register_file_size)
        module_output_words = max(
            int(self.config.output_word_count),
            _OUTPUT_HEADER_WORDS + bounded_register_count,
        )
        segments_per_partition = [
            segment_stream_by_contiguous_module(stream) for stream in instruction_streams
        ]
        all_bci_configs = _collect_payload_bci_configs(
            rns_moduli=_get_context_rns_moduli(self.context),
            instruction_streams=instruction_streams,
        )
        base_memory = copy.deepcopy(
            self._last_materialized_program_memory or self.get_materialized_program_memory()
        )
        self._last_materialized_program_memory = copy.deepcopy(base_memory)
        self._last_materialized_output_terms = {}
        self.last_dispatch = []

        partition_states: List[tuple[List[int], List[int]]] = []
        bci_config_counts: List[int] = []
        for partition_id in range(num_partitions):
            partition_bci_configs = _collect_payload_bci_configs(
                rns_moduli=_get_context_rns_moduli(self.context),
                instruction_streams=[instruction_streams[partition_id]],
            )
            control_words, payload_words = _build_payload_partition_buffers(
                context=self.context,
                register_file_size=register_file_size,
                materialized_memory=base_memory,
                output_descriptors=output_descriptors,
                instruction_streams=[instruction_streams[partition_id]],
                bci_configs=partition_bci_configs,
            )
            partition_states.append((control_words, payload_words))
            bci_config_counts.append(len(partition_bci_configs))

        partition_records: List[Dict[str, Any]] = []
        stage_timing_records: List[Dict[str, Any]] = []
        for idx, stream in enumerate(instruction_streams):
            control_words, payload_words = partition_states[idx]
            partition_records.append(
                {
                    "partition_id": idx,
                    "board_index": int(board_indices[idx]),
                    "instruction_count": len(stream.lines),
                    "control_count": len(control_words),
                    "payload_count": len(payload_words),
                    "opcode_counts": dict(stream.opcodes),
                    "bci_config_count": bci_config_counts[idx],
                    "all_bci_config_count": len(all_bci_configs),
                    "module_results": [],
                }
            )

        run_start_s = time.perf_counter()
        for partition_id in range(num_partitions):
            control_words, payload_words = partition_states[partition_id]
            for segment in segments_per_partition[partition_id]:
                if segment.module == host_managed_module_name():
                    continue
                kernel_name = kernel_map[segment.module]
                dispatch_cfg = DispatchConfig(
                    xclbin_path=self.config.xclbin_path,
                    kernel_name=kernel_name,
                    output_word_count=module_output_words,
                    word_alignment=8 if segment.module == "memory" else 1,
                    abi="payload",
                )
                dispatch = run_payload_partition_dispatch(
                    dispatch_cfg,
                    int(board_indices[partition_id]),
                    partition_id,
                    segment.instruction_words,
                    control_words,
                    payload_words,
                )
                self.last_dispatch.append(dispatch)
                if self.config.verify_kernel_results:
                    self._verify_module_dispatch(
                        module_name=segment.module,
                        dispatch=dispatch,
                        instruction_words=segment.instruction_words,
                        input_words=control_words,
                        output_count=module_output_words,
                    )

                control_words = [int(word) for word in dispatch.control_words]
                payload_words = [int(word) for word in dispatch.payload_words]
                stage_wall_s = float(dispatch.total_s)
                stage_schedule_s = max(stage_wall_s - float(dispatch.wait_s), 0.0)
                stage_timing_records.append(
                    {
                        "partition_id": partition_id,
                        "module": segment.module,
                        "kernel_name": kernel_name,
                        "segment_start_instruction": int(segment.start_instruction),
                        "segment_end_instruction": int(segment.end_instruction),
                        "segment_line_count": len(segment.lines),
                        "stage_wall_s": stage_wall_s,
                        "setup_s": float(dispatch.setup_s),
                        "h2d_s": float(dispatch.h2d_s),
                        "wait_s": float(dispatch.wait_s),
                        "d2h_s": float(dispatch.d2h_s),
                        "schedule_s": stage_schedule_s,
                        "parallel_dispatch": False,
                        "dispatch_pool": "sequential",
                        "critical_partition": partition_id,
                        "critical_total_s": float(dispatch.total_s),
                    }
                )

                module_result = {
                    "module": segment.module,
                    "kernel_name": kernel_name,
                    "segment_start_instruction": int(segment.start_instruction),
                    "segment_end_instruction": int(segment.end_instruction),
                    "segment_lines": list(segment.lines),
                    "opcode_counts": dict(segment.opcode_counts),
                    "status": int(dispatch.output_words[0]) if len(dispatch.output_words) > 0 else None,
                    "executed": int(dispatch.output_words[1]) if len(dispatch.output_words) > 1 else None,
                    "register_count": int(dispatch.output_words[2]) if len(dispatch.output_words) > 2 else None,
                    "module_id": int(dispatch.output_words[3]) if len(dispatch.output_words) > 3 else None,
                    "partition_id": int(dispatch.output_words[4]) if len(dispatch.output_words) > 4 else None,
                    "trace_acc": int(dispatch.output_words[5]) if len(dispatch.output_words) > 5 else None,
                    "control_count": int(dispatch.control_count),
                    "payload_count": int(dispatch.payload_count),
                    "timing": {
                        "setup_s": float(dispatch.setup_s),
                        "h2d_s": float(dispatch.h2d_s),
                        "wait_s": float(dispatch.wait_s),
                        "d2h_s": float(dispatch.d2h_s),
                        "partition_total_s": float(dispatch.total_s),
                    },
                    "output_words": list(dispatch.output_words),
                }
                partition_records[partition_id]["module_results"].append(module_result)

            partition_states[partition_id] = (control_words, payload_words)
            materialized_output_terms = _materialize_output_terms_from_payload(
                control_words=control_words,
                payload_words=payload_words,
                output_descriptors=output_descriptors,
            )
            if partition_id == 0:
                self._last_materialized_output_terms = materialized_output_terms
            partition_records[partition_id]["materialized_output_terms"] = sorted(
                materialized_output_terms.keys()
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

        for partition_id in range(num_partitions):
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
            "backend": "payload_phase1",
        }
        self.last_output_ciphertexts = {
            name: {
                "name": str(payload.get("name", name)),
                "c0_term": str(payload.get("c0_term", "")),
                "c1_term": str(payload.get("c1_term", "")),
                "rns_base_ids": [int(v) for v in payload.get("rns_base_ids", [])],
                "source": "fpga_payload",
                "materialized_terms": sorted(
                    term
                    for term in self._last_materialized_output_terms.keys()
                    if term.startswith(f"{payload.get('c0_term', '')}(")
                    or term.startswith(f"{payload.get('c1_term', '')}(")
                ),
            }
            for name, payload in output_descriptors.items()
        }
        self._payload_backend_active = True

    def run_program(
        self,
        instruction_file_base: str,
        num_partitions: int,
        register_file_size: int,
    ) -> None:
        self._payload_backend_active = False
        self._last_materialized_output_terms = {}
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
        output_descriptors = _parse_output_stream_descriptors(program_input_sections.get("output", []))
        self.last_output_ciphertexts = {
            name: {
                "name": str(payload.get("name", name)),
                "c0_term": str(payload.get("c0_term", "")),
                "c1_term": str(payload.get("c1_term", "")),
                "rns_base_ids": [int(v) for v in payload.get("rns_base_ids", [])],
                "source": "output_stream_descriptor",
            }
            for name, payload in output_descriptors.items()
        }
        self._last_instruction_file_base = str(instruction_file_base)
        self._last_num_partitions = int(num_partitions)
        self._last_register_file_size = int(register_file_size)
        self._run_generation += 1
        self._mirror_generation = -1

        if _uses_phase1_payload_backend(instruction_streams):
            self._run_phase1_payload_program(
                instruction_streams=instruction_streams,
                instruction_file_base=instruction_file_base,
                num_partitions=num_partitions,
                register_file_size=register_file_size,
                board_indices=board_indices,
                kernel_map=kernel_map,
                output_descriptors=output_descriptors,
            )
            return

        blockers = _collect_phase1_payload_blockers(instruction_streams)
        blocker_preview = ", ".join(blockers[:8]) if blockers else "unknown"
        raise RuntimeError(
            "run_program currently supports payload-backend instruction streams only; "
            f"unsupported opcode/module pairs: {blocker_preview}"
            + (" ..." if len(blockers) > 8 else "")
        )

        can_parallel = self.config.parallel_dispatch and not (
            self.config.target == "sw_emu" and num_partitions > 1
        )
        pool_mode = str(self.config.dispatch_pool).strip().lower()
        dispatch_pool = "process" if pool_mode == "process" else "thread"

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
                if self.config.verify_kernel_results and module_name not in _LEGACY_COMPAT_MODULES:
                    self._verify_module_dispatch(
                        module_name=module_name,
                        dispatch=dispatch,
                        instruction_words=bucket.instruction_words,
                        input_words=current_input_words,
                        output_count=module_output_words,
                    )

                effective_output_words = list(dispatch.output_words)
                compat_fallback_reason: str | None = None
                try:
                    next_state = _slice_state_from_output(
                        effective_output_words,
                        expected_register_count=bounded_register_count,
                        partition_id=partition_id,
                        module_name=module_name,
                    )
                except RuntimeError as exc:
                    if module_name not in _LEGACY_COMPAT_MODULES:
                        raise
                    compat_fallback_reason = str(exc)
                    effective_output_words = expected_module_output_words(
                        module_name=module_name,
                        instruction_words=bucket.instruction_words,
                        input_words=current_input_words,
                        output_count=module_output_words,
                        partition_id=partition_id,
                    )
                    next_state = _slice_state_from_output(
                        effective_output_words,
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

                module_result = {
                    "module": module_name,
                    "kernel_name": kernel_name,
                    "opcode_counts": dict(bucket.opcode_counts),
                    "status": int(effective_output_words[0]) if len(effective_output_words) > 0 else None,
                    "executed": int(effective_output_words[1]) if len(effective_output_words) > 1 else None,
                    "register_count": int(effective_output_words[2]) if len(effective_output_words) > 2 else None,
                    "module_id": int(effective_output_words[3]) if len(effective_output_words) > 3 else None,
                    "partition_id": int(effective_output_words[4]) if len(effective_output_words) > 4 else None,
                    "trace_acc": int(effective_output_words[5]) if len(effective_output_words) > 5 else None,
                    "timing": {
                        "setup_s": float(dispatch.setup_s),
                        "h2d_s": float(dispatch.h2d_s),
                        "wait_s": float(dispatch.wait_s),
                        "d2h_s": float(dispatch.d2h_s),
                        "partition_total_s": float(dispatch.total_s),
                    },
                    "output_words": list(effective_output_words),
                }
                if compat_fallback_reason is not None:
                    module_result["compat_fallback"] = True
                    module_result["compat_reason"] = compat_fallback_reason
                    module_result["raw_output_words"] = list(dispatch.output_words)
                partition_records[partition_id]["module_results"].append(module_result)
                _emit_jsonl(
                    "api.module_results_append",
                    payload={
                        "card_id": int(dispatch.board_index),
                        "partition_id": int(partition_id),
                        "module": str(module_name),
                        "kernel_name": str(kernel_name),
                        "kernel_status": module_result.get("status"),
                        "kernel_executed": module_result.get("executed"),
                        "kernel_register_count": module_result.get("register_count"),
                        "kernel_module_id": module_result.get("module_id"),
                        "kernel_partition_id": module_result.get("partition_id"),
                        "kernel_trace_acc": module_result.get("trace_acc"),
                        "instruction_count": int(len(bucket.instruction_words)),
                    },
                    debug_payload={
                        "opcode_counts": dict(bucket.opcode_counts),
                        "raw_output_preview": [
                            int(word)
                            for word in module_result.get("output_words", [])[:_DEBUG_PREVIEW_WORDS]
                        ],
                    },
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

    def get_output_ciphertexts(self) -> Dict[str, Dict[str, Any]]:
        self._require_run_context()
        payload = copy.deepcopy(self.last_output_ciphertexts)
        if self._payload_backend_active:
            for value in payload.values():
                value["source"] = "fpga_payload"
            return payload
        raise RuntimeError(
            "Output ciphertext materialization requires payload backend execution. "
            "Current run did not use the payload backend."
        )

    def get_decrypted_outputs(
        self,
        encryptor: Any,
        output_scales: Dict[str, float],
    ) -> Dict[str, Any]:
        if self._payload_backend_active:
            if self._emulator is None:
                raise RuntimeError(
                    "Payload backend decrypt requires cinnamon_emulator for host-side decrypt"
                )
            materialized_memory = copy.deepcopy(self._last_materialized_program_memory)
            materialized_memory.update(copy.deepcopy(self._last_materialized_output_terms))
            coeff_count = _get_context_coeff_count(self.context)
            backfill_from_memory = 0
            synthesized_zero = 0
            for payload in self.last_output_ciphertexts.values():
                rns_base_ids = [int(v) for v in payload.get("rns_base_ids", [])]
                for component_term in (
                    str(payload.get("c0_term", "")),
                    str(payload.get("c1_term", "")),
                ):
                    if not component_term:
                        continue
                    for rns_base_id in rns_base_ids:
                        term = f"{component_term}({rns_base_id})"
                        if term in materialized_memory:
                            continue
                        existing = self._last_materialized_program_memory.get(term)
                        if existing is not None:
                            materialized_memory[term] = copy.deepcopy(existing)
                            backfill_from_memory += 1
                            continue
                        parsed_rns = _term_rns_base_id(term)
                        materialized_memory[term] = {
                            "rns_base_id": int(parsed_rns if parsed_rns is not None else rns_base_id),
                            "is_ntt_form": False,
                            "coeffs": [0] * coeff_count,
                        }
                        synthesized_zero += 1
            self.set_materialized_program_memory(materialized_memory)
            _emit_jsonl(
                "api.get_decrypted_outputs",
                payload={
                    "mode": "fpga_payload",
                    "output_count": len(output_scales),
                    "run_generation": int(self._run_generation),
                    "backfill_from_memory": int(backfill_from_memory),
                    "synthesized_zero_terms": int(synthesized_zero),
                },
            )
            try:
                return dict(self._emulator.get_decrypted_outputs(encryptor, output_scales))
            finally:
                self.set_materialized_program_memory(self._last_materialized_program_memory)
        raise RuntimeError(
            "Decryption requires payload backend execution. "
            "CPU emulator mirror execution has been removed from normal runtime path."
        )

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
                "control_count": item.control_count,
                "payload_count": item.payload_count,
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
