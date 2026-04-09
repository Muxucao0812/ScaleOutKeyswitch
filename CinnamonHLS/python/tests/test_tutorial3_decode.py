from __future__ import annotations

import pytest

from cinnamon_fpga.tutorial3_decode import (
    SUPPORTED_PRED_DECODE_MODES,
    decode_mode_votes,
    decode_scores_with_mode,
    select_reference_prediction,
)


def _build_bias_only_pred_vector() -> list[float]:
    values = [0.0] * (32 * 1024)
    for cls in range(10):
        for rep in range(16):
            idx = cls * 128 + rep * 2048
            values[idx] = float(cls)
    return values


def test_explicit_decode_matches_fc2_bias_layout() -> None:
    values = _build_bias_only_pred_vector()
    scores, mode, index_map, _ = decode_scores_with_mode(values, "explicit16x128_mean")
    assert mode == "explicit16x128_mean"
    assert index_map["block_stride"] == 2048
    assert scores == [float(v) for v in range(10)]
    assert max(range(10), key=lambda idx: scores[idx]) == 9


def test_decode_mode_votes_capture_mode_disagreement() -> None:
    values = _build_bias_only_pred_vector()
    votes = decode_mode_votes(values)
    assert set(votes.keys()) == set(SUPPORTED_PRED_DECODE_MODES)
    assert votes["explicit16x128_mean"] == 9
    assert votes["explicit16x128_median"] == 9
    assert votes["explicit16x128_max"] == 9
    assert votes["stride128_mean"] == 0


def test_select_reference_prediction_prefers_decoded_when_stable() -> None:
    mode_votes = {mode: 7 for mode in SUPPORTED_PRED_DECODE_MODES}
    pred, source, decode_unstable = select_reference_prediction(
        decoded_pred_label=7,
        plain_pred_label=2,
        top1_margin=0.2,
        mode_votes=mode_votes,
    )
    assert pred == 7
    assert source == "cpu_decoded"
    assert decode_unstable is False


def test_select_reference_prediction_falls_back_on_vote_conflict() -> None:
    pred, source, decode_unstable = select_reference_prediction(
        decoded_pred_label=7,
        plain_pred_label=2,
        top1_margin=0.2,
        mode_votes={"first10": 7, "stride128_mean": 2},
    )
    assert pred == 2
    assert source == "plain_model_fallback"
    assert decode_unstable is True


def test_select_reference_prediction_falls_back_on_small_margin() -> None:
    mode_votes = {mode: 4 for mode in SUPPORTED_PRED_DECODE_MODES}
    pred, source, decode_unstable = select_reference_prediction(
        decoded_pred_label=4,
        plain_pred_label=1,
        top1_margin=1e-8,
        mode_votes=mode_votes,
    )
    assert pred == 1
    assert source == "plain_model_fallback"
    assert decode_unstable is True


def test_decode_scores_rejects_unsupported_mode() -> None:
    with pytest.raises(ValueError, match="Unsupported pred decode mode"):
        decode_scores_with_mode([0.0] * 64, "invalid_mode")
