#!/usr/bin/env python3
"""Compare ESPHome debug captures with older ice-panel sniffer raw captures."""

from __future__ import annotations

import argparse
import csv
import io
import math
import statistics
import tarfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable


DEFAULT_OLD_TAR = Path("/Users/ljzd/Documents/ice_panel_sniffer/captures/ice_panel_sniffer-captures-20260630-130202.tar.gz")
DEFAULT_OLD_CAPTURES = {
    "old_standby": ("captures/20260628-203622-direct_standby_adc/raw.csv", "standby"),
    "old_large": ("captures/20260628-205853-direct_large_normal_adc/raw.csv", "large"),
    "old_small": ("captures/20260628-210541-direct_running_select_adc/raw.csv", "small"),
    "old_small_to_large": ("captures/20260628-211027-direct_running_select_back_adc/raw.csv", "mixed"),
}
WINDOWS_S = [0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0]


def bucket(value: float) -> str:
    if value < 100:
        return "0"
    if value > 3995:
        return "H"
    if 1500 <= value <= 2500:
        return "M"
    return "x"


def signature(values: Iterable[float]) -> str:
    return "".join(bucket(value) for value in values)


def fnum(value: str | float | int | None, default: float = 0.0) -> float:
    if value in (None, ""):
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def median(values: Iterable[float]) -> float:
    values = list(values)
    return statistics.median(values) if values else 0.0


def pct(part: float, total: float) -> float:
    return 100.0 * part / total if total else 0.0


def load_old_samples_from_tar(tar_path: Path, member: str) -> list[dict[str, float | str]]:
    rows: list[dict[str, float | str]] = []
    with tarfile.open(tar_path) as tf:
        raw_file = tf.extractfile(member)
        if raw_file is None:
            return rows
        reader = csv.reader(io.TextIOWrapper(raw_file, encoding="utf-8", errors="replace"))
        first_us: int | None = None
        for row in reader:
            if not row:
                continue
            line = row[0].strip()
            if not line.startswith("A,"):
                continue
            parts = line.split(",")
            if len(parts) < 8:
                continue
            try:
                device_us = int(parts[1])
                values = [float(part) for part in parts[2:7]]
            except ValueError:
                continue
            if first_us is None:
                first_us = device_us
            t = ((device_us - first_us) & 0xFFFFFFFF) / 1_000_000.0
            rows.append(
                {
                    "t": t,
                    "p1": values[0],
                    "p2": values[1],
                    "p3": values[2],
                    "p4": values[3],
                    "p5": values[4],
                    "signature": signature(values),
                }
            )
    return rows


def load_debug_frames(capture_dir: Path) -> list[dict[str, float | str]]:
    path = capture_dir / "debug_frames.csv"
    if not path.exists():
        return []
    frames: list[dict[str, float | str]] = []
    current: dict[str, float | str] | None = None
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            device_ms = int(fnum(row.get("device_ms"), -1))
            if device_ms < 0:
                continue
            if row.get("type") == "D1" or current is None:
                if current is not None:
                    frames.append(current)
                current = {}
                current["device_ms"] = device_ms
                current["t"] = device_ms / 1000.0
            frame = current
            for key, value in row.items():
                if key in {"", "raw", "host_ts", "type", "device_ms"}:
                    continue
                if value != "":
                    frame[key] = value
        if current is not None:
            frames.append(current)
    if not frames:
        return []
    first_t = min(fnum(frame["t"]) for frame in frames)
    result = []
    for frame in sorted(frames, key=lambda item: fnum(item["t"])):
        frame["t"] = fnum(frame["t"]) - first_t
        result.append(frame)
    return result


def window_summaries(rows: list[dict[str, float | str]], window_s: float, is_debug: bool) -> list[dict[str, float]]:
    if not rows:
        return []
    end = fnum(rows[-1]["t"])
    summaries: list[dict[str, float]] = []
    start = 0.0
    while start + window_s <= end + 1e-6:
        stop = start + window_s
        chunk = [row for row in rows if start <= fnum(row["t"]) < stop]
        if chunk:
            if is_debug:
                samples = sum(fnum(row.get("samples")) for row in chunk)
                ratio_0 = median(fnum(row.get("ratio_0hhhh")) for row in chunk)
                ratio_m = median(fnum(row.get("ratio_mhmhh")) for row in chunk)
                p1_values = [fnum(row.get("p1_mean")) for row in chunk]
                p3_values = [fnum(row.get("p3_mean")) for row in chunk]
                p4_values = [fnum(row.get("p4_mean")) for row in chunk]
                p1_stddev = median(fnum(row.get("p1_stddev")) for row in chunk)
                delta_p1_p3 = median(fnum(row.get("delta_p1_p3")) for row in chunk)
                delta_p1_p4 = median(fnum(row.get("delta_p1_p4")) for row in chunk)
            else:
                samples = len(chunk)
                sigs = Counter(str(row.get("signature", "")) for row in chunk)
                ratio_0 = pct(sigs["0HHHH"], samples)
                ratio_m = pct(sigs["MHMHH"], samples)
                p1_values = [fnum(row.get("p1")) for row in chunk]
                p3_values = [fnum(row.get("p3")) for row in chunk]
                p4_values = [fnum(row.get("p4")) for row in chunk]
                p1_stddev = statistics.pstdev(p1_values) if len(p1_values) > 1 else 0.0
                delta_p1_p3 = median(fnum(row.get("p1")) - fnum(row.get("p3")) for row in chunk)
                delta_p1_p4 = median(fnum(row.get("p1")) - fnum(row.get("p4")) for row in chunk)
            summaries.append(
                {
                    "start": start,
                    "samples": samples,
                    "ratio_0hhhh": ratio_0,
                    "ratio_mhmhh": ratio_m,
                    "p1_amp": max(p1_values) - min(p1_values) if p1_values else 0.0,
                    "p1_stddev": p1_stddev,
                    "delta_p1_p3": delta_p1_p3,
                    "delta_p1_p4": delta_p1_p4,
                    "p1_p3_amp": (max(p1_values) - min(p3_values)) if p1_values and p3_values else 0.0,
                    "p1_p4_amp": (max(p1_values) - min(p4_values)) if p1_values and p4_values else 0.0,
                }
            )
        start += window_s
    return summaries


def summarize_dataset(name: str, state: str, rows: list[dict[str, float | str]], is_debug: bool) -> list[str]:
    lines = [f"### {name} ({state})", ""]
    if not rows:
        lines.append("No rows loaded.")
        lines.append("")
        return lines

    duration = fnum(rows[-1]["t"]) - fnum(rows[0]["t"])
    lines.append(f"- Rows: `{len(rows)}`; duration: `{duration:.1f}s`; source: `{'debug D-frames' if is_debug else 'old A samples'}`")
    if is_debug:
        sample_rates = [fnum(row.get("samples")) for row in rows if fnum(row.get("samples")) > 0]
        error_rates = [fnum(row.get("adc_error_rate")) for row in rows]
        backends = sorted({str(row.get("adc_backend")) for row in rows if row.get("adc_backend")})
        frame_steps = [fnum(b["t"]) - fnum(a["t"]) for a, b in zip(rows, rows[1:]) if fnum(b["t"]) > fnum(a["t"])]
        lines.append(f"- Backend: `{', '.join(backends) or 'unknown'}`")
        lines.append(f"- Median frame sample rate: `{median(sample_rates):.1f} Hz`; median ADC error: `{median(error_rates):.3f}%`")
        if frame_steps:
            lines.append(f"- Debug frame period: median `{median(frame_steps):.3f}s`")
    else:
        sigs = Counter(str(row.get("signature", "")) for row in rows)
        top = ", ".join(f"`{sig}` {pct(count, len(rows)):.1f}%" for sig, count in sigs.most_common(5))
        lines.append(f"- Top signatures: {top}")

    lines.append("")
    lines.append("| Window | ratio 0HHHH median | ratio MHMHH median | P1 amp median | P1 stddev median | P1-P3 median | P1-P4 median |")
    lines.append("|---:|---:|---:|---:|---:|---:|---:|")
    for window_s in WINDOWS_S:
        summaries = window_summaries(rows, window_s, is_debug)
        if not summaries:
            lines.append(f"| {window_s:g}s | n/a | n/a | n/a | n/a | n/a | n/a |")
            continue
        lines.append(
            f"| {window_s:g}s | {median(s['ratio_0hhhh'] for s in summaries):.1f}% | "
            f"{median(s['ratio_mhmhh'] for s in summaries):.1f}% | "
            f"{median(s['p1_amp'] for s in summaries):.1f} | "
            f"{median(s['p1_stddev'] for s in summaries):.1f} | "
            f"{median(s['delta_p1_p3'] for s in summaries):.1f} | "
            f"{median(s['delta_p1_p4'] for s in summaries):.1f} |"
        )
    lines.append("")
    return lines


def quantile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    index = (len(values) - 1) * fraction
    low = int(index)
    high = min(low + 1, len(values) - 1)
    weight = index - low
    return values[low] * (1.0 - weight) + values[high] * weight


def best_single_threshold(
    standby: list[dict[str, float | str]],
    small: list[dict[str, float | str]],
    field: str,
    greater_means_small: bool,
) -> tuple[float, float, int]:
    values = sorted({fnum(row.get(field)) for row in standby + small})
    best_accuracy = -1.0
    best_threshold = 0.0
    best_errors = 0
    for threshold in values:
        errors = 0
        for row in standby:
            predicted_small = fnum(row.get(field)) > threshold if greater_means_small else fnum(row.get(field)) < threshold
            errors += int(predicted_small)
        for row in small:
            predicted_small = fnum(row.get(field)) > threshold if greater_means_small else fnum(row.get(field)) < threshold
            errors += int(not predicted_small)
        accuracy = 1.0 - errors / max(1, len(standby) + len(small))
        if accuracy > best_accuracy:
            best_accuracy = accuracy
            best_threshold = threshold
            best_errors = errors
    return best_accuracy, best_threshold, best_errors


def summarize_debug_discriminators(debug_sets: list[tuple[str, str, list[dict[str, float | str]]]]) -> list[str]:
    standby_rows = [rows for _, state, rows in debug_sets if state == "standby" and rows]
    small_rows = [rows for _, state, rows in debug_sets if state == "small" and rows]
    if not standby_rows or not small_rows:
        return []

    standby = standby_rows[0]
    small = small_rows[0]
    fields = [
        ("p1_stddev", True),
        ("delta_p1_p3", True),
        ("p2_stddev", False),
        ("delta_p2_p4", True),
        ("delta_p5_p2", False),
    ]

    lines = [
        "## Standby vs Small Discriminator Hints",
        "",
        "These thresholds are derived from the current debug captures only. Treat them as candidates for the main classifier, not universal constants yet.",
        "",
        "| Feature | Standby p10/p50/p90 | Small p10/p50/p90 | Best rule for Small | Accuracy |",
        "|---|---:|---:|---|---:|",
    ]
    for field, greater_means_small in fields:
        standby_values = [fnum(row.get(field)) for row in standby if row.get(field) not in (None, "")]
        small_values = [fnum(row.get(field)) for row in small if row.get(field) not in (None, "")]
        accuracy, threshold, errors = best_single_threshold(standby, small, field, greater_means_small)
        op = ">" if greater_means_small else "<"
        lines.append(
            f"| `{field}` | "
            f"{quantile(standby_values, 0.1):.1f}/{quantile(standby_values, 0.5):.1f}/{quantile(standby_values, 0.9):.1f} | "
            f"{quantile(small_values, 0.1):.1f}/{quantile(small_values, 0.5):.1f}/{quantile(small_values, 0.9):.1f} | "
            f"`{field} {op} {threshold:.1f}` | {accuracy * 100.0:.1f}% ({errors} errors) |"
        )
    lines.extend(
        [
            "",
            "Practical first-pass rule: keep `MHMHH` as the shared running/standby candidate, then separate Small from Standby with `p1_stddev`, `delta_p1_p3`, or `delta_p2_p4` over a 1-2 second frame window.",
            "",
        ]
    )
    return lines


def infer_state_from_path(path: Path) -> str:
    text = path.name.lower()
    if "standby" in text:
        return "standby"
    if "small" in text:
        return "small"
    if "large" in text:
        return "large"
    return "unknown"


def build_report(args: argparse.Namespace) -> str:
    lines = [
        "# Ice Maker Debug Capture Analysis",
        "",
        "This report compares old 1 kHz Arduino sniffer captures with ESPHome debug firmware D-frame captures.",
        "ADC values are relative floating-GPIO signatures, not real panel voltages.",
        "",
    ]

    if args.old_tar and args.old_tar.exists():
        lines.extend(["## Old Sniffer Baseline", ""])
        for name, (member, state) in DEFAULT_OLD_CAPTURES.items():
            try:
                rows = load_old_samples_from_tar(args.old_tar, member)
            except KeyError:
                rows = []
            lines.extend(summarize_dataset(name, state, rows, is_debug=False))
    elif args.old_tar:
        lines.extend([f"Old tar not found: `{args.old_tar}`", ""])

    debug_sets: list[tuple[str, str, list[dict[str, float | str]]]] = []
    if args.debug_capture:
        lines.extend(["## ESPHome Debug Captures", ""])
        for capture_dir in args.debug_capture:
            state = infer_state_from_path(capture_dir)
            rows = load_debug_frames(capture_dir)
            debug_sets.append((capture_dir.name, state, rows))
            lines.extend(summarize_dataset(capture_dir.name, state, rows, is_debug=True))
        lines.extend(summarize_debug_discriminators(debug_sets))
        lines.extend(
            [
                "Note: debug captures are currently exported as firmware-side summary frames.",
                "Windows shorter than the debug frame period are therefore limited by the emitted frame cadence.",
                "",
            ]
        )

    lines.extend(
        [
            "## Interpretation Checklist",
            "",
            "- If debug `Sample Rate` is near `10000 Hz` per P1-P5 frame and `ADC Error Rate` is low, sampling performance improved over the old 1 kHz sniffer.",
            "- If standby and small both keep `MHMHH` dominant, prefer `P1 stddev`, `P1-P4`, and blink-window features over signature-only detection.",
            "- If short windows still overlap heavily, the limiting factor is likely the floating direct-GPIO electrical interface rather than ESP32-C3 sampling throughput.",
            "",
        ]
    )
    return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old-tar", type=Path, default=DEFAULT_OLD_TAR)
    parser.add_argument("--debug-capture", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, default=Path("debug_captures/analysis_report.md"))
    return parser


def main() -> int:
    args = build_parser().parse_args()
    report = build_report(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(report, encoding="utf-8")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
