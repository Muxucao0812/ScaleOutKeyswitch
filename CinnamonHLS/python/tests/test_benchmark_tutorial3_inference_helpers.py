from __future__ import annotations

import importlib.util
import pathlib
import sys
import types

import pytest


def _install_import_stubs() -> None:
    if "cinnamon_emulator" not in sys.modules:
        sys.modules["cinnamon_emulator"] = types.ModuleType("cinnamon_emulator")

    if "cinnamon" not in sys.modules:
        sys.modules["cinnamon"] = types.ModuleType("cinnamon")

    if "cinnamon.compiler" not in sys.modules:
        compiler_mod = types.ModuleType("cinnamon.compiler")

        def _noop_compile(*args, **kwargs):
            return None

        compiler_mod.cinnamon_compile = _noop_compile
        sys.modules["cinnamon.compiler"] = compiler_mod

    if "cinnamon.dsl" not in sys.modules:
        dsl_mod = types.ModuleType("cinnamon.dsl")
        dsl_mod.CiphertextInput = object
        dsl_mod.CinnamonProgram = object
        dsl_mod.Output = object
        dsl_mod.PlaintextInput = object
        sys.modules["cinnamon.dsl"] = dsl_mod

    if "cinnamon.passes" not in sys.modules:
        passes_mod = types.ModuleType("cinnamon.passes")

        def _noop_keyswitch(*args, **kwargs):
            return None

        passes_mod.keyswitch_pass = _noop_keyswitch
        sys.modules["cinnamon.passes"] = passes_mod

    if "mnist_io" not in sys.modules:
        mnist_mod = types.ModuleType("mnist_io")
        mnist_mod.Primes = [268042241]

        def _noop_mnist_program_io(*args, **kwargs):
            return {}, {}

        mnist_mod.get_mnist_program_io = _noop_mnist_program_io
        sys.modules["mnist_io"] = mnist_mod

    if "PIL" not in sys.modules:
        pil_mod = types.ModuleType("PIL")
        pil_mod.Image = object
        sys.modules["PIL"] = pil_mod

    if "torchvision" not in sys.modules:
        torchvision_mod = types.ModuleType("torchvision")
        transforms_mod = types.ModuleType("torchvision.transforms")
        torchvision_mod.transforms = transforms_mod
        sys.modules["torchvision"] = torchvision_mod
        sys.modules["torchvision.transforms"] = transforms_mod


def _load_benchmark_module():
    _install_import_stubs()
    script_path = (
        pathlib.Path(__file__).resolve().parents[1]
        / "examples"
        / "benchmark_tutorial3_inference.py"
    )
    spec = importlib.util.spec_from_file_location("benchmark_tutorial3_inference", script_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except ModuleNotFoundError as exc:
        pytest.skip(f"benchmark helpers unavailable in this test environment: {exc}")
    return module


def test_extract_pred_explicit16x128_mean_matches_layout() -> None:
    mod = _load_benchmark_module()
    values = [0.0] * (32 * 1024)
    for cls in range(10):
        for rep in range(16):
            idx = cls * 128 + rep * 2048
            values[idx] = float(cls + rep)

    scores = mod._extract_pred_explicit16x128_mean(values, class_count=10)
    assert len(scores) == 10
    for cls, score in enumerate(scores):
        # mean(cls + rep for rep=0..15) == cls + 7.5
        assert score == pytest.approx(float(cls) + 7.5)


def test_compare_he_vs_plain_marks_missing_layers_as_failure() -> None:
    mod = _load_benchmark_module()
    plain = {
        "conv": [1.0] * 256,
        "conv_sq": [1.0] * 256,
        "o2": [1.0] * 64,
        "o2_sq": [1.0] * 64,
        "pred": [1.0] * 10,
    }
    # Missing conv/conv_sq/o2/o2_sq on HE side should fail at first layer.
    he_unpacked = {
        "pred": [1.0] * 10,
    }

    result = mod.compare_he_vs_plain_intermediates(
        reference_outputs=plain,
        he_unpacked_outputs=he_unpacked,
        abs_tol=1e-2,
        rel_tol=1e-2,
    )
    assert result["all_passed"] is False
    assert result["first_bad_layer"] == "conv"
    assert result["conv"]["compare_count"] == 0
    assert result["conv"]["pass"] is False


def test_compare_he_vs_plain_passes_on_identical_vectors() -> None:
    mod = _load_benchmark_module()
    plain = {
        "conv": [float(i) for i in range(256)],
        "conv_sq": [float(i * i) for i in range(256)],
        "o2": [float(i) * 0.1 for i in range(64)],
        "o2_sq": [float(i) * 0.01 for i in range(64)],
        "pred": [float(i) for i in range(10)],
    }
    result = mod.compare_he_vs_plain_intermediates(
        reference_outputs=plain,
        he_unpacked_outputs=plain,
        abs_tol=1e-6,
        rel_tol=1e-6,
    )
    assert result["all_passed"] is True
    assert result["first_bad_layer"] is None


def test_missing_expected_layers_detected() -> None:
    mod = _load_benchmark_module()
    missing = mod._missing_expected_layers({"pred": [0.0] * 10})
    assert missing == ["conv", "conv_sq", "o2", "o2_sq"]
