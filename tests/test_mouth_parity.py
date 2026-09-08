from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIMULATOR = ROOT / "simulator" / "index.html"
FIRMWARE = ROOT / "firmware" / "amoled-mouth" / "src" / "main.cpp"
SCHEMA = ROOT / "schema" / "emote-frame.schema.json"


def canonical_affects() -> set[str]:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    return set(schema["$defs"]["affectState"]["enum"])


class MouthParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.simulator = SIMULATOR.read_text(encoding="utf-8")
        cls.firmware = FIRMWARE.read_text(encoding="utf-8")
        cls.affects = canonical_affects()

    def test_simulator_authors_every_canonical_affect(self) -> None:
        block = self.simulator.split("var MOUTH_POSES={", 1)[1].split("\n};", 1)[0]
        authored = set(re.findall(r"^\s+([a-z_]+):\{", block, re.MULTILINE))
        self.assertEqual(self.affects, authored)

    def test_firmware_maps_every_non_neutral_affect(self) -> None:
        block = self.firmware.split("MouthShape shapeForAffect", 1)[1].split(
            "void setAffect", 1
        )[0]
        mapped = set(re.findall(r'equalsIgnoreCase\(name, "([a-z_]+)"\)', block))
        self.assertEqual(self.affects - {"neutral"}, mapped)

    def test_firmware_keeps_rich_motion_local_and_semantic(self) -> None:
        for marker in (
            "Decoration::THOUGHT",
            "Decoration::SPARKLE",
            "Decoration::CHECK",
            "Decoration::HEART",
            "Decoration::BLUSH",
            "Decoration::SWEAT",
            "Decoration::ALERT",
            "visemeOpen",
            "shapeChangedAtMs",
            "animation=expressive-v2",
        ):
            self.assertIn(marker, self.firmware)
        self.assertNotRegex(self.firmware, r'Serial\.println\("\s*(PIXEL|LINE|ELLIPSE)')


if __name__ == "__main__":
    unittest.main()
