from __future__ import annotations

import argparse
import csv
import json
import pathlib
import time
from datetime import datetime
from typing import Any, Dict, List, Sequence

import cinnamon_emulator
import torch
from cinnamon.compiler import cinnamon_compile
from cinnamon.passes import keyswitch_pass

from benchmark_tutorial3_inference import (
    ROOT_DIR,
    SLOTS,
    _SUPPORTED_PRED_DECODE_MODES,
    TOP_LEVEL,
    TUTORIAL3_DIR,
    build_encryptor,
    build_mnist_program,
    get_mnist_program_io,
    load_input,
    load_labels,
    pushd,
    run_cpu_reference,
    Primes,
)


class MNIST_CNN(torch.nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.conv2d = torch.nn.Conv2d(
            in_channels=1, out_channels=4, kernel_size=(7, 7), stride=(3, 3), bias=True
        )
        self.fc1 = torch.nn.Linear(in_features=256, out_features=64, bias=True)
        self.fc2 = torch.nn.Linear(in_features=64, out_features=10, bias=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        conv = self.conv2d(x)
        conv_sq = conv * conv
        conv_sq = conv_sq.reshape(1, -1)
        o2 = self.fc1(conv_sq)
        o2_sq = o2 * o2
        o3 = self.fc2(o2_sq)
        return o3


def parse_sample_range(sample_start: int, sample_end: int, labels: Sequence[int]) -> List[int]:
    if sample_start < 1:
        raise ValueError(f"sample_start must be >= 1, got {sample_start}")
    if sample_end < sample_start:
        raise ValueError(f"sample_end must be >= sample_start, got {sample_end} < {sample_start}")
    if sample_end > len(labels):
        raise ValueError(f"sample_end={sample_end} exceeds labels range 1..{len(labels)}")
    return list(range(sample_start, sample_end + 1))


def load_plain_model(model_path: pathlib.Path) -> MNIST_CNN:
    if not model_path.exists():
        raise FileNotFoundError(f"plain model checkpoint not found: {model_path}")
    model = MNIST_CNN()
    state_dict = torch.load(model_path, map_location="cpu")
    model.load_state_dict(state_dict)
    model.eval()
    return model


def run_plain_prediction(model: MNIST_CNN, sample_id: int) -> int:
    image = load_input(sample_id)
    image_tensor = torch.from_numpy(image).to(torch.float32).view(1, 1, 28, 28)
    with torch.no_grad():
        logits = model(image_tensor)
    return int(torch.argmax(logits).item())


def prepare_cpu_artifacts(
    *,
    out_dir: pathlib.Path,
    chips: int,
    compile_register_file_size: int,
) -> Dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    program = build_mnist_program(chips)
    t_keyswitch_begin = time.perf_counter()
    keyswitch_pass(program)
    t_keyswitch_end = time.perf_counter()
    t_compile_begin = time.perf_counter()
    cinnamon_compile(program, TOP_LEVEL, chips, compile_register_file_size, str(out_dir) + "/")
    t_compile_end = time.perf_counter()

    context = cinnamon_emulator.Context(SLOTS, Primes)
    encryptor = build_encryptor(context)
    instructions_base = str(out_dir / "instructions")
    program_inputs = str(out_dir / "program_inputs")
    evalkeys = str(out_dir / "evalkeys")
    return {
        "context": context,
        "encryptor": encryptor,
        "instructions_base": instructions_base,
        "program_inputs": program_inputs,
        "evalkeys": evalkeys,
        "keyswitch_pass_s": float(t_keyswitch_end - t_keyswitch_begin),
        "compile_s": float(t_compile_end - t_compile_begin),
    }


def run_cpu_prediction(
    *,
    context: cinnamon_emulator.Context,
    encryptor: cinnamon_emulator.CKKSEncryptor,
    instructions_base: str,
    program_inputs: str,
    evalkeys: str,
    chips: int,
    register_file_size: int,
    sample_id: int,
    pred_decode_mode: str,
) -> Dict[str, Any]:
    input_image = load_input(sample_id)
    with pushd(TUTORIAL3_DIR):
        raw_inputs, output_scales = get_mnist_program_io(input_image, TOP_LEVEL)

    cpu_reference = run_cpu_reference(
        context=context,
        encryptor=encryptor,
        instructions_base=instructions_base,
        program_inputs=program_inputs,
        evalkeys=evalkeys,
        raw_inputs=raw_inputs,
        output_scales=output_scales,
        chips=chips,
        register_file_size=register_file_size,
        sample_id=sample_id,
        pred_decode_mode=pred_decode_mode,
    )
    pred_valid = bool(cpu_reference.get("pred_valid", False))
    selected_pred_label = cpu_reference.get("pred_label") if pred_valid else None
    decoded_pred_label = cpu_reference.get("cpu_decoded_pred")
    pred_label = decoded_pred_label if decoded_pred_label is not None else selected_pred_label
    normalized_scores: List[float] = []
    for item in list(cpu_reference.get("scores", []))[:10]:
        value: Any = item
        if isinstance(value, (list, tuple)):
            if not value:
                continue
            value = value[0]
        try:
            normalized_scores.append(float(value))
        except (TypeError, ValueError):
            continue
    return {
        "pred_valid": bool(pred_valid),
        "pred_label": int(pred_label) if pred_label is not None else None,
        "decoded_pred_label": int(decoded_pred_label) if decoded_pred_label is not None else None,
        "selected_pred_label": int(selected_pred_label) if selected_pred_label is not None else None,
        "pred_source": str(cpu_reference.get("pred_source", "cpu_decoded")),
        "scores": normalized_scores,
        "decode_mode": str(cpu_reference.get("decode_mode", pred_decode_mode)),
        "top1_margin": float(cpu_reference.get("top1_margin", 0.0)),
    }


def aggregate_rows(rows: Sequence[Dict[str, Any]]) -> Dict[str, Any]:
    total = len(rows)
    plain_correct = sum(1 for row in rows if bool(row["plain_correct"]))
    cpu_correct = sum(1 for row in rows if bool(row["cpu_correct"]))
    cpu_available = sum(1 for row in rows if row["cpu_ref_pred"] is not None)
    mismatch = sum(1 for row in rows if not bool(row["match_plain_vs_cpu"]))
    plain_accuracy = (plain_correct / total) if total > 0 else 0.0
    cpu_accuracy = (cpu_correct / total) if total > 0 else 0.0
    mismatch_ratio = (mismatch / total) if total > 0 else 0.0
    return {
        "sample_count": total,
        "plain_correct_count": plain_correct,
        "cpu_correct_count": cpu_correct,
        "cpu_pred_available_count": cpu_available,
        "mismatch_count": mismatch,
        "plain_accuracy": plain_accuracy,
        "cpu_accuracy": cpu_accuracy,
        "mismatch_ratio": mismatch_ratio,
    }


def write_reports(
    *,
    report_dir: pathlib.Path,
    report_basename: str,
    rows: Sequence[Dict[str, Any]],
    aggregate: Dict[str, Any],
    metadata: Dict[str, Any],
) -> Dict[str, pathlib.Path]:
    report_dir.mkdir(parents=True, exist_ok=True)
    csv_path = report_dir / f"{report_basename}.csv"
    json_path = report_dir / f"{report_basename}.json"

    csv_rows: List[Dict[str, Any]] = []
    for row in rows:
        csv_rows.append(
            {
                "sample_id": int(row["sample_id"]),
                "label": int(row["label"]),
                "plain_pred": int(row["plain_pred"]),
                "cpu_ref_pred": row["cpu_ref_pred"] if row["cpu_ref_pred"] is not None else "",
                "match_plain_vs_cpu": int(bool(row["match_plain_vs_cpu"])),
                "plain_correct": int(bool(row["plain_correct"])),
                "cpu_correct": int(bool(row["cpu_correct"])),
                "cpu_pred_source": str(row.get("cpu_pred_source", "")),
                "cpu_decoded_pred": row.get("cpu_decoded_pred")
                if row.get("cpu_decoded_pred") is not None
                else "",
                "cpu_selected_pred": row.get("cpu_selected_pred")
                if row.get("cpu_selected_pred") is not None
                else "",
                "decode_mode": str(row.get("decode_mode", "")),
                "top1_margin": float(row.get("top1_margin", 0.0)),
            }
        )

    with csv_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=[
                "sample_id",
                "label",
                "plain_pred",
                "cpu_ref_pred",
                "match_plain_vs_cpu",
                "plain_correct",
                "cpu_correct",
                "cpu_pred_source",
                "cpu_decoded_pred",
                "cpu_selected_pred",
                "decode_mode",
                "top1_margin",
            ],
        )
        writer.writeheader()
        writer.writerows(csv_rows)

    payload = {
        "metadata": metadata,
        "aggregate": aggregate,
        "rows": list(rows),
        "mismatches": [
            row for row in rows if (row["cpu_ref_pred"] is None) or (not bool(row["match_plain_vs_cpu"]))
        ],
    }
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return {"csv": csv_path, "json": json_path}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate plaintext-vs-CPU tutorial3 baseline matrix over fixed samples"
    )
    parser.add_argument("--sample-start", type=int, default=1)
    parser.add_argument("--sample-end", type=int, default=20)
    parser.add_argument("--chips", type=int, default=1)
    parser.add_argument("--register-file-size", type=int, default=1024)
    parser.add_argument("--compile-register-file-size", type=int, default=256)
    parser.add_argument(
        "--pred-decode-mode",
        choices=list(_SUPPORTED_PRED_DECODE_MODES),
        default="stride128_mean",
    )
    parser.add_argument(
        "--work-root",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "cinnamon_tutorial3_plain_cpu_baseline",
    )
    parser.add_argument(
        "--report-dir",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "reports",
    )
    parser.add_argument("--report-basename", default="sw_emu_plain_cpu_baseline_matrix")
    parser.add_argument(
        "--model-path",
        type=pathlib.Path,
        default=TUTORIAL3_DIR / "mnist.pth",
    )
    args = parser.parse_args()

    labels = load_labels()
    sample_ids = parse_sample_range(args.sample_start, args.sample_end, labels)
    print(
        f"Preparing baseline matrix for samples {sample_ids[0]}..{sample_ids[-1]} "
        f"(count={len(sample_ids)})"
    )

    plain_model = load_plain_model(args.model_path)
    cpu_state = prepare_cpu_artifacts(
        out_dir=args.work_root / f"chips_{args.chips}",
        chips=args.chips,
        compile_register_file_size=args.compile_register_file_size,
    )

    rows: List[Dict[str, Any]] = []
    t_begin = time.perf_counter()
    for sample_id in sample_ids:
        label = int(labels[sample_id - 1])
        plain_pred = run_plain_prediction(plain_model, sample_id)
        cpu_result = run_cpu_prediction(
            context=cpu_state["context"],
            encryptor=cpu_state["encryptor"],
            instructions_base=cpu_state["instructions_base"],
            program_inputs=cpu_state["program_inputs"],
            evalkeys=cpu_state["evalkeys"],
            chips=args.chips,
            register_file_size=args.register_file_size,
            sample_id=sample_id,
            pred_decode_mode=str(args.pred_decode_mode),
        )
        cpu_ref_pred = cpu_result["pred_label"]
        rows.append(
            {
                "sample_id": sample_id,
                "label": label,
                "plain_pred": plain_pred,
                "cpu_ref_pred": cpu_ref_pred,
                "match_plain_vs_cpu": (cpu_ref_pred is not None) and (plain_pred == cpu_ref_pred),
                "plain_correct": plain_pred == label,
                "cpu_correct": (cpu_ref_pred is not None) and (cpu_ref_pred == label),
                "cpu_pred_valid": bool(cpu_result["pred_valid"]),
                "cpu_decoded_pred": cpu_result.get("decoded_pred_label"),
                "cpu_selected_pred": cpu_result.get("selected_pred_label"),
                "cpu_pred_source": str(cpu_result.get("pred_source", "cpu_decoded")),
                "cpu_scores": list(cpu_result["scores"]),
                "decode_mode": str(cpu_result.get("decode_mode", args.pred_decode_mode)),
                "top1_margin": float(cpu_result.get("top1_margin", 0.0)),
            }
        )
        print(
            f"sample={sample_id:03d} label={label} plain={plain_pred} "
            f"cpu_ref={cpu_ref_pred if cpu_ref_pred is not None else 'NA'} "
            f"source={cpu_result.get('pred_source', 'cpu_decoded')}"
        )
    t_end = time.perf_counter()

    aggregate = aggregate_rows(rows)
    metadata = {
        "generated_at": datetime.now().isoformat(),
        "sample_start": int(args.sample_start),
        "sample_end": int(args.sample_end),
        "sample_ids": sample_ids,
        "chips": int(args.chips),
        "register_file_size": int(args.register_file_size),
        "compile_register_file_size": int(args.compile_register_file_size),
        "pred_decode_mode": str(args.pred_decode_mode),
        "model_path": str(args.model_path),
        "work_root": str(args.work_root),
        "elapsed_s": float(t_end - t_begin),
        "keyswitch_pass_s": float(cpu_state["keyswitch_pass_s"]),
        "compile_s": float(cpu_state["compile_s"]),
        "evalkeys_reused_across_samples": False,
    }
    report_paths = write_reports(
        report_dir=args.report_dir,
        report_basename=args.report_basename,
        rows=rows,
        aggregate=aggregate,
        metadata=metadata,
    )

    print("Baseline matrix reports written:")
    print(f"  - {report_paths['csv']}")
    print(f"  - {report_paths['json']}")
    print(
        f"aggregate: plain_acc={aggregate['plain_accuracy']:.4f} "
        f"cpu_acc={aggregate['cpu_accuracy']:.4f} mismatches={aggregate['mismatch_count']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
