from __future__ import annotations

import asyncio
import threading
import time
import unittest

from host.youandeye.contracts import ContractError
from host.youandeye.face_service import ExpressionService
from host.youandeye.mcp_server import create_mcp


class FakeDevice:
    def __init__(self) -> None:
        self.batches: list[list[str]] = []
        self.closed = False

    def send_commands(self, commands: list[str]) -> dict:
        batch = list(commands)
        self.batches.append(batch)
        return {
            "connected": True,
            "device": {"port": "COM77", "vid": 0x10C4, "pid": 0xEA60},
            "commands": batch,
            "acknowledgements": [f"OK {command}" for command in batch],
        }

    def status(self) -> dict:
        return {
            "connected": True,
            "device": {"port": "COM77"},
            "runtime": {"renderer": "sdf", "display": "live", "misses": 0},
            "mouth": {"ready": 1, "mode": "auto"},
        }

    def describe(self) -> dict:
        return {"connected": True, "device": {"port": "COM77"}}

    def close(self) -> None:
        self.closed = True


class ExpressionServiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.now = 1000
        self.device = FakeDevice()
        self.service = ExpressionService(
            self.device,  # type: ignore[arg-type]
            session_id="test-session",
            monotonic_ms=lambda: self.now,
            schedule_expiry=False,
        )

    def tearDown(self) -> None:
        self.service.close()

    def test_expression_builds_valid_frame_and_reaches_both_channels(self) -> None:
        receipt = self.service.express(
            affect="thinking",
            message="PLEASE WAIT...",
            text_mode="scroll",
            ttl_ms=1000,
        )
        self.assertTrue(receipt["ok"])
        self.assertEqual(
            ["EMOTE THINKING", "SCROLL PLEASE WAIT..."], self.device.batches[-1]
        )
        self.assertEqual("thinking", receipt["frame"]["affect"]["state"])

    def test_sequence_becomes_one_device_owned_beat(self) -> None:
        self.service.express(affect="success", sequence="celebrate", ttl_ms=2500)
        self.assertEqual(["BEAT SUCCESS"], self.device.batches[-1])

    def test_expiry_reconciles_to_safe_neutral(self) -> None:
        self.service.express(affect="error", ttl_ms=100)
        self.now += 701
        result = self.service.reconcile()
        self.assertEqual("baseline", result["phase"])
        self.assertEqual(["EMOTE NEUTRAL", "MOUTH AUTO"], self.device.batches[-1])

    def test_background_expiry_returns_to_neutral(self) -> None:
        service = ExpressionService(
            self.device,  # type: ignore[arg-type]
            session_id="timer-session",
            schedule_expiry=True,
        )
        try:
            service.express(affect="happy", ttl_ms=100)
            time.sleep(0.8)
            self.assertEqual(["EMOTE NEUTRAL", "MOUTH AUTO"], self.device.batches[-1])
        finally:
            service.close()

    def test_expiry_uses_one_scheduler_thread_for_many_frames(self) -> None:
        before = sum(t.name == "youandeye-expiry" for t in threading.enumerate())
        service = ExpressionService(
            self.device,  # type: ignore[arg-type]
            session_id="scheduler-session",
            schedule_expiry=True,
        )
        try:
            scheduler = service._expiry_thread
            self.assertIsNotNone(scheduler)
            for _ in range(200):
                service.express(affect="happy", ttl_ms=600000)
            self.assertIs(scheduler, service._expiry_thread)
            self.assertTrue(scheduler.is_alive())
            self.assertEqual(
                before + 1,
                sum(t.name == "youandeye-expiry" for t in threading.enumerate()),
            )
        finally:
            service.close()

    def test_serial_io_does_not_hold_the_semantic_state_lock(self) -> None:
        write_started = threading.Event()
        release_write = threading.Event()

        class BlockingDevice(FakeDevice):
            def send_commands(self, commands: list[str]) -> dict:
                write_started.set()
                release_write.wait(timeout=2)
                return super().send_commands(commands)

        device = BlockingDevice()
        service = ExpressionService(
            device,  # type: ignore[arg-type]
            session_id="nonblocking-session",
            schedule_expiry=False,
        )
        worker = threading.Thread(
            target=service.express,
            kwargs={"affect": "happy", "ttl_ms": 1000},
        )
        worker.start()
        try:
            self.assertTrue(write_started.wait(timeout=1))
            acquired = service._lock.acquire(timeout=0.1)
            self.assertTrue(acquired, "serial I/O held the semantic-state lock")
            if acquired:
                service._lock.release()
        finally:
            release_write.set()
            worker.join(timeout=2)
            service.close()
        self.assertFalse(worker.is_alive())

    def test_sequence_and_explicit_message_are_rejected(self) -> None:
        with self.assertRaisesRegex(ContractError, "mutually exclusive"):
            self.service.build_frame(
                affect="success",
                sequence="celebrate",
                message="DONE",
                text_mode="static",
            )

    def test_frame_for_another_surface_is_rejected(self) -> None:
        frame = self.service.build_frame(affect="happy")
        frame["surface_id"] = "different-face"
        with self.assertRaisesRegex(ContractError, "different-face"):
            self.service.submit_frame(frame)

    def test_neutral_clears_active_intent(self) -> None:
        self.service.express(affect="suspicious")
        receipt = self.service.neutral()
        self.assertTrue(receipt["ok"])
        self.assertEqual(["EMOTE NEUTRAL", "MOUTH AUTO"], self.device.batches[-1])

    def test_mcp_surface_exposes_only_semantic_tools(self) -> None:
        server = create_mcp(self.service)
        tools = asyncio.run(server.list_tools())
        self.assertEqual(
            {"express", "face_status", "face_capabilities", "neutral"},
            {tool.name for tool in tools},
        )
        express = next(tool for tool in tools if tool.name == "express")
        properties = express.inputSchema["properties"]
        self.assertIn("affect", properties)
        self.assertEqual(0.0, properties["intensity"]["minimum"])
        self.assertEqual(1.0, properties["intensity"]["maximum"])
        self.assertEqual(100, properties["ttl_ms"]["minimum"])
        self.assertEqual(600000, properties["ttl_ms"]["maximum"])
        self.assertNotIn("port", properties)
        self.assertNotIn("pixels", properties)


if __name__ == "__main__":
    unittest.main()
