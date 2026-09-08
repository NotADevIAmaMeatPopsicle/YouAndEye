from __future__ import annotations

import uuid
from copy import deepcopy
from typing import Any, Mapping, Sequence

from .contracts import validate_message
from .profile_store import validate_agent_id


PERFORMANCE_PROTOCOL = "youandeye.performance/1"
PACE_MS = {
    "glance": 700,
    "brief": 1200,
    "normal": 2200,
    "held": 3600,
    "lingering": 5200,
}


def build_performance(
    source_id: str,
    session_id: str,
    beats: Sequence[Mapping[str, Any]],
    *,
    title: str = "",
    return_policy: str = "profile_neutral",
    interrupt_policy: str = "replace",
    performance_id: str | None = None,
) -> dict[str, Any]:
    source_id = validate_agent_id(source_id)
    document: dict[str, Any] = {
        "protocol": PERFORMANCE_PROTOCOL,
        "kind": "performance",
        "performance_id": performance_id or f"scene-{uuid.uuid4().hex[:16]}",
        "source": {"id": source_id, "session": session_id},
        "beats": [deepcopy(dict(beat)) for beat in beats],
        "return_policy": return_policy,
        "interrupt_policy": interrupt_policy,
    }
    if title:
        document["title"] = title
    return validate_message(document, "performance")


def caption_mode(beat: Mapping[str, Any]) -> str:
    requested = beat.get("caption_mode", "auto")
    if requested != "auto":
        return str(requested)
    caption = str(beat.get("caption", ""))
    return "scroll" if len(caption) > 8 else "static"


def beat_duration_ms(beat: Mapping[str, Any]) -> int:
    duration = PACE_MS[str(beat["pace"])]
    caption = str(beat.get("caption", ""))
    if caption and caption_mode(beat) == "scroll":
        # Match the physical OLED's bounded software scroll: each character can
        # occupy six logical columns, each logical column is three pixels wide,
        # and the viewport advances two pixels every 90 ms. The 4.2 s lead/trail
        # allowance covers the full off-screen entry and exit. This intentionally
        # uses the worst-case glyph width so a beat never clears half a word.
        duration = max(duration, 4_200 + len(caption) * 810)
    return duration


def beat_expression(beat: Mapping[str, Any]) -> dict[str, Any]:
    modifiers = dict(beat.get("modifiers", {}))
    urgency = float(modifiers.get("urgency", 0.35))
    confidence = float(modifiers.get("confidence", 0.65))
    gaze_aversion = modifiers.get("gaze_aversion", "none")
    affect = str(beat["affect"])
    behavior_mode = (
        "tracking"
        if affect in {"thinking", "working", "curious"}
        else "speaking"
        if affect == "speaking"
        else "attentive"
    )
    message = beat.get("caption")
    return {
        "affect": affect,
        "intensity": float(beat.get("intensity", 0.7)),
        "message": str(message) if message is not None else None,
        "text_mode": caption_mode(beat),
        "sequence": beat.get("sequence"),
        "ttl_ms": beat_duration_ms(beat) + 600,
        "priority": "alert" if urgency >= 0.8 else "normal",
        "gaze": "away" if gaze_aversion in {"brief", "moderate"} else "user",
        "behavior_mode": behavior_mode,
        "autonomy": True,
        "cause": "semantic performance",
        "modifiers": {
            "warmth": float(modifiers.get("warmth", 0.5)),
            "confidence": confidence,
            "urgency": urgency,
            "gaze_aversion": gaze_aversion,
        },
    }


__all__ = [
    "PACE_MS",
    "beat_duration_ms",
    "beat_expression",
    "build_performance",
    "caption_mode",
]
