from __future__ import annotations

import json
import unittest
from copy import deepcopy
from pathlib import Path

from jsonschema import Draft202012Validator, ValidationError


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = ROOT / "schema"
EXAMPLE_DIR = ROOT / "protocol" / "examples"


def load_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


class ProtocolSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.frame_schema = load_json(SCHEMA_DIR / "emote-frame.schema.json")
        cls.capability_schema = load_json(SCHEMA_DIR / "emote-capabilities.schema.json")
        cls.event_schema = load_json(SCHEMA_DIR / "emote-event.schema.json")
        cls.profile_schema = load_json(SCHEMA_DIR / "youandeye-profile.schema.json")
        cls.performance_schema = load_json(SCHEMA_DIR / "youandeye-performance.schema.json")
        cls.frame = load_json(EXAMPLE_DIR / "frame-thinking.json")
        cls.baseline = load_json(EXAMPLE_DIR / "frame-neutral-baseline.json")
        cls.capabilities = load_json(EXAMPLE_DIR / "capabilities-concept-rig.json")
        cls.event = load_json(EXAMPLE_DIR / "event-button.json")
        cls.profile = load_json(EXAMPLE_DIR / "profile-example.json")
        cls.performance = load_json(EXAMPLE_DIR / "performance-example.json")

    def test_schemas_are_valid_draft_2020_12(self) -> None:
        for schema in (
            self.frame_schema,
            self.capability_schema,
            self.event_schema,
            self.profile_schema,
            self.performance_schema,
        ):
            Draft202012Validator.check_schema(schema)

    def test_profile_and_performance_examples_validate(self) -> None:
        profile = {
            "protocol": "youandeye.profile/1",
            "kind": "profile",
            "agent_id": "agent.alpha",
            "revision": 1,
            "lifecycle": "active",
            "appearance": {
                "iris_palette": "teal",
                "accent": "mint",
                "mouth_style": "expressive",
            },
            "temperament": {
                "default_energy": 0.5,
                "blink_style": "natural",
                "gaze_style": "attentive",
                "idle_temperament": "curious",
            },
            "signature": {
                "affect": "success",
                "message": "GOT IT",
                "text_mode": "static",
            },
            "created_at": "2026-09-07T20:00:00.000Z",
            "updated_at": "2026-09-07T20:00:00.000Z",
        }
        performance = {
            "protocol": "youandeye.performance/1",
            "kind": "performance",
            "performance_id": "scene-1",
            "source": {"id": "agent.alpha", "session": "session-1"},
            "beats": [
                {
                    "affect": "thinking",
                    "caption": "ONE MOMENT",
                    "caption_mode": "scroll",
                    "pace": "normal",
                    "modifiers": {
                        "warmth": 0.4,
                        "confidence": 0.7,
                        "urgency": 0.2,
                        "gaze_aversion": "brief",
                    },
                }
            ],
            "return_policy": "profile_neutral",
            "interrupt_policy": "replace",
        }
        Draft202012Validator(self.profile_schema).validate(profile)
        Draft202012Validator(self.performance_schema).validate(performance)

    def test_checked_in_examples_validate(self) -> None:
        Draft202012Validator(self.frame_schema).validate(self.frame)
        Draft202012Validator(self.frame_schema).validate(self.baseline)
        Draft202012Validator(self.capability_schema).validate(self.capabilities)
        Draft202012Validator(self.event_schema).validate(self.event)
        Draft202012Validator(self.profile_schema).validate(self.profile)
        Draft202012Validator(self.performance_schema).validate(self.performance)

    def test_frame_rejects_unknown_affect(self) -> None:
        invalid = deepcopy(self.frame)
        invalid["affect"]["state"] = "confused-ish"
        with self.assertRaises(ValidationError):
            Draft202012Validator(self.frame_schema).validate(invalid)

    def test_nuanced_affects_are_part_of_the_public_contract(self) -> None:
        nuanced = {
            "curious",
            "uncertain",
            "concerned",
            "delighted",
            "embarrassed",
            "reassuring",
        }
        advertised = set(self.capabilities["affects"])
        self.assertTrue(nuanced <= advertised)
        for affect in nuanced:
            frame = deepcopy(self.frame)
            frame["affect"]["state"] = affect
            Draft202012Validator(self.frame_schema).validate(frame)

    def test_concept_sheet_affects_are_part_of_the_public_contract(self) -> None:
        concept_affects = {
            "shocked",
            "weary",
            "confused",
            "blushing",
            "nervous",
            "maniacal",
            "stressed",
            "determined",
            "bored",
            "panicked",
            "scheming",
            "fatigued",
            "content",
            "pleading",
            "sick",
            "hyped",
            "baffled",
        }
        advertised = set(self.capabilities["affects"])
        self.assertTrue(concept_affects <= advertised)
        for affect in concept_affects:
            frame = deepcopy(self.frame)
            frame["affect"]["state"] = affect
            Draft202012Validator(self.frame_schema).validate(frame)

    def test_frame_rejects_out_of_range_gaze(self) -> None:
        invalid = deepcopy(self.frame)
        invalid["gaze"] = {"target": "point", "x": 1.1, "y": 0}
        with self.assertRaises(ValidationError):
            Draft202012Validator(self.frame_schema).validate(invalid)

    def test_frame_rejects_scroll_without_text(self) -> None:
        invalid = deepcopy(self.frame)
        invalid["utterance"] = {"mode": "scroll", "sound": "processing"}
        with self.assertRaises(ValidationError):
            Draft202012Validator(self.frame_schema).validate(invalid)

    def test_frame_rejects_unknown_top_level_fields(self) -> None:
        invalid = deepcopy(self.frame)
        invalid["pixels"] = [[0, 1], [1, 0]]
        with self.assertRaises(ValidationError):
            Draft202012Validator(self.frame_schema).validate(invalid)

    def test_capabilities_allow_a_non_face_surface(self) -> None:
        terminal = deepcopy(self.capabilities)
        terminal["surface"]["id"] = "terminal-01"
        terminal["surface"]["renderer"] = "plain-terminal"
        terminal["channels"] = [
            {
                "id": "terminal",
                "kind": "terminal",
                "count": 1,
                "color_model": "symbolic",
                "max_fps": 10,
                "features": ["text", "icons"],
            }
        ]
        Draft202012Validator(self.capability_schema).validate(terminal)

if __name__ == "__main__":
    unittest.main()
