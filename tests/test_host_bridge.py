from __future__ import annotations

import json
import threading
import unittest
import urllib.error
import urllib.request
from copy import deepcopy
from pathlib import Path

from host.youandeye.arbitration import Arbiter
from host.youandeye.bridge import SurfaceBridge
from host.youandeye.downmix import downmix_frame
from host.youandeye.face_service import ExpressionService, heltec_bridge
from host.youandeye.server import make_server


ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "protocol" / "examples"


def fixture(name: str) -> dict:
    return json.loads((EXAMPLES / name).read_text(encoding="utf-8"))


def with_source(frame: dict, source_id: str, session: str, seq: int) -> dict:
    changed = deepcopy(frame)
    changed["source"] = {"id": source_id, "session": session}
    changed["seq"] = seq
    return changed


class ArbiterTests(unittest.TestCase):
    def setUp(self) -> None:
        self.frame = fixture("frame-thinking.json")
        self.arbiter = Arbiter()

    def test_stale_sequence_is_rejected(self) -> None:
        accepted = self.arbiter.submit(self.frame, 1000)
        stale = self.arbiter.submit(self.frame, 1010)
        self.assertTrue(accepted.accepted)
        self.assertFalse(stale.accepted)
        self.assertIn("stale sequence", stale.reason)

    def test_same_priority_source_cannot_interleave(self) -> None:
        first = with_source(self.frame, "agent-a", "one", 1)
        second = with_source(self.frame, "agent-b", "one", 1)
        self.arbiter.submit(first, 1000)
        result = self.arbiter.submit(second, 1010)
        self.assertTrue(result.accepted)
        self.assertFalse(result.activated)
        self.assertEqual("agent-a", self.arbiter.resolve(1010)["frame"]["source"]["id"])

    def test_alert_preempts_then_live_normal_candidate_resumes(self) -> None:
        normal = with_source(self.frame, "agent-a", "one", 1)
        normal["ttl_ms"] = 5000
        normal["decay"]["duration_ms"] = 0
        alert = with_source(self.frame, "agent-b", "one", 1)
        alert["priority"] = "alert"
        alert["ttl_ms"] = 100
        alert["decay"]["duration_ms"] = 0

        self.arbiter.submit(normal, 1000)
        preempted = self.arbiter.submit(alert, 1100)
        self.assertTrue(preempted.activated)
        self.assertEqual("agent-b", self.arbiter.resolve(1150)["frame"]["source"]["id"])
        self.assertEqual("agent-a", self.arbiter.resolve(1201)["frame"]["source"]["id"])

    def test_decay_weight_falls_toward_baseline(self) -> None:
        frame = deepcopy(self.frame)
        frame["ttl_ms"] = 100
        frame["decay"] = {"curve": "linear", "duration_ms": 1000}
        self.arbiter.submit(frame, 1000)
        resolved = self.arbiter.resolve(1600)
        self.assertEqual("decay", resolved["phase"])
        self.assertAlmostEqual(0.5, resolved["weight"])

    def test_session_and_candidate_state_is_bounded(self) -> None:
        arbiter = Arbiter(max_sessions=4, replay_retention_ms=1000)
        for index in range(4):
            frame = with_source(self.frame, f"agent-{index}", "one", 1)
            frame["ttl_ms"] = 600000
            self.assertTrue(arbiter.submit(frame, 1000).accepted)

        overflow = with_source(self.frame, "agent-overflow", "one", 1)
        rejected = arbiter.submit(overflow, 1001)
        self.assertFalse(rejected.accepted)
        self.assertEqual("session capacity reached", rejected.reason)
        self.assertEqual(4, arbiter.candidate_count)
        self.assertEqual(4, arbiter.tracked_session_count)

    def test_expired_replay_records_are_evicted_after_the_retention_window(self) -> None:
        arbiter = Arbiter(max_sessions=1, replay_retention_ms=50)
        first = with_source(self.frame, "agent-a", "one", 7)
        first["ttl_ms"] = 100
        first["decay"]["duration_ms"] = 0
        self.assertTrue(arbiter.submit(first, 1000).accepted)
        self.assertIsNone(arbiter.resolve(1151))

        replacement = with_source(self.frame, "agent-b", "one", 1)
        self.assertTrue(arbiter.submit(replacement, 1151).accepted)
        self.assertEqual(1, arbiter.tracked_session_count)


class DownmixTests(unittest.TestCase):
    def test_terminal_surface_needs_no_agent_frame_change(self) -> None:
        frame = fixture("frame-thinking.json")
        capabilities = fixture("capabilities-concept-rig.json")
        capabilities["surface"]["id"] = "terminal-01"
        capabilities["surface"]["renderer"] = "plain-terminal"
        capabilities["channels"] = [
            {
                "id": "terminal",
                "kind": "terminal",
                "count": 1,
                "color_model": "symbolic",
                "max_fps": 10,
                "features": ["text", "icons"],
            }
        ]
        result = downmix_frame(frame, capabilities)
        self.assertEqual("thinking", result["channels"]["terminal"]["affect"])
        self.assertEqual(
            "PLEASE WAIT...", result["channels"]["terminal"]["utterance"]["text"]
        )

    def test_unsupported_affect_maps_by_valence_and_arousal(self) -> None:
        frame = fixture("frame-thinking.json")
        frame["affect"] = {
            "state": "love",
            "intensity": 1,
            "valence": 0.95,
            "arousal": 0.7,
        }
        capabilities = fixture("capabilities-concept-rig.json")
        capabilities["affects"] = ["neutral", "happy", "error"]
        result = downmix_frame(frame, capabilities)
        self.assertEqual("happy", result["channels"]["eyes"]["affect"]["state"])
        self.assertTrue(result["warnings"])


class HttpBridgeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bridge = SurfaceBridge(
            fixture("capabilities-concept-rig.json"),
            fixture("frame-neutral-baseline.json"),
        )
        self.server = make_server("127.0.0.1", 0, self.bridge)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def request_json(self, path: str, data: dict | None = None) -> tuple[int, dict]:
        body = None if data is None else json.dumps(data).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + path,
            data=body,
            headers={"Content-Type": "application/json"} if body else {},
            method="POST" if body else "GET",
        )
        try:
            with urllib.request.urlopen(request, timeout=2) as response:
                return response.status, json.loads(response.read())
        except urllib.error.HTTPError as error:
            return error.code, json.loads(error.read())

    def test_post_frame_then_read_render_state(self) -> None:
        status, receipt = self.request_json(
            "/v1/frames", fixture("frame-thinking.json")
        )
        self.assertEqual(202, status)
        self.assertTrue(receipt["ok"])
        status, state = self.request_json("/v1/state")
        self.assertEqual(200, status)
        self.assertEqual("thinking", state["frame"]["affect"]["state"])
        self.assertEqual(
            "thinking", state["render_intent"]["channels"]["eyes"]["affect"]["state"]
        )

    def test_bad_frame_returns_400_without_partial_state(self) -> None:
        bad = fixture("frame-thinking.json")
        bad["affect"]["intensity"] = 7
        status, response = self.request_json("/v1/frames", bad)
        self.assertEqual(400, status)
        self.assertFalse(response["ok"])
        _, state = self.request_json("/v1/state")
        self.assertEqual("baseline", state["phase"])

    def test_non_loopback_browser_origin_is_rejected(self) -> None:
        body = json.dumps(fixture("frame-thinking.json")).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + "/v1/frames",
            data=body,
            headers={
                "Content-Type": "application/json",
                "Origin": "https://example.com",
            },
            method="POST",
        )
        with self.assertRaises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(request, timeout=2)
        self.assertEqual(403, raised.exception.code)

    def test_non_json_post_is_rejected(self) -> None:
        request = urllib.request.Request(
            self.base_url + "/v1/frames",
            data=b"{}",
            headers={"Content-Type": "text/plain"},
            method="POST",
        )
        with self.assertRaises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(request, timeout=2)
        self.assertEqual(415, raised.exception.code)


class _RecordingDevice:
    def __init__(self) -> None:
        self.batches: list[list[str]] = []

    def send_commands(self, commands: list[str]) -> dict:
        batch = list(commands)
        self.batches.append(batch)
        return {
            "connected": True,
            "device": {"port": "COM77"},
            "commands": batch,
            "acknowledgements": [f"OK {command}" for command in batch],
        }

    def status(self) -> dict:
        return {"connected": True, "runtime": {}, "mouth": {}}

    def describe(self) -> dict:
        return {"connected": True, "device": {"port": "COM77"}}

    def close(self) -> None:
        pass


class PhysicalHttpBridgeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.device = _RecordingDevice()
        self.service = ExpressionService(
            self.device,  # type: ignore[arg-type]
            bridge=heltec_bridge(),
            schedule_expiry=False,
        )
        self.server = make_server("127.0.0.1", 0, face_service=self.service)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.service.close()

    def test_accepted_http_frame_is_dispatched_to_physical_adapter(self) -> None:
        frame = fixture("frame-thinking.json")
        frame["surface_id"] = "youandeye-heltec-01"
        body = json.dumps(frame).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + "/v1/frames",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=2) as response:
            receipt = json.loads(response.read())
        self.assertEqual(202, response.status)
        self.assertTrue(receipt["delivery"]["sent"])
        self.assertEqual(
            ["EMOTE THINKING 0.70", "SCROLL PLEASE WAIT..."], self.device.batches[-1]
        )

    def test_device_endpoint_returns_physical_status(self) -> None:
        with urllib.request.urlopen(
            self.base_url + "/v1/device", timeout=2
        ) as response:
            status = json.loads(response.read())
        self.assertEqual(200, response.status)
        self.assertTrue(status["connected"])

    def test_physical_dispatch_rejects_a_frame_for_another_surface(self) -> None:
        body = json.dumps(fixture("frame-thinking.json")).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + "/v1/frames",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(request, timeout=2)
        self.assertEqual(400, caught.exception.code)
        self.assertEqual([], self.device.batches)


if __name__ == "__main__":
    unittest.main()
