from __future__ import annotations

import os
from typing import Annotated, Any, Literal

from mcp.server.fastmcp import FastMCP
from mcp.server.fastmcp.exceptions import ToolError
from mcp.types import ToolAnnotations
from pydantic import Field

from .contracts import ContractError
from .device import DEFAULT_USB_SERIAL, DeviceError, HeltecDevice
from .face_service import ExpressionService

Affect = Literal[
    "neutral",
    "happy",
    "surprised",
    "thinking",
    "suspicious",
    "sad",
    "excited",
    "love",
    "error",
    "listening",
    "speaking",
    "working",
    "success",
    "playful",
    "encouraging",
    "curious",
    "uncertain",
    "concerned",
    "delighted",
    "embarrassed",
    "reassuring",
]
Sequence = Literal["attention", "acknowledge", "celebrate", "reassure", "error"]
TextMode = Literal["static", "scroll", "icon", "speech"]
Priority = Literal["ambient", "normal", "alert", "critical"]
GazeTarget = Literal["user", "away", "wander"]
BehaviorMode = Literal["idle", "attentive", "tracking", "speaking", "sleepy"]


SERVER_INSTRUCTIONS = (
    "YouandEye is a local expressive face. Use express at meaningful conversational state changes, not "
    "for every token. Let the eyes carry ordinary interaction; omit message unless words materially add "
    "clarity. Prefer device-owned sequences when no custom message is needed. Effects expire to "
    "a safe autonomous neutral baseline. After a connection error, check face_status and call neutral once "
    "connected. Never guess a serial port or expose low-level pixel controls."
)


def default_service() -> ExpressionService:
    usb_serial = os.environ.get("YOUANDEYE_USB_SERIAL", DEFAULT_USB_SERIAL)
    return ExpressionService(
        HeltecDevice(
            port=os.environ.get("YOUANDEYE_PORT", "auto"),
            baudrate=int(os.environ.get("YOUANDEYE_BAUDRATE", "115200")),
            expected_usb_serial=usb_serial or None,
        ),
        source_id=os.environ.get("YOUANDEYE_SOURCE_ID", "agent"),
    )


def _tool_error(exc: Exception) -> ToolError:
    if isinstance(exc, DeviceError):
        return ToolError(f"{exc.code}: {exc}")
    return ToolError(str(exc))


def create_mcp(service: ExpressionService) -> FastMCP:
    server = FastMCP(
        "YouAndEye",
        instructions=SERVER_INSTRUCTIONS,
    )

    @server.tool(
        name="express",
        title="Express through YouAndEye",
        description=(
            "Show one temporary semantic expression on the local face. The firmware owns animation. "
            "When sequence is set, omit message so the device can perform its coordinated eye/OLED beat."
        ),
        annotations=ToolAnnotations(
            readOnlyHint=False,
            destructiveHint=False,
            idempotentHint=False,
            openWorldHint=False,
        ),
        structured_output=True,
    )
    def express(
        affect: Affect,
        intensity: Annotated[float, Field(ge=0.0, le=1.0)] = 0.7,
        message: Annotated[str | None, Field(max_length=128)] = None,
        text_mode: TextMode = "static",
        sequence: Sequence | None = None,
        ttl_ms: Annotated[int, Field(ge=100, le=600000)] = 4000,
        priority: Priority = "normal",
        gaze: GazeTarget = "user",
        behavior_mode: BehaviorMode = "attentive",
        autonomy: bool = True,
        cause: Annotated[str, Field(max_length=256)] = "agent expression",
    ) -> dict[str, Any]:
        """Render an expiring affect, optional utterance, or coordinated character beat."""

        try:
            return service.express(
                affect=affect,
                intensity=intensity,
                message=message,
                text_mode=text_mode,
                sequence=sequence,
                ttl_ms=ttl_ms,
                priority=priority,
                gaze=gaze,
                behavior_mode=behavior_mode,
                autonomy=autonomy,
                cause=cause,
            )
        except (ContractError, DeviceError, OSError, ValueError) as exc:
            raise _tool_error(exc) from exc

    @server.tool(
        name="face_status",
        title="Read YouAndEye status",
        description="Read connection, active expression, eye renderer, mouth mode, frame timing, and misses.",
        annotations=ToolAnnotations(
            readOnlyHint=True,
            destructiveHint=False,
            idempotentHint=True,
            openWorldHint=False,
        ),
        structured_output=True,
    )
    def face_status() -> dict[str, Any]:
        """Return live hardware telemetry and the host's active semantic state."""

        try:
            return service.status()
        except (DeviceError, OSError, ValueError) as exc:
            raise _tool_error(exc) from exc

    @server.tool(
        name="face_capabilities",
        title="Read YouAndEye capabilities",
        description="Describe supported affects, sequences, channels, limits, and local connection state.",
        annotations=ToolAnnotations(
            readOnlyHint=True,
            destructiveHint=False,
            idempotentHint=True,
            openWorldHint=False,
        ),
        structured_output=True,
    )
    def face_capabilities() -> dict[str, Any]:
        """Return the validated emote/1 capability descriptor without opening the serial port."""

        return service.capabilities()

    @server.tool(
        name="neutral",
        title="Return YouAndEye to neutral",
        description="Clear pending expressions and restore the safe autonomous neutral face immediately.",
        annotations=ToolAnnotations(
            readOnlyHint=False,
            destructiveHint=False,
            idempotentHint=True,
            openWorldHint=False,
        ),
        structured_output=True,
    )
    def neutral() -> dict[str, Any]:
        """Cancel active host intent and restore the local neutral baseline."""

        try:
            return service.neutral()
        except (DeviceError, OSError, ValueError) as exc:
            raise _tool_error(exc) from exc

    @server.resource(
        "youandeye://interface",
        name="YouAndEye interface guide",
        description="Concise operating rules for the local expressive-face tools.",
        mime_type="text/markdown",
    )
    def interface_guide() -> str:
        return """# YouAndEye interface

- Call `express` only when the conversational state materially changes.
- Use `sequence` without `message` for device-owned attention, acknowledgement, celebration, reassurance, or error beats.
- Every expression expires; the face then returns to autonomous neutral.
- Use `face_status` for live health and `face_capabilities` before relying on an optional channel.
- Use `neutral` to clear all pending host intent immediately.
- The interface is local-only and does not provide firmware flashing, Wi-Fi credential, or raw pixel controls.
"""

    return server


_service = default_service()
mcp = create_mcp(_service)


def main() -> int:
    try:
        mcp.run(transport="stdio")
    finally:
        _service.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
