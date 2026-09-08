from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BENCH = ROOT / "simulator" / "index.html"
EMBEDDED = ROOT / "firmware" / "components" / "emote_motion" / "emote_motion.c"


def browser_blink_profile(source: str) -> dict[str, float]:
    match = re.search(r"var BLINK_PROFILE=\{([^}]+)\}", source)
    if not match:
        raise AssertionError("Expression Bench has no BLINK_PROFILE")
    return {
        name: float(value)
        for name, value in re.findall(r"([A-Za-z]+):\s*(\d+(?:\.\d+)?)", match.group(1))
    }


def embedded_blink_profile(source: str) -> dict[str, float]:
    names = {
        "anticipationMs": "BLINK_ANTICIPATION_MS",
        "anticipationOpen": "BLINK_ANTICIPATION_OPEN",
        "closeMs": "BLINK_CLOSE_MS",
        "holdMs": "BLINK_HOLD_MS",
        "openMs": "BLINK_OPEN_MS",
        "rightLagMs": "BLINK_RIGHT_LAG_MS",
        "rightScaleMin": "BLINK_RIGHT_SCALE_MIN",
        "rightScaleMax": "BLINK_RIGHT_SCALE_MAX",
    }
    result: dict[str, float] = {}
    for public_name, macro_name in names.items():
        match = re.search(rf"^#define {macro_name}\s+(\d+(?:\.\d+)?)[uf]$", source, re.MULTILINE)
        if not match:
            raise AssertionError(f"embedded motion is missing {macro_name}")
        result[public_name] = float(match.group(1))
    return result


def browser_gaze_arc_profile(source: str) -> dict[str, float]:
    match = re.search(r"var GAZE_ARC_PROFILE=\{([^}]+)\}", source)
    if not match:
        raise AssertionError("Expression Bench has no GAZE_ARC_PROFILE")
    return {
        name: float(value)
        for name, value in re.findall(r"([A-Za-z]+):\s*(\d+(?:\.\d+)?)", match.group(1))
    }


def embedded_gaze_arc_profile(source: str) -> dict[str, float]:
    names = {
        "crossX": "GAZE_ARC_CROSS_X_PX",
        "crossY": "GAZE_ARC_CROSS_Y_PX",
        "maxPx": "GAZE_ARC_LIMIT_PX",
    }
    result: dict[str, float] = {}
    for public_name, macro_name in names.items():
        match = re.search(rf"^#define {macro_name}\s+(\d+(?:\.\d+)?)f$", source, re.MULTILINE)
        if not match:
            raise AssertionError(f"embedded motion is missing {macro_name}")
        result[public_name] = float(match.group(1))
    return result


def browser_eye_drift_profile(source: str) -> dict[str, float]:
    match = re.search(r"var EYE_DRIFT_PROFILE=\{([^}]+)\}", source)
    if not match:
        raise AssertionError("Expression Bench has no EYE_DRIFT_PROFILE")
    return {
        name: float(value)
        for name, value in re.findall(r"([A-Za-z]+):\s*(\d+(?:\.\d+)?)", match.group(1))
    }


def embedded_eye_drift_profile(source: str) -> dict[str, float]:
    names = {
        "speed": "EYE_DRIFT_SPEED",
        "amplitude": "EYE_DRIFT_AMPLITUDE",
        "yScale": "EYE_DRIFT_Y_SCALE",
        "leftPhase": "EYE_DRIFT_LEFT_PHASE",
        "rightPhase": "EYE_DRIFT_RIGHT_PHASE",
    }
    result: dict[str, float] = {}
    for public_name, macro_name in names.items():
        match = re.search(rf"^#define {macro_name}\s+(\d+(?:\.\d+)?)f$", source, re.MULTILINE)
        if not match:
            raise AssertionError(f"embedded motion is missing {macro_name}")
        result[public_name] = float(match.group(1))
    return result


def browser_attention_decay_profile(source: str) -> dict[str, float]:
    match = re.search(r"var ATTENTION_DECAY_PROFILE=\{([^}]+)\}", source)
    if not match:
        raise AssertionError("Expression Bench has no ATTENTION_DECAY_PROFILE")
    return {
        name: float(value)
        for name, value in re.findall(r"([A-Za-z]+):\s*(\d+(?:\.\d+)?)", match.group(1))
    }


def embedded_attention_decay_profile(source: str) -> dict[str, float]:
    names = {
        "startMs": "ATTENTION_DECAY_START_MS",
        "durationMs": "ATTENTION_DECAY_DURATION_MS",
        "openDrop": "ATTENTION_DECAY_OPEN_DROP",
        "lowerLidRise": "ATTENTION_DECAY_LOWER_LID_RISE",
        "pupilDrop": "ATTENTION_DECAY_PUPIL_DROP",
        "gazeDown": "ATTENTION_DECAY_GAZE_DOWN",
        "browDrop": "ATTENTION_DECAY_BROW_DROP",
        "wanderScale": "ATTENTION_DECAY_WANDER_SCALE",
        "saccadeSlow": "ATTENTION_DECAY_SACCADE_SLOW",
        "blinkSlow": "ATTENTION_DECAY_BLINK_SLOW",
    }
    result: dict[str, float] = {}
    for public_name, macro_name in names.items():
        match = re.search(rf"^#define {macro_name}\s+(\d+(?:\.\d+)?)[uf]$", source, re.MULTILINE)
        if not match:
            raise AssertionError(f"embedded motion is missing {macro_name}")
        result[public_name] = float(match.group(1))
    return result


class MotionParityTests(unittest.TestCase):
    def test_browser_and_embedded_blink_profiles_match_spec(self) -> None:
        expected = {
            "anticipationMs": 34.0,
            "anticipationOpen": 0.04,
            "closeMs": 70.0,
            "holdMs": 30.0,
            "openMs": 145.0,
            "rightLagMs": 0.0,
            "rightScaleMin": 0.97,
            "rightScaleMax": 1.0,
        }
        browser = browser_blink_profile(BENCH.read_text(encoding="utf-8"))
        embedded = embedded_blink_profile(EMBEDDED.read_text(encoding="utf-8"))
        self.assertEqual(expected, browser)
        self.assertEqual(expected, embedded)

    def test_browser_and_embedded_gaze_arc_profiles_match(self) -> None:
        expected = {"crossX": 1.5, "crossY": 1.1, "maxPx": 4.0}
        browser = browser_gaze_arc_profile(BENCH.read_text(encoding="utf-8"))
        embedded = embedded_gaze_arc_profile(EMBEDDED.read_text(encoding="utf-8"))
        self.assertEqual(expected, browser)
        self.assertEqual(expected, embedded)

    def test_browser_and_embedded_eye_drift_profiles_match(self) -> None:
        expected = {
            "speed": 0.83,
            "amplitude": 0.006,
            "yScale": 0.55,
            "leftPhase": 0.7,
            "rightPhase": 2.1,
        }
        browser = browser_eye_drift_profile(BENCH.read_text(encoding="utf-8"))
        embedded = embedded_eye_drift_profile(EMBEDDED.read_text(encoding="utf-8"))
        self.assertEqual(expected, browser)
        self.assertEqual(expected, embedded)

    def test_browser_and_embedded_attention_decay_profiles_match(self) -> None:
        expected = {
            "startMs": 45000.0,
            "durationMs": 90000.0,
            "openDrop": 0.10,
            "lowerLidRise": 0.04,
            "pupilDrop": 0.03,
            "gazeDown": 0.05,
            "browDrop": 0.03,
            "wanderScale": 1.45,
            "saccadeSlow": 1.60,
            "blinkSlow": 1.35,
        }
        browser = browser_attention_decay_profile(BENCH.read_text(encoding="utf-8"))
        embedded = embedded_attention_decay_profile(EMBEDDED.read_text(encoding="utf-8"))
        self.assertEqual(expected, browser)
        self.assertEqual(expected, embedded)


if __name__ == "__main__":
    unittest.main()
