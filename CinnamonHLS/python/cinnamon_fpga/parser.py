from __future__ import annotations

import collections
import dataclasses
import json
import os
import pathlib
import re
import time
import zlib
from typing import Dict, Iterable, Iterator, List, Sequence

from .generated_ntt_tables import NEGACYCLIC_ROOT_TABLE

_INSTRUCTION_WORD_STRIDE = 4
_INPUT_MAGIC = 0x43494E4E414D4F4E  # "CINNAMON"
_DEFAULT_MODULUS = 268042241
_OUTPUT_HEADER_WORDS = 6

_OPCODE_IDS = {
    "load": 1,
    "store": 2,
    "mov": 3,
    "add": 4,
    "sub": 5,
    "mul": 6,
    "mup": 7,
    "mus": 8,
    "ads": 9,
    "sus": 10,
    "sud": 11,
    "ntt": 12,
    "int": 13,
    "rot": 14,
    "con": 15,
    "neg": 16,
    "bci": 17,
    "pl1": 18,
    "evg": 19,
    "joi": 20,
    "jol": 21,
    "syn": 22,
    "rcv": 23,
    "dis": 24,
    "rsv": 25,
    "mod": 26,
    "rec": 27,
    "snd": 28,
    "drm": 29,
    "loas": 30,
    "spill": 31,
    "bcw": 32,
    "rsi": 33,
}

_OPCODE_NAMES = {v: k for k, v in _OPCODE_IDS.items()}

_MODULE_ORDER = [
    "memory",
    "arithmetic",
    "montgomery",
    "ntt",
    "base_conv",
    "automorphism",
    "transpose",
]

_HOST_MANAGED_MODULE = "host_comm"

_MODULE_IDS = {
    "memory": 1,
    "arithmetic": 2,
    "montgomery": 3,
    "ntt": 4,
    "base_conv": 5,
    "automorphism": 6,
    "transpose": 7,
}

_MODULE_KERNELS = {
    "memory": "cinnamon_memory",
    "arithmetic": "cinnamon_arithmetic",
    "montgomery": "cinnamon_montgomery",
    "ntt": "cinnamon_ntt",
    "base_conv": "cinnamon_base_conv",
    "automorphism": "cinnamon_automorphism",
    "transpose": "cinnamon_transpose",
}

_OPCODE_TO_MODULE = {
    "load": "memory",
    "loas": "memory",
    "store": "memory",
    "spill": "memory",
    "mov": "memory",
    "evg": "memory",
    "rec": "memory",
    "snd": "memory",
    "add": "arithmetic",
    "sub": "arithmetic",
    "ads": "arithmetic",
    "sus": "arithmetic",
    "sud": "arithmetic",
    "con": "arithmetic",
    "neg": "arithmetic",
    "mul": "montgomery",
    "mup": "montgomery",
    "mus": "montgomery",
    "ntt": "ntt",
    "int": "ntt",
    "bci": "base_conv",
    "pl1": "base_conv",
    "bcw": "base_conv",
    "rsi": "base_conv",
    "rsv": "base_conv",
    "mod": "base_conv",
    "rot": "automorphism",
    "joi": _HOST_MANAGED_MODULE,
    "jol": _HOST_MANAGED_MODULE,
    "syn": _HOST_MANAGED_MODULE,
    "rcv": _HOST_MANAGED_MODULE,
    "dis": _HOST_MANAGED_MODULE,
    "drm": _HOST_MANAGED_MODULE,
}

_SECTION_IDS = {
    "ciphertext": 1,
    "plaintext": 2,
    "scalar": 3,
    "output": 4,
    "evalkey": 5,
}

_IMM_OPERAND_ID = 0xFFF
_TOKEN_OPERAND_ID = 0xFFE
_MAX_NTT_SPAN = 64
_JSONL_ENV = "CINNAMON_FPGA_JSONL_LOG"
_JSONL_DEBUG_ENV = "CINNAMON_FPGA_JSONL_DEBUG_LOG"
_JSONL_DEBUG_FLAG_ENV = "CINNAMON_FPGA_JSONL_DEBUG"
_EXPECTED_LOG_CAP_ENV = "CINNAMON_FPGA_EXPECTED_LOG_CAP"
_DEBUG_PREVIEW_WORDS = 8
_expected_log_count = 0


def _env_flag(name: str) -> bool:
    value = os.getenv(name, "").strip().lower()
    return value in {"1", "true", "yes", "on"}


def _append_jsonl(path: str, payload: Dict[str, object]) -> None:
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
    payload: Dict[str, object],
    debug_payload: Dict[str, object] | None = None,
) -> None:
    always_path = os.getenv(_JSONL_ENV, "").strip()
    debug_enabled = _env_flag(_JSONL_DEBUG_FLAG_ENV)
    debug_path = os.getenv(_JSONL_DEBUG_ENV, "").strip() or always_path
    if not always_path and not (debug_enabled and debug_path):
        return

    base_payload: Dict[str, object] = {
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


def _consume_expected_log_budget() -> bool:
    global _expected_log_count
    try:
        budget = int(os.getenv(_EXPECTED_LOG_CAP_ENV, "32"))
    except ValueError:
        budget = 32
    if budget <= 0 or _expected_log_count >= budget:
        return False
    _expected_log_count += 1
    return True

_REG_RE = re.compile(r"^r(\d+)(?:\[X\])?$", re.IGNORECASE)
_SCALAR_RE = re.compile(r"^s(\d+)(?:\[X\])?$", re.IGNORECASE)
_BCU_RE = re.compile(r"^b(\d+)\{(\d+)\}$", re.IGNORECASE)
_BCU_UNIT_RE = re.compile(r"^B(\d+)$", re.IGNORECASE)
_SIGNED_INT_RE = re.compile(r"^[+-]?\d+$")
_TOKEN_RE = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_:]*)\(([-+]?\d+)\)(\[X\])?(\{F\})?$"
)


@dataclasses.dataclass(frozen=True)
class InstructionStream:
    partition_id: int
    path: pathlib.Path
    lines: List[str]
    words: List[int]
    opcodes: Dict[str, int]


@dataclasses.dataclass(frozen=True)
class ProgramInputs:
    path: pathlib.Path
    sections: Dict[str, List[str]]
    words: List[int]


@dataclasses.dataclass(frozen=True)
class ModuleInstructionBucket:
    module: str
    instruction_words: List[int]
    opcode_counts: Dict[str, int]


@dataclasses.dataclass(frozen=True)
class DecodedInstruction:
    opcode: int
    dst: int
    src0: int
    src1: int
    rns: int
    flags: int
    imm0: int
    imm1: int
    aux: int
    line_crc: int


def _line_crc32(value: str) -> int:
    return zlib.crc32(value.encode("utf-8")) & 0xFFFFFFFF


def _hash_u64(value: str) -> int:
    crc = _line_crc32(value)
    length = len(value) & 0xFFFF
    mix = (crc << 16) ^ length ^ 0x9E3779B9
    x = mix & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 33
    x = (x * 0xFF51AFD7ED558CCD) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 33
    x = (x * 0xC4CEB9FE1A85EC53) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 33
    return x


def extract_opcode(line: str) -> str:
    match = re.match(r"\s*([a-zA-Z0-9_]+)\b", line)
    return match.group(1).lower() if match else "unknown"


def _decode_signed32(value: int) -> int:
    value &= 0xFFFFFFFF
    if value & 0x80000000:
        return -((~value + 1) & 0xFFFFFFFF)
    return value


def _encode_signed32(value: int) -> int:
    return value & 0xFFFFFFFF


def _token_kind(name: str) -> int:
    head = name[:1].lower()
    if head == "i":
        return 1
    if head == "o":
        return 2
    if head == "k":
        return 3
    if head == "p":
        return 4
    if head == "s":
        return 5
    if head == "x":
        return 6
    if head == "z":
        return 7
    if head == "e":
        return 8
    return 15


def _encode_stream_token_parts(name: str, limb_index: int, dead: bool, free: bool) -> int:
    cleaned = name.strip()
    match = re.match(r"^([A-Za-z_]+)(\d*)$", cleaned)
    if match:
        alpha = match.group(1)
        token_id = int(match.group(2)) if match.group(2) else 0
    else:
        alpha = cleaned
        token_id = 0

    c0 = ord(alpha[0]) & 0xFF if alpha else 0
    c1 = ord(alpha[1]) & 0xFF if len(alpha) > 1 else 0
    kind = _token_kind(alpha)

    packed = 0
    packed |= kind & 0xFF
    packed |= (token_id & 0xFFFF) << 8
    packed |= (int(limb_index) & 0xFFFF) << 24
    packed |= (1 if dead else 0) << 40
    packed |= (1 if free else 0) << 41
    packed |= (c0 & 0xFF) << 48
    packed |= (c1 & 0xFF) << 56
    return packed & 0xFFFFFFFFFFFFFFFF


def encode_stream_token(token: str) -> int:
    tok = token.strip()
    m = _TOKEN_RE.match(tok)
    if not m:
        return _encode_stream_token_parts(tok, limb_index=0, dead=False, free=False)
    name = m.group(1)
    limb = int(m.group(2))
    dead = m.group(3) is not None
    free = m.group(4) is not None
    return _encode_stream_token_parts(name, limb, dead, free)


def _combine_aux(current: int, value: int) -> int:
    if value == 0:
        return current & 0xFFFFFFFFFFFFFFFF
    if current == 0:
        return value & 0xFFFFFFFFFFFFFFFF
    return (((current << 1) ^ value) & 0xFFFFFFFFFFFFFFFF)


def _pack_list_summary(primary: Sequence[int], secondary: Sequence[int]) -> int:
    p_count = len(primary) & 0xFFFF
    s_count = len(secondary) & 0xFFFF
    p0 = (primary[0] if primary else 0) & 0xFFFF
    s0 = (secondary[0] if secondary else 0) & 0xFFFF
    return (
        (p_count & 0xFFFF)
        | ((s_count & 0xFFFF) << 16)
        | ((p0 & 0xFFFF) << 32)
        | ((s0 & 0xFFFF) << 48)
    )


def _parse_uint_list(text: str) -> List[int]:
    items = [token.strip() for token in text.split(",") if token.strip()]
    values: List[int] = []
    for item in items:
        if _SIGNED_INT_RE.match(item):
            values.append(int(item))
    return values


def _parse_register_list(text: str) -> List[int]:
    items = [token.strip() for token in text.split(",") if token.strip()]
    values: List[int] = []
    for item in items:
        reg = _REG_RE.match(item)
        if reg:
            values.append(int(reg.group(1)) & 0xFFF)
    return values


def _parse_sync_prefix(rest: str) -> tuple[int, int, str] | None:
    match = re.match(r"^\s*@\s*([0-9]+)\s*:\s*([0-9]+)\s*(.*)$", rest)
    if not match:
        return None
    return (int(match.group(1)), int(match.group(2)), match.group(3).strip())


def _parse_operand(token: str) -> tuple[int, bool, int, int]:
    tok = token.strip()
    if not tok:
        return (0, False, 0, 0)

    reg = _REG_RE.match(tok)
    if reg:
        return (int(reg.group(1)) & 0xFFF, False, 0, 0)

    scalar = _SCALAR_RE.match(tok)
    if scalar:
        sid = 0xC00 + (int(scalar.group(1)) & 0x3F)
        return (sid & 0xFFF, False, 0, 0)

    bcu = _BCU_RE.match(tok)
    if bcu:
        bcu_id = int(bcu.group(1)) & 0x1F
        bcu_idx = int(bcu.group(2)) & 0x7F
        packed = 0x800 + (bcu_id << 7) + bcu_idx
        return (packed & 0xFFF, False, 0, 0)

    bcu_unit = _BCU_UNIT_RE.match(tok)
    if bcu_unit:
        packed = 0xA00 + (int(bcu_unit.group(1)) & 0xFF)
        return (packed & 0xFFF, False, 0, 0)

    if _SIGNED_INT_RE.match(tok):
        return (_IMM_OPERAND_ID, True, int(tok), 0)

    # Memory/evalkey/program token path with structured encoding.
    return (_TOKEN_OPERAND_ID, False, 0, encode_stream_token(tok))


def _parse_rns(body: str) -> tuple[str, int]:
    if "|" not in body:
        return body.strip(), 0
    expr, tail = body.rsplit("|", 1)
    tail = tail.strip()
    m = re.match(r"^([+-]?\d+)", tail)
    if not m:
        return expr.strip(), 0
    return expr.strip(), int(m.group(1))


def encode_instruction_descriptor(line: str) -> List[int]:
    opcode_name = extract_opcode(line)
    opcode_id = _OPCODE_IDS.get(opcode_name, 255)

    rest = line.strip()
    if " " in rest:
        rest = rest.split(" ", 1)[1].strip()
    else:
        rest = ""

    dst = 0
    src0 = 0
    src1 = 0
    rns = 0
    flags = 0
    imm0 = 0
    imm1 = 0
    aux = 0

    def assign_dst(token: str) -> None:
        nonlocal dst, flags, imm0, aux
        oid, is_imm, imm_val, o_aux = _parse_operand(token)
        dst = oid
        if is_imm:
            flags |= 1 << 2
            imm0 = imm_val
        aux = _combine_aux(aux, o_aux)

    def assign_src(token: str, use_src1: bool = False) -> None:
        nonlocal src0, src1, flags, imm0, imm1, aux
        oid, is_imm, imm_val, o_aux = _parse_operand(token)
        if use_src1:
            src1 = oid
            if is_imm:
                flags |= 1 << 1
                imm1 = imm_val
        else:
            src0 = oid
            if is_imm:
                flags |= 1 << 0
                imm0 = imm_val
        aux = _combine_aux(aux, o_aux)

    binary_ops = {"add", "sub", "mul", "mup", "mus", "ads", "sus", "sud"}
    unary_ops = {"mov", "neg", "con", "ntt", "int", "rot"}

    if opcode_name in {"load", "loas", "evg", "rec"} and ":" in rest:
        dest_text, rhs = rest.split(":", 1)
        expr, rns = _parse_rns(rhs)
        assign_dst(dest_text.strip())
        if expr.strip():
            assign_src(expr.strip(), use_src1=False)
        rns &= 0xFFF
    elif opcode_name in {"store", "spill", "snd"} and ":" in rest:
        src_text, rhs = rest.split(":", 1)
        expr, rns = _parse_rns(rhs)
        assign_src(src_text.strip(), use_src1=False)
        if expr.strip():
            assign_src(expr.strip(), use_src1=True)
        rns &= 0xFFF
    elif opcode_name in binary_ops and ":" in rest:
        dest_text, rhs = rest.split(":", 1)
        expr, rns = _parse_rns(rhs)
        assign_dst(dest_text.strip())
        src_tokens = [t.strip() for t in expr.split(",") if t.strip()]
        if len(src_tokens) > 0:
            assign_src(src_tokens[0], use_src1=False)
        if len(src_tokens) > 1:
            assign_src(src_tokens[1], use_src1=True)
        rns &= 0xFFF
    elif opcode_name in unary_ops and ":" in rest:
        local_rest = rest
        if opcode_name == "rot":
            pref = re.match(r"^\s*([+-]?[0-9]+)\s+(.*)$", local_rest)
            if pref:
                imm0 = int(pref.group(1))
                local_rest = pref.group(2).strip()
        dest_text, rhs = local_rest.split(":", 1)
        expr, rns = _parse_rns(rhs)
        assign_dst(dest_text.strip())
        src_tokens = [t.strip() for t in expr.split(",") if t.strip()]
        if len(src_tokens) > 0:
            assign_src(src_tokens[0], use_src1=False)
        if len(src_tokens) > 1:
            assign_src(src_tokens[1], use_src1=True)
        rns &= 0xFFF
        if opcode_name == "rot" and imm0 == 0:
            imm0 = rns
    elif opcode_name == "bci":
        match = re.match(
            r"^\s*B([0-9]+)\s*:\s*\[([^\]]*)\]\s*,\s*\[([^\]]*)\]\s*$", rest
        )
        if match:
            bcu_id = int(match.group(1))
            dst = (0xA00 + (bcu_id & 0xFF)) & 0xFFF
            dst_bases = _parse_uint_list(match.group(2))
            src_bases = _parse_uint_list(match.group(3))
            imm0 = dst_bases[0] if dst_bases else 0
            imm1 = src_bases[0] if src_bases else 0
            aux = _pack_list_summary(dst_bases, src_bases)
    elif opcode_name in {"pl1", "bcw"}:
        match = re.match(r"^\s*B([0-9]+)\s*:\s*([^|]+?)(?:\|\s*([+-]?[0-9]+))?\s*$", rest)
        if match:
            dst = (0xA00 + (int(match.group(1)) & 0xFF)) & 0xFFF
            assign_src(match.group(2).strip(), use_src1=False)
            rns = int(match.group(3)) if match.group(3) else 0
            rns &= 0xFFF
        elif ":" in rest:
            dest_text, rhs = rest.split(":", 1)
            expr, rns = _parse_rns(rhs)
            assign_dst(dest_text.strip())
            if expr.strip():
                assign_src(expr.strip(), use_src1=False)
            rns &= 0xFFF
    elif opcode_name == "rsi":
        match = re.match(r"^\s*\{([^}]*)\}\s*$", rest)
        if match:
            regs = _parse_register_list(match.group(1))
            if len(regs) > 0:
                dst = regs[0]
            if len(regs) > 1:
                src0 = regs[1]
            if len(regs) > 2:
                src1 = regs[2]
            imm0 = len(regs)
            imm1 = regs[-1] if regs else 0
            aux = _pack_list_summary(regs, regs[2:])
    elif opcode_name == "rsv":
        match = re.match(
            r"^\s*\{([^}]*)\}\s*:\s*([^:]+?)\s*:\s*\[([^\]]*)\]\s*(?:\|\s*([+-]?[0-9]+))?\s*$",
            rest,
        )
        if match:
            dest_regs = _parse_register_list(match.group(1))
            assign_src(match.group(2).strip(), use_src1=False)
            base_list = _parse_uint_list(match.group(3))
            if len(dest_regs) > 0:
                dst = dest_regs[0]
            if len(dest_regs) > 1:
                src1 = dest_regs[1]
            imm0 = len(dest_regs)
            imm1 = len(base_list)
            rns = int(match.group(4)) if match.group(4) else 0
            rns &= 0xFFF
            aux = _pack_list_summary(dest_regs, base_list)
    elif opcode_name == "mod" and ":" in rest:
        match = re.match(r"^\s*([^:]+?)\s*:\s*\{([^}]*)\}\s*(?:\|\s*([+-]?[0-9]+))?\s*$", rest)
        if match:
            assign_dst(match.group(1).strip())
            src_regs = _parse_register_list(match.group(2))
            if len(src_regs) > 0:
                src0 = src_regs[0]
            if len(src_regs) > 1:
                src1 = src_regs[1]
            imm0 = len(src_regs)
            imm1 = src_regs[-1] if src_regs else 0
            rns = int(match.group(3)) if match.group(3) else 0
            rns &= 0xFFF
            aux = _pack_list_summary(src_regs, src_regs[2:])
        else:
            dest_text, rhs = rest.split(":", 1)
            expr, rns = _parse_rns(rhs)
            assign_dst(dest_text.strip())
            if expr.strip():
                assign_src(expr.strip(), use_src1=False)
            rns &= 0xFFF
    elif opcode_name == "dis":
        sync = _parse_sync_prefix(rest)
        if sync is not None:
            sync_id, sync_size, tail = sync
            imm0 = sync_id
            imm1 = sync_size
            match = re.match(r"^\s*:\s*(.+?)\s*$", tail)
            if match:
                assign_src(match.group(1).strip(), use_src1=False)
    elif opcode_name == "rcv":
        sync = _parse_sync_prefix(rest)
        if sync is not None:
            sync_id, sync_size, tail = sync
            imm0 = sync_id
            imm1 = sync_size
            match = re.match(r"^\s*(.+?)\s*:\s*$", tail)
            if match:
                assign_dst(match.group(1).strip())
    elif opcode_name == "joi":
        sync = _parse_sync_prefix(rest)
        if sync is not None:
            sync_id, sync_size, tail = sync
            imm0 = sync_id
            imm1 = sync_size
            expr, rns = _parse_rns(tail)
            rns &= 0xFFF
            if ":" in expr:
                left, right = expr.split(":", 1)
                if left.strip():
                    assign_dst(left.strip())
                if right.strip():
                    assign_src(right.strip(), use_src1=False)
            elif expr.strip():
                assign_src(expr.strip(), use_src1=False)
    elif opcode_name in {"syn", "jol", "drm"}:
        sync = _parse_sync_prefix(rest)
        if sync is not None:
            sync_id, sync_size, tail = sync
            imm0 = sync_id
            imm1 = sync_size
            if tail:
                aux = _combine_aux(aux, encode_stream_token(tail))
        else:
            part = re.match(r"^\s*p([0-9]+)\s*$", rest, flags=re.IGNORECASE)
            if part:
                imm0 = int(part.group(1))
    elif ":" in rest:
        dest_text, rhs = rest.split(":", 1)
        expr, rns = _parse_rns(rhs)
        rns &= 0xFFF
        if dest_text.strip():
            assign_dst(dest_text.strip())
        src_tokens = [t.strip() for t in expr.split(",") if t.strip()]
        if len(src_tokens) > 0:
            assign_src(src_tokens[0], use_src1=False)
        if len(src_tokens) > 1:
            assign_src(src_tokens[1], use_src1=True)
    elif rest:
        aux = _combine_aux(aux, encode_stream_token(rest))

    word0 = (
        (opcode_id & 0xFF)
        | ((dst & 0xFFF) << 8)
        | ((src0 & 0xFFF) << 20)
        | ((src1 & 0xFFF) << 32)
        | ((rns & 0xFFF) << 44)
        | ((flags & 0xFF) << 56)
    )
    word1 = _encode_signed32(imm0) | (_encode_signed32(imm1) << 32)
    word2 = aux & 0xFFFFFFFFFFFFFFFF
    word3 = _line_crc32(line)
    return [word0, word1, word2, word3]


def encode_instruction_line(line: str) -> int:
    # Kept for compatibility; returns the first descriptor word.
    return encode_instruction_descriptor(line)[0]


def encode_program_input_line(section: str, line: str) -> int:
    section_id = _SECTION_IDS.get(section, 255)
    line_crc = _line_crc32(line)
    line_len = min(len(line), 0xFFFF)
    return ((section_id & 0xFF) << 56) | ((line_len & 0xFFFF) << 40) | line_crc


def _read_nonempty_lines(path: pathlib.Path) -> List[str]:
    lines: List[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        lines.append(line)
    return lines


def parse_instruction_file(path: pathlib.Path, partition_id: int) -> InstructionStream:
    lines = _read_nonempty_lines(path)
    clean_lines = [
        line
        for line in lines
        if line != ";"
        and not line.startswith("Instruction Stream")
        and not re.match(r"^[A-Za-z_][A-Za-z0-9_]*:$", line)
    ]

    words: List[int] = []
    for line in clean_lines:
        words.extend(encode_instruction_descriptor(line))

    opcodes = collections.Counter(extract_opcode(line) for line in clean_lines)
    return InstructionStream(
        partition_id=partition_id,
        path=path,
        lines=clean_lines,
        words=words,
        opcodes=dict(opcodes),
    )


def resolve_instruction_files(instruction_base: pathlib.Path, num_partitions: int) -> List[pathlib.Path]:
    files: List[pathlib.Path] = []

    if num_partitions == 1:
        if instruction_base.exists():
            files.append(instruction_base)
            return files

        numbered = instruction_base.with_name(f"{instruction_base.name}0")
        if numbered.exists():
            files.append(numbered)
            return files

        raise FileNotFoundError(
            f"Could not find instruction file: {instruction_base} or {numbered}"
        )

    for partition_id in range(num_partitions):
        file_path = instruction_base.with_name(f"{instruction_base.name}{partition_id}")
        if not file_path.exists():
            raise FileNotFoundError(f"Missing instruction file for partition {partition_id}: {file_path}")
        files.append(file_path)

    return files


def load_instruction_streams(
    instruction_file_base: str | pathlib.Path,
    num_partitions: int,
) -> List[InstructionStream]:
    base = pathlib.Path(instruction_file_base)
    files = resolve_instruction_files(base, num_partitions)
    return [parse_instruction_file(path, partition_id=i) for i, path in enumerate(files)]


def parse_program_inputs(path: str | pathlib.Path) -> ProgramInputs:
    file_path = pathlib.Path(path)
    if not file_path.exists():
        raise FileNotFoundError(f"program_inputs not found: {file_path}")

    sections: Dict[str, List[str]] = {
        "ciphertext": [],
        "plaintext": [],
        "scalar": [],
        "output": [],
        "evalkey": [],
    }

    header_to_section = {
        "Ciphertext Stream:": "ciphertext",
        "Plaintext Stream:": "plaintext",
        "Scalar Stream:": "scalar",
        "Output Stream:": "output",
        "Evalkey Stream:": "evalkey",
    }

    active_section: str | None = None
    for raw in file_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line in header_to_section:
            active_section = header_to_section[line]
            continue
        if line == ";":
            active_section = None
            continue
        if active_section is None:
            continue
        sections[active_section].append(line)

    words: List[int] = []
    for section_name, entries in sections.items():
        words.extend(encode_program_input_line(section_name, entry) for entry in entries)

    return ProgramInputs(path=file_path, sections=sections, words=words)


def summarize_opcodes(streams: Sequence[InstructionStream]) -> Dict[str, int]:
    summary: collections.Counter[str] = collections.Counter()
    for stream in streams:
        summary.update(stream.opcodes)
    return dict(summary)


def flatten_words(streams: Iterable[InstructionStream]) -> List[int]:
    words: List[int] = []
    for stream in streams:
        words.extend(stream.words)
    return words


def module_order() -> List[str]:
    return list(_MODULE_ORDER)


def module_kernels() -> Dict[str, str]:
    return dict(_MODULE_KERNELS)


def host_managed_module_name() -> str:
    return _HOST_MANAGED_MODULE


def module_for_opcode(opcode: str) -> str:
    return _OPCODE_TO_MODULE.get(opcode.lower(), "arithmetic")


def _iter_descriptors(words: Sequence[int]) -> Iterator[DecodedInstruction]:
    if len(words) % _INSTRUCTION_WORD_STRIDE != 0:
        raise ValueError(
            f"instruction word length must be a multiple of {_INSTRUCTION_WORD_STRIDE}, got {len(words)}"
        )

    for i in range(0, len(words), _INSTRUCTION_WORD_STRIDE):
        w0 = int(words[i + 0]) & 0xFFFFFFFFFFFFFFFF
        w1 = int(words[i + 1]) & 0xFFFFFFFFFFFFFFFF
        w2 = int(words[i + 2]) & 0xFFFFFFFFFFFFFFFF
        w3 = int(words[i + 3]) & 0xFFFFFFFFFFFFFFFF

        yield DecodedInstruction(
            opcode=w0 & 0xFF,
            dst=(w0 >> 8) & 0xFFF,
            src0=(w0 >> 20) & 0xFFF,
            src1=(w0 >> 32) & 0xFFF,
            rns=(w0 >> 44) & 0xFFF,
            flags=(w0 >> 56) & 0xFF,
            imm0=_decode_signed32(w1),
            imm1=_decode_signed32(w1 >> 32),
            aux=w2,
            line_crc=w3,
        )


def split_stream_by_module(stream: InstructionStream) -> Dict[str, ModuleInstructionBucket]:
    raw_words: Dict[str, List[int]] = {name: [] for name in _MODULE_ORDER}
    raw_words[_HOST_MANAGED_MODULE] = []

    raw_counts: Dict[str, collections.Counter[str]] = {
        name: collections.Counter() for name in list(_MODULE_ORDER) + [_HOST_MANAGED_MODULE]
    }

    if len(stream.words) != len(stream.lines) * _INSTRUCTION_WORD_STRIDE:
        raise ValueError(
            f"encoded instruction words do not match line count for {stream.path}: "
            f"{len(stream.words)} words vs {len(stream.lines)} lines"
        )

    for idx, line in enumerate(stream.lines):
        opcode = extract_opcode(line)
        module = module_for_opcode(opcode)
        start = idx * _INSTRUCTION_WORD_STRIDE
        raw_words[module].extend(stream.words[start : start + _INSTRUCTION_WORD_STRIDE])
        raw_counts[module][opcode] += 1

    buckets: Dict[str, ModuleInstructionBucket] = {}
    for module_name, words in raw_words.items():
        buckets[module_name] = ModuleInstructionBucket(
            module=module_name,
            instruction_words=words,
            opcode_counts=dict(raw_counts[module_name]),
        )
    return buckets


def _rotl64(value: int, shift: int) -> int:
    s = shift & 63
    masked = value & 0xFFFFFFFFFFFFFFFF
    if s == 0:
        return masked
    return ((masked << s) & 0xFFFFFFFFFFFFFFFF) | (masked >> (64 - s))


def _rotr64(value: int, shift: int) -> int:
    s = shift & 63
    masked = value & 0xFFFFFFFFFFFFFFFF
    if s == 0:
        return masked
    return (masked >> s) | ((masked << (64 - s)) & 0xFFFFFFFFFFFFFFFF)


def _hash_mix_u64(value: int) -> int:
    x = value & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 33
    x = (x * 0xFF51AFD7ED558CCD) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 33
    x = (x * 0xC4CEB9FE1A85EC53) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 33
    return x


def _mod_add(a: int, b: int, mod: int) -> int:
    aa = a % mod
    bb = b % mod
    s = aa + bb
    return s - mod if s >= mod else s


def _mod_sub(a: int, b: int, mod: int) -> int:
    aa = a % mod
    bb = b % mod
    return aa - bb if aa >= bb else aa + (mod - bb)


def _mod_mul(a: int, b: int, mod: int) -> int:
    return ((a % mod) * (b % mod)) % mod


def _egcd(a: int, b: int) -> tuple[int, int, int]:
    if b == 0:
        return (a, 1, 0)
    g, x1, y1 = _egcd(b, a % b)
    return (g, y1, x1 - (a // b) * y1)


def _mod_inv(a: int, mod: int) -> int:
    if mod == 0:
        return 0
    g, x, _ = _egcd(a % mod, mod)
    if g != 1:
        return 0
    return x % mod


def _abs_mod_u32(value: int, mod: int) -> int:
    if mod == 0:
        return 0
    signed = int(value)
    if signed >= 0:
        return int(signed % mod)
    return int((-signed) % mod)


def _is_power_of_two(value: int) -> bool:
    v = int(value)
    return v > 0 and (v & (v - 1)) == 0


def _select_ntt_span(imm0: int, register_count: int) -> int:
    mod = _MAX_NTT_SPAN + 1
    signed = int(imm0)
    span = (signed % mod) if signed >= 0 else ((-signed) % mod)
    if span < 2 or (not _is_power_of_two(span)):
        if register_count >= 8:
            span = 8
        elif register_count >= 4:
            span = 4
        else:
            span = 2
    while span > register_count and span > 2:
        span >>= 1
    return span


def _choose_four_step_shape(span: int) -> tuple[int, int]:
    levels = int(span).bit_length() - 1
    row_levels = levels // 2
    rows = 1 << row_levels
    cols = 1 << (levels - row_levels)
    return rows, cols


def _resolve_negacyclic_roots(mod: int, span: int, prime_id: int) -> Dict[str, int]:
    entry = NEGACYCLIC_ROOT_TABLE.get((int(mod), int(span)))
    if entry is not None:
        return {
            "psi": int(entry["psi"]) % mod,
            "psi_inv": int(entry["psi_inv"]) % mod,
            "omega": int(entry["omega"]) % mod,
            "omega_inv": int(entry["omega_inv"]) % mod,
        }
    if mod <= 2:
        return {"psi": 1, "psi_inv": 1, "omega": 1, "omega_inv": 1}
    omega = 1 + ((int(prime_id) + 17) % (mod - 1))
    omega_inv = _mod_inv(omega, mod)
    return {
        "psi": 1,
        "psi_inv": 1,
        "omega": omega % mod,
        "omega_inv": (omega_inv % mod) if omega_inv != 0 else 1,
    }


def _small_cyclic_dft(input_values: Sequence[int], length: int, root: int, mod: int) -> List[int]:
    out = [0] * length
    for k in range(length):
        step = pow(root, k, mod)
        tw = 1
        acc = 0
        for n in range(length):
            acc = _mod_add(acc, _mod_mul(input_values[n], tw, mod), mod)
            tw = _mod_mul(tw, step, mod)
        out[k] = acc
    return out


def _cyclic_ntt_four_step(
    values: List[int], span: int, omega: int, mod: int, inverse_cyclic: bool
) -> None:
    if span < 2 or mod == 0:
        return
    rows, cols = _choose_four_step_shape(span)
    if rows * cols != span:
        return

    stage1 = [0] * span
    stage2 = [0] * span
    row_root = pow(omega, rows, mod)
    col_root = pow(omega, cols, mod)

    for n1 in range(rows):
        lane = [int(values[n1 + rows * n2]) % mod for n2 in range(cols)]
        row_out = _small_cyclic_dft(lane, cols, row_root, mod)
        for k2 in range(cols):
            stage1[n1 * cols + k2] = row_out[k2]

    for n1 in range(rows):
        tw = 1
        step = pow(omega, n1, mod)
        for k2 in range(cols):
            idx = n1 * cols + k2
            stage1[idx] = _mod_mul(stage1[idx], tw, mod)
            tw = _mod_mul(tw, step, mod)

    for n1 in range(rows):
        for k2 in range(cols):
            stage2[k2 * rows + n1] = stage1[n1 * cols + k2]

    for k2 in range(cols):
        lane = [stage2[k2 * rows + n1] for n1 in range(rows)]
        col_out = _small_cyclic_dft(lane, rows, col_root, mod)
        for k1 in range(rows):
            values[k2 + cols * k1] = col_out[k1]

    if inverse_cyclic:
        span_inv = _mod_inv(span, mod)
        if span_inv != 0:
            for i in range(span):
                values[i] = _mod_mul(values[i], span_inv, mod)


def _ntt_apply_negacyclic_four_step(
    values: List[int], span: int, mod: int, prime_id: int, inverse: bool
) -> None:
    if span < 2 or mod == 0 or (not _is_power_of_two(span)):
        return
    roots = _resolve_negacyclic_roots(mod, span, prime_id)
    if not inverse:
        twist = 1
        for i in range(span):
            values[i] = _mod_mul(values[i] % mod, twist, mod)
            twist = _mod_mul(twist, roots["psi"], mod)
        _cyclic_ntt_four_step(values, span, roots["omega"], mod, False)
        return

    _cyclic_ntt_four_step(values, span, roots["omega_inv"], mod, True)
    twist = 1
    for i in range(span):
        values[i] = _mod_mul(values[i] % mod, twist, mod)
        twist = _mod_mul(twist, roots["psi_inv"], mod)


def _compute_trace(state: Sequence[int], register_count: int, module_id: int, executed: int) -> int:
    acc = (0x9E3779B97F4A7C15 ^ int(module_id)) & 0xFFFFFFFFFFFFFFFF
    acc ^= (int(executed) << 16) & 0xFFFFFFFFFFFFFFFF
    for i in range(register_count):
        acc ^= _hash_mix_u64((int(state[i]) ^ int(i + 1)) & 0xFFFFFFFFFFFFFFFF)
        acc = _rotl64(acc, 7) & 0xFFFFFFFFFFFFFFFF
    return acc


def _montgomery_reduce_ntt_friendly(a: int, q: int) -> int:
    if q == 0:
        return 0
    r = 1 << (17 * 2)
    r_mod_q = r % q
    r_inv = _mod_inv(r_mod_q, q)
    if r_inv == 0:
        return a % q
    return _mod_mul(a % q, r_inv, q)


def _montgomery_mul_ntt_friendly(a: int, b: int, q: int) -> int:
    return _montgomery_reduce_ntt_friendly(_mod_mul(a, b, q), q)


def _opcode_matches_module(module_id: int, opcode: int) -> bool:
    if module_id == 1:
        return opcode in (
            _OPCODE_IDS["load"],
            _OPCODE_IDS["loas"],
            _OPCODE_IDS["store"],
            _OPCODE_IDS["spill"],
            _OPCODE_IDS["mov"],
            _OPCODE_IDS["evg"],
            _OPCODE_IDS["rec"],
            _OPCODE_IDS["snd"],
        )
    if module_id == 2:
        return opcode in (
            _OPCODE_IDS["add"],
            _OPCODE_IDS["sub"],
            _OPCODE_IDS["ads"],
            _OPCODE_IDS["sus"],
            _OPCODE_IDS["sud"],
            _OPCODE_IDS["con"],
            _OPCODE_IDS["neg"],
        )
    if module_id == 3:
        return opcode in (_OPCODE_IDS["mul"], _OPCODE_IDS["mup"], _OPCODE_IDS["mus"])
    if module_id == 4:
        return opcode in (_OPCODE_IDS["ntt"], _OPCODE_IDS["int"])
    if module_id == 5:
        return opcode in (
            _OPCODE_IDS["bci"],
            _OPCODE_IDS["pl1"],
            _OPCODE_IDS["bcw"],
            _OPCODE_IDS["rsi"],
            _OPCODE_IDS["rsv"],
            _OPCODE_IDS["mod"],
        )
    if module_id == 6:
        return opcode == _OPCODE_IDS["rot"]
    if module_id == 7:
        return opcode in (_OPCODE_IDS["rec"], _OPCODE_IDS["rsv"])
    return False


def _lookup_stream_value(
    input_words: Sequence[int],
    state_base: int,
    register_count: int,
    token_key: int,
    mod: int,
) -> int:
    table_base = state_base + register_count
    if table_base >= len(input_words):
        return 0
    table_count = int(input_words[table_base])
    cursor = table_base + 1
    for _ in range(max(table_count, 0)):
        if cursor + 1 >= len(input_words):
            break
        key = int(input_words[cursor]) & 0xFFFFFFFFFFFFFFFF
        value = int(input_words[cursor + 1]) & 0xFFFFFFFFFFFFFFFF
        if key == (token_key & 0xFFFFFFFFFFFFFFFF):
            return value % mod
        cursor += 2
    return 0


def _load_operand(
    state: List[int],
    register_count: int,
    inst: DecodedInstruction,
    use_src1: bool,
    mod: int,
    input_words: Sequence[int],
    state_base: int,
) -> int:
    src = inst.src1 if use_src1 else inst.src0
    imm_flag_bit = 1 if use_src1 else 0
    is_imm = ((inst.flags >> imm_flag_bit) & 0x1) != 0

    if is_imm or src == _IMM_OPERAND_ID:
        imm = inst.imm1 if use_src1 else inst.imm0
        if imm >= 0:
            return imm % mod
        mag = (-imm) % mod
        return 0 if mag == 0 else (mod - mag)

    if src == _TOKEN_OPERAND_ID:
        return _lookup_stream_value(
            input_words=input_words,
            state_base=state_base,
            register_count=register_count,
            token_key=inst.aux,
            mod=mod,
        )

    if register_count == 0:
        return 0

    return state[src % register_count] % mod


def _apply_module_op(
    module_id: int,
    inst: DecodedInstruction,
    src0: int,
    src1: int,
    mod: int,
    state: List[int],
    register_count: int,
) -> int:
    op = inst.opcode

    if module_id == 1:
        if op in (
            _OPCODE_IDS["load"],
            _OPCODE_IDS["loas"],
            _OPCODE_IDS["evg"],
            _OPCODE_IDS["rec"],
        ):
            return src0 % mod
        if op == _OPCODE_IDS["mov"]:
            return src0
        return src0

    if module_id == 2:
        if op in (_OPCODE_IDS["add"], _OPCODE_IDS["ads"]):
            return _mod_add(src0, src1, mod)
        if op in (_OPCODE_IDS["sub"], _OPCODE_IDS["sus"]):
            return _mod_sub(src0, src1, mod)
        if op == _OPCODE_IDS["sud"]:
            diff = _mod_sub(src0, src1, mod)
            return ((diff + mod) >> 1) % mod if (diff & 1) else ((diff >> 1) % mod)
        if op == _OPCODE_IDS["con"]:
            return src0 % mod
        if op == _OPCODE_IDS["neg"]:
            return 0 if src0 == 0 else (mod - (src0 % mod))
        return src0

    if module_id == 3:
        if op in (_OPCODE_IDS["mul"], _OPCODE_IDS["mup"], _OPCODE_IDS["mus"]):
            return _montgomery_mul_ntt_friendly(src0, src1, mod)
        return src0

    if module_id == 4:
        twiddle = int(inst.rns) % mod
        if twiddle <= 1:
            twiddle = 1 + (int(inst.line_crc) % (mod - 1))
        if op == _OPCODE_IDS["ntt"]:
            return _mod_mul(src0, twiddle, mod)
        if op == _OPCODE_IDS["int"]:
            tw_inv = _mod_inv(twiddle, mod)
            return src0 % mod if tw_inv == 0 else _mod_mul(src0, tw_inv, mod)
        return src0

    if module_id == 5:
        if op == _OPCODE_IDS["bci"]:
            return (int(inst.imm0) + int(inst.imm1) + int(inst.rns) + int(inst.aux & 0xFFFF)) % mod
        if op in (_OPCODE_IDS["pl1"], _OPCODE_IDS["bcw"]):
            factor = ((int(inst.rns) % mod) + 1) % mod
            return _mod_add(_montgomery_mul_ntt_friendly(src0, factor, mod), int(inst.imm0), mod)
        if op == _OPCODE_IDS["rsi"]:
            return (_mod_add(src0, int(inst.imm0), mod) + int(inst.imm1)) % mod
        if op == _OPCODE_IDS["rsv"]:
            return _mod_add(_mod_add(src0, src1, mod), int(inst.imm0 + inst.imm1), mod)
        if op == _OPCODE_IDS["mod"]:
            return _mod_sub(src0, src1, mod)
        return src0

    if module_id == 6:
        if register_count == 0:
            return src0 % mod
        # Match kernel_common.hpp::abs_mod_u32 semantics for negative immediates.
        rot = _abs_mod_u32(inst.imm0, register_count)
        if rot == 0:
            rot = inst.rns
        src_idx = inst.src0 % register_count
        mapped_idx = (src_idx + (rot % register_count)) % register_count
        return state[mapped_idx] % mod

    if module_id == 7:
        if op in (_OPCODE_IDS["rec"], _OPCODE_IDS["rsv"]):
            if register_count == 0:
                return src0 % mod
            side = 1
            while (side + 1) * (side + 1) <= register_count:
                side += 1
            idx = inst.src0 % register_count
            r = idx // side
            c = idx % side
            mapped = c * side + r
            if mapped >= register_count:
                mapped = idx
            return state[mapped] % mod
        return src0

    return src0


def expected_module_output_words(
    module_name: str,
    instruction_words: Sequence[int],
    input_words: Sequence[int],
    output_count: int,
    partition_id: int,
) -> List[int]:
    if module_name not in _MODULE_IDS:
        raise ValueError(f"unknown module: {module_name}")
    if output_count <= 0:
        return []

    module_id = _MODULE_IDS[module_name]

    has_layout = len(input_words) >= 3 and int(input_words[0]) == _INPUT_MAGIC
    if has_layout:
        register_count = int(input_words[1])
        mod = int(input_words[2]) or _DEFAULT_MODULUS
        state_base = 3
    else:
        register_count = len(input_words)
        mod = _DEFAULT_MODULUS
        state_base = 0

    bounded = min(max(register_count, 0), 2048)
    state = [0] * bounded
    for i in range(bounded):
        idx = state_base + i
        state[i] = (int(input_words[idx]) % mod) if idx < len(input_words) else 0

    executed = 0

    for inst in _iter_descriptors(instruction_words):
        if not _opcode_matches_module(module_id, inst.opcode):
            continue

        if module_id == _MODULE_IDS["ntt"]:
            if bounded == 0:
                executed += 1
                continue

            span = _select_ntt_span(inst.imm0, bounded)
            src_base = int(inst.src0) % bounded
            dst_base = int(inst.dst) % bounded
            block = [0] * span
            for j in range(span):
                block[j] = int(state[(src_base + j) % bounded]) % mod
            _ntt_apply_negacyclic_four_step(
                block,
                span=span,
                mod=mod,
                prime_id=int(inst.rns),
                inverse=(inst.opcode == _OPCODE_IDS["int"]),
            )
            for j in range(span):
                state[(dst_base + j) % bounded] = int(block[j]) % mod
            executed += 1
            continue

        src0 = _load_operand(state, bounded, inst, False, mod, input_words, state_base)
        src1 = _load_operand(state, bounded, inst, True, mod, input_words, state_base)

        if inst.opcode in (
            _OPCODE_IDS["store"],
            _OPCODE_IDS["spill"],
            _OPCODE_IDS["snd"],
        ):
            executed += 1
            continue

        result = _apply_module_op(module_id, inst, src0, src1, mod, state, bounded)
        if bounded > 0:
            state[inst.dst % bounded] = result % mod

        executed += 1

    out = [0] * output_count
    out[0] = 0
    if output_count > 1:
        out[1] = executed
    if output_count > 2:
        out[2] = bounded
    if output_count > 3:
        out[3] = module_id
    if output_count > 4:
        out[4] = partition_id
    if output_count > 5:
        out[5] = _compute_trace(state, bounded, module_id, executed)

    words_available = max(output_count - _OUTPUT_HEADER_WORDS, 0)
    words_to_copy = min(words_available, bounded)
    for i in range(words_to_copy):
        out[_OUTPUT_HEADER_WORDS + i] = state[i]

    kernel_status = int(out[0]) if len(out) > 0 else None
    kernel_executed = int(out[1]) if len(out) > 1 else None
    kernel_register_count = int(out[2]) if len(out) > 2 else None
    kernel_module_id = int(out[3]) if len(out) > 3 else None
    kernel_partition_id = int(out[4]) if len(out) > 4 else None
    kernel_trace_acc = int(out[5]) if len(out) > 5 else None

    debug_payload = None
    if _env_flag(_JSONL_DEBUG_FLAG_ENV) and _consume_expected_log_budget():
        debug_payload = {
            "mismatch_context": {
                "instruction_words_preview": [
                    int(word) for word in instruction_words[:_DEBUG_PREVIEW_WORDS]
                ],
                "input_words_preview": [int(word) for word in input_words[:_DEBUG_PREVIEW_WORDS]],
                "output_words_preview": [int(word) for word in out[:_DEBUG_PREVIEW_WORDS]],
            },
        }
    _emit_jsonl(
        "parser.expected_module_output_words",
        payload={
            "partition_id": int(partition_id),
            "module": str(module_name),
            "instruction_count": int(len(instruction_words) // _INSTRUCTION_WORD_STRIDE),
            "input_count": int(len(input_words)),
            "output_count": int(output_count),
            "kernel_status": kernel_status,
            "kernel_executed": kernel_executed,
            "kernel_register_count": kernel_register_count,
            "kernel_module_id": kernel_module_id,
            "kernel_partition_id": kernel_partition_id,
            "kernel_trace_acc": kernel_trace_acc,
        },
        debug_payload=debug_payload,
    )
    return out


def expected_host_comm_token(
    partition_id: int,
    opcode_counts: Dict[str, int],
    num_partitions: int,
) -> int:
    checksum = (0xCBF29CE484222325 ^ int(partition_id) ^ (int(num_partitions) << 32)) & 0xFFFFFFFFFFFFFFFF
    fnv_prime = 1099511628211
    for op in ("syn", "dis", "rcv", "drm", "joi", "jol"):
        op_id = _OPCODE_IDS.get(op, 255)
        encoded = ((op_id & 0xFF) << 32) | (int(opcode_counts.get(op, 0)) & 0xFFFFFFFF)
        checksum ^= encoded
        checksum = (checksum * fnv_prime) & 0xFFFFFFFFFFFFFFFF
    return checksum


def expected_kernel_checksum(
    instruction_words: Sequence[int],
    input_words: Sequence[int],
) -> int:
    checksum = 0xCBF29CE484222325
    fnv_prime = 1099511628211

    for word in instruction_words:
        checksum ^= int(word) & 0xFFFFFFFFFFFFFFFF
        checksum = (checksum * fnv_prime) & 0xFFFFFFFFFFFFFFFF

    for word in input_words:
        value = int(word) & 0xFFFFFFFFFFFFFFFF
        rotated = ((value << 1) | (value >> 63)) & 0xFFFFFFFFFFFFFFFF
        checksum ^= rotated
        checksum = (checksum * fnv_prime) & 0xFFFFFFFFFFFFFFFF

    return checksum
