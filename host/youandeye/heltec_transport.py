from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Callable, Mapping, Protocol

from .contracts import ContractError, validate_message


HELTEC_TEXT_MAX_CHARS = 64
HELTEC_COMMAND_MAX_BYTES = 512
HELTEC_SEQUENCE_COMMANDS = {
    "attention": "ATTENTION",
    "acknowledge": "ACKNOWLEDGE",
    "celebrate": "SUCCESS",
    "reassure": "REASSURE",
    "error": "ERROR",
}


class HeltecTransportError(ValueError):
    """Raised before or during a bounded write to the compatibility face."""


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
    def close(self) -> None: ...


def _safe_text(value: object) -> str:
    text = re.sub(r"\s+", " ", str(value)).strip()
    return text[:HELTEC_TEXT_MAX_CHARS]


def commands_for_frame(frame: Mapping[str, Any]) -> list[str]:
    """Translate one canonical emote/1 frame into the Heltec rig's local commands."""

    checked = validate_message(frame, "frame")
    policy = checked.get("channel_policy", {})
    commands: list[str] = []

    if policy.get("eyes", "auto") != "mute":
        commands.append(
            f"EMOTE {checked['affect']['state'].upper()} {checked['affect']['intensity']:.2f}"
        )

    utterance_muted = policy.get("utterance", "auto") == "mute" or policy.get(
        "mouth", "auto"
    ) == "mute"
    utterance = checked.get("utterance", {"mode": "none"})
    mode = utterance["mode"]
    text = _safe_text(utterance.get("text", ""))

    sequence = checked.get("sequence")
    if (
        sequence in HELTEC_SEQUENCE_COMMANDS
        and policy.get("eyes", "auto") != "mute"
        and not utterance_muted
        and mode == "none"
    ):
        return [f"BEAT {HELTEC_SEQUENCE_COMMANDS[sequence]}"]

    if utterance_muted:
        commands.append("MOUTH BLANK")
    elif mode == "none":
        commands.append("MOUTH AUTO")
    elif mode == "scroll":
        commands.append(f"SCROLL {text}" if text else "MOUTH BLANK")
    elif mode in {"static", "speech"}:
        commands.append(f"TEXT {text}" if text else "MOUTH BLANK")
    elif mode == "icon":
        icon = _safe_text(utterance.get("icon", "")).replace("_", " ").upper()
        commands.append(f"TEXT {icon}" if icon else "MOUTH BLANK")
    else:  # The schema currently makes this unreachable; keep the adapter fail-closed.
        raise HeltecTransportError(f"unsupported utterance mode: {mode!r}")

    if not commands:
        raise HeltecTransportError("frame mutes every supported Heltec channel")
    return commands


def encode_heltec_frame(frame: Mapping[str, Any]) -> bytes:
    payload = "".join(f"{command}\n" for command in commands_for_frame(frame)).encode("utf-8")
    if len(payload) > HELTEC_COMMAND_MAX_BYTES:
        raise HeltecTransportError(
            f"encoded command batch is {len(payload)} bytes; limit is {HELTEC_COMMAND_MAX_BYTES}"
        )
    return payload


def write_heltec_frame(
    port: str,
    payload: bytes,
    baudrate: int = 115200,
    serial_factory: Callable[[], SerialConnection] | None = None,
) -> int:
    if not port:
        raise HeltecTransportError("serial port is required")
    if baudrate <= 0:
        raise HeltecTransportError("baudrate must be positive")
    if not payload.endswith(b"\n") or len(payload) > HELTEC_COMMAND_MAX_BYTES:
        raise HeltecTransportError("payload must be one bounded newline-terminated command batch")
    if serial_factory is None:
        try:
            import serial
        except ImportError as exc:
            raise HeltecTransportError(
                "pyserial is required for --send; run with `uv run --extra serial`"
            ) from exc
        serial_factory = serial.Serial

    connection = serial_factory()
    connection.port = port
    connection.baudrate = baudrate
    connection.timeout = 0.2
    connection.write_timeout = 1.0
    connection.xonxoff = False
    connection.rtscts = False
    connection.dsrdtr = False
    connection.dtr = False
    connection.rts = False
    connection.open()
    try:
        written = connection.write(payload)
        if written != len(payload):
            raise HeltecTransportError(f"short serial write: {written}/{len(payload)} bytes")
        connection.flush()
        return written
    finally:
        connection.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate and translate one emote/1 frame for the Heltec OLED + dual-eye rig."
    )
    parser.add_argument("frame", type=Path, help="Path to one canonical emote/1 frame JSON file.")
    parser.add_argument("--send", action="store_true", help="Write translated commands to serial.")
    parser.add_argument("--port", help="Explicit serial port; required with --send.")
    parser.add_argument("--baudrate", type=int, default=115200)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.send and not args.port:
        parser.error("--send requires an explicit --port")
    if args.port and not args.send:
        parser.error("--port has no effect without --send")

    try:
        frame = json.loads(args.frame.read_text(encoding="utf-8"))
        payload = encode_heltec_frame(frame)
        if not args.send:
            sys.stdout.buffer.write(payload)
            return 0
        written = write_heltec_frame(args.port, payload, args.baudrate)
    except (OSError, json.JSONDecodeError, ContractError, HeltecTransportError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print(
        json.dumps(
            {"ok": True, "port": args.port, "bytes": written, "commands": len(payload.splitlines())},
            separators=(",", ":"),
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
