from __future__ import annotations

import json
import os
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from host.youandeye.port_yield import acquire, release, yield_path
from host.youandeye.port_lease import try_acquire_port_lease


class PortYieldTests(unittest.TestCase):
    def test_acquire_writes_a_bounded_marker_at_the_configured_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "port.yield"
            with patch.dict(os.environ, {"YOUANDEYE_YIELD_PATH": str(marker)}):
                written = acquire(duration_s=30, owner="test-runner")
                payload = json.loads(marker.read_text(encoding="utf-8"))

            self.assertEqual(marker.resolve(), written)
            self.assertEqual("test-runner", payload["owner"])
            self.assertGreater(payload["expires_at"], payload["created_at"])

    def test_release_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "port.yield"
            with patch.dict(os.environ, {"YOUANDEYE_YIELD_PATH": str(marker)}):
                acquire()
                release()
                release()
                configured = yield_path()

            self.assertEqual(marker.resolve(), configured)
            self.assertFalse(marker.exists())

    def test_acquire_waits_for_the_active_serial_lease(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "port.yield"
            lease = try_acquire_port_lease(marker)
            self.assertIsNotNone(lease)
            finished = threading.Event()

            def request_yield() -> None:
                with patch.dict(os.environ, {"YOUANDEYE_YIELD_PATH": str(marker)}):
                    acquire(wait_timeout_s=2)
                finished.set()

            worker = threading.Thread(target=request_yield)
            worker.start()
            try:
                time.sleep(0.1)
                self.assertFalse(finished.is_set())
                assert lease is not None
                lease.release()
                self.assertTrue(finished.wait(timeout=1))
            finally:
                if lease is not None:
                    lease.release()
                worker.join(timeout=1)
                with patch.dict(os.environ, {"YOUANDEYE_YIELD_PATH": str(marker)}):
                    release()


if __name__ == "__main__":
    unittest.main()
