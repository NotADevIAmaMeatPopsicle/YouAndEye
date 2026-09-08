from __future__ import annotations

import json
import os
import re
import threading
import time
from collections.abc import Callable, Iterable, Sequence
from contextlib import suppress
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Protocol

from .port_lease import DEFAULT_YIELD_PATH, PortLease, try_acquire_port_lease

HELTEC_USB_VID = 0x10C4
HELTEC_USB_PID = 0xEA60
AMOLED_USB_VID = 0x303A
AMOLED_USB_PID = 0x1001
DEFAULT_USB_SERIAL: str | None = None
DEFAULT_BAUDRATE = 115200
DEFAULT_IDLE_RELEASE_S = 10.0
DEFAULT_STARTUP_DELAY_S = 0.15
DEFAULT_FAILURE_THRESHOLD = 3
DEFAULT_FAILURE_COOLDOWN_S = 5.0
DEFAULT_AMOLED_YIELD_PATH = DEFAULT_YIELD_PATH.with_name("amoled-port.yield")


class DeviceError(RuntimeError):
    """Raised when the physical face cannot be identified or controlled safely."""

    def __init__(
        self,
        message: str,
        *,
        code: str = "device_error",
        retry_after_s: float | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.retry_after_s = retry_after_s

    def as_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {"code": self.code, "message": str(self)}
        if self.retry_after_s is not None:
            result["retry_after_s"] = round(max(0.0, self.retry_after_s), 3)
        return result


class PortInfo(Protocol):
    device: str
    description: str
    hwid: str
    vid: int | None
    pid: int | None
    serial_number: str | None


class SerialConnection(Protocol):
    port: str
    baudrate: int
    timeout: float
    write_timeout: float
    xonxoff: bool
    rtscts: bool
    dsrdtr: bool
    dtr: bool
    rts: bool

    def open(self) -> None: ...
    def write(self, data: bytes) -> int: ...
    def flush(self) -> None: ...
    def read_all(self) -> bytes: ...
    def reset_input_buffer(self) -> None: ...
    def close(self) -> None: ...


@dataclass(frozen=True)
class DeviceIdentity:
    port: str
    description: str
    hardware_id: str
    vid: int
    pid: int
    usb_serial: str | None

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


def _system_ports() -> Iterable[PortInfo]:
    try:
        from serial.tools import list_ports
    except ImportError as exc:  # pragma: no cover - exercised only without serial extra
        raise DeviceError(
            "pyserial is required; run with `uv run --extra serial`",
            code="dependency_missing",
        ) from exc
    return list_ports.comports()


def discover_heltec(
    requested_port: str | None = None,
    *,
    expected_usb_serial: str | None = DEFAULT_USB_SERIAL,
    enumerator: Callable[[], Iterable[PortInfo]] | None = None,
) -> DeviceIdentity:
    """Find exactly one approved CP210x bridge and fail closed on ambiguity."""

    ports = list((enumerator or _system_ports)())
    requested = None if requested_port in {None, "", "auto"} else requested_port
    if requested is not None:
        candidates = [
            port for port in ports if port.device.casefold() == requested.casefold()
        ]
        if not candidates:
            raise DeviceError(
                f"requested serial port {requested!r} is not present",
                code="device_absent",
            )
    else:
        candidates = [
            port
            for port in ports
            if port.vid == HELTEC_USB_VID and port.pid == HELTEC_USB_PID
        ]

    approved = [
        port
        for port in candidates
        if port.vid == HELTEC_USB_VID
        and port.pid == HELTEC_USB_PID
        and (expected_usb_serial is None or port.serial_number == expected_usb_serial)
    ]
    if not approved:
        requested_label = requested or "auto"
        raise DeviceError(
            f"no approved YouandEye CP210x device for {requested_label!r}; "
            f"expected VID:PID {HELTEC_USB_VID:04X}:{HELTEC_USB_PID:04X}"
            + (
                f" and USB serial {expected_usb_serial!r}"
                if expected_usb_serial
                else ""
            ),
            code="device_absent" if requested is None else "wrong_device",
        )
    if len(approved) != 1:
        names = ", ".join(sorted(port.device for port in approved))
        raise DeviceError(
            f"multiple approved CP210x devices found ({names}); set YOUANDEYE_PORT",
            code="device_ambiguous",
        )

    port = approved[0]
    return DeviceIdentity(
        port=port.device,
        description=port.description or "CP210x USB to UART bridge",
        hardware_id=port.hwid or "",
        vid=port.vid,
        pid=port.pid,
        usb_serial=port.serial_number,
    )


def discover_amoled_mouth(
    requested_port: str | None = None,
    *,
    expected_usb_serial: str | None = DEFAULT_USB_SERIAL,
    enumerator: Callable[[], Iterable[PortInfo]] | None = None,
) -> DeviceIdentity:
    """Find exactly one Waveshare ESP32-S3 USB endpoint and fail closed."""

    ports = list((enumerator or _system_ports)())
    requested = None if requested_port in {None, "", "auto"} else requested_port
    if requested is not None:
        candidates = [
            port for port in ports if port.device.casefold() == requested.casefold()
        ]
        if not candidates:
            raise DeviceError(
                f"requested AMOLED serial port {requested!r} is not present",
                code="device_absent",
            )
    else:
        candidates = [
            port
            for port in ports
            if port.vid == AMOLED_USB_VID and port.pid == AMOLED_USB_PID
        ]

    approved = [
        port
        for port in candidates
        if port.vid == AMOLED_USB_VID
        and port.pid == AMOLED_USB_PID
        and (expected_usb_serial is None or port.serial_number == expected_usb_serial)
    ]
    if not approved:
        requested_label = requested or "auto"
        raise DeviceError(
            f"no approved YouAndEye AMOLED controller for {requested_label!r}; "
            f"expected VID:PID {AMOLED_USB_VID:04X}:{AMOLED_USB_PID:04X}"
            + (
                f" and USB serial {expected_usb_serial!r}"
                if expected_usb_serial
                else ""
            ),
            code="device_absent" if requested is None else "wrong_device",
        )
    if len(approved) != 1:
        names = ", ".join(sorted(port.device for port in approved))
        raise DeviceError(
            f"multiple approved AMOLED controllers found ({names}); set YOUANDEYE_AMOLED_PORT",
            code="device_ambiguous",
        )

    port = approved[0]
    return DeviceIdentity(
        port=port.device,
        description=port.description or "ESP32-S3 USB Serial/JTAG",
        hardware_id=port.hwid or "",
        vid=port.vid,
        pid=port.pid,
        usb_serial=port.serial_number,
    )


def _parse_fields(line: str) -> dict[str, Any]:
    fields: dict[str, Any] = {}
    for name, raw_value in re.findall(r"([A-Za-z][A-Za-z0-9_]*)=([^\s]+)", line):
        value: Any = raw_value
        try:
            value = float(raw_value) if "." in raw_value else int(raw_value)
        except ValueError:
            pass
        fields[name] = value
    return fields


class HeltecDevice:
    """Persistent, identity-gated connection to the accepted Heltec face."""

    def __init__(
        self,
        port: str | None = None,
        *,
        baudrate: int = DEFAULT_BAUDRATE,
        expected_usb_serial: str | None = DEFAULT_USB_SERIAL,
        enumerator: Callable[[], Iterable[PortInfo]] | None = None,
        serial_factory: Callable[[], SerialConnection] | None = None,
        startup_delay_s: float = DEFAULT_STARTUP_DELAY_S,
        response_timeout_s: float = 0.9,
        idle_release_s: float = DEFAULT_IDLE_RELEASE_S,
        yield_path: str | os.PathLike[str] | None = DEFAULT_YIELD_PATH,
        lease_poll_s: float = 0.25,
        failure_threshold: int = DEFAULT_FAILURE_THRESHOLD,
        failure_cooldown_s: float = DEFAULT_FAILURE_COOLDOWN_S,
        sleep: Callable[[float], None] = time.sleep,
        monotonic: Callable[[], float] = time.monotonic,
    ) -> None:
        self.requested_port = port or os.environ.get("YOUANDEYE_PORT", "auto")
        self.baudrate = baudrate
        self.expected_usb_serial = expected_usb_serial
        self._enumerator = enumerator
        self._serial_factory = serial_factory
        self._startup_delay_s = startup_delay_s
        self._response_timeout_s = response_timeout_s
        self._idle_release_s = max(0.0, idle_release_s)
        self._yield_path = Path(yield_path).resolve() if yield_path is not None else None
        self._lease_poll_s = max(0.05, lease_poll_s)
        self._failure_threshold = max(1, failure_threshold)
        self._failure_cooldown_s = max(0.0, failure_cooldown_s)
        self._sleep = sleep
        self._monotonic = monotonic
        self._lock = threading.RLock()
        self._connection: SerialConnection | None = None
        self._identity: DeviceIdentity | None = None
        self._port_lease: PortLease | None = None
        self._connection_generation = 0
        self._last_activity_s = self._monotonic()
        self._consecutive_failures = 0
        self._circuit_until_s = 0.0
        self._closed = False
        self._lease_stop = threading.Event()
        self._lease_thread = threading.Thread(
            target=self._lease_loop,
            name="youandeye-port-lease",
            daemon=True,
        )
        self._lease_thread.start()

    @property
    def connected(self) -> bool:
        with self._lock:
            return self._connection is not None

    @property
    def connection_generation(self) -> int:
        with self._lock:
            return self._connection_generation

    def _yield_requested_locked(self) -> bool:
        if self._yield_path is None or not self._yield_path.exists():
            return False
        try:
            payload = json.loads(self._yield_path.read_text(encoding="utf-8"))
            expires_at = float(payload.get("expires_at", 0))
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            return True
        if expires_at > time.time():
            return True
        with suppress(OSError):
            self._yield_path.unlink()
        return False

    def _check_available_locked(self) -> None:
        if self._closed:
            raise DeviceError("device service is closed", code="service_closed")
        if self._yield_requested_locked():
            self._close_locked()
            raise DeviceError(
                "serial port is yielded to a local hardware tool",
                code="port_yielded",
            )
        now = self._monotonic()
        if now < self._circuit_until_s:
            raise DeviceError(
                "serial retry cooldown is active",
                code="retry_cooldown",
                retry_after_s=self._circuit_until_s - now,
            )

    @staticmethod
    def _normalize_error(exc: Exception, *, opening: bool = False) -> DeviceError:
        if isinstance(exc, DeviceError):
            return exc
        message = str(exc) or exc.__class__.__name__
        lowered = message.casefold()
        busy_markers = (
            "access is denied",
            "permission denied",
            "resource busy",
            "device or resource busy",
            "could not open port",
        )
        if isinstance(exc, PermissionError) or any(marker in lowered for marker in busy_markers):
            return DeviceError(
                "serial port is busy; close its monitor or request a cooperative port yield",
                code="port_busy",
            )
        return DeviceError(
            message,
            code="device_absent" if opening else "serial_io",
        )

    def _record_failure_locked(self, error: DeviceError) -> None:
        counted = {
            "port_busy",
            "device_absent",
            "wrong_device",
            "wrong_firmware",
            "serial_io",
            "communication_timeout",
        }
        if error.code not in counted:
            return
        self._consecutive_failures += 1
        if self._consecutive_failures >= self._failure_threshold:
            self._circuit_until_s = self._monotonic() + self._failure_cooldown_s
            self._close_locked()

    def _record_success_locked(self) -> None:
        self._consecutive_failures = 0
        self._circuit_until_s = 0.0
        self._last_activity_s = self._monotonic()

    def _lease_loop(self) -> None:
        while not self._lease_stop.wait(self._lease_poll_s):
            with self._lock:
                if self._closed:
                    return
                if self._connection is None:
                    continue
                idle = self._monotonic() - self._last_activity_s
                if self._yield_requested_locked() or (
                    self._idle_release_s > 0 and idle >= self._idle_release_s
                ):
                    self._close_locked()

    def _make_serial(self) -> SerialConnection:
        if self._serial_factory is not None:
            return self._serial_factory()
        try:
            import serial
        except (
            ImportError
        ) as exc:  # pragma: no cover - exercised only without serial extra
            raise DeviceError(
                "pyserial is required; run with `uv run --extra serial`",
                code="dependency_missing",
            ) from exc
        return serial.Serial()

    def _discover_identity(self) -> DeviceIdentity:
        return discover_heltec(
            self.requested_port,
            expected_usb_serial=self.expected_usb_serial,
            enumerator=self._enumerator,
        )

    def _validate_status_signature(self, status_line: str) -> None:
        required_markers = (
            "renderer=",
            "pipeline=",
            "display=",
            "mouth=",
            "affect=",
        )
        if not all(marker in status_line for marker in required_markers):
            raise DeviceError(
                "serial device did not return a YouAndEye runtime STATUS signature",
                code="wrong_firmware",
            )

    def _status_query(self) -> tuple[Sequence[str], Sequence[str]]:
        return ("STATUS", "MOUTH STATUS"), ("STATUS ", "MOUTH ")

    def _parse_status(self, lines: Sequence[str]) -> dict[str, Any]:
        status_line = next(line for line in lines if line.startswith("STATUS "))
        mouth_line = next(line for line in lines if line.startswith("MOUTH "))
        return {
            "runtime": _parse_fields(status_line),
            "mouth": _parse_fields(mouth_line),
        }

    def _open_locked(self) -> None:
        self._check_available_locked()
        if self._connection is not None:
            return
        lease = (
            try_acquire_port_lease(self._yield_path)
            if self._yield_path is not None
            else None
        )
        if self._yield_path is not None and lease is None:
            raise DeviceError(
                "serial port is leased by another local YouAndEye process",
                code="port_busy",
            )
        connection: SerialConnection | None = None
        try:
            # Close the marker/lease TOCTOU window: a yield request written after
            # the first check must win before this process touches the serial port.
            self._check_available_locked()
            identity = self._discover_identity()
            connection = self._make_serial()
            connection.port = identity.port
            connection.baudrate = self.baudrate
            connection.timeout = 0.05
            connection.write_timeout = 1.0
            connection.xonxoff = False
            connection.rtscts = False
            connection.dsrdtr = False
            connection.dtr = False
            connection.rts = False
            try:
                connection.open()
            except Exception as exc:
                error = self._normalize_error(exc, opening=True)
                if error is exc:
                    raise
                raise error from exc
            self._sleep(self._startup_delay_s)
            connection.reset_input_buffer()
            lines = self._write_read_locked(
                connection,
                ("STATUS",),
                required_prefixes=("STATUS ",),
                timeout_s=max(0.75, self._response_timeout_s),
            )
            status_line = next(
                (line for line in lines if line.startswith("STATUS ")), ""
            )
            self._validate_status_signature(status_line)
        except Exception:
            try:
                if connection is not None:
                    connection.close()
            finally:
                if lease is not None:
                    lease.release()
                self._connection = None
                self._identity = None
            raise
        assert connection is not None
        self._connection = connection
        self._identity = identity
        self._port_lease = lease
        self._connection_generation += 1
        self._last_activity_s = self._monotonic()

    def _write_read_locked(
        self,
        connection: SerialConnection,
        commands: Sequence[str],
        *,
        expected_ok: int = 0,
        required_prefixes: Sequence[str] = (),
        timeout_s: float | None = None,
    ) -> list[str]:
        clean = [command.strip() for command in commands]
        if not clean or any(
            not command or "\n" in command or "\r" in command for command in clean
        ):
            raise DeviceError(
                "device commands must be non-empty single lines",
                code="invalid_command",
            )
        payload = "".join(f"{command}\n" for command in clean).encode("utf-8")
        if len(payload) > 512:
            raise DeviceError(
                "device command batch exceeds the 512-byte serial limit",
                code="invalid_command",
            )
        written = connection.write(payload)
        if written != len(payload):
            raise DeviceError(
                f"short serial write: {written}/{len(payload)} bytes",
                code="serial_io",
            )
        connection.flush()

        deadline = self._monotonic() + (timeout_s or self._response_timeout_s)
        buffer = ""
        lines: list[str] = []
        while self._monotonic() < deadline:
            chunk = connection.read_all()
            if chunk:
                buffer += chunk.decode("utf-8", errors="replace")
                pieces = buffer.split("\n")
                buffer = pieces.pop()
                lines.extend(piece.strip() for piece in pieces if piece.strip())
                ok_count = sum(line.startswith("OK ") for line in lines)
                prefixes_found = all(
                    any(line.startswith(prefix) for line in lines)
                    for prefix in required_prefixes
                )
                if ok_count >= expected_ok and prefixes_found:
                    break
            self._sleep(0.02)
        if buffer.strip():
            lines.append(buffer.strip())
        errors = [line for line in lines if line.startswith("ERR ")]
        if errors:
            raise DeviceError("; ".join(errors), code="device_rejected")
        if sum(line.startswith("OK ") for line in lines) < expected_ok:
            raise DeviceError(
                f"device acknowledgement timeout for: {', '.join(clean)}",
                code="communication_timeout",
            )
        for prefix in required_prefixes:
            if not any(line.startswith(prefix) for line in lines):
                raise DeviceError(
                    f"device response missing {prefix.strip()!r}",
                    code="wrong_firmware" if prefix == "STATUS " else "communication_timeout",
                )
        return lines

    def send_commands(self, commands: Sequence[str]) -> dict[str, Any]:
        with self._lock:
            try:
                self._open_locked()
                assert self._connection is not None and self._identity is not None
                self._connection.reset_input_buffer()
                lines = self._write_read_locked(
                    self._connection,
                    commands,
                    expected_ok=len(commands),
                )
                self._record_success_locked()
                return {
                    "connected": True,
                    "device": self._identity.as_dict(),
                    "commands": list(commands),
                    "acknowledgements": [
                        line for line in lines if line.startswith("OK ")
                    ],
                }
            except Exception as exc:
                error = self._normalize_error(exc)
                self._record_failure_locked(error)
                if error.code in {"serial_io", "wrong_firmware"}:
                    self._close_locked()
                if error is exc:
                    raise
                raise error from exc

    def status(self) -> dict[str, Any]:
        with self._lock:
            try:
                self._open_locked()
                assert self._connection is not None and self._identity is not None
                self._connection.reset_input_buffer()
                commands, prefixes = self._status_query()
                lines = self._write_read_locked(
                    self._connection,
                    commands,
                    required_prefixes=prefixes,
                )
                self._record_success_locked()
                return {
                    "connected": True,
                    "device": self._identity.as_dict(),
                    **self._parse_status(lines),
                }
            except Exception as exc:
                error = self._normalize_error(exc)
                self._record_failure_locked(error)
                if error.code in {"serial_io", "wrong_firmware"}:
                    self._close_locked()
                if error is exc:
                    raise
                raise error from exc

    def describe(self) -> dict[str, Any]:
        with self._lock:
            if self._yield_requested_locked():
                return {
                    "connected": False,
                    "available": False,
                    "error": "serial port is yielded to a local hardware tool",
                    "error_code": "port_yielded",
                }
            if self._identity is not None:
                return {
                    "connected": self._connection is not None,
                    "device": self._identity.as_dict(),
                }
        try:
            identity = self._discover_identity()
        except DeviceError as exc:
            return {
                "connected": False,
                "available": False,
                "error": str(exc),
                "error_code": exc.code,
            }
        return {"connected": False, "available": True, "device": identity.as_dict()}

    def _close_locked(self) -> None:
        connection = self._connection
        lease = self._port_lease
        self._connection = None
        self._identity = None
        self._port_lease = None
        if connection is not None:
            with suppress(Exception):
                connection.close()
        if lease is not None:
            with suppress(Exception):
                lease.release()

    def close(self) -> None:
        self._lease_stop.set()
        with self._lock:
            self._closed = True
            self._close_locked()
        if self._lease_thread is not threading.current_thread():
            self._lease_thread.join(timeout=1.0)


class AmoledMouthDevice(HeltecDevice):
    """Identity- and firmware-gated connection to the round AMOLED mouth."""

    def __init__(
        self,
        port: str | None = None,
        *,
        expected_usb_serial: str | None = DEFAULT_USB_SERIAL,
        yield_path: str | os.PathLike[str] | None = DEFAULT_AMOLED_YIELD_PATH,
        **kwargs: Any,
    ) -> None:
        requested = port or os.environ.get("YOUANDEYE_AMOLED_PORT", "auto")
        super().__init__(
            requested,
            expected_usb_serial=expected_usb_serial,
            yield_path=yield_path,
            **kwargs,
        )

    def _discover_identity(self) -> DeviceIdentity:
        return discover_amoled_mouth(
            self.requested_port,
            expected_usb_serial=self.expected_usb_serial,
            enumerator=self._enumerator,
        )

    def _validate_status_signature(self, status_line: str) -> None:
        required_markers = (
            "product=youandeye-mouth",
            "display=co5300",
            "size=466x466",
            "mode=",
            "affect=",
        )
        if not all(marker in status_line for marker in required_markers):
            raise DeviceError(
                "serial device did not return a YouAndEye AMOLED mouth STATUS signature",
                code="wrong_firmware",
            )

    def _status_query(self) -> tuple[Sequence[str], Sequence[str]]:
        return ("STATUS",), ("STATUS ",)

    def _parse_status(self, lines: Sequence[str]) -> dict[str, Any]:
        status_line = next(line for line in lines if line.startswith("STATUS "))
        fields = _parse_fields(status_line)
        return {"runtime": fields, "mouth": fields}


__all__ = [
    "AMOLED_USB_PID",
    "AMOLED_USB_VID",
    "DEFAULT_AMOLED_YIELD_PATH",
    "DEFAULT_BAUDRATE",
    "DEFAULT_FAILURE_COOLDOWN_S",
    "DEFAULT_FAILURE_THRESHOLD",
    "DEFAULT_IDLE_RELEASE_S",
    "DEFAULT_STARTUP_DELAY_S",
    "DEFAULT_USB_SERIAL",
    "DEFAULT_YIELD_PATH",
    "HELTEC_USB_PID",
    "HELTEC_USB_VID",
    "AmoledMouthDevice",
    "DeviceError",
    "DeviceIdentity",
    "HeltecDevice",
    "discover_amoled_mouth",
    "discover_heltec",
]
