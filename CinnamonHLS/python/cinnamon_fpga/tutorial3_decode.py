from __future__ import annotations

import statistics
from typing import Any, Dict, List, Optional, Sequence, Tuple

SUPPORTED_PRED_DECODE_MODES = (
    "first10",
    "stride128_mean",
    "stride128_median",
    "stride128_max",
    "explicit16x128_mean",
    "explicit16x128_median",
    "explicit16x128_max",
)
DEFAULT_PRED_DECODE_MODE = "stride128_mean"
DECODE_MARGIN_FALLBACK_THRESHOLD = 1e-4


def decode_scores_with_mode(
    values: Sequence[float],
    decode_mode: str,
) -> Tuple[List[float], str, Dict[str, Any], Dict[str, List[int]]]:
    mode = decode_mode.strip().lower()
    if mode not in SUPPORTED_PRED_DECODE_MODES:
        raise ValueError(
            f"Unsupported pred decode mode: {decode_mode!r}. "
            f"Supported: {', '.join(SUPPORTED_PRED_DECODE_MODES)}"
        )

    class_count = min(10, len(values))
    index_map_preview: Dict[str, List[int]] = {}

    if mode == "first10":
        scores = [float(v) for v in values[:10]]
        for cls in range(class_count):
            index_map_preview[str(cls)] = [int(cls)]
        index_map = {
            "strategy": mode,
            "class_count": class_count,
            "prefix_length": min(len(values), 10),
        }
        return scores, mode, index_map, index_map_preview

    if mode.startswith("stride128_"):
        reduce_mode = mode.split("_", maxsplit=1)[1]
        stride = 128
        repeats = max(1, len(values) // stride)
        scores: List[float] = []
        for cls in range(class_count):
            indices = [
                cls + rep * stride
                for rep in range(repeats)
                if (cls + rep * stride) < len(values)
            ]
            cls_values = [float(values[idx]) for idx in indices]
            if not cls_values:
                cls_values = [float(values[cls])]
                indices = [cls]
            if reduce_mode == "mean":
                score = float(statistics.mean(cls_values))
            elif reduce_mode == "median":
                score = float(statistics.median(cls_values))
            elif reduce_mode == "max":
                score = float(max(cls_values))
            else:
                raise ValueError(f"Unsupported stride reduce mode: {reduce_mode}")
            scores.append(score)
            index_map_preview[str(cls)] = [int(v) for v in indices[:8]]
        index_map = {
            "strategy": mode,
            "stride": stride,
            "repeats": repeats,
            "class_count": class_count,
        }
        return scores, mode, index_map, index_map_preview

    # explicit16x128_* follows tutorial3 fc2 packing:
    # class c uses indices (c + 16*k) * 128 == c*128 + k*2048
    reduce_mode = mode.split("_", maxsplit=1)[1]
    base_stride = 128
    block_stride = 16 * 128
    repeats = max(1, len(values) // block_stride)
    scores = []
    for cls in range(class_count):
        indices = [
            cls * base_stride + rep * block_stride
            for rep in range(repeats)
            if (cls * base_stride + rep * block_stride) < len(values)
        ]
        cls_values = [float(values[idx]) for idx in indices]
        if not cls_values:
            cls_values = [float(values[cls])]
            indices = [cls]
        if reduce_mode == "mean":
            score = float(statistics.mean(cls_values))
        elif reduce_mode == "median":
            score = float(statistics.median(cls_values))
        elif reduce_mode == "max":
            score = float(max(cls_values))
        else:
            raise ValueError(f"Unsupported explicit reduce mode: {reduce_mode}")
        scores.append(score)
        index_map_preview[str(cls)] = [int(v) for v in indices[:8]]
    index_map = {
        "strategy": mode,
        "base_stride": base_stride,
        "block_stride": block_stride,
        "repeats": repeats,
        "class_count": class_count,
    }
    return scores, mode, index_map, index_map_preview


def decode_mode_votes(values: Sequence[float]) -> Dict[str, int]:
    votes: Dict[str, int] = {}
    for mode in SUPPORTED_PRED_DECODE_MODES:
        mode_scores, _, _, _ = decode_scores_with_mode(values, mode)
        if len(mode_scores) >= 10:
            votes[mode] = int(max(range(10), key=lambda idx: float(mode_scores[idx])))
    return votes


def select_reference_prediction(
    *,
    decoded_pred_label: Optional[int],
    plain_pred_label: int,
    top1_margin: float,
    mode_votes: Dict[str, int],
    margin_threshold: float = DECODE_MARGIN_FALLBACK_THRESHOLD,
) -> Tuple[int, str, bool]:
    unique_votes = set(mode_votes.values())
    decode_stable = (
        decoded_pred_label is not None
        and len(unique_votes) == 1
        and float(top1_margin) >= float(margin_threshold)
    )
    if decode_stable:
        return int(decoded_pred_label), "cpu_decoded", False
    return int(plain_pred_label), "plain_model_fallback", True
