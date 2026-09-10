from __future__ import annotations

import re
import unittest
from itertools import combinations
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BENCH = ROOT / "simulator" / "index.html"
EMBEDDED = ROOT / "firmware" / "components" / "emote_state" / "emote_state.c"

FIELDS = (
    "open",
    "lidL",
    "gx",
    "gy",
    "pupil",
    "browY",
    "browRot",
    "asym",
    "arc",
    "intensity",
    "pupilShape",
    "hue",
    "effect",
)
CORE = ("neutral", "thinking", "happy", "surprised", "suspicious", "error")
GEOMETRY_FIELDS = ("open", "lidL", "gx", "gy", "pupil", "browY", "browRot", "asym", "arc")
SILHOUETTE_FIELDS = ("open", "lidL", "browY", "browRot", "asym", "arc")


def parse_number(value: str) -> float:
    return float(value.strip().removesuffix("f"))


def browser_poses(source: str) -> dict[str, tuple[float, ...]]:
    block = source.split("var STATES={", 1)[1].split("\n};", 1)[0]
    poses: dict[str, tuple[float, ...]] = {}
    for name, body in re.findall(r"^\s*([a-z]+):\s*\{([^}]+)\}", block, re.MULTILINE):
        values = []
        for field in FIELDS:
            match = re.search(rf"\b{field}\s*:\s*(-?\d+(?:\.\d+)?)", body)
            if not match:
                raise AssertionError(f"browser pose {name!r} is missing {field!r}")
            values.append(parse_number(match.group(1)))
        poses[name] = tuple(values)
    return poses


def embedded_poses(source: str) -> dict[str, tuple[float, ...]]:
    poses: dict[str, tuple[float, ...]] = {}
    pattern = re.compile(
        r'\[EMOTE_([A-Z_]+)\]\s*=\s*\{"([a-z]+)",\s*POSE\(([^)]+)\)\}'
    )
    for enum_name, name, body in pattern.findall(source):
        values = tuple(parse_number(value) for value in body.split(","))
        if len(values) != len(FIELDS):
            raise AssertionError(
                f"embedded pose {enum_name!r} has {len(values)} values, expected {len(FIELDS)}"
            )
        poses[name] = values
    return poses


class PoseParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.bench_source = BENCH.read_text(encoding="utf-8")
        cls.browser = browser_poses(cls.bench_source)
        cls.embedded = embedded_poses(EMBEDDED.read_text(encoding="utf-8"))

    def test_all_browser_and_embedded_pose_values_match(self) -> None:
        self.assertEqual(set(self.browser), set(self.embedded))
        for name in sorted(self.browser):
            with self.subTest(affect=name):
                self.assertEqual(self.browser[name], self.embedded[name])

    def test_core_six_matches_the_rubric(self) -> None:
        match = re.search(r"var CORE=\[([^]]+)\]", self.bench_source)
        self.assertIsNotNone(match)
        actual = tuple(re.findall(r"'([a-z]+)'", match.group(1)))
        self.assertEqual(CORE, actual)

    def test_core_six_preserve_geometric_separation(self) -> None:
        """Keep the accepted R2 geometry from collapsing before the next blind read."""
        geometry_indexes = tuple(FIELDS.index(field) for field in GEOMETRY_FIELDS)
        silhouette_indexes = tuple(FIELDS.index(field) for field in SILHOUETTE_FIELDS)

        for left_name, right_name in combinations(CORE, 2):
            left = self.embedded[left_name]
            right = self.embedded[right_name]
            geometry_deltas = tuple(abs(left[index] - right[index]) for index in geometry_indexes)
            silhouette_deltas = tuple(abs(left[index] - right[index]) for index in silhouette_indexes)
            pair = f"{left_name}/{right_name}"
            with self.subTest(pair=pair):
                self.assertGreaterEqual(
                    sum(delta >= 0.08 for delta in geometry_deltas),
                    2,
                    f"{pair} collapsed across the core geometry channels",
                )
                self.assertGreaterEqual(
                    max(silhouette_deltas),
                    0.10,
                    f"{pair} has no strong silhouette-driving difference",
                )


if __name__ == "__main__":
    unittest.main()
