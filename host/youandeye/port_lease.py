from __future__ import annotations

import json
import os
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO


DEFAULT_YIELD_PATH = Path(__file__).resolve().parents[2] / ".youandeye" / "port.yield"


def lease_path(yield_marker: Path) -> Path:
    return yield_marker.with_name(f"{yield_marker.name}.lease")


@dataclass
class PortLease:
    handle: BinaryIO
    locked: bool = True

    def release(self) -> None:
        if not self.locked:
            return
        self.handle.seek(0)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(self.handle.fileno(), msvcrt.LK_UNLCK, 1)
        else:  # pragma: no cover - exercised on POSIX hosts
            import fcntl

            fcntl.flock(self.handle.fileno(), fcntl.LOCK_UN)
        self.locked = False
        self.handle.close()


def try_acquire_port_lease(yield_marker: Path) -> PortLease | None:
    path = lease_path(yield_marker)
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open("a+b")
    handle.seek(0, os.SEEK_END)
    if handle.tell() == 0:
        handle.write(b"\0")
        handle.flush()
    handle.seek(0)
    try:
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        else:  # pragma: no cover - exercised on POSIX hosts
            import fcntl

            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        handle.close()
        return None
    return PortLease(handle)


def wait_until_port_released(
    yield_marker: Path,
    *,
    timeout_s: float = 5.0,
    poll_s: float = 0.05,
) -> None:
    deadline = time.monotonic() + max(0.0, timeout_s)
    while True:
        lease = try_acquire_port_lease(yield_marker)
        if lease is not None:
            lease.release()
            return
        if time.monotonic() >= deadline:
            raise TimeoutError(
                "the YouAndEye serial service did not release its port before the handoff timeout"
            )
        time.sleep(max(0.01, poll_s))


def write_yield_request(path: Path, *, duration_s: float, owner: str) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    request_id = uuid.uuid4().hex
    now = time.time()
    payload = {
        "request_id": request_id,
        "owner": owner,
        "pid": os.getpid(),
        "created_at": now,
        "expires_at": now + max(5.0, duration_s),
    }
    temporary = path.with_name(f".{path.name}.{request_id}.tmp")
    try:
        temporary.write_text(json.dumps(payload, sort_keys=True) + "\n", encoding="utf-8")
        temporary.replace(path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
    return request_id


def clear_yield_request(path: Path, request_id: str | None = None) -> None:
    if request_id is not None:
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (FileNotFoundError, OSError, ValueError, TypeError, json.JSONDecodeError):
            return
        if payload.get("request_id") != request_id:
            return
    try:
        path.unlink()
    except FileNotFoundError:
        pass


__all__ = [
    "DEFAULT_YIELD_PATH",
    "PortLease",
    "clear_yield_request",
    "lease_path",
    "try_acquire_port_lease",
    "wait_until_port_released",
    "write_yield_request",
]
