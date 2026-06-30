#!/usr/bin/env python3
"""Regression tests for the panel classifier using recorded raw captures."""

from __future__ import annotations

import csv
import io
import os
from pathlib import Path
import tarfile
import unittest


BIN_COUNT = 64
BIN_INTERVAL_US = 500_000


CAPTURES = {
    "standby": ("captures/20260628-203622-direct_standby_adc/raw.csv", "standby"),
    "large": ("captures/20260628-205853-direct_large_normal_adc/raw.csv", "running_large"),
    "large_no_water": ("captures/20260628-205011-direct_power_button_short_adc/raw.csv", "running_large"),
    "large_to_small": ("captures/20260628-210541-direct_running_select_adc/raw.csv", "running_small"),
    "small_to_large": ("captures/20260628-211027-direct_running_select_back_adc/raw.csv", "running_large"),
    "sim_large_to_small": ("captures/20260628-212952-direct_sw2_sim_retry_adc/raw.csv", "running_small"),
    "sim_small_to_large": ("captures/20260628-213241-direct_sw2_sim_back_adc/raw.csv", "running_large"),
    "sim_power_off": ("captures/20260628-213639-direct_sw1_sim_power_adc/raw.csv", "standby"),
}


def find_capture_archive() -> Path | None:
    env_path = os.environ.get("ICE_PANEL_CAPTURE_ARCHIVE")
    if env_path:
        path = Path(env_path).expanduser()
        return path if path.exists() else None

    candidates = sorted(Path("..").glob("ice_panel_sniffer-captures-*.tar.gz"))
    return candidates[-1] if candidates else None


def parse_raw_adc_line(line: str) -> tuple[int, tuple[int, int, int, int, int]] | None:
    line = line.strip()
    if not line or line.startswith("#"):
        return None

    row = next(csv.reader([line]))
    if len(row) == 1:
        row = next(csv.reader([row[0]]))

    if not row or row[0] != "A" or len(row) < 8:
        return None

    return int(row[1]), tuple(int(value) for value in row[2:7])


def bucket(value: int) -> str:
    if value < 100:
        return "0"
    if value > 3995:
        return "H"
    if 1500 <= value <= 2500:
        return "M"
    return "x"


class FirmwareClassifier:
    """Small Python mirror of the ESPHome component's ADC classifier."""

    def __init__(self) -> None:
        self.bins = [self._empty_bin() for _ in range(BIN_COUNT)]
        self.current_bin = 0
        self.current_bin_started_us: int | None = None
        self.last_signature = "xxxxx"

    @staticmethod
    def _empty_bin() -> dict[str, object]:
        return {"total": 0, "sig_0hhhh": 0, "sig_mhmhh": 0, "raw_sum": [0, 0, 0, 0, 0]}

    def _advance_bin_if_needed(self, sample_us: int) -> None:
        if self.current_bin_started_us is None:
            self.current_bin_started_us = sample_us

        while sample_us - self.current_bin_started_us >= BIN_INTERVAL_US:
            self.current_bin_started_us += BIN_INTERVAL_US
            self.current_bin = (self.current_bin + 1) % BIN_COUNT
            self.bins[self.current_bin] = self._empty_bin()

    def sample(self, sample_us: int, values: tuple[int, int, int, int, int]) -> None:
        self._advance_bin_if_needed(sample_us)
        signature = "".join(bucket(value) for value in values)
        self.last_signature = signature

        current = self.bins[self.current_bin]
        current["total"] = int(current["total"]) + 1
        if signature == "0HHHH":
            current["sig_0hhhh"] = int(current["sig_0hhhh"]) + 1
        if signature == "MHMHH":
            current["sig_mhmhh"] = int(current["sig_mhmhh"]) + 1

        raw_sum = current["raw_sum"]
        assert isinstance(raw_sum, list)
        for index, value in enumerate(values):
            raw_sum[index] += value

    def evaluate(self) -> dict[str, float | str]:
        total = sum(int(bin_["total"]) for bin_ in self.bins)
        if total == 0:
            return {"state": "unknown", "ratio_0hhhh": 0.0, "ratio_mhmhh": 0.0, "blink": 0.0}

        sig_large = sum(int(bin_["sig_0hhhh"]) for bin_ in self.bins)
        sig_mhmhh = sum(int(bin_["sig_mhmhh"]) for bin_ in self.bins)
        ratio_large = 100.0 * sig_large / total
        ratio_mhmhh = 100.0 * sig_mhmhh / total
        blink = self._blink_score()

        if ratio_large > 65.0 and blink < 60.0:
            state = "running_large"
        elif ratio_mhmhh > 55.0:
            state = "standby" if blink >= 60.0 else "running_small"
        else:
            state = "unknown"

        return {
            "state": state,
            "ratio_0hhhh": ratio_large,
            "ratio_mhmhh": ratio_mhmhh,
            "blink": blink,
        }

    def _blink_score(self) -> float:
        p1_means: list[float] = []
        for offset in range(BIN_COUNT):
            index = (self.current_bin + 1 + offset) % BIN_COUNT
            bin_ = self.bins[index]
            total = int(bin_["total"])
            if total == 0:
                continue
            raw_sum = bin_["raw_sum"]
            assert isinstance(raw_sum, list)
            p1_means.append(float(raw_sum[0]) / float(total))

        if len(p1_means) < 16:
            return 0.0

        min_value = min(p1_means)
        max_value = max(p1_means)
        amplitude = max_value - min_value
        if amplitude < 150.0:
            return 0.0

        threshold = min_value + amplitude * 0.5
        high_states = [value >= threshold for value in p1_means]
        high_count = sum(high_states)
        transitions = sum(
            1 for previous, current in zip(high_states, high_states[1:]) if previous != current
        )

        duration_s = len(p1_means) * 0.5
        expected_transitions = max(2.0, duration_s / 2.0)
        transition_error = abs(float(transitions) - expected_transitions) / expected_transitions
        transition_score = max(0.0, 1.0 - transition_error)
        amplitude_score = min(1.0, (amplitude - 150.0) / 100.0)

        duty = float(high_count) / float(len(p1_means))
        if 0.12 <= duty <= 0.48:
            duty_score = 1.0
        elif duty < 0.12:
            duty_score = max(0.0, duty / 0.12)
        else:
            duty_score = max(0.0, (0.70 - duty) / 0.22)

        return 100.0 * amplitude_score * transition_score * duty_score


class PanelClassifierCaptureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.archive = find_capture_archive()
        if cls.archive is None:
            raise unittest.SkipTest(
                "Set ICE_PANEL_CAPTURE_ARCHIVE or place ice_panel_sniffer-captures-*.tar.gz next to this repo"
            )

    def classify_capture(self, member_name: str) -> dict[str, float | str]:
        classifier = FirmwareClassifier()
        rows = 0
        assert self.archive is not None
        with tarfile.open(self.archive, "r:gz") as archive:
            with archive.extractfile(member_name) as raw_file:
                self.assertIsNotNone(raw_file, member_name)
                assert raw_file is not None
                for line in io.TextIOWrapper(raw_file):
                    parsed = parse_raw_adc_line(line)
                    if parsed is None:
                        continue
                    sample_us, values = parsed
                    classifier.sample(sample_us, values)
                    rows += 1

        self.assertGreater(rows, 10_000, member_name)
        result = classifier.evaluate()
        result["rows"] = rows
        return result

    def test_bucket_boundaries_match_firmware(self) -> None:
        self.assertEqual(bucket(0), "0")
        self.assertEqual(bucket(99), "0")
        self.assertEqual(bucket(100), "x")
        self.assertEqual(bucket(1499), "x")
        self.assertEqual(bucket(1500), "M")
        self.assertEqual(bucket(2500), "M")
        self.assertEqual(bucket(2501), "x")
        self.assertEqual(bucket(3995), "x")
        self.assertEqual(bucket(3996), "H")
        self.assertEqual(bucket(4095), "H")

    def test_final_state_classification_from_raw_captures(self) -> None:
        for label, (member_name, expected_state) in CAPTURES.items():
            with self.subTest(label=label):
                result = self.classify_capture(member_name)
                self.assertEqual(result["state"], expected_state, result)

                if expected_state == "running_large":
                    self.assertGreater(result["ratio_0hhhh"], 65.0, result)
                    self.assertLess(result["blink"], 60.0, result)
                elif expected_state == "running_small":
                    self.assertGreater(result["ratio_mhmhh"], 55.0, result)
                    self.assertLess(result["blink"], 60.0, result)
                elif expected_state == "standby":
                    self.assertGreater(result["ratio_mhmhh"], 55.0, result)
                    self.assertGreaterEqual(result["blink"], 60.0, result)


if __name__ == "__main__":
    unittest.main()
