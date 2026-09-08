from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from host.youandeye.face_service import ExpressionService
from host.youandeye.performance import beat_duration_ms, build_performance
from host.youandeye.profile_store import (
    MemoryProfileStore,
    ProfileError,
    ProfileStore,
    build_profile,
)


class RecordingDevice:
    def __init__(self) -> None:
        self.batches: list[list[str]] = []
        self.closed = False

    def send_commands(self, commands: list[str]) -> dict:
        batch = list(commands)
        self.batches.append(batch)
        return {
            "connected": True,
            "commands": batch,
            "acknowledgements": [f"OK {command}" for command in batch],
        }

    def status(self) -> dict:
        return {"connected": True, "runtime": {}, "mouth": {}}

    def describe(self) -> dict:
        return {"connected": True}

    def close(self) -> None:
        self.closed = True


class ScrollFeedbackDevice(RecordingDevice):
    def __init__(self) -> None:
        super().__init__()
        self.status_calls = 0

    def status(self) -> dict:
        self.status_calls += 1
        return {
            "connected": True,
            "runtime": {},
            "mouth": {"scrolling": 0, "scrollComplete": 1},
        }


class ProfileStoreTests(unittest.TestCase):
    def test_sqlite_store_persists_by_stable_agent_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profiles.sqlite3"
            first = ProfileStore(path)
            profile = build_profile(
                "agent.alpha",
                changes={"appearance": {"iris_palette": "violet"}},
            )
            first.put(profile)
            active = dict(profile)
            active["lifecycle"] = "active"
            first.put(active)

            reopened = ProfileStore(path)
            self.assertEqual("violet", reopened.get("agent.alpha")["appearance"]["iris_palette"])
            self.assertEqual("active", reopened.get_active("agent.alpha")["lifecycle"])
            self.assertIsNone(reopened.get("agent.beta"))

    def test_profile_rejects_unbounded_colors_and_invalid_identity(self) -> None:
        with self.assertRaisesRegex(Exception, "not one of"):
            build_profile(
                "agent.alpha",
                changes={"appearance": {"iris_palette": "#ffffff"}},
            )
        with self.assertRaises(ProfileError):
            build_profile("agent with spaces")


class IdentityLifecycleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.store = MemoryProfileStore()
        self.device = RecordingDevice()
        self.service = ExpressionService(
            self.device,  # type: ignore[arg-type]
            source_id="agent.alpha",
            agent_id="agent.alpha",
            profile_store=self.store,
            session_id="profile-test",
            schedule_expiry=False,
            performance_time_scale=0,
        )

    def tearDown(self) -> None:
        self.service.close()

    def test_first_run_safe_default_then_full_approval_lifecycle(self) -> None:
        self.assertEqual("profile_required", self.service.profile_status()["status"])
        legacy = self.service.express(affect="neutral")
        self.assertTrue(legacy["ok"])
        self.assertFalse(any(command.startswith("IRIS ") for command in self.device.batches[-1]))

        created = self.service.configure_profile(
            "create",
            changes={
                "appearance": {
                    "iris_palette": "violet",
                    "accent": "mint",
                    "mouth_style": "minimal",
                },
                "temperament": {
                    "default_energy": 0.42,
                    "blink_style": "gentle",
                    "gaze_style": "soft",
                    "idle_temperament": "calm",
                },
                "signature": {
                    "affect": "reassuring",
                    "message": "WITH YOU",
                    "text_mode": "static",
                },
            },
        )
        self.assertEqual("draft", created["status"])
        with self.assertRaisesRegex(ProfileError, "preview"):
            self.service.configure_profile("approve")

        previewed = self.service.configure_profile("preview")
        self.assertEqual("previewed", previewed["status"])
        self.assertEqual("completed", previewed["preview"]["status"])
        self.assertTrue(
            any(
                any(command.startswith("EMOTE LISTENING") for command in batch)
                for batch in self.device.batches
            )
        )

        approved = self.service.configure_profile("approve")
        self.assertEqual("approved", approved["status"])
        activated = self.service.configure_profile("activate")
        self.assertEqual("active", activated["status"])
        self.assertEqual("completed", activated["acknowledgement"]["status"])
        self.assertTrue(
            any("IRIS 145 108 224" in batch for batch in self.device.batches),
            self.device.batches,
        )

        resumed_device = RecordingDevice()
        resumed = ExpressionService(
            resumed_device,  # type: ignore[arg-type]
            source_id="agent.alpha",
            agent_id="agent.alpha",
            profile_store=self.store,
            session_id="later-session",
            schedule_expiry=False,
            performance_time_scale=0,
        )
        try:
            self.assertEqual("active", resumed.profile_status()["status"])
            receipt = resumed.express(affect="happy")
            self.assertEqual("violet", receipt["frame"]["extensions"]["youandeye"]["profile"]["iris_palette"])
            self.assertEqual(
                "PROFILE GENTLE SOFT CALM MINIMAL 0.42",
                resumed_device.batches[-1][0],
            )
            self.assertEqual("IRIS 145 108 224", resumed_device.batches[-1][1])
        finally:
            resumed.close()

    def test_update_requires_new_preview_and_reset_is_agent_scoped(self) -> None:
        self.service.configure_profile("create")
        self.service.configure_profile("preview")
        self.service.configure_profile("approve")
        self.service.configure_profile("activate")

        updated = self.service.configure_profile(
            "update", changes={"appearance": {"iris_palette": "teal"}}
        )
        self.assertEqual("draft", updated["status"])
        self.assertFalse(updated["using_safe_default"])
        self.assertEqual(
            "azure", updated["active_profile"]["appearance"]["iris_palette"]
        )
        reset = self.service.configure_profile("reset")
        self.assertTrue(reset["reset"])
        self.assertEqual("profile_required", reset["status"])

    def test_two_agents_select_distinct_profiles_on_one_surface_contract(self) -> None:
        alpha = build_profile(
            "agent.alpha",
            changes={"appearance": {"iris_palette": "teal"}},
            lifecycle="active",
        )
        beta = build_profile(
            "agent.beta",
            changes={"appearance": {"iris_palette": "amber"}},
            lifecycle="active",
        )
        self.store.put(alpha)
        self.store.put(beta)

        alpha_device = RecordingDevice()
        beta_device = RecordingDevice()
        alpha_service = ExpressionService(
            alpha_device,  # type: ignore[arg-type]
            source_id="shared-source",
            agent_id="agent.alpha",
            profile_store=self.store,
            schedule_expiry=False,
        )
        beta_service = ExpressionService(
            beta_device,  # type: ignore[arg-type]
            source_id="shared-source",
            agent_id="agent.beta",
            profile_store=self.store,
            schedule_expiry=False,
        )
        try:
            alpha_service.express(affect="neutral")
            beta_service.express(affect="neutral")
            self.assertEqual("IRIS 54 190 184", alpha_device.batches[-1][1])
            self.assertEqual("IRIS 232 165 62", beta_device.batches[-1][1])
        finally:
            alpha_service.close()
            beta_service.close()


class PerformanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.device = RecordingDevice()
        self.service = ExpressionService(
            self.device,  # type: ignore[arg-type]
            source_id="agent.scene",
            agent_id="agent.scene",
            session_id="scene-test",
            schedule_expiry=False,
            performance_time_scale=0,
        )

    def tearDown(self) -> None:
        self.service.close()

    def test_scene_completes_without_client_sleep_and_restores_neutral(self) -> None:
        result = self.service.start_performance(
            [
                {"affect": "listening", "pace": "brief"},
                {
                    "affect": "thinking",
                    "caption": "CONSIDERING...",
                    "caption_mode": "auto",
                    "pace": "normal",
                    "modifiers": {
                        "warmth": 0.45,
                        "confidence": 0.4,
                        "urgency": 0.2,
                        "gaze_aversion": "brief",
                    },
                },
                {"affect": "success", "pace": "brief"},
            ],
            title="Short story",
        )
        self.assertEqual("completed", result["status"])
        self.assertEqual("neutral", result["current_affect"])
        self.assertEqual(["EMOTE NEUTRAL 0.45", "MOUTH AUTO"], self.device.batches[-1])
        self.assertTrue(any("SCROLL CONSIDERING..." in batch for batch in self.device.batches))

    def test_neutral_cancels_an_async_scene_and_wins_the_race(self) -> None:
        self.service._performance_time_scale = 1
        started = self.service.start_performance(
            [{"affect": "thinking", "pace": "lingering"}],
            wait_for_completion=False,
        )
        self.assertIn(started["status"], {"starting", "running"})
        restored = self.service.neutral()
        self.assertTrue(restored["performance_cancelled"])
        self.assertEqual(["EMOTE NEUTRAL 0.45", "MOUTH AUTO"], self.device.batches[-1])
        self.assertEqual("cancelled", self.service.performance_status()["status"])

    def test_performance_contract_rejects_pixels_and_manual_coordinates(self) -> None:
        with self.assertRaises(Exception):
            build_performance(
                "agent.scene",
                "scene-test",
                [{"affect": "happy", "pace": "brief", "pixels": [[1]]}],
            )
        with self.assertRaises(Exception):
            build_performance(
                "agent.scene",
                "scene-test",
                [{"affect": "happy", "pace": "brief", "x": 0.5}],
            )

    def test_scroll_pacing_covers_the_caption_before_advancing(self) -> None:
        short = beat_duration_ms({"affect": "thinking", "pace": "brief"})
        scrolling = beat_duration_ms(
            {
                "affect": "thinking",
                "pace": "brief",
                "caption": "PLEASE WAIT...",
                "caption_mode": "scroll",
            }
        )
        self.assertGreater(scrolling, short)
        self.assertEqual(15_540, scrolling)

    def test_scroll_uses_surface_completion_feedback_instead_of_full_fallback(self) -> None:
        device = ScrollFeedbackDevice()
        service = ExpressionService(
            device,  # type: ignore[arg-type]
            source_id="agent.feedback",
            schedule_expiry=False,
            performance_time_scale=0.1,
        )
        try:
            result = service.start_performance(
                [
                    {
                        "affect": "thinking",
                        "caption": "PLEASE WAIT...",
                        "caption_mode": "scroll",
                        "pace": "glance",
                    }
                ]
            )
            self.assertEqual("completed", result["status"])
            self.assertGreaterEqual(device.status_calls, 1)
        finally:
            service.close()

    def test_timeout_reports_timeout_and_restores_neutral(self) -> None:
        self.service._performance_time_scale = 1
        result = self.service.start_performance(
            [{"affect": "thinking", "pace": "lingering"}],
            wait_timeout_ms=1,
        )
        self.assertFalse(result["ok"])
        self.assertEqual("timed_out", result["status"])
        self.assertEqual(["EMOTE NEUTRAL 0.45", "MOUTH AUTO"], self.device.batches[-1])

    def test_scene_can_reject_or_replace_an_active_scene(self) -> None:
        self.service._performance_time_scale = 1
        first = self.service.start_performance(
            [{"affect": "thinking", "pace": "lingering"}],
            wait_for_completion=False,
        )
        with self.assertRaisesRegex(Exception, "already running"):
            self.service.start_performance(
                [{"affect": "happy", "pace": "brief"}],
                interrupt_policy="reject",
            )

        self.service._performance_time_scale = 0
        replacement = self.service.start_performance(
            [{"affect": "success", "pace": "brief"}],
            interrupt_policy="replace",
        )
        self.assertNotEqual(first["performance_id"], replacement["performance_id"])
        self.assertEqual("completed", replacement["status"])
        self.assertEqual("neutral", replacement["current_affect"])


if __name__ == "__main__":
    unittest.main()
