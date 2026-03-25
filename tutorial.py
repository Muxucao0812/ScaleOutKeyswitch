import Hardware
import Context
import sst

VERBOSE = 2


def align_up(value, alignment=64):
    return ((value + alignment - 1) // alignment) * alignment


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


def main():
    print("Setting up simulation components...")

 
    HE_N = Context.N
    HE_DATA_WIDTH_BITS = Context.BITS_WIDTH
    HE_LIMBS = Context.L

    HE_ELEMENT_BYTES = ((HE_DATA_WIDTH_BITS + 7) // 8) * HE_LIMBS
    CT_BYTES = HE_N * HE_ELEMENT_BYTES
    TWIDDLE_BYTES = (HE_N // 2) * HE_ELEMENT_BYTES

    MEM_ALIGN_BYTES = 64

    HBM_TOTAL_PCS = Hardware.hbm_num_channels * Hardware.hbm_pseudo_channels_per_channel
    HBM_PC_SIZE_BYTES = Hardware.hbm_capacity_bytes // HBM_TOTAL_PCS

    def hbm_pc_base(pc):
        if not (0 <= pc < HBM_TOTAL_PCS):
            raise ValueError(f"Invalid pseudo-channel index {pc}")
        return pc * HBM_PC_SIZE_BYTES

    def hbm_alloc_in_pc(pc, size_bytes, offset_bytes=0):
        addr = hbm_pc_base(pc) + align_up(offset_bytes, MEM_ALIGN_BYTES)
        end_addr = addr + size_bytes
        pc_end = hbm_pc_base(pc) + HBM_PC_SIZE_BYTES
        if end_addr > pc_end:
            raise RuntimeError(
                f"HBM allocation exceeds pseudo-channel {pc}: "
                f"addr=0x{addr:x}, size={size_bytes}, pc_end=0x{pc_end:x}"
            )
        return addr

    # HBM 地址：显式落到不同 pseudo-channel
    HBM_CT0 = hbm_alloc_in_pc(0, CT_BYTES)
    HBM_CT1 = hbm_alloc_in_pc(2, CT_BYTES)
    HBM_TWID = hbm_alloc_in_pc(4, TWIDDLE_BYTES)
    HBM_OUT = hbm_alloc_in_pc(6, CT_BYTES)

    # Host 地址仍然线性分配
    _host_cursor = 0x10000000
    HOST_CT0 = align_up(_host_cursor, MEM_ALIGN_BYTES)
    _host_cursor = HOST_CT0 + CT_BYTES
    HOST_CT1 = align_up(_host_cursor + 0x80, MEM_ALIGN_BYTES)
    _host_cursor = HOST_CT1 + CT_BYTES
    HOST_TWID = align_up(_host_cursor + 0x100, MEM_ALIGN_BYTES)
    _host_cursor = HOST_TWID + TWIDDLE_BYTES
    HOST_OUT = align_up(_host_cursor + 0x180, MEM_ALIGN_BYTES)

    NUM_LANES = Hardware.Num_Lanes
    SCRATCHPAD_BYTES = Hardware.Scratchpad_Size_Bytes
    TWIDDLE_FROM_HBM = 0

    # SPM
    SPM_CT0 = 0x00000000
    SPM_CT1 = align_up(SPM_CT0 + CT_BYTES + 0x80, MEM_ALIGN_BYTES)
    SPM_REQUIRED_BYTES = SPM_CT1 + CT_BYTES
    if SPM_REQUIRED_BYTES > SCRATCHPAD_BYTES:
        raise RuntimeError(
            f"Scratchpad too small: requires {SPM_REQUIRED_BYTES} bytes, configured {SCRATCHPAD_BYTES} bytes"
        )

    PROGRAM = "\n".join(
        [
            f"PCIE_H2D host=0x{HOST_CT0:08x} hbm=0x{HBM_CT0:08x} bytes={CT_BYTES}",
            # f"PCIE_H2D host=0x{HOST_CT1:08x} hbm=0x{HBM_CT1:08x} bytes={CT_BYTES}",
            # f"PCIE_H2D host=0x{HOST_TWID:08x} hbm=0x{HBM_TWID:08x} bytes={TWIDDLE_BYTES}",
            # f"HBM_LOAD hbm=0x{HBM_CT0:08x} spm=0x{SPM_CT0:08x} elements={HE_N} lanes={NUM_LANES} modulus=65537",
            # f"HBM_LOAD hbm=0x{HBM_CT1:08x} spm=0x{SPM_CT1:08x} elements={HE_N} lanes={NUM_LANES} modulus=65537",
            # f"NTT_STAGE src=0x{SPM_CT0:08x} dst=0x{SPM_CT0:08x} stage=0 degree={HE_N} twiddle=0x{HBM_TWID:08x} lanes={NUM_LANES} modulus=65537",
            # f"MOD_MUL src0=0x{SPM_CT0:08x} src1=0x{SPM_CT1:08x} dst=0x{SPM_CT0:08x} elements={HE_N} lanes={NUM_LANES} modulus=65537",
            # f"AUTOMORPHISM src=0x{SPM_CT0:08x} dst=0x{SPM_CT0:08x} rot=3 elements={HE_N} lanes={NUM_LANES} modulus=65537",
            # f"INTT_STAGE src=0x{SPM_CT0:08x} dst=0x{SPM_CT0:08x} stage=0 degree={HE_N} twiddle=0x{HBM_TWID:08x} lanes={NUM_LANES} modulus=65537",
            # f"HBM_STORE spm=0x{SPM_CT0:08x} hbm=0x{HBM_OUT:08x} elements={HE_N} lanes={NUM_LANES} modulus=65537",
            # f"PCIE_D2H hbm=0x{HBM_OUT:08x} host=0x{HOST_OUT:08x} bytes={CT_BYTES}",
        ]
    )

    cpu = sst.Component("cpu", "tutorial.CPU")
    cpu.addParams(
        {
            "clock": Hardware.cpu_clock,
            "issue_width": 1,
            "max_inflight": 1,
            "default_lanes": NUM_LANES,
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
            "clock": Hardware.pcie_clock,
            "doorbell_setup_cycles": Hardware.pcie_doorbell_setup_cycles,
            "dma_setup_cycles": Hardware.pcie_dma_setup_cycles,
            "protocol_overhead_cycles": Hardware.pcie_protocol_overhead_cycles,
            "effective_bytes_per_cycle": Hardware.pcie_effective_bytes_per_cycle,
            "completion_return_cycles": Hardware.pcie_completion_return_cycles,
            "verbose": VERBOSE,
        }
    )

    hbm = sst.Component("hbm", "tutorial.HBM")
    hbm.addParams(
        {
            "clock": Hardware.hbm_clock,
            "capacity_bytes": Hardware.hbm_capacity_bytes,
            "num_channels": Hardware.hbm_num_channels,
            "pseudo_channels_per_channel": Hardware.hbm_pseudo_channels_per_channel,
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
            "spu.count": Hardware.spu_count,
            "spu.automorphism_latency": Hardware.spu_automorphism_latency_cycles,
            "spu.switch_latency": Hardware.spu_switch_latency_cycles,
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

    axi_interconnect = sst.Link("axi_interconnect")
    axi_interconnect.connect(
        (hbm, "compute", "2ns"),
        (compute_unit, "hbm", "2ns"),
    )

    print("Simulation components set up complete.")
    print(f"HBM pseudo-channels: {HBM_TOTAL_PCS}")
    print(f"HBM pseudo-channel size: {HBM_PC_SIZE_BYTES} bytes")
    print(f"HBM_CT0  = 0x{HBM_CT0:08x}")
    print(f"HBM_CT1  = 0x{HBM_CT1:08x}")
    print(f"HBM_TWID = 0x{HBM_TWID:08x}")
    print(f"HBM_OUT  = 0x{HBM_OUT:08x}")


if __name__ == "__main__":
    main()