from __future__ import annotations

import argparse
import os
import subprocess
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator, Sequence

from .port_lease import (
    DEFAULT_YIELD_PATH,
    clear_yield_request,
    wait_until_port_released,
    write_yield_request,
)


def yield_path(role: str = "eyes") -> Path:
    if role == "mouth":
        configured = os.environ.get("YOUANDEYE_AMOLED_YIELD_PATH")
        return (
            Path(configured).resolve()
            if configured
            else DEFAULT_YIELD_PATH.with_name("amoled-port.yield")
        )
    configured = os.environ.get("YOUANDEYE_YIELD_PATH")
    return Path(configured).resolve() if configured else DEFAULT_YIELD_PATH


def acquire(
    *,
    duration_s: float = 600.0,
    owner: str = "local-hardware-tool",
    wait_timeout_s: float = 5.0,
    role: str = "eyes",
) -> Path:
    path = yield_path(role)
    request_id = write_yield_request(path, duration_s=duration_s, owner=owner)
    try:
        wait_until_port_released(path, timeout_s=wait_timeout_s)
    except Exception:
        clear_yield_request(path, request_id)
        raise
    return path


def release(*, role: str = "eyes") -> None:
    clear_yield_request(yield_path(role))


@contextmanager
def yielded_port(
    *,
    duration_s: float = 600.0,
    owner: str = "local-hardware-tool",
    role: str = "eyes",
) -> Iterator[Path]:
    path = yield_path(role)
    request_id = write_yield_request(path, duration_s=duration_s, owner=owner)
    try:
        wait_until_port_released(path)
        yield path
    finally:
        clear_yield_request(path, request_id)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Cooperatively yield the YouAndEye serial port.")
    parser.add_argument("--duration", type=float, default=600.0, help="Safety expiry in seconds.")
    parser.add_argument("--role", choices=("eyes", "mouth"), default="eyes")
    subparsers = parser.add_subparsers(dest="action", required=True)
    subparsers.add_parser("acquire")
    subparsers.add_parser("release")
    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("command", nargs=argparse.REMAINDER)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    if args.action == "acquire":
        print(acquire(duration_s=args.duration, role=args.role))
        return 0
    if args.action == "release":
        release(role=args.role)
        return 0
    command = list(args.command)
    if command and command[0] == "--":
        command.pop(0)
    if not command:
        raise SystemExit("run requires a command after --")
    with yielded_port(
        duration_s=args.duration, owner=" ".join(command[:2]), role=args.role
    ):
        return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
