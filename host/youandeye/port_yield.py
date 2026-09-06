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


def yield_path() -> Path:
    configured = os.environ.get("YOUANDEYE_YIELD_PATH")
    return Path(configured).resolve() if configured else DEFAULT_YIELD_PATH


def acquire(
    *,
    duration_s: float = 600.0,
    owner: str = "local-hardware-tool",
    wait_timeout_s: float = 5.0,
) -> Path:
    path = yield_path()
    request_id = write_yield_request(path, duration_s=duration_s, owner=owner)
    try:
        wait_until_port_released(path, timeout_s=wait_timeout_s)
    except Exception:
        clear_yield_request(path, request_id)
        raise
    return path


def release() -> None:
    clear_yield_request(yield_path())


@contextmanager
def yielded_port(*, duration_s: float = 600.0, owner: str = "local-hardware-tool") -> Iterator[Path]:
    path = yield_path()
    request_id = write_yield_request(path, duration_s=duration_s, owner=owner)
    try:
        wait_until_port_released(path)
        yield path
    finally:
        clear_yield_request(path, request_id)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Cooperatively yield the YouAndEye serial port.")
    parser.add_argument("--duration", type=float, default=600.0, help="Safety expiry in seconds.")
    subparsers = parser.add_subparsers(dest="action", required=True)
    subparsers.add_parser("acquire")
    subparsers.add_parser("release")
    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("command", nargs=argparse.REMAINDER)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    if args.action == "acquire":
        print(acquire(duration_s=args.duration))
        return 0
    if args.action == "release":
        release()
        return 0
    command = list(args.command)
    if command and command[0] == "--":
        command.pop(0)
    if not command:
        raise SystemExit("run requires a command after --")
    with yielded_port(duration_s=args.duration, owner=" ".join(command[:2])):
        return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
