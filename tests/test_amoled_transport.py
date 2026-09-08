from __future__ import annotations

import json
import unittest
from copy import deepcopy
from pathlib import Path

from host.youandeye.amoled_transport import (
    AMOLED_TEXT_MAX_CHARS,
    commands_for_amoled,
)

ROOT = Path(__file__).resolve().parents[1]


class AmoledTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.frame = json.loads(
            (ROOT / "protocol" / "examples" / "frame-thinking.json").read_text(
                encoding="utf-8"
            )
        )

    def test_frame_maps_to_bounded_semantic_commands(self) -> None:
        commands = commands_for_amoled(self.frame)
        self.assertEqual("PROFILE EXPRESSIVE BLUE 0.42", commands[0])
        self.assertEqual("AFFECT THINKING 0.70 0.50 0.50 0.30", commands[1])
        self.assertEqual("SCROLL PLEASE WAIT...", commands[2])
        self.assertFalse(any("pixel" in command.casefold() for command in commands))

    def test_profile_and_modifiers_are_preserved_without_coordinates(self) -> None:
        frame = deepcopy(self.frame)
        frame["extensions"] = {
            "youandeye": {
                "profile": {
                    "agent_id": "agent.alpha",
                    "revision": 3,
                    "accent": "mint",
                    "mouth_style": "minimal",
                    "default_energy": 0.42,
                },
                "modifiers": {
                    "warmth": 0.81,
                    "confidence": 0.64,
                    "urgency": 0.22,
                },
            }
        }
        commands = commands_for_amoled(frame)
        self.assertEqual("PROFILE MINIMAL MINT 0.42", commands[0])
        self.assertEqual("AFFECT THINKING 0.70 0.81 0.64 0.22", commands[1])
        self.assertFalse(
            any(" x=" in command or " y=" in command for command in commands)
        )

    def test_sequence_is_owned_by_the_amoled_surface(self) -> None:
        frame = deepcopy(self.frame)
        frame["sequence"] = "celebrate"
        frame["utterance"] = {"mode": "none", "sound": "celebrate"}
        self.assertEqual("BEAT SUCCESS", commands_for_amoled(frame)[-1])

    def test_text_is_single_line_and_bounded(self) -> None:
        frame = deepcopy(self.frame)
        frame["utterance"] = {"mode": "static", "text": "hello\n" + "x" * 100}
        command = commands_for_amoled(frame)[-1]
        self.assertEqual(AMOLED_TEXT_MAX_CHARS, len(command.removeprefix("TEXT ")))


if __name__ == "__main__":
    unittest.main()
