import re
from pathlib import Path
import sst


NUM_LANES = 256
VERBOSE = 2
TWIDDLE_FROM_HBM = 1
SCRATCHPAD_BYTES = 35 * 1024 * 1024


def load_context_defaults():
    script_dir = Path(__file__).resolve().parent if "__file__" in globals() else Path.cwd()
    context_header = script_dir / "context.h"
    try:
        header_text = context_header.read_text(encoding="utf-8-sig")
    except OSError as exc:
        raise RuntimeError(f"Failed to read context header: {context_header}") from exc

    def parse_uint32(field):
        match = re.search(rf"\b{field}\s*=\s*(\d+)\s*;", header_text)
        if not match:
            raise RuntimeError(f"Missing `{field}` default in {context_header}")
        return int(match.group(1))

    return {
        "n": parse_uint32("poly_degree_n"),
        "data_width_bits": parse_uint32("data_width_bits"),
        "limbs": parse_uint32("limbs"),
    }


_CONTEXT = load_context_defaults()
HE_N = _CONTEXT["n"]
HE_DATA_WIDTH_BITS = _CONTEXT["data_width_bits"]
HE_LIMBS = _CONTEXT["limbs"]
print(f"Context defaults: n={HE_N}, data_width_bits={HE_DATA_WIDTH_BITS}, limbs={HE_LIMBS}")
HE_ELEMENT_BYTES = ((HE_DATA_WIDTH_BITS + 7) // 8) * HE_LIMBS

CT_BYTES = HE_N * HE_ELEMENT_BYTES
TWIDDLE_BYTES = (HE_N // 2) * HE_ELEMENT_BYTES

MEM_ALIGN_BYTES = 64


def align_up(value, alignment=MEM_ALIGN_BYTES):
    return ((value + alignment - 1) // alignment) * alignment


# Spread HBM addresses across pseudo-channels with small skews while keeping regions disjoint.
_hbm_cursor = 0
HBM_CT0 = align_up(_hbm_cursor)
_hbm_cursor = HBM_CT0 + CT_BYTES
HBM_CT1 = align_up(_hbm_cursor + 0x80)
_hbm_cursor = HBM_CT1 + CT_BYTES
HBM_TWID = align_up(_hbm_cursor + 0x100)
_hbm_cursor = HBM_TWID + TWIDDLE_BYTES
HBM_OUT = align_up(_hbm_cursor + 0x180)

_host_cursor = 0x10000000
HOST_CT0 = align_up(_host_cursor)
_host_cursor = HOST_CT0 + CT_BYTES
HOST_CT1 = align_up(_host_cursor + 0x80)
_host_cursor = HOST_CT1 + CT_BYTES
HOST_TWID = align_up(_host_cursor + 0x100)
_host_cursor = HOST_TWID + TWIDDLE_BYTES
HOST_OUT = align_up(_host_cursor + 0x180)

# Keep only two large SPM buffers and run NTT/MOD/AUTO/INTT in-place on SPM_CT0.
SPM_CT0 = 0x00000000
SPM_CT1 = align_up(SPM_CT0 + CT_BYTES + 0x80)
SPM_REQUIRED_BYTES = SPM_CT1 + CT_BYTES
if SPM_REQUIRED_BYTES > SCRATCHPAD_BYTES:
    raise RuntimeError(
        f"Scratchpad too small: requires {SPM_REQUIRED_BYTES} bytes, configured {SCRATCHPAD_BYTES} bytes"
    )

PROGRAM = "\n".join(
    [
        f"PCIE_H2D host=0x{HOST_CT0:08x} hbm=0x{HBM_CT0:08x} bytes={CT_BYTES}",
        f"PCIE_H2D host=0x{HOST_CT1:08x} hbm=0x{HBM_CT1:08x} bytes={CT_BYTES}",
        f"PCIE_H2D host=0x{HOST_TWID:08x} hbm=0x{HBM_TWID:08x} bytes={TWIDDLE_BYTES}",
        f"HBM_LOAD hbm=0x{HBM_CT0:08x} spm=0x{SPM_CT0:08x} elements={HE_N} lanes={NUM_LANES} modulus=65537",
        f"HBM_LOAD hbm=0x{HBM_CT1:08x} spm=0x{SPM_CT1:08x} elements={HE_N} lanes={NUM_LANES} modulus=65537",
        f"NTT_STAGE src=0x{SPM_CT0:08x} dst=0x{SPM_CT0:08x} stage=0 degree={HE_N} twiddle=0x{HBM_TWID:08x} lanes={NUM_LANES} modulus=65537",
        f"MOD_MUL src0=0x{SPM_CT0:08x} src1=0x{SPM_CT1:08x} dst=0x{SPM_CT0:08x} elements={HE_N} lanes={NUM_LANES} modulus=65537",
        f"AUTOMORPHISM src=0x{SPM_CT0:08x} dst=0x{SPM_CT0:08x} rot=3 elements={HE_N} lanes={NUM_LANES} modulus=65537",
        f"INTT_STAGE src=0x{SPM_CT0:08x} dst=0x{SPM_CT0:08x} stage=0 degree={HE_N} twiddle=0x{HBM_TWID:08x} lanes={NUM_LANES} modulus=65537",
        f"HBM_STORE spm=0x{SPM_CT0:08x} hbm=0x{HBM_OUT:08x} elements={HE_N} lanes={NUM_LANES} modulus=65537",
        f"PCIE_D2H hbm=0x{HBM_OUT:08x} host=0x{HOST_OUT:08x} bytes={CT_BYTES}",
        "BARRIER",
    ]
)


def build_lane_params(num_lanes):
    params = {
        "num_lanes": num_lanes,
    }

    for lane in range(num_lanes):
        lane_name = f"lane{lane:03d}"
        params[f"{lane_name}.mod_multiplier.count"] = 1
        params[f"{lane_name}.mod_multiplier.latency"] = 11
        params[f"{lane_name}.mod_adder.count"] = 2
        params[f"{lane_name}.mod_adder.latency"] = 1

    return params


cpu = sst.Component("cpu", "tutorial.CPU")
cpu.addParams(
    {
        "clock": "2GHz",
        "issue_width": 4,
        "max_inflight": 32,
        "default_lanes": NUM_LANES,
        "default_modulus": 65537,
        "instructions_to_generate": 1,
        "default_src_address": 0x10000000,
        "default_dst_address": 0x10080000,
        "program": PROGRAM,
        "program_file": "",
        "context.n": HE_N,
        "context.data_width_bits": HE_DATA_WIDTH_BITS,
        "context.limbs": HE_LIMBS,
        "verbose": VERBOSE,
    }
)

pcie = sst.Component("pcie", "tutorial.PCIe")
pcie.addParams(
    {
        "clock": "1.0GHz",
        "doorbell_setup_cycles": 4,
        "dma_setup_cycles": 12,
        "protocol_overhead_cycles": 4,
        "effective_bytes_per_cycle": 64,
        "completion_return_cycles": 2,
        "verbose": VERBOSE,
    }
)

hbm = sst.Component("hbm", "tutorial.HBM")
hbm.addParams(
    {
        "clock": "1.2GHz",
        "capacity_bytes": 17179869184,
        "num_channels": 8,
        "pseudo_channels_per_channel": 2,
        "burst_bytes": 64,
        "base_access_cycles": 24,
        "burst_cycles": 2,
        "compute_dispatch_cycles": 2,
        "network_return_cycles": 4,
        "nttu.twiddle_from_hbm": TWIDDLE_FROM_HBM,
        "context.n": HE_N,
        "context.data_width_bits": HE_DATA_WIDTH_BITS,
        "context.limbs": HE_LIMBS,
        "verbose": VERBOSE,
    }
)

compute_unit = sst.Component("compute_unit", "tutorial.ComputeUnit")
compute_unit.addParams(
    {
        "clock": "1.5GHz",
        "lane_width_bits": HE_DATA_WIDTH_BITS,
        "scratchpad_bytes": SCRATCHPAD_BYTES,
        "scratchpad_bandwidth_bytes_per_cycle": 128,
        "scratchpad_banks": 32,
        "bank_interleave_bytes": 32,
        "bank_read_ports": 1,
        "bank_write_ports": 1,
        "bank_conflict_penalty_cycles": 1,
        "dma_engines": 2,
        "alu_engines": 2,
        "spu.count": 1,
        "spu.automorphism_latency": 12,
        "spu.switch_latency": 1,
        "nttu.count": 1,
        "nttu.butterfly_latency": 4,
        "nttu.twiddle_penalty": 2,
        "nttu.shuffle_penalty": 2,
        "nttu.twiddle_from_hbm": TWIDDLE_FROM_HBM,
        "context.n": HE_N,
        "context.data_width_bits": HE_DATA_WIDTH_BITS,
        "context.limbs": HE_LIMBS,
        "verbose": VERBOSE,
    }
)
compute_unit.addParams(build_lane_params(NUM_LANES))


cpu_pcie_ctrl = sst.Link("cpu_pcie_ctrl")
cpu_pcie_ctrl.connect(
    (cpu, "ctrl", "2ns"),
    (pcie, "cpu_ctrl", "2ns"),
)

pcie_hbm_fabric = sst.Link("pcie_hbm_fabric")
pcie_hbm_fabric.connect(
    (pcie, "fabric", "4ns"),
    (hbm, "fabric", "4ns"),
)

hbm_compute = sst.Link("hbm_compute")
hbm_compute.connect(
    (hbm, "compute", "2ns"),
    (compute_unit, "hbm", "2ns"),
)
