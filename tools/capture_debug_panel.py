#!/usr/bin/env python3
"""Capture Chang Hong ice-maker ESPHome debug firmware logs."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import queue
import re
import select
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

import serial
from serial.tools import list_ports


DEFAULT_BAUD = 921600
DEFAULT_OUT_DIR = Path("debug_captures")
CSV_RE = re.compile(r"\b(D[1-4],.*)$")

FRAME_FIELDS = [
    "type",
    "host_ts",
    "device_ms",
    "bias",
    "samples",
    "signature",
    "digital_mask",
    "ratio_0hhhh",
    "ratio_mhmhh",
    "ratio_active_hhhh",
    "adc_error_rate",
    "p1_mean",
    "p2_mean",
    "p3_mean",
    "p4_mean",
    "p5_mean",
    "p1_min",
    "p2_min",
    "p3_min",
    "p4_min",
    "p5_min",
    "p1_max",
    "p2_max",
    "p3_max",
    "p4_max",
    "p5_max",
    "p1_stddev",
    "p2_stddev",
    "p3_stddev",
    "p4_stddev",
    "p5_stddev",
    "p1_duty",
    "p2_duty",
    "p3_duty",
    "p4_duty",
    "p5_duty",
    "p1_edges",
    "p2_edges",
    "p3_edges",
    "p4_edges",
    "p5_edges",
    "delta_p5_p1",
    "delta_p5_p2",
    "delta_p1_p2",
    "delta_p1_p3",
    "delta_p3_p4",
    "delta_p2_p4",
    "delta_p1_p4",
    "adc_backend",
    "adc_calibration",
    "p1_mv_mean",
    "p2_mv_mean",
    "p3_mv_mean",
    "p4_mv_mean",
    "p5_mv_mean",
    "raw",
]


def timestamp_slug() -> str:
    return dt.datetime.now().strftime("%Y%m%d-%H%M%S")


def safe_label(label: str | None) -> str:
    if not label:
        return "debug_capture"
    return "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in label)


def choose_serial_port(port: str) -> str:
    if port != "auto":
        return port

    ports = list(list_ports.comports())
    preferred = [
        p.device
        for p in ports
        if "usbmodem" in p.device.lower()
        or "usbserial" in p.device.lower()
        or "wchusbserial" in p.device.lower()
    ]
    if preferred:
        return sorted(preferred)[0]
    if ports:
        return sorted(p.device for p in ports)[0]
    raise SystemExit("No serial ports found. Pass --port explicitly after reconnecting the ESP32-C3.")


def input_worker(commands: queue.Queue[str], stop: threading.Event) -> None:
    print("Interactive commands: mark <label>, quit")
    while not stop.is_set():
        try:
            line = sys.stdin.readline()
        except KeyboardInterrupt:
            commands.put("quit")
            return
        if line == "":
            time.sleep(0.05)
            continue
        commands.put(line.strip())


def parse_float(value: str) -> float | str:
    try:
        return float(value)
    except ValueError:
        return value


def parse_debug_payload(payload: str, host_ts: float) -> dict[str, Any] | None:
    parts = [part.strip() for part in payload.split(",")]
    if not parts or parts[0] not in {"D1", "D2", "D3", "D4"}:
        return None

    record: dict[str, Any] = {field: "" for field in FRAME_FIELDS}
    record["type"] = parts[0]
    record["host_ts"] = f"{host_ts:.6f}"
    record["raw"] = payload

    try:
        record["device_ms"] = int(parts[1])
        if parts[0] == "D1" and len(parts) >= 15:
            record.update(
                {
                    "bias": parts[2],
                    "samples": int(parts[3]),
                    "signature": parts[4],
                    "digital_mask": parts[5],
                    "ratio_0hhhh": parse_float(parts[6]),
                    "ratio_mhmhh": parse_float(parts[7]),
                    "ratio_active_hhhh": parse_float(parts[8]),
                    "adc_error_rate": parse_float(parts[9]),
                    "p1_mean": parse_float(parts[10]),
                    "p2_mean": parse_float(parts[11]),
                    "p3_mean": parse_float(parts[12]),
                    "p4_mean": parse_float(parts[13]),
                    "p5_mean": parse_float(parts[14]),
                }
            )
        elif parts[0] == "D2" and len(parts) >= 17:
            names = [
                "p1_min",
                "p2_min",
                "p3_min",
                "p4_min",
                "p5_min",
                "p1_max",
                "p2_max",
                "p3_max",
                "p4_max",
                "p5_max",
                "p1_stddev",
                "p2_stddev",
                "p3_stddev",
                "p4_stddev",
                "p5_stddev",
            ]
            for name, value in zip(names, parts[2:17], strict=True):
                record[name] = parse_float(value)
        elif parts[0] == "D3" and len(parts) >= 19:
            names = [
                "p1_duty",
                "p2_duty",
                "p3_duty",
                "p4_duty",
                "p5_duty",
                "p1_edges",
                "p2_edges",
                "p3_edges",
                "p4_edges",
                "p5_edges",
                "delta_p5_p1",
                "delta_p5_p2",
                "delta_p1_p2",
                "delta_p1_p3",
                "delta_p3_p4",
                "delta_p2_p4",
                "delta_p1_p4",
            ]
            for name, value in zip(names, parts[2:19], strict=True):
                record[name] = parse_float(value)
        elif parts[0] == "D4" and len(parts) >= 10:
            record.update(
                {
                    "adc_backend": parts[2],
                    "adc_calibration": parts[3],
                    "adc_error_rate": parse_float(parts[4]),
                    "p1_mv_mean": parse_float(parts[5]),
                    "p2_mv_mean": parse_float(parts[6]),
                    "p3_mv_mean": parse_float(parts[7]),
                    "p4_mv_mean": parse_float(parts[8]),
                    "p5_mv_mean": parse_float(parts[9]),
                }
            )
    except (IndexError, ValueError):
        return None
    return record


def capture(args: argparse.Namespace) -> Path:
    out_dir = args.out_dir / f"{timestamp_slug()}-{safe_label(args.label)}"
    out_dir.mkdir(parents=True, exist_ok=True)
    raw_path = out_dir / "raw.log"
    frames_path = out_dir / "debug_frames.csv"
    markers_path = out_dir / "markers.csv"

    stop = threading.Event()
    commands: queue.Queue[str] = queue.Queue()
    input_thread = threading.Thread(target=input_worker, args=(commands, stop), daemon=True)
    input_thread.start()

    print(f"Opening {args.source} log source")
    print(f"Writing debug capture to {out_dir}")

    with raw_path.open("w", encoding="utf-8") as raw_file, frames_path.open(
        "w", newline="", encoding="utf-8"
    ) as frames_file, markers_path.open("w", newline="", encoding="utf-8") as markers_file:
        frame_writer = csv.DictWriter(frames_file, fieldnames=FRAME_FIELDS)
        marker_writer = csv.DictWriter(markers_file, fieldnames=["host_ts", "label"])
        frame_writer.writeheader()
        marker_writer.writeheader()
        marker_writer.writerow({"host_ts": f"{time.time():.6f}", "label": "capture_start"})

        def service_commands() -> None:
            while True:
                try:
                    command = commands.get_nowait()
                except queue.Empty:
                    break
                if not command:
                    continue
                if command == "quit":
                    marker_writer.writerow({"host_ts": f"{time.time():.6f}", "label": "quit"})
                    stop.set()
                    break
                if command.startswith("mark "):
                    label = command[5:].strip() or "unnamed"
                    marker_writer.writerow({"host_ts": f"{time.time():.6f}", "label": label})
                    print(f"MARK {label}")
                else:
                    print(f"Ignoring unsupported command: {command}")

        def handle_line(line: str) -> None:
            host_ts = time.time()
            raw_file.write(f"{host_ts:.6f} {line}\n")
            raw_file.flush()
            match = CSV_RE.search(line)
            if not match:
                return
            record = parse_debug_payload(match.group(1), host_ts)
            if record is not None:
                frame_writer.writerow(record)
                frames_file.flush()

        started = time.monotonic()
        time.sleep(args.settle_s)

        if args.source == "serial":
            port = choose_serial_port(args.port)
            print(f"Serial: {port} at {args.baud} baud")
            with serial.Serial(port=port, baudrate=args.baud, timeout=0.2) as ser:
                while not stop.is_set():
                    if args.duration_s and (time.monotonic() - started) >= args.duration_s:
                        marker_writer.writerow({"host_ts": f"{time.time():.6f}", "label": "duration_elapsed"})
                        break
                    service_commands()
                    raw = ser.readline()
                    if raw:
                        handle_line(raw.decode("utf-8", errors="replace").rstrip("\r\n"))
        else:
            cmd = [
                args.esphome_bin,
                "logs",
                args.config,
                "--device",
                args.device,
                "--no-states",
            ]
            print("Network logs:", " ".join(cmd))
            proc = subprocess.Popen(
                cmd,
                cwd=Path.cwd(),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            assert proc.stdout is not None
            try:
                while not stop.is_set():
                    if args.duration_s and (time.monotonic() - started) >= args.duration_s:
                        marker_writer.writerow({"host_ts": f"{time.time():.6f}", "label": "duration_elapsed"})
                        break
                    service_commands()
                    readable, _, _ = select.select([proc.stdout], [], [], 0.2)
                    if not readable:
                        if proc.poll() is not None:
                            break
                        continue
                    line = proc.stdout.readline()
                    if line == "":
                        if proc.poll() is not None:
                            break
                        continue
                    handle_line(line.rstrip("\r\n"))
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()

    print(f"Capture complete: {out_dir}")
    return out_dir


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", choices=["serial", "esphome"], default="serial")
    parser.add_argument("--port", default="auto", help="Serial port, or 'auto' for the first USB serial device.")
    parser.add_argument("--device", default="chang-hong-ice-maker-debug.local", help="ESPHome log device/address.")
    parser.add_argument("--config", default="chang-hong-ice-maker-debug.yaml", help="ESPHome YAML for network logs.")
    parser.add_argument("--esphome-bin", default=str(Path(sys.executable).with_name("esphome")))
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--duration-s", type=float, default=0.0, help="0 means run until 'quit'.")
    parser.add_argument("--settle-s", type=float, default=1.0)
    parser.add_argument("--label", default=None)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    return parser


def main() -> int:
    capture(build_parser().parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
