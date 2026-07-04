#!/usr/bin/env python3
"""Regression tests for the panel classifier using recorded raw captures."""

from __future__ import annotations

import csv
import io
import math
import os
from pathlib import Path
import tarfile
import unittest


BIN_COUNT = 64
BIN_INTERVAL_US = 500_000
FAST_WINDOW_BINS = 2
STANDBY_WINDOW_BINS = 32
EVALUATE_INTERVAL_US = 1_000_000

LARGE_SIGNATURE_THRESHOLD = 65.0
MHMHH_SIGNATURE_THRESHOLD = 55.0
STANDBY_P1_MIN_AMPLITUDE = 150.0
STANDBY_P1_MAX_AMPLITUDE = 800.0
STANDBY_MIN_DUTY = 0.08
STANDBY_MAX_DUTY = 0.55
SMALL_P2_STDDEV_MAX = 650.0
SMALL_DELTA_P2_P4_MIN = -120.0
SMALL_DELTA_P5_P2_MAX = 80.0
STANDBY_P2_STDDEV_MIN = 700.0
STANDBY_DELTA_P2_P4_MAX = -145.0
STANDBY_DELTA_P5_P2_MIN = 120.0
FEATURE_SCORE_THRESHOLD = 2
FEATURE_CONFIRM_COUNT = 2
RUNNING_UNKNOWN_LIMIT = 8
RUNNING_STANDBY_LIMIT = 8
STANDBY_UNKNOWN_LIMIT = 8


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


DMA_FIXTURE = Path(__file__).resolve().parent / "fixtures" / "debug_dma_summary.csv"


def find_capture_archive() -> Path | None:
    env_path = os.environ.get("ICE_PANEL_CAPTURE_ARCHIVE")
    if env_path:
        path = Path(env_path).expanduser()
        return path if path.exists() else None

    candidates = sorted(Path("..").glob("ice_panel_sniffer-captures-*.tar.gz"))
    if candidates:
        return candidates[-1]

    local_archive = Path("/Users/ljzd/Documents/ice_panel_sniffer/captures/ice_panel_sniffer-captures-20260630-130202.tar.gz")
    return local_archive if local_archive.exists() else None


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


def feature_scores_from_summary(row: dict[str, str]) -> tuple[int, int]:
    p2_stddev = float(row["p2_stddev"])
    delta_p2_p4 = float(row["delta_p2_p4"])
    delta_p5_p2 = float(row["delta_p5_p2"])
    small_score = sum(
        [
            p2_stddev <= SMALL_P2_STDDEV_MAX,
            delta_p2_p4 >= SMALL_DELTA_P2_P4_MIN,
            delta_p5_p2 <= SMALL_DELTA_P5_P2_MAX,
        ]
    )
    standby_score = sum(
        [
            p2_stddev >= STANDBY_P2_STDDEV_MIN,
            delta_p2_p4 <= STANDBY_DELTA_P2_P4_MAX,
            delta_p5_p2 >= STANDBY_DELTA_P5_P2_MIN,
        ]
    )
    return small_score, standby_score


class FirmwareClassifier:
    """Small Python mirror of the ESPHome component's ADC classifier."""

    def __init__(self) -> None:
        self.bins = [self._empty_bin() for _ in range(BIN_COUNT)]
        self.current_bin = 0
        self.current_bin_started_us: int | None = None
        self.last_eval_us: int | None = None
        self.last_signature = "xxxxx"
        self.fast_state_candidate = "unknown"
        self.feature_state_candidate = "unknown"
        self.previous_feature_candidate = "unknown"
        self.feature_candidate_count = 0
        self.exposed_state = "unknown"
        self.classified_state = "unknown"
        self.running_unknown_windows = 0
        self.running_standby_windows = 0
        self.ratio_0hhhh = 0.0
        self.ratio_mhmhh = 0.0
        self.small_feature_score = 0
        self.standby_feature_score = 0
        self.p2_stddev = 0.0
        self.delta_p2_p4 = 0.0
        self.delta_p5_p2 = 0.0
        self.blink_score = 0.0
        self.standby_window_valid = False
        self.standby_unknown_windows = 0

    @staticmethod
    def _empty_bin() -> dict[str, object]:
        return {
            "total": 0,
            "sig_0hhhh": 0,
            "sig_mhmhh": 0,
            "raw_sum": [0, 0, 0, 0, 0],
            "raw_sq_sum": [0, 0, 0, 0, 0],
            "delta_p2_p4_sum": 0,
            "delta_p5_p2_sum": 0,
        }

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
        raw_sq_sum = current["raw_sq_sum"]
        assert isinstance(raw_sum, list)
        assert isinstance(raw_sq_sum, list)
        for index, value in enumerate(values):
            raw_sum[index] += value
            raw_sq_sum[index] += value * value

        current["delta_p2_p4_sum"] = int(current["delta_p2_p4_sum"]) + values[1] - values[3]
        current["delta_p5_p2_sum"] = int(current["delta_p5_p2_sum"]) + values[4] - values[1]

        if self.last_eval_us is None:
            self.last_eval_us = sample_us
        if sample_us - self.last_eval_us >= EVALUATE_INTERVAL_US:
            self.last_eval_us = sample_us
            self.evaluate()

    def evaluate(self) -> dict[str, float | str | bool]:
        fast = self._window_stats(FAST_WINDOW_BINS)
        standby = self._window_stats(STANDBY_WINDOW_BINS)
        blink = self._standby_blink_stats(STANDBY_WINDOW_BINS)

        if fast["total"] == 0:
            self.fast_state_candidate = "unknown"
            self.feature_state_candidate = "unknown"
            self.previous_feature_candidate = "unknown"
            self.feature_candidate_count = 0
            classified = self._apply_transition_guard("unknown")
            self.classified_state = classified
            self.exposed_state = classified
            return self.result()

        self.ratio_0hhhh = 100.0 * float(fast["sig_0hhhh"]) / float(fast["total"])
        self.ratio_mhmhh = 100.0 * float(fast["sig_mhmhh"]) / float(fast["total"])
        self.p2_stddev = self._pin_stddev(fast, 1)
        self.delta_p2_p4 = self._delta_mean(fast["delta_p2_p4_sum"], fast["total"])
        self.delta_p5_p2 = self._delta_mean(fast["delta_p5_p2_sum"], fast["total"])
        self.small_feature_score = self._small_feature_score(fast)
        self.standby_feature_score = self._standby_feature_score(fast)

        standby_mhmhh = 0.0
        if standby["total"]:
            standby_mhmhh = 100.0 * float(standby["sig_mhmhh"]) / float(standby["total"])

        self.blink_score = float(blink["score"])
        self.standby_window_valid = (
            int(standby["bins"]) >= STANDBY_WINDOW_BINS
            and standby_mhmhh >= MHMHH_SIGNATURE_THRESHOLD
            and bool(blink["valid"])
        )

        feature_candidate = "unknown"
        if self.ratio_0hhhh >= LARGE_SIGNATURE_THRESHOLD:
            feature_candidate = "running_large"
        elif self.ratio_mhmhh >= MHMHH_SIGNATURE_THRESHOLD:
            small_match = self.small_feature_score >= FEATURE_SCORE_THRESHOLD
            standby_match = self.standby_feature_score >= FEATURE_SCORE_THRESHOLD
            if small_match and not standby_match:
                feature_candidate = "running_small"
            elif standby_match and not small_match:
                feature_candidate = "standby"

        self._update_fast_candidate(feature_candidate)

        if self.fast_state_candidate == "running_large":
            classified = "running_large"
            self.running_unknown_windows = 0
        elif self.fast_state_candidate == "running_small":
            classified = "running_small"
            self.running_unknown_windows = 0
        elif self.fast_state_candidate == "standby":
            classified = "standby"
            self.running_unknown_windows = 0
        elif self.standby_window_valid:
            classified = "standby"
            self.running_unknown_windows = 0
        elif self.exposed_state in {"running_large", "running_small"}:
            classified = "unknown"
        elif self.exposed_state == "standby" and int(standby["bins"]) < STANDBY_WINDOW_BINS:
            classified = "standby"
        else:
            self.running_unknown_windows = 0
            classified = "unknown"

        classified = self._apply_transition_guard(classified)
        self.classified_state = classified
        self.exposed_state = classified
        return self.result()

    def result(self) -> dict[str, float | str | bool]:
        return {
            "state": self.exposed_state,
            "classifier": self.classified_state,
            "fast_candidate": self.fast_state_candidate,
            "feature_candidate": self.feature_state_candidate,
            "ratio_0hhhh": self.ratio_0hhhh,
            "ratio_mhmhh": self.ratio_mhmhh,
            "small_feature_score": self.small_feature_score,
            "standby_feature_score": self.standby_feature_score,
            "p2_stddev": self.p2_stddev,
            "delta_p2_p4": self.delta_p2_p4,
            "delta_p5_p2": self.delta_p5_p2,
            "blink": self.blink_score,
            "standby_window_valid": self.standby_window_valid,
        }

    def _window_stats(self, window_bins: int) -> dict[str, object]:
        stats: dict[str, object] = {
            "bins": 0,
            "total": 0,
            "sig_0hhhh": 0,
            "sig_mhmhh": 0,
            "raw_sum": [0, 0, 0, 0, 0],
            "raw_sq_sum": [0, 0, 0, 0, 0],
            "delta_p2_p4_sum": 0,
            "delta_p5_p2_sum": 0,
        }
        for offset in range(min(window_bins, BIN_COUNT)):
            index = (self.current_bin + BIN_COUNT - offset) % BIN_COUNT
            bin_ = self.bins[index]
            total = int(bin_["total"])
            if total == 0:
                continue
            stats["bins"] = int(stats["bins"]) + 1
            stats["total"] = int(stats["total"]) + total
            stats["sig_0hhhh"] = int(stats["sig_0hhhh"]) + int(bin_["sig_0hhhh"])
            stats["sig_mhmhh"] = int(stats["sig_mhmhh"]) + int(bin_["sig_mhmhh"])
            stats_sum = stats["raw_sum"]
            stats_sq_sum = stats["raw_sq_sum"]
            bin_sum = bin_["raw_sum"]
            bin_sq_sum = bin_["raw_sq_sum"]
            assert isinstance(stats_sum, list)
            assert isinstance(stats_sq_sum, list)
            assert isinstance(bin_sum, list)
            assert isinstance(bin_sq_sum, list)
            for index, value in enumerate(bin_sum):
                stats_sum[index] += value
                stats_sq_sum[index] += bin_sq_sum[index]
            stats["delta_p2_p4_sum"] = int(stats["delta_p2_p4_sum"]) + int(bin_["delta_p2_p4_sum"])
            stats["delta_p5_p2_sum"] = int(stats["delta_p5_p2_sum"]) + int(bin_["delta_p5_p2_sum"])
        return stats

    def _update_fast_candidate(self, feature_candidate: str) -> None:
        self.feature_state_candidate = feature_candidate
        if feature_candidate == "unknown":
            self.previous_feature_candidate = "unknown"
            self.feature_candidate_count = 0
            self.fast_state_candidate = "unknown"
            return

        if feature_candidate == self.previous_feature_candidate:
            self.feature_candidate_count = min(self.feature_candidate_count + 1, FEATURE_CONFIRM_COUNT)
        else:
            self.previous_feature_candidate = feature_candidate
            self.feature_candidate_count = 1

        self.fast_state_candidate = (
            feature_candidate if self.feature_candidate_count >= FEATURE_CONFIRM_COUNT else "unknown"
        )

    def _apply_transition_guard(self, classified: str) -> str:
        if self.exposed_state in {"running_large", "running_small"}:
            running_evidence = (
                self.fast_state_candidate in {"running_large", "running_small"}
                or self.feature_state_candidate in {"running_large", "running_small"}
                or self.ratio_0hhhh >= LARGE_SIGNATURE_THRESHOLD
                or self.small_feature_score >= FEATURE_SCORE_THRESHOLD
            )

            if classified == "unknown":
                self.running_standby_windows = 0
                if self.running_unknown_windows < RUNNING_UNKNOWN_LIMIT:
                    self.running_unknown_windows += 1
                    return self.exposed_state
            elif classified == "standby":
                self.running_unknown_windows = 0
                if running_evidence:
                    self.running_standby_windows = 0
                    return self.exposed_state
                if self.running_standby_windows < RUNNING_STANDBY_LIMIT:
                    self.running_standby_windows += 1
                    return self.exposed_state
            else:
                self.running_unknown_windows = 0
                self.running_standby_windows = 0

        if self.exposed_state == "standby":
            if classified == "running_small":
                self.standby_unknown_windows = 0
                return "standby"

            if classified == "unknown":
                standby_evidence = (
                    self.standby_window_valid
                    or self.standby_feature_score >= 1
                    or self.blink_score >= 40.0
                    or self.ratio_mhmhh >= MHMHH_SIGNATURE_THRESHOLD
                )
                if standby_evidence:
                    self.standby_unknown_windows = 0
                    return "standby"
                if self.standby_unknown_windows < STANDBY_UNKNOWN_LIMIT:
                    self.standby_unknown_windows += 1
                    return "standby"

        if classified != "unknown":
            self.standby_unknown_windows = 0
            if classified not in {"running_large", "running_small"}:
                self.running_unknown_windows = 0
                self.running_standby_windows = 0
        return classified

    def _small_feature_score(self, stats: dict[str, object]) -> int:
        if int(stats["total"]) == 0:
            return 0
        return sum(
            [
                self._pin_stddev(stats, 1) <= SMALL_P2_STDDEV_MAX,
                self._delta_mean(stats["delta_p2_p4_sum"], stats["total"]) >= SMALL_DELTA_P2_P4_MIN,
                self._delta_mean(stats["delta_p5_p2_sum"], stats["total"]) <= SMALL_DELTA_P5_P2_MAX,
            ]
        )

    def _standby_feature_score(self, stats: dict[str, object]) -> int:
        if int(stats["total"]) == 0:
            return 0
        return sum(
            [
                self._pin_stddev(stats, 1) >= STANDBY_P2_STDDEV_MIN,
                self._delta_mean(stats["delta_p2_p4_sum"], stats["total"]) <= STANDBY_DELTA_P2_P4_MAX,
                self._delta_mean(stats["delta_p5_p2_sum"], stats["total"]) >= STANDBY_DELTA_P5_P2_MIN,
            ]
        )

    def _pin_stddev(self, stats: dict[str, object], pin_index: int) -> float:
        total = int(stats["total"])
        if total == 0:
            return 0.0
        raw_sum = stats["raw_sum"]
        raw_sq_sum = stats["raw_sq_sum"]
        assert isinstance(raw_sum, list)
        assert isinstance(raw_sq_sum, list)
        mean = float(raw_sum[pin_index]) / float(total)
        mean_sq = float(raw_sq_sum[pin_index]) / float(total)
        return math.sqrt(max(0.0, mean_sq - mean * mean))

    def _delta_mean(self, delta_sum: object, total: object) -> float:
        total_int = int(total)
        if total_int == 0:
            return 0.0
        return float(delta_sum) / float(total_int)

    def _standby_blink_stats(self, window_bins: int) -> dict[str, float | int | bool]:
        p1_means: list[float] = []
        capped = min(window_bins, BIN_COUNT)
        for reverse_offset in range(capped, 0, -1):
            offset = reverse_offset - 1
            index = (self.current_bin + BIN_COUNT - offset) % BIN_COUNT
            bin_ = self.bins[index]
            total = int(bin_["total"])
            if total == 0:
                continue
            raw_sum = bin_["raw_sum"]
            assert isinstance(raw_sum, list)
            p1_means.append(float(raw_sum[0]) / float(total))

        if len(p1_means) < capped:
            return {"valid": False, "score": 0.0, "amplitude": 0.0, "duty": 0.0, "transitions": 0}

        min_value = min(p1_means)
        max_value = max(p1_means)
        amplitude = max_value - min_value
        threshold = min_value + amplitude * 0.5
        high_states = [value >= threshold for value in p1_means]
        high_count = sum(high_states)
        transitions = sum(
            1 for previous, current in zip(high_states, high_states[1:]) if previous != current
        )
        duty = float(high_count) / float(len(high_states))

        valid = (
            STANDBY_P1_MIN_AMPLITUDE <= amplitude <= STANDBY_P1_MAX_AMPLITUDE
            and STANDBY_MIN_DUTY <= duty <= STANDBY_MAX_DUTY
            and transitions >= 1
        )

        amplitude_score = 0.0 if amplitude <= STANDBY_P1_MIN_AMPLITUDE else min(1.0, (amplitude - STANDBY_P1_MIN_AMPLITUDE) / 100.0)
        transition_score = min(1.0, float(transitions) / 2.0)
        if STANDBY_MIN_DUTY <= duty <= STANDBY_MAX_DUTY:
            duty_score = 1.0
        elif duty < STANDBY_MIN_DUTY:
            duty_score = max(0.0, duty / STANDBY_MIN_DUTY)
        else:
            duty_score = max(0.0, (0.70 - duty) / (0.70 - STANDBY_MAX_DUTY))

        return {
            "valid": valid,
            "score": 100.0 * amplitude_score * transition_score * duty_score,
            "amplitude": amplitude,
            "duty": duty,
            "transitions": transitions,
        }


class PanelClassifierCaptureTest(unittest.TestCase):
    def feed_capture(self, archive_path: Path, member_name: str, max_seconds: float | None = None) -> tuple[FirmwareClassifier, int]:
        classifier = FirmwareClassifier()
        rows = 0
        first_us: int | None = None
        with tarfile.open(archive_path, "r:gz") as archive:
            with archive.extractfile(member_name) as raw_file:
                self.assertIsNotNone(raw_file, member_name)
                assert raw_file is not None
                for line in io.TextIOWrapper(raw_file):
                    parsed = parse_raw_adc_line(line)
                    if parsed is None:
                        continue
                    sample_us, values = parsed
                    if first_us is None:
                        first_us = sample_us
                    if max_seconds is not None and sample_us - first_us > max_seconds * 1_000_000:
                        break
                    classifier.sample(sample_us, values)
                    rows += 1

        classifier.evaluate()
        return classifier, rows

    def feed_synthetic(
        self,
        classifier: FirmwareClassifier,
        start_us: int,
        seconds: float,
        values_fn,
    ) -> int:
        samples = int(seconds * 1000)
        for offset in range(samples):
            classifier.sample(start_us + offset * 1000, values_fn(offset))
        classifier.evaluate()
        return start_us + samples * 1000

    @staticmethod
    def synthetic_standby_sample(offset: int) -> tuple[int, int, int, int, int]:
        p2 = 4095 if offset % 10 < 7 else 1500
        return 1900, p2, 1900, 4095, 4095

    @staticmethod
    def synthetic_small_like_sample(offset: int) -> tuple[int, int, int, int, int]:
        return 1900, 4095, 1900, 4095, 4095

    @staticmethod
    def synthetic_unknown_sample(offset: int) -> tuple[int, int, int, int, int]:
        return 3000, 3000, 3000, 3000, 3000

    @staticmethod
    def synthetic_large_sample(offset: int) -> tuple[int, int, int, int, int]:
        return 0, 4095, 4095, 4095, 4095

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

    def test_dma_fixture_feature_vote_separates_standby_and_small(self) -> None:
        with DMA_FIXTURE.open(newline="") as fixture:
            rows = list(csv.DictReader(fixture))

        self.assertGreaterEqual(sum(1 for row in rows if row["label"] == "standby"), 60)
        self.assertGreaterEqual(sum(1 for row in rows if row["label"] == "small"), 60)

        for row in rows:
            with self.subTest(label=row["label"], device_ms=row["device_ms"]):
                self.assertGreaterEqual(float(row["ratio_mhmhh"]), MHMHH_SIGNATURE_THRESHOLD)
                small_score, standby_score = feature_scores_from_summary(row)
                if row["label"] == "standby":
                    self.assertGreaterEqual(standby_score, FEATURE_SCORE_THRESHOLD, row)
                    self.assertLess(small_score, FEATURE_SCORE_THRESHOLD, row)
                elif row["label"] == "small":
                    self.assertGreaterEqual(small_score, FEATURE_SCORE_THRESHOLD, row)
                    self.assertLess(standby_score, FEATURE_SCORE_THRESHOLD, row)
                else:
                    self.fail(f"Unexpected fixture label: {row['label']}")

    def test_final_state_classification_from_raw_captures(self) -> None:
        archive_path = find_capture_archive()
        if archive_path is None:
            self.skipTest(
                "Set ICE_PANEL_CAPTURE_ARCHIVE or place ice_panel_sniffer-captures-*.tar.gz next to this repo"
            )

        for label, (member_name, expected_state) in CAPTURES.items():
            with self.subTest(label=label):
                classifier, rows = self.feed_capture(archive_path, member_name)
                result = classifier.result()
                self.assertGreater(rows, 10_000, member_name)
                self.assertEqual(result["state"], expected_state, result)

                if expected_state == "running_large":
                    self.assertEqual(result["fast_candidate"], "running_large", result)
                    self.assertGreaterEqual(result["ratio_0hhhh"], LARGE_SIGNATURE_THRESHOLD, result)
                elif expected_state == "running_small":
                    self.assertEqual(result["fast_candidate"], "running_small", result)
                    self.assertGreaterEqual(result["ratio_mhmhh"], MHMHH_SIGNATURE_THRESHOLD, result)
                elif expected_state == "standby":
                    self.assertTrue(result["standby_window_valid"], result)
                    self.assertGreaterEqual(result["blink"], 40.0, result)

    def test_standby_uses_fast_feature_vote_before_slow_window(self) -> None:
        archive_path = find_capture_archive()
        if archive_path is None:
            self.skipTest(
                "Set ICE_PANEL_CAPTURE_ARCHIVE or place ice_panel_sniffer-captures-*.tar.gz next to this repo"
            )

        member_name, _ = CAPTURES["standby"]
        classifier, rows = self.feed_capture(archive_path, member_name, max_seconds=8.0)
        self.assertGreater(rows, 1000)
        result = classifier.result()
        self.assertEqual(result["feature_candidate"], "standby", result)
        self.assertEqual(result["fast_candidate"], "standby", result)
        self.assertNotEqual(result["state"], "running_small", result)
        self.assertFalse(result["standby_window_valid"], result)
        self.assertGreaterEqual(result["standby_feature_score"], FEATURE_SCORE_THRESHOLD, result)

    def test_standby_latch_blocks_small_like_short_window(self) -> None:
        classifier = FirmwareClassifier()
        t = self.feed_synthetic(classifier, 0, 3.0, self.synthetic_standby_sample)
        self.assertEqual(classifier.result()["state"], "standby", classifier.result())

        self.feed_synthetic(classifier, t, 3.0, self.synthetic_small_like_sample)
        result = classifier.result()
        self.assertEqual(result["feature_candidate"], "running_small", result)
        self.assertEqual(result["fast_candidate"], "running_small", result)
        self.assertEqual(result["classifier"], "standby", result)
        self.assertEqual(result["state"], "standby", result)

    def test_standby_latch_blocks_short_unknown_window(self) -> None:
        classifier = FirmwareClassifier()
        t = self.feed_synthetic(classifier, 0, 3.0, self.synthetic_standby_sample)
        self.assertEqual(classifier.result()["state"], "standby", classifier.result())

        self.feed_synthetic(classifier, t, 3.0, self.synthetic_unknown_sample)
        result = classifier.result()
        self.assertEqual(result["feature_candidate"], "unknown", result)
        self.assertEqual(result["fast_candidate"], "unknown", result)
        self.assertEqual(result["classifier"], "standby", result)
        self.assertEqual(result["state"], "standby", result)

    def test_running_latch_blocks_short_unknown_window(self) -> None:
        classifier = FirmwareClassifier()
        t = self.feed_synthetic(classifier, 0, 3.0, self.synthetic_large_sample)
        self.assertEqual(classifier.result()["state"], "running_large", classifier.result())

        self.feed_synthetic(classifier, t, 3.0, self.synthetic_unknown_sample)
        result = classifier.result()
        self.assertEqual(result["feature_candidate"], "unknown", result)
        self.assertEqual(result["fast_candidate"], "unknown", result)
        self.assertEqual(result["classifier"], "running_large", result)
        self.assertEqual(result["state"], "running_large", result)

    def test_running_latch_blocks_slow_standby_override_when_small_evidence_exists(self) -> None:
        classifier = FirmwareClassifier()
        t = self.feed_synthetic(classifier, 0, 3.0, self.synthetic_small_like_sample)
        self.assertEqual(classifier.result()["state"], "running_small", classifier.result())

        # A slow standby detector may still be valid briefly from stale data, but
        # the current short window is clearly small-ice. Running state must win.
        classifier.standby_window_valid = True
        classified = classifier._apply_transition_guard("standby")
        self.assertEqual(classified, "running_small", classifier.result())

        self.feed_synthetic(classifier, t, 3.0, self.synthetic_small_like_sample)
        result = classifier.result()
        self.assertEqual(result["feature_candidate"], "running_small", result)
        self.assertEqual(result["fast_candidate"], "running_small", result)
        self.assertEqual(result["state"], "running_small", result)

    def test_stable_states_are_classified_from_short_windows(self) -> None:
        archive_path = find_capture_archive()
        if archive_path is None:
            self.skipTest(
                "Set ICE_PANEL_CAPTURE_ARCHIVE or place ice_panel_sniffer-captures-*.tar.gz next to this repo"
            )

        expectations = {
            "standby": "standby",
            "large": "running_large",
        }
        for label, expected_state in expectations.items():
            with self.subTest(label=label):
                member_name, _ = CAPTURES[label]
                classifier, rows = self.feed_capture(archive_path, member_name, max_seconds=3.0)
                result = classifier.result()
                self.assertGreater(rows, 1000)
                self.assertEqual(result["state"], expected_state, result)
                self.assertEqual(result["fast_candidate"], expected_state, result)

    def test_fast_candidate_uses_two_second_running_window(self) -> None:
        archive_path = find_capture_archive()
        if archive_path is None:
            self.skipTest(
                "Set ICE_PANEL_CAPTURE_ARCHIVE or place ice_panel_sniffer-captures-*.tar.gz next to this repo"
            )

        expectations = {
            "large": "running_large",
            "large_to_small": "running_small",
            "small_to_large": "running_large",
            "sim_large_to_small": "running_small",
            "sim_small_to_large": "running_large",
        }
        for label, expected_candidate in expectations.items():
            with self.subTest(label=label):
                member_name, _ = CAPTURES[label]
                classifier, _ = self.feed_capture(archive_path, member_name)
                self.assertEqual(classifier.result()["fast_candidate"], expected_candidate, classifier.result())


if __name__ == "__main__":
    unittest.main()
