from __future__ import annotations

import json
import tempfile
import time
import unittest
from dataclasses import dataclass
from pathlib import Path
from unittest.mock import patch

from host.youandeye.device import DeviceError, HeltecDevice, discover_heltec
from host.youandeye.port_lease import try_acquire_port_lease


@dataclass
class FakePort:
    device: str
    description: str = "Silicon Labs CP210x USB to UART Bridge"
    hwid: str = "USB VID:PID=10C4:EA60 SER=TEST-SERIAL"
    vid: int | None = 0x10C4
    pid: int | None = 0xEA60
    serial_number: str | None = "TEST-SERIAL"


class FakeSerial:
    def __init__(self) -> None:
        self.port = ""
        self.baudrate = 0
        self.timeout = 0.0
        self.write_timeout = 0.0
        self.xonxoff = True
        self.rtscts = True
        self.dsrdtr = True
        self.dtr = True
        self.rts = True
        self.closed = False
        self.written: list[str] = []
        self._incoming = bytearray()

    def open(self) -> None:
        pass

    def write(self, payload: bytes) -> int:
        for command in payload.decode().splitlines():
            self.written.append(command)
            if command == "STATUS":
                self._incoming.extend(
                    b"STATUS renderer=sdf pipeline=1/1 display=live mouth=1/auto affect=neutral "
                    b"fps=30.5 misses=0\n"
                )
            elif command == "MOUTH STATUS":
                self._incoming.extend(b"MOUTH ready=1 mode=auto text=NEUTRAL\n")
            else:
                self._incoming.extend(f"OK {command.lower()}\n".encode())
        return len(payload)

    def flush(self) -> None:
        pass

    def read_all(self) -> bytes:
        payload = bytes(self._incoming)
        self._incoming.clear()
        return payload

    def reset_input_buffer(self) -> None:
        self._incoming.clear()

    def close(self) -> None:
        self.closed = True


class DeviceDiscoveryTests(unittest.TestCase):
    def test_auto_discovery_accepts_one_exact_cp210x(self) -> None:
        identity = discover_heltec(enumerator=lambda: [FakePort("COM77")])
        self.assertEqual("COM77", identity.port)
        self.assertEqual(0x10C4, identity.vid)

    def test_auto_discovery_fails_closed_on_multiple_matches(self) -> None:
        with self.assertRaisesRegex(DeviceError, "multiple approved"):
            discover_heltec(enumerator=lambda: [FakePort("COM77"), FakePort("COM78")])

    def test_explicit_port_still_requires_expected_usb_identity(self) -> None:
        wrong = FakePort("COM77", vid=0x1234, pid=0x5678)
        with self.assertRaisesRegex(DeviceError, "no approved"):
            discover_heltec("COM77", enumerator=lambda: [wrong])

    def test_expected_usb_serial_is_enforced(self) -> None:
        with self.assertRaisesRegex(DeviceError, "no approved"):
            discover_heltec(
                "COM77",
                expected_usb_serial="EXPECTED",
                enumerator=lambda: [FakePort("COM77")],
            )


class HeltecDeviceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.serial = FakeSerial()
        self.device = HeltecDevice(
            port="auto",
            enumerator=lambda: [FakePort("COM77")],
            serial_factory=lambda: self.serial,
            startup_delay_s=0,
            response_timeout_s=0.05,
        )

    def tearDown(self) -> None:
        self.device.close()

    def test_handshake_and_persistent_command_delivery(self) -> None:
        receipt = self.device.send_commands(("EMOTE HAPPY", "MOUTH AUTO"))
        self.assertTrue(receipt["connected"])
        self.assertEqual("COM77", receipt["device"]["port"])
        self.assertEqual(2, len(receipt["acknowledgements"]))
        self.assertEqual(["STATUS", "EMOTE HAPPY", "MOUTH AUTO"], self.serial.written)
        self.assertFalse(self.serial.dtr)
        self.assertFalse(self.serial.rts)

    def test_status_returns_structured_runtime_and_mouth_fields(self) -> None:
        status = self.device.status()
        self.assertEqual("sdf", status["runtime"]["renderer"])
        self.assertEqual(0, status["runtime"]["misses"])
        self.assertEqual("auto", status["mouth"]["mode"])

    def test_idle_lease_releases_the_serial_port(self) -> None:
        device = HeltecDevice(
            port="auto",
            enumerator=lambda: [FakePort("COM77")],
            serial_factory=lambda: self.serial,
            startup_delay_s=0,
            response_timeout_s=0.05,
            idle_release_s=0.05,
            lease_poll_s=0.01,
        )
        try:
            device.send_commands(("EMOTE HAPPY",))
            self.assertTrue(device.connected)
            time.sleep(0.15)
            self.assertFalse(device.connected)
            self.assertTrue(self.serial.closed)
        finally:
            device.close()

    def test_yield_sentinel_blocks_open_with_typed_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "port.yield"
            path.write_text(
                json.dumps({"expires_at": time.time() + 60}), encoding="utf-8"
            )
            device = HeltecDevice(
                port="auto",
                enumerator=lambda: [FakePort("COM77")],
                serial_factory=lambda: self.serial,
                startup_delay_s=0,
                yield_path=path,
            )
            try:
                with self.assertRaises(DeviceError) as raised:
                    device.send_commands(("EMOTE HAPPY",))
                self.assertEqual("port_yielded", raised.exception.code)
            finally:
                device.close()

    def test_yield_request_wins_if_it_arrives_during_lease_acquisition(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "port.yield"
            original_acquire = try_acquire_port_lease

            def racing_acquire(marker: Path):
                lease = original_acquire(marker)
                path.write_text(
                    json.dumps({"expires_at": time.time() + 60}), encoding="utf-8"
                )
                return lease

            device = HeltecDevice(
                port="auto",
                enumerator=lambda: [FakePort("COM77")],
                serial_factory=lambda: self.serial,
                startup_delay_s=0,
                yield_path=path,
            )
            try:
                with patch(
                    "host.youandeye.device.try_acquire_port_lease",
                    side_effect=racing_acquire,
                ):
                    with self.assertRaises(DeviceError) as raised:
                        device.send_commands(("EMOTE HAPPY",))
                self.assertEqual("port_yielded", raised.exception.code)
                lease = original_acquire(path)
                self.assertIsNotNone(lease)
                assert lease is not None
                lease.release()
            finally:
                device.close()

    def test_busy_port_opens_circuit_after_repeated_failures(self) -> None:
        class BusySerial(FakeSerial):
            def open(self) -> None:
                raise PermissionError("Access is denied")

        device = HeltecDevice(
            port="auto",
            enumerator=lambda: [FakePort("COM77")],
            serial_factory=BusySerial,
            startup_delay_s=0,
            failure_threshold=2,
            failure_cooldown_s=60,
        )
        try:
            for _ in range(2):
                with self.assertRaises(DeviceError) as raised:
                    device.status()
                self.assertEqual("port_busy", raised.exception.code)
            with self.assertRaises(DeviceError) as raised:
                device.status()
            self.assertEqual("retry_cooldown", raised.exception.code)
        finally:
            device.close()

    def test_reconnecting_command_failures_open_circuit(self) -> None:
        serials: list[FakeSerial] = []

        class FailingCommandSerial(FakeSerial):
            def write(self, payload: bytes) -> int:
                if payload != b"STATUS\n":
                    raise OSError("device vanished during command")
                return super().write(payload)

        def factory() -> FakeSerial:
            connection = FailingCommandSerial()
            serials.append(connection)
            return connection

        device = HeltecDevice(
            port="auto",
            enumerator=lambda: [FakePort("COM77")],
            serial_factory=factory,
            startup_delay_s=0,
            response_timeout_s=0.05,
            failure_threshold=2,
            failure_cooldown_s=60,
        )
        try:
            for _ in range(2):
                with self.assertRaises(DeviceError) as raised:
                    device.send_commands(("EMOTE HAPPY",))
                self.assertEqual("serial_io", raised.exception.code)
            with self.assertRaises(DeviceError) as raised:
                device.send_commands(("EMOTE HAPPY",))
            self.assertEqual("retry_cooldown", raised.exception.code)
            self.assertEqual(2, len(serials))
        finally:
            device.close()


if __name__ == "__main__":
    unittest.main()
