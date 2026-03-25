# Hardware Parameters

# CPU Parameters
cpu_clock = "2GHz"

# PCIe Parameters
pcie_clock = "1.0GHz"
pcie_doorbell_setup_cycles = 4
pcie_dma_setup_cycles = 12
pcie_protocol_overhead_cycles = 4
pcie_effective_bytes_per_cycle = 64
pcie_completion_return_cycles = 2

# HBM Parameters
hbm_clock = "1.2GHz"
hbm_capacity_bytes = 8 * 1024 * 1024 * 1024
hbm_num_channels = 16
hbm_pseudo_channels_per_channel = 2

# Accelerator Parameters
Num_Lanes = 256
Scratchpad_Size_Bytes = 35 * 1024 * 1024
spu_count = 1
spu_automorphism_latency_cycles = 12
spu_switch_latency_cycles = 1

modular_multiplier_latency_cycles = 11
modular_adder_latency_cycles = 2