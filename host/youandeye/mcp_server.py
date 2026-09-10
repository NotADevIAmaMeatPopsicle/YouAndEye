from __future__ import annotations

import os
from typing import Annotated, Any, Literal

from mcp.server.fastmcp import FastMCP
from mcp.server.fastmcp.exceptions import ToolError
from mcp.types import ToolAnnotations
from pydantic import BaseModel, ConfigDict, Field

from .contracts import ContractError
from .device import DEFAULT_USB_SERIAL, AmoledMouthDevice, DeviceError, HeltecDevice
from .face_service import ExpressionService
from .profile_store import ProfileError, ProfileStore, default_profile_db_path

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
    "shocked",
    "weary",
    "confused",
    "blushing",
    "nervous",
    "maniacal",
    "stressed",
    "determined",
    "bored",
    "panicked",
    "scheming",
    "fatigued",
    "content",
    "pleading",
    "sick",
    "hyped",
    "baffled",
]
Sequence = Literal["attention", "acknowledge", "celebrate", "reassure", "error"]
TextMode = Literal["static", "scroll", "icon", "speech"]
Priority = Literal["ambient", "normal", "alert", "critical"]
GazeTarget = Literal["user", "away", "wander"]
BehaviorMode = Literal["idle", "attentive", "tracking", "speaking", "sleepy"]
ProfileAction = Literal["status", "create", "update", "preview", "approve", "activate", "reset"]
IrisPalette = Literal["azure", "teal", "violet", "amber", "rose", "emerald"]
Accent = Literal["cyan", "violet", "gold", "coral", "mint", "blue"]
MouthStyle = Literal["minimal", "expressive", "text_friendly"]
BlinkStyle = Literal["gentle", "natural", "lively"]
GazeStyle = Literal["soft", "attentive", "curious", "direct"]
IdleTemperament = Literal["calm", "curious", "playful", "focused"]
SignatureAffect = Literal["happy", "success", "playful", "encouraging", "reassuring"]
PerformanceAction = Literal["start", "status", "cancel"]
Pace = Literal["glance", "brief", "normal", "held", "lingering"]
GazeAversion = Literal["none", "brief", "moderate"]
ReturnPolicy = Literal["profile_neutral", "safe_neutral"]
InterruptPolicy = Literal["replace", "reject"]


class SemanticModifiers(BaseModel):
    model_config = ConfigDict(extra="forbid")

    warmth: Annotated[float, Field(ge=0.0, le=1.0)] = 0.5
    confidence: Annotated[float, Field(ge=0.0, le=1.0)] = 0.65
    urgency: Annotated[float, Field(ge=0.0, le=1.0)] = 0.35
    gaze_aversion: GazeAversion = "none"


class PerformanceBeat(BaseModel):
    model_config = ConfigDict(extra="forbid")

    affect: Affect
    intensity: Annotated[float | None, Field(ge=0.0, le=1.0)] = None
    caption: Annotated[str | None, Field(min_length=1, max_length=64)] = None
    caption_mode: Literal["auto", "static", "scroll", "icon", "speech"] = "auto"
    pace: Pace = "normal"
    sequence: Sequence | None = None
    modifiers: SemanticModifiers = Field(default_factory=SemanticModifiers)


SERVER_INSTRUCTIONS = (
    "YouandEye is a local expressive face. Use express at meaningful conversational state changes, not "
    "for every token. Let the eyes carry ordinary interaction; omit message unless words materially add "
    "clarity. Prefer device-owned sequences when no custom message is needed. Effects expire to "
    "a safe autonomous neutral baseline. After a connection error, check face_status and call neutral once "
    "connected. On first use, read youandeye://profile; if profile_required, create and preview an identity, "
    "then ask the user before approval and activation. Use perform for multi-beat scenes so the surface owns "
    "timing and returns to neutral. Never guess a serial port or expose low-level pixel controls."
)


def default_service() -> ExpressionService:
    usb_serial = os.environ.get("YOUANDEYE_USB_SERIAL", DEFAULT_USB_SERIAL)
    source_id = os.environ.get("YOUANDEYE_SOURCE_ID", "agent")
    agent_id = os.environ.get("YOUANDEYE_AGENT_ID", source_id)
    amoled_mode = os.environ.get("YOUANDEYE_AMOLED_MODE", "auto").casefold()
    mouth_device = None
    if amoled_mode not in {"off", "disabled", "0", "false"}:
        mouth_device = AmoledMouthDevice(
            port=os.environ.get("YOUANDEYE_AMOLED_PORT", "auto"),
            expected_usb_serial=os.environ.get("YOUANDEYE_AMOLED_USB_SERIAL") or None,
        )
    return ExpressionService(
        HeltecDevice(
            port=os.environ.get("YOUANDEYE_PORT", "auto"),
            baudrate=int(os.environ.get("YOUANDEYE_BAUDRATE", "115200")),
            expected_usb_serial=usb_serial or None,
        ),
        mouth_device=mouth_device,
        source_id=source_id,
        agent_id=agent_id,
        profile_store=ProfileStore(default_profile_db_path()),
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
        warmth: Annotated[float | None, Field(ge=0.0, le=1.0)] = None,
        confidence: Annotated[float | None, Field(ge=0.0, le=1.0)] = None,
        urgency: Annotated[float | None, Field(ge=0.0, le=1.0)] = None,
        gaze_aversion: GazeAversion = "none",
        cause: Annotated[str, Field(max_length=256)] = "agent expression",
    ) -> dict[str, Any]:
        """Render an expiring affect, optional utterance, or coordinated character beat."""

        try:
            modifiers = {
                key: value
                for key, value in {
                    "warmth": warmth,
                    "confidence": confidence,
                    "urgency": urgency,
                    "gaze_aversion": gaze_aversion,
                }.items()
                if value is not None and not (key == "gaze_aversion" and value == "none")
            }
            return service.express(
                affect=affect,
                intensity=intensity,
                message=message,
                text_mode=text_mode,
                sequence=sequence,
                ttl_ms=ttl_ms,
                priority=priority,
                gaze="away" if gaze_aversion != "none" else gaze,
                behavior_mode=behavior_mode,
                autonomy=autonomy,
                cause=cause,
                modifiers=modifiers,
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

    @server.tool(
        name="configure_profile",
        title="Configure the connected agent's YouAndEye identity",
        description=(
            "Create, revise, preview, approve, activate, inspect, or reset the profile bound to the "
            "stable local agent identity. Preview always shows neutral, listening, thinking, and success."
        ),
        annotations=ToolAnnotations(
            readOnlyHint=False,
            destructiveHint=True,
            idempotentHint=False,
            openWorldHint=False,
        ),
        structured_output=True,
    )
    def configure_profile(
        action: ProfileAction,
        iris_palette: IrisPalette | None = None,
        accent: Accent | None = None,
        default_energy: Annotated[float | None, Field(ge=0.25, le=0.85)] = None,
        blink_style: BlinkStyle | None = None,
        gaze_style: GazeStyle | None = None,
        idle_temperament: IdleTemperament | None = None,
        mouth_style: MouthStyle | None = None,
        signature_affect: SignatureAffect | None = None,
        signature_message: Annotated[str | None, Field(min_length=1, max_length=24)] = None,
        signature_text_mode: Literal["static", "scroll"] | None = None,
    ) -> dict[str, Any]:
        """Manage the approval-gated, locally persisted profile for this agent identity."""

        appearance = {
            key: value
            for key, value in {
                "iris_palette": iris_palette,
                "accent": accent,
                "mouth_style": mouth_style,
            }.items()
            if value is not None
        }
        temperament = {
            key: value
            for key, value in {
                "default_energy": default_energy,
                "blink_style": blink_style,
                "gaze_style": gaze_style,
                "idle_temperament": idle_temperament,
            }.items()
            if value is not None
        }
        signature = {
            key: value
            for key, value in {
                "affect": signature_affect,
                "message": signature_message,
                "text_mode": signature_text_mode,
            }.items()
            if value is not None
        }
        changes = {
            key: value
            for key, value in {
                "appearance": appearance,
                "temperament": temperament,
                "signature": signature,
            }.items()
            if value
        }
        try:
            return service.configure_profile(action, changes=changes or None)
        except (ContractError, ProfileError, DeviceError, OSError, ValueError) as exc:
            raise _tool_error(exc) from exc

    @server.tool(
        name="perform",
        title="Perform a semantic scene",
        description=(
            "Start, inspect, or cancel a bounded sequence of emotional beats. Supply pacing words and "
            "semantic modifiers; YouAndEye owns timing, transitions, scrolling completion, and neutral return."
        ),
        annotations=ToolAnnotations(
            readOnlyHint=False,
            destructiveHint=False,
            idempotentHint=False,
            openWorldHint=False,
        ),
        structured_output=True,
    )
    def perform(
        action: PerformanceAction = "start",
        beats: Annotated[list[PerformanceBeat] | None, Field(max_length=16)] = None,
        title: Annotated[str, Field(max_length=80)] = "",
        return_policy: ReturnPolicy = "profile_neutral",
        interrupt_policy: InterruptPolicy = "replace",
        wait_for_completion: bool = True,
        wait_timeout_ms: Annotated[int, Field(ge=0, le=1200000)] = 0,
    ) -> dict[str, Any]:
        """Run semantic beats without exposing pixels, coordinates, or keyframes."""

        try:
            if action == "status":
                return {"ok": True, **service.performance_status()}
            if action == "cancel":
                return service.cancel_performance()
            if not beats:
                raise ContractError("perform(action='start') requires 1-16 beats")
            return service.start_performance(
                [beat.model_dump(exclude_none=True) for beat in beats],
                title=title,
                return_policy=return_policy,
                interrupt_policy=interrupt_policy,
                wait_for_completion=wait_for_completion,
                wait_timeout_ms=wait_timeout_ms,
            )
        except (ContractError, DeviceError, OSError, ValueError) as exc:
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
- On first use, read `youandeye://profile`. If it reports `profile_required`, create a profile, run its preview, and ask the user before approval and activation.
- Use `perform` for a short story or demonstration. It owns pacing, interruption, scrolling completion, and restoration to neutral.
- The interface is local-only and does not provide firmware flashing, Wi-Fi credential, or raw pixel controls.
"""

    @server.resource(
        "youandeye://profile",
        name="YouAndEye profile status",
        description="Readable first-run, approval, and active identity status for this agent.",
        mime_type="text/markdown",
    )
    def profile_status_resource() -> str:
        return service.profile_status_markdown()

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
