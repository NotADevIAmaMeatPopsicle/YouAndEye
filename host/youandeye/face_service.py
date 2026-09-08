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
from .performance import PACE_MS, beat_duration_ms, beat_expression, build_performance, caption_mode
from .profile_store import (
    DEFAULT_PROFILE_CHOICES,
    IRIS_PALETTES,
    MemoryProfileStore,
    ProfileError,
    ProfileRepository,
    build_profile,
    profile_visuals,
    validate_agent_id,
    with_lifecycle,
)

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "protocol" / "examples"
AFFECTS = tuple(AFFECT_CENTERS)
BEHAVIOR_MODES = ("idle", "attentive", "tracking", "speaking", "sleepy")
GAZE_TARGETS = ("user", "away", "wander")
PRIORITIES = ("ambient", "normal", "alert", "critical")
SEQUENCES = ("attention", "acknowledge", "celebrate", "reassure", "error")
TEXT_MODES = ("static", "scroll", "icon", "speech")
PROFILE_ACTIONS = ("status", "create", "update", "preview", "approve", "activate", "reset")
PERFORMANCE_ACTIONS = ("start", "status", "cancel")
_AUTO_PROFILE = object()


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
        agent_id: str | None = None,
        profile_store: ProfileRepository | None = None,
        session_id: str | None = None,
        monotonic_ms: Callable[[], int] | None = None,
        schedule_expiry: bool = True,
        performance_time_scale: float = 1.0,
    ) -> None:
        self.device = device
        self.bridge = bridge or heltec_bridge()
        self.source_id = validate_agent_id(source_id)
        self.agent_id = validate_agent_id(agent_id or source_id)
        self.profile_store = profile_store or MemoryProfileStore()
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
        self._applied_profile_key: tuple[str, int] | None = None
        self._applied_connection_generation: int | None = None
        self._performance_time_scale = max(0.0, performance_time_scale)
        self._performance_thread: threading.Thread | None = None
        self._performance_cancel: threading.Event | None = None
        self._performance_done: threading.Event | None = None
        self._performance_restore: threading.Event | None = None
        self._performance_terminal: dict[str, str] | None = None
        self._performance_state: dict[str, Any] = {
            "status": "idle",
            "performance_id": None,
            "title": "",
            "current_beat": None,
            "total_beats": 0,
            "error": None,
        }
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
        modifiers: Mapping[str, Any] | None = None,
        profile_override: Mapping[str, Any] | None = None,
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

        profile = profile_override or self._active_profile(self.agent_id)
        profile_energy = (
            float(profile["temperament"]["default_energy"])
            if profile is not None
            else arousal
        )
        modifiers = dict(modifiers or {})
        unknown_modifiers = set(modifiers) - {
            "warmth",
            "confidence",
            "urgency",
            "gaze_aversion",
        }
        if unknown_modifiers:
            raise ContractError(
                f"unsupported semantic modifiers: {', '.join(sorted(unknown_modifiers))}"
            )
        for name in ("warmth", "confidence", "urgency"):
            if name in modifiers:
                value = modifiers[name]
                if not isinstance(value, (int, float)) or isinstance(value, bool) or not 0 <= value <= 1:
                    raise ContractError(f"modifier {name!r} must be between 0 and 1")
        gaze_aversion = modifiers.get("gaze_aversion", "none")
        if gaze_aversion not in {"none", "brief", "moderate"}:
            raise ContractError("gaze_aversion must be none, brief, or moderate")
        urgency = float(modifiers.get("urgency", 0.0))
        confidence = float(modifiers.get("confidence", intensity))
        energy = min(1.0, max(0.0, (arousal + profile_energy) / 2.0 + urgency * 0.15))
        engagement = min(1.0, max(0.0, (intensity + confidence) / 2.0))

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
                "energy": energy,
                "engagement": engagement,
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
        checked = validate_message(frame, "frame")
        return self._trusted_context(checked, profile, modifiers)

    @staticmethod
    def _fingerprint(
        frame: Mapping[str, Any], phase: str
    ) -> tuple[str, str, int] | tuple[str]:
        if phase == "baseline":
            return ("baseline",)
        source = frame["source"]
        return source["id"], source["session"], frame["seq"]

    def _active_profile(self, agent_id: str | None = None) -> dict[str, Any] | None:
        return self.profile_store.get_active(agent_id or self.agent_id)

    def profile_status(self) -> dict[str, Any]:
        profile = self.profile_store.get(self.agent_id)
        active_profile = self.profile_store.get_active(self.agent_id)
        if profile is None:
            return {
                "status": "profile_required",
                "agent_id": self.agent_id,
                "profile_required": True,
                "using_safe_default": True,
                "safe_default": deepcopy(DEFAULT_PROFILE_CHOICES),
                "storage": "local_sqlite_or_injected_store",
            }
        return {
            "status": profile["lifecycle"],
            "agent_id": self.agent_id,
            "profile_required": False,
            "using_safe_default": active_profile is None,
            "profile": profile,
            "active_profile": active_profile,
            "storage": "local_sqlite_or_injected_store",
        }

    def profile_status_markdown(self) -> str:
        status = self.profile_status()
        lines = [
            "# YouAndEye profile",
            "",
            f"- Agent identity: `{status['agent_id']}`",
            f"- State: `{status['status']}`",
            f"- Safe default active: `{str(status['using_safe_default']).lower()}`",
        ]
        profile = status.get("profile")
        if profile:
            lines.extend(
                [
                    f"- Revision: `{profile['revision']}`",
                    f"- Iris: `{profile['appearance']['iris_palette']}`",
                    f"- Accent: `{profile['appearance']['accent']}`",
                    f"- Mouth: `{profile['appearance']['mouth_style']}`",
                    f"- Blink: `{profile['temperament']['blink_style']}`",
                    f"- Gaze: `{profile['temperament']['gaze_style']}`",
                    f"- Idle temperament: `{profile['temperament']['idle_temperament']}`",
                    f"- Signature: `{profile['signature']['message']}`",
                ]
            )
        else:
            lines.extend(
                [
                    "",
                    "Create a profile, preview neutral/listening/thinking/success, then ask the user to approve it.",
                    "Existing expression calls remain available through the safe default while setup is pending.",
                ]
            )
        return "\n".join(lines) + "\n"

    @staticmethod
    def _trusted_context(
        frame: Mapping[str, Any],
        profile: Mapping[str, Any] | None,
        modifiers: Mapping[str, Any] | None,
    ) -> dict[str, Any]:
        changed = deepcopy(dict(frame))
        extensions = deepcopy(changed.get("extensions", {}))
        extensions.pop("youandeye", None)
        context: dict[str, Any] = {}
        if profile is not None:
            context["profile"] = profile_visuals(profile)
        if modifiers:
            context["modifiers"] = deepcopy(dict(modifiers))
        if context:
            extensions["youandeye"] = context
        if extensions:
            changed["extensions"] = extensions
        else:
            changed.pop("extensions", None)
        return validate_message(changed, "frame")

    @staticmethod
    def _context_profile(frame: Mapping[str, Any]) -> Mapping[str, Any] | None:
        extensions = frame.get("extensions")
        if not isinstance(extensions, Mapping):
            return None
        context = extensions.get("youandeye")
        if not isinstance(context, Mapping):
            return None
        profile = context.get("profile")
        return profile if isinstance(profile, Mapping) else None

    def _profile_commands(
        self,
        frame: Mapping[str, Any],
        phase: str,
        profile_override: Mapping[str, Any] | None | object = _AUTO_PROFILE,
    ) -> tuple[list[str], tuple[str, int] | None]:
        profile: Mapping[str, Any] | None
        if profile_override is _AUTO_PROFILE:
            embedded = self._context_profile(frame)
            if embedded is not None:
                profile = embedded
            else:
                source_id = self.agent_id if phase == "baseline" else frame["source"]["id"]
                profile = self._active_profile(source_id)
        else:
            profile = profile_override  # type: ignore[assignment]

        if profile is None:
            if self._applied_profile_key is None:
                return [], None
            rgb = IRIS_PALETTES["azure"]
            return [
                "PROFILE NATURAL ATTENTIVE CALM EXPRESSIVE 0.50",
                f"IRIS {rgb[0]} {rgb[1]} {rgb[2]}",
            ], None

        if "appearance" in profile:
            checked = validate_message(profile, "profile")
            key = (checked["agent_id"], checked["revision"])
            appearance = checked["appearance"]
            temperament = checked["temperament"]
        else:
            # A trusted, reduced profile context embedded into a frame.
            agent_id = str(profile.get("agent_id", self.agent_id))
            revision = int(profile.get("revision", 0))
            key = (agent_id, revision)
            appearance = {
                "iris_palette": profile.get("iris_palette", "azure"),
                "mouth_style": profile.get("mouth_style", "expressive"),
            }
            temperament = {
                "default_energy": profile.get("default_energy", 0.5),
                "blink_style": profile.get("blink_style", "natural"),
                "gaze_style": profile.get("gaze_style", "attentive"),
                "idle_temperament": profile.get("idle_temperament", "calm"),
            }

        generation = getattr(self.device, "connection_generation", None)
        if key == self._applied_profile_key and generation == self._applied_connection_generation:
            return [], key
        iris_name = str(appearance["iris_palette"])
        if iris_name not in IRIS_PALETTES:
            iris_name = "azure"
        rgb = IRIS_PALETTES[iris_name]
        profile_command = "PROFILE {} {} {} {} {:.2f}".format(
            str(temperament["blink_style"]).upper(),
            str(temperament["gaze_style"]).upper(),
            str(temperament["idle_temperament"]).upper(),
            str(appearance["mouth_style"]).upper(),
            float(temperament["default_energy"]),
        )
        return [profile_command, f"IRIS {rgb[0]} {rgb[1]} {rgb[2]}"], key

    def _wait_for_beat(self, beat: Mapping[str, Any], cancel: threading.Event) -> bool:
        """Wait for a semantic beat, preferring physical OLED completion feedback."""

        scale = self._performance_time_scale
        if scale <= 0:
            return cancel.is_set()

        maximum_s = beat_duration_ms(beat) * scale / 1000.0
        caption = str(beat.get("caption", ""))
        if not caption or caption_mode(beat) != "scroll":
            return cancel.wait(maximum_s)

        minimum_s = PACE_MS[str(beat["pace"])] * scale / 1000.0
        started = time.monotonic()
        feedback_supported = False
        while True:
            elapsed = time.monotonic() - started
            if elapsed >= maximum_s:
                return cancel.is_set()
            if cancel.wait(min(0.1, maximum_s - elapsed)):
                return True
            try:
                mouth = self.device.status().get("mouth", {})
            except DeviceError:
                continue
            if "scrollComplete" in mouth:
                feedback_supported = True
            complete = mouth.get("scrollComplete") in {1, "1", True, "true"}
            if feedback_supported and complete and time.monotonic() - started >= minimum_s:
                return False

    def _sync(
        self,
        now_ms: int,
        *,
        profile_override: Mapping[str, Any] | None | object = _AUTO_PROFILE,
    ) -> dict[str, Any]:
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
                profile_commands, profile_key = self._profile_commands(
                    frame, state["phase"], profile_override
                )
                commands = profile_commands + commands_for_frame(frame)

            receipt = self.device.send_commands(commands)

            with self._lock:
                self._last_dispatched = fingerprint
                self._applied_profile_key = profile_key
                self._applied_connection_generation = getattr(
                    self.device, "connection_generation", None
                )
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

    def submit_frame(
        self,
        frame: Mapping[str, Any],
        *,
        _profile_override: Mapping[str, Any] | None | object = _AUTO_PROFILE,
        _modifiers: Mapping[str, Any] | None = None,
    ) -> dict[str, Any]:
        checked = validate_message(frame, "frame")
        requested_surface = checked.get("surface_id")
        actual_surface = self.bridge.capabilities["surface"]["id"]
        if requested_surface is not None and requested_surface != actual_surface:
            raise ContractError(
                f"frame targets surface {requested_surface!r}; this service owns {actual_surface!r}"
            )
        if _profile_override is _AUTO_PROFILE:
            profile = self._active_profile(checked["source"]["id"])
        else:
            profile = _profile_override
        checked = self._trusted_context(checked, profile, _modifiers)  # type: ignore[arg-type]
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
        from_performance = bool(kwargs.pop("_from_performance", False))
        profile_override = kwargs.pop("_profile_override", _AUTO_PROFILE)
        modifiers = kwargs.get("modifiers")
        if not from_performance:
            self.cancel_performance(reason="interrupted_by_expression", restore=False)
        if profile_override is _AUTO_PROFILE:
            frame_profile = self._active_profile(self.agent_id)
        else:
            frame_profile = profile_override
        frame = self.build_frame(**kwargs, profile_override=frame_profile)
        receipt = self.submit_frame(
            frame,
            _profile_override=frame_profile,
            _modifiers=modifiers,
        )
        receipt["frame"] = frame
        return receipt

    def performance_status(self) -> dict[str, Any]:
        with self._lock:
            return deepcopy(self._performance_state)

    def _set_performance_state(self, performance_id: str, **changes: Any) -> None:
        with self._lock:
            if self._performance_state.get("performance_id") != performance_id:
                return
            self._performance_state.update(changes)

    def _restore_neutral(self, *, use_profile: bool = True) -> dict[str, Any]:
        with self._lock:
            self._expiry_deadline_ms = None
            self._expiry_condition.notify_all()
            self.bridge = SurfaceBridge(self.bridge.capabilities, self.bridge.baseline)
            self._last_dispatched = None
            now_ms = self._monotonic_ms()
        profile: Mapping[str, Any] | None | object = (
            _AUTO_PROFILE if use_profile else None
        )
        delivery = self._sync(now_ms, profile_override=profile)
        return {
            "ok": True,
            "active_affect": "neutral",
            "phase": "baseline",
            "profile": self.profile_status()["status"] if use_profile else "safe_default",
            "delivery": delivery,
        }

    def _performance_worker(
        self,
        document: Mapping[str, Any],
        cancel: threading.Event,
        done: threading.Event,
        restore: threading.Event,
        terminal: dict[str, str],
        profile_override: Mapping[str, Any] | None,
    ) -> None:
        performance_id = str(document["performance_id"])
        terminal_status = "completed"
        error: str | None = None
        try:
            beats = document["beats"]
            for index, beat in enumerate(beats):
                if cancel.is_set():
                    terminal_status = terminal["status"]
                    break
                self._set_performance_state(
                    performance_id,
                    status="running",
                    current_beat=index,
                    current_affect=beat["affect"],
                )
                expression = beat_expression(beat)
                self.express(
                    **expression,
                    _from_performance=True,
                    _profile_override=profile_override or _AUTO_PROFILE,
                )
                if self._wait_for_beat(beat, cancel):
                    terminal_status = terminal["status"]
                    break
        except Exception as exc:  # The status surface carries a bounded failure reason.
            terminal_status = "failed"
            error = f"{exc.__class__.__name__}: {exc}"[:256]
        finally:
            try:
                if restore.is_set():
                    use_profile = document["return_policy"] == "profile_neutral"
                    self._restore_neutral(use_profile=use_profile)
            except Exception as exc:
                terminal_status = "failed"
                error = f"neutral restoration failed: {exc}"[:256]
            self._set_performance_state(
                performance_id,
                status=terminal_status,
                current_beat=None,
                current_affect="neutral",
                completed_at_ms=self._monotonic_ms(),
                error=error,
            )
            done.set()

    def start_performance(
        self,
        beats: list[Mapping[str, Any]],
        *,
        title: str = "",
        return_policy: str = "profile_neutral",
        interrupt_policy: str = "replace",
        wait_for_completion: bool = True,
        wait_timeout_ms: int = 0,
        profile_override: Mapping[str, Any] | None = None,
    ) -> dict[str, Any]:
        document = build_performance(
            self.source_id,
            self.session_id,
            beats,
            title=title,
            return_policy=return_policy,
            interrupt_policy=interrupt_policy,
        )
        active = self.performance_status()
        if active["status"] in {"starting", "running"}:
            if interrupt_policy == "reject":
                raise ContractError("a semantic performance is already running")
            self.cancel_performance(reason="replaced", restore=False)

        cancel = threading.Event()
        done = threading.Event()
        restore = threading.Event()
        restore.set()
        terminal = {"status": "cancelled"}
        performance_id = document["performance_id"]
        with self._lock:
            self._performance_cancel = cancel
            self._performance_done = done
            self._performance_restore = restore
            self._performance_terminal = terminal
            self._performance_state = {
                "status": "starting",
                "performance_id": performance_id,
                "title": document.get("title", ""),
                "current_beat": None,
                "current_affect": None,
                "total_beats": len(document["beats"]),
                "return_policy": document["return_policy"],
                "error": None,
                "started_at_ms": self._monotonic_ms(),
                "completed_at_ms": None,
            }
            worker = threading.Thread(
                target=self._performance_worker,
                args=(document, cancel, done, restore, terminal, profile_override),
                name=f"youandeye-performance-{performance_id[-8:]}",
                daemon=True,
            )
            self._performance_thread = worker
            worker.start()

        if wait_for_completion:
            timeout_ms = wait_timeout_ms or (
                sum(beat_duration_ms(beat) for beat in document["beats"]) + 10_000
            )
            if not done.wait(timeout_ms / 1000.0):
                self.cancel_performance(reason="timed_out", restore=True)
        state = self.performance_status()
        return {"ok": state["status"] not in {"failed", "timed_out"}, **state}

    def cancel_performance(
        self,
        *,
        reason: str = "cancelled",
        restore: bool = True,
    ) -> dict[str, Any]:
        with self._lock:
            thread = self._performance_thread
            cancel = self._performance_cancel
            restore_event = self._performance_restore
            terminal = self._performance_terminal
            performance_id = self._performance_state.get("performance_id")
            running = self._performance_state.get("status") in {"starting", "running"}
            if running and cancel is not None:
                if terminal is not None:
                    terminal["status"] = (
                        "timed_out" if reason == "timed_out" else "cancelled"
                    )
                if restore_event is not None:
                    if restore:
                        restore_event.set()
                    else:
                        restore_event.clear()
                cancel.set()
                self._performance_state["status"] = reason
        if thread is not None and thread is not threading.current_thread() and running:
            thread.join(timeout=2.0)
        if restore and running and thread is None:
            self._restore_neutral(use_profile=True)
        state = self.performance_status()
        return {
            "ok": True,
            "cancelled": bool(running),
            "performance_id": performance_id,
            "performance": state,
        }

    def configure_profile(
        self,
        action: str,
        *,
        changes: Mapping[str, Any] | None = None,
        wait_for_preview: bool = True,
    ) -> dict[str, Any]:
        if action not in PROFILE_ACTIONS:
            raise ProfileError(f"unsupported profile action: {action!r}")
        current = self.profile_store.get(self.agent_id)
        if action == "status":
            return {"ok": True, **self.profile_status()}
        if action == "reset":
            self.cancel_performance(reason="profile_reset", restore=False)
            removed = self.profile_store.delete(self.agent_id)
            neutral = self._restore_neutral(use_profile=False)
            return {"ok": True, "reset": removed, **self.profile_status(), "neutral": neutral}
        if action == "create":
            if current is not None:
                raise ProfileError("profile already exists; use update or reset")
            profile = build_profile(self.agent_id, changes=changes, lifecycle="draft")
            self.profile_store.put(profile)
            return {"ok": True, **self.profile_status(), "next_action": "preview"}
        if current is None:
            raise ProfileError("profile_required: create a profile first")
        if action == "update":
            profile = build_profile(
                self.agent_id,
                changes=changes,
                existing=current,
                lifecycle="draft",
            )
            self.profile_store.put(profile)
            return {"ok": True, **self.profile_status(), "next_action": "preview"}
        if changes:
            raise ProfileError(f"profile fields are not accepted for action {action!r}")
        if action == "preview":
            preview = self.start_performance(
                [
                    {"affect": "neutral", "pace": "brief"},
                    {"affect": "listening", "pace": "brief"},
                    {"affect": "thinking", "pace": "brief"},
                    {"affect": "success", "pace": "brief"},
                ],
                title="Identity preview",
                wait_for_completion=wait_for_preview,
                profile_override=current,
            )
            if preview["status"] == "completed":
                self.profile_store.put(with_lifecycle(current, "previewed"))
            return {
                "ok": preview["status"] != "failed",
                **self.profile_status(),
                "preview": preview,
                "next_action": "approve_or_update",
            }
        if action == "approve":
            if current["lifecycle"] != "previewed":
                raise ProfileError("profile must complete preview before approval")
            self.profile_store.put(with_lifecycle(current, "approved"))
            return {"ok": True, **self.profile_status(), "next_action": "activate"}
        if action == "activate":
            if current["lifecycle"] not in {"approved", "active"}:
                raise ProfileError("profile must be approved before activation")
            profile = current if current["lifecycle"] == "active" else with_lifecycle(current, "active")
            self.profile_store.put(profile)
            self._last_dispatched = None
            signature = profile["signature"]
            acknowledgement = self.start_performance(
                [
                    {
                        "affect": signature["affect"],
                        "caption": signature["message"],
                        "caption_mode": signature["text_mode"],
                        "pace": "brief",
                        "modifiers": {"warmth": 0.8, "confidence": 0.8},
                    }
                ],
                title="Profile activation",
                wait_for_completion=True,
                profile_override=profile,
            )
            return {
                "ok": acknowledgement["status"] == "completed",
                **self.profile_status(),
                "acknowledgement": acknowledgement,
                "next_action": "ready",
            }
        raise AssertionError("unreachable profile action")

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
        cancelled = self.cancel_performance(reason="cancelled_by_neutral", restore=False)
        result = self._restore_neutral(use_profile=True)
        result["performance_cancelled"] = cancelled["cancelled"]
        return result

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
            "profile": self.profile_status(),
            "performance": self.performance_status(),
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
                "profiles": "youandeye.profile/1",
                "performances": "youandeye.performance/1",
            },
            "profile": self.profile_status(),
            "performance": {
                "max_beats": 16,
                "pacing": ["glance", "brief", "normal", "held", "lingering"],
                "modifiers": ["warmth", "confidence", "urgency", "gaze_aversion"],
                "return_policies": ["profile_neutral", "safe_neutral"],
                "cancellation": ["perform(action='cancel')", "neutral()", "express(...)"],
            },
        }

    def close(self) -> None:
        self.cancel_performance(reason="service_closed", restore=False)
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
