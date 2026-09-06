from __future__ import annotations

import json
import threading
import time
import uuid
from collections.abc import Callable, Mapping
from copy import deepcopy
from pathlib import Path
from typing import Any

from .bridge import SurfaceBridge
from .contracts import ContractError, validate_message
from .device import DeviceError, HeltecDevice
from .downmix import AFFECT_CENTERS
from .heltec_transport import commands_for_frame

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "protocol" / "examples"
AFFECTS = tuple(AFFECT_CENTERS)
BEHAVIOR_MODES = ("idle", "attentive", "tracking", "speaking", "sleepy")
GAZE_TARGETS = ("user", "away", "wander")
PRIORITIES = ("ambient", "normal", "alert", "critical")
SEQUENCES = ("attention", "acknowledge", "celebrate", "reassure", "error")
TEXT_MODES = ("static", "scroll", "icon", "speech")


def _load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def heltec_bridge() -> SurfaceBridge:
    return SurfaceBridge(
        _load_json(EXAMPLES / "capabilities-heltec-face.json"),
        _load_json(EXAMPLES / "frame-heltec-baseline.json"),
    )


class ExpressionService:
    """Own arbitration, expiry, and delivery for one local YouandEye face."""

    def __init__(
        self,
        device: HeltecDevice,
        *,
        bridge: SurfaceBridge | None = None,
        source_id: str = "agent",
        session_id: str | None = None,
        monotonic_ms: Callable[[], int] | None = None,
        schedule_expiry: bool = True,
    ) -> None:
        self.device = device
        self.bridge = bridge or heltec_bridge()
        self.source_id = source_id
        self.session_id = session_id or f"mcp-{uuid.uuid4().hex[:16]}"
        self._monotonic_ms = monotonic_ms or (lambda: time.monotonic_ns() // 1_000_000)
        self._schedule_expiry = schedule_expiry
        self._seq = 0
        self._lock = threading.RLock()
        self._dispatch_lock = threading.Lock()
        self._expiry_condition = threading.Condition(self._lock)
        self._expiry_deadline_ms: int | None = None
        self._closed = False
        self._expiry_thread: threading.Thread | None = None
        self._last_dispatched: tuple[str, str, int] | tuple[str] | None = None
        if self._schedule_expiry:
            self._expiry_thread = threading.Thread(
                target=self._expiry_loop,
                name="youandeye-expiry",
                daemon=True,
            )
            self._expiry_thread.start()

    def _next_seq(self) -> int:
        with self._lock:
            self._seq += 1
            return self._seq

    def build_frame(
        self,
        *,
        affect: str,
        intensity: float = 0.7,
        message: str | None = None,
        text_mode: str = "static",
        sequence: str | None = None,
        ttl_ms: int = 4000,
        priority: str = "normal",
        gaze: str = "user",
        behavior_mode: str = "attentive",
        autonomy: bool = True,
        cause: str = "agent expression",
    ) -> dict[str, Any]:
        if affect not in AFFECTS:
            raise ContractError(f"unsupported affect: {affect!r}")
        if text_mode not in TEXT_MODES:
            raise ContractError(f"unsupported text mode: {text_mode!r}")
        if sequence is not None and sequence not in SEQUENCES:
            raise ContractError(f"unsupported sequence: {sequence!r}")
        if gaze not in GAZE_TARGETS:
            raise ContractError(f"unsupported gaze target: {gaze!r}")
        if behavior_mode not in BEHAVIOR_MODES:
            raise ContractError(f"unsupported behavior mode: {behavior_mode!r}")
        if priority not in PRIORITIES:
            raise ContractError(f"unsupported priority: {priority!r}")
        if sequence is not None and message:
            raise ContractError(
                "message and sequence are mutually exclusive; omit message to use device-owned choreography"
            )

        valence, arousal = AFFECT_CENTERS[affect]
        if message:
            utterance: dict[str, Any]
            if text_mode == "icon":
                utterance = {"mode": "icon", "icon": message.lower().replace(" ", "_")}
            else:
                utterance = {"mode": text_mode, "text": message, "sound": "none"}
        else:
            utterance = {"mode": "none", "sound": "none"}

        frame: dict[str, Any] = {
            "protocol": "emote/1",
            "kind": "frame",
            "seq": self._next_seq(),
            "source": {"id": self.source_id, "session": self.session_id},
            "surface_id": self.bridge.capabilities["surface"]["id"],
            "affect": {
                "state": affect,
                "intensity": intensity,
                "valence": valence,
                "arousal": arousal,
            },
            "behavior": {
                "mode": behavior_mode,
                "autonomy": autonomy,
                "energy": min(1.0, max(0.0, arousal)),
                "engagement": min(1.0, max(0.0, intensity)),
                "speaking": 1.0 if affect == "speaking" else 0.0,
            },
            "gaze": {"target": gaze},
            "utterance": utterance,
            "channel_policy": {"eyes": "render", "utterance": "render"},
            "ttl_ms": ttl_ms,
            "decay": {"curve": "ease_out", "duration_ms": 600},
            "priority": priority,
            "cause": cause[:256],
        }
        if sequence is not None:
            frame["sequence"] = sequence
        return validate_message(frame, "frame")

    @staticmethod
    def _fingerprint(
        frame: Mapping[str, Any], phase: str
    ) -> tuple[str, str, int] | tuple[str]:
        if phase == "baseline":
            return ("baseline",)
        source = frame["source"]
        return source["id"], source["session"], frame["seq"]

    def _sync(self, now_ms: int) -> dict[str, Any]:
        """Serialize hardware writes without holding the semantic-state lock."""

        with self._dispatch_lock:
            with self._lock:
                state = self.bridge.state(now_ms)
                frame = state["frame"]
                fingerprint = self._fingerprint(frame, state["phase"])
                if fingerprint == self._last_dispatched and getattr(
                    self.device, "connected", True
                ):
                    return {"sent": False, "reason": "already active"}
                commands = commands_for_frame(frame)

            receipt = self.device.send_commands(commands)

            with self._lock:
                self._last_dispatched = fingerprint
            return {"sent": True, **receipt}

    def _schedule(self, now_ms: int) -> None:
        if not self._schedule_expiry:
            return
        self._expiry_deadline_ms = self.bridge.next_completion_ms(now_ms)
        self._expiry_condition.notify_all()

    def _expiry_loop(self) -> None:
        while True:
            with self._expiry_condition:
                while not self._closed:
                    deadline = self._expiry_deadline_ms
                    if deadline is None:
                        self._expiry_condition.wait()
                        continue
                    now_ms = self._monotonic_ms()
                    remaining_s = (deadline - now_ms) / 1000.0
                    if remaining_s > 0:
                        self._expiry_condition.wait(timeout=remaining_s)
                        continue
                    self._expiry_deadline_ms = None
                    break
                else:
                    return

            try:
                self._sync(now_ms)
            except DeviceError:
                # A later tool call retries; firmware keeps its safe local autonomy.
                pass

            with self._expiry_condition:
                if self._closed:
                    return
                self._expiry_deadline_ms = self.bridge.next_completion_ms(
                    self._monotonic_ms()
                )

    def submit_frame(self, frame: Mapping[str, Any]) -> dict[str, Any]:
        checked = validate_message(frame, "frame")
        requested_surface = checked.get("surface_id")
        actual_surface = self.bridge.capabilities["surface"]["id"]
        if requested_surface is not None and requested_surface != actual_surface:
            raise ContractError(
                f"frame targets surface {requested_surface!r}; this service owns {actual_surface!r}"
            )
        with self._lock:
            now_ms = self._monotonic_ms()
            result = self.bridge.submit(checked, now_ms)
            if result.accepted:
                self._schedule(now_ms)
            active = self.bridge.state(now_ms)
        delivery = self._sync(now_ms) if result.accepted else {"sent": False}
        return {
            "ok": result.accepted,
            "activated": result.activated,
            "reason": result.reason,
            "active_source": result.active_source,
            "active_session": result.active_session,
            "active_affect": active["frame"]["affect"]["state"],
            "phase": active["phase"],
            "delivery": delivery,
        }

    def express(self, **kwargs: Any) -> dict[str, Any]:
        frame = self.build_frame(**kwargs)
        receipt = self.submit_frame(frame)
        receipt["frame"] = frame
        return receipt

    def reconcile(self) -> dict[str, Any]:
        with self._lock:
            now_ms = self._monotonic_ms()
            state = self.bridge.state(now_ms)
        delivery = self._sync(now_ms)
        return {
            "phase": state["phase"],
            "active_affect": state["frame"]["affect"]["state"],
            "delivery": delivery,
        }

    def neutral(self) -> dict[str, Any]:
        with self._lock:
            self._expiry_deadline_ms = None
            self._expiry_condition.notify_all()
            self.bridge = SurfaceBridge(self.bridge.capabilities, self.bridge.baseline)
            self._last_dispatched = None
            now_ms = self._monotonic_ms()
        delivery = self._sync(now_ms)
        return {
            "ok": True,
            "active_affect": "neutral",
            "phase": "baseline",
            "delivery": delivery,
        }

    def status(self) -> dict[str, Any]:
        with self._lock:
            now_ms = self._monotonic_ms()
            state = self.bridge.state(now_ms)
        device_status = self.device.status()
        return {
            "ok": True,
            "surface_id": self.bridge.capabilities["surface"]["id"],
            "phase": state["phase"],
            "active_affect": state["frame"]["affect"]["state"],
            "active_source": state["frame"]["source"]["id"],
            "expires_at_ms": state["expires_at_ms"],
            **device_status,
        }

    def capabilities(self) -> dict[str, Any]:
        return {
            "capabilities": deepcopy(self.bridge.capabilities),
            "connection": self.device.describe(),
            "interface": {
                "agent": "mcp-stdio",
                "contract": "emote/1",
                "hardware": ["serial"],
            },
        }

    def close(self) -> None:
        expiry_thread = self._expiry_thread
        with self._expiry_condition:
            self._closed = True
            self._expiry_deadline_ms = None
            self._expiry_condition.notify_all()
        self.device.close()
        if expiry_thread is not None and expiry_thread is not threading.current_thread():
            expiry_thread.join(timeout=1.0)


__all__ = [
    "AFFECTS",
    "BEHAVIOR_MODES",
    "GAZE_TARGETS",
    "PRIORITIES",
    "SEQUENCES",
    "TEXT_MODES",
    "ExpressionService",
    "heltec_bridge",
]
