from __future__ import annotations

import json
import pathlib


def test_rtl_vector_assets_exist_and_are_well_formed() -> None:
    root = pathlib.Path(__file__).resolve().parents[2]
    vector_path = root / "tests" / "vectors" / "rtl_vectors.json"
    assert vector_path.exists(), f"Missing RTL vector asset file: {vector_path}"

    data = json.loads(vector_path.read_text(encoding="utf-8"))
    for key in (
        "arithmetic",
        "montgomery",
        "memory",
        "ntt",
        "base_conv",
        "rns_resolve",
        "automorphism",
    ):
        assert key in data, f"Missing vector group: {key}"

    assert len(data["arithmetic"]["modular_add"]) >= 3
    assert len(data["montgomery"]["multiplier"]) >= 4
    assert len(data["base_conv"]["multiply_accumulate"]) >= 2
    assert "perm8_case0" in data["automorphism"]
