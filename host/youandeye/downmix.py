from __future__ import annotations

from copy import deepcopy
from typing import Any, Mapping

from .contracts import validate_message


AFFECT_CENTERS: dict[str, tuple[float, float]] = {
    "neutral": (0.0, 0.25),
    "happy": (0.75, 0.55),
    "surprised": (0.05, 0.9),
    "thinking": (0.05, 0.4),
    "suspicious": (-0.25, 0.55),
    "sad": (-0.65, 0.25),
    "excited": (0.8, 0.95),
    "love": (0.95, 0.65),
    "error": (-0.8, 0.9),
    "listening": (0.15, 0.45),
    "speaking": (0.2, 0.6),
    "working": (0.05, 0.5),
    "success": (0.9, 0.75),
    "playful": (0.55, 0.7),
    "encouraging": (0.75, 0.45),
    "curious": (0.35, 0.55),
    "uncertain": (-0.15, 0.38),
    "concerned": (-0.38, 0.42),
    "delighted": (0.82, 0.48),
    "embarrassed": (0.10, 0.52),
    "reassuring": (0.58, 0.32),
    "shocked": (-0.05, 1.0),
    "weary": (-0.42, 0.12),
    "confused": (-0.08, 0.55),
    "blushing": (0.48, 0.58),
    "nervous": (-0.32, 0.72),
    "maniacal": (0.28, 1.0),
    "stressed": (-0.55, 0.82),
    "determined": (0.24, 0.72),
    "bored": (-0.18, 0.08),
    "panicked": (-0.72, 1.0),
    "scheming": (0.08, 0.58),
    "fatigued": (-0.46, 0.05),
    "content": (0.62, 0.18),
    "pleading": (-0.08, 0.62),
    "sick": (-0.72, 0.45),
    "hyped": (0.86, 1.0),
    "baffled": (-0.12, 0.68),
}


def _nearest_affect(frame: Mapping[str, Any], supported: list[str]) -> str:
    requested = frame["affect"]["state"]
    if requested in supported:
        return requested
    valence = frame["affect"]["valence"]
    arousal = frame["affect"]["arousal"]
    candidates = supported or ["neutral"]
    return min(
        candidates,
        key=lambda name: (AFFECT_CENTERS.get(name, AFFECT_CENTERS["neutral"])[0] - valence) ** 2
        + (AFFECT_CENTERS.get(name, AFFECT_CENTERS["neutral"])[1] - arousal) ** 2,
    )


def downmix_frame(frame: Mapping[str, Any], capabilities: Mapping[str, Any]) -> dict[str, Any]:
    checked_frame = validate_message(frame, "frame")
    checked_capabilities = validate_message(capabilities, "capabilities")
    warnings: list[str] = []

    requested_affect = checked_frame["affect"]["state"]
    mapped_affect = _nearest_affect(checked_frame, checked_capabilities["affects"])
    affect = deepcopy(checked_frame["affect"])
    affect["state"] = mapped_affect
    if requested_affect != mapped_affect:
        warnings.append(f"affect {requested_affect!r} mapped to {mapped_affect!r}")

    channel_policy = checked_frame.get("channel_policy", {})
    available_ids = {channel["id"] for channel in checked_capabilities["channels"]}
    for channel_id, policy in channel_policy.items():
        if policy == "render" and channel_id not in available_ids:
            warnings.append(f"required channel {channel_id!r} is unavailable")

    rendered: dict[str, Any] = {}
    utterance = deepcopy(checked_frame.get("utterance", {"mode": "none"}))
    max_chars = checked_capabilities["limits"]["max_utterance_chars"]
    if "text" in utterance and len(utterance["text"]) > max_chars:
        utterance["text"] = utterance["text"][:max_chars]
        warnings.append(f"utterance truncated to {max_chars} characters")

    for channel in checked_capabilities["channels"]:
        channel_id = channel["id"]
        if channel_policy.get(channel_id, "auto") == "mute":
            continue
        kind = channel["kind"]
        if kind == "dual_raster_eyes":
            rendered[channel_id] = {
                "kind": kind,
                "affect": affect,
                "behavior": deepcopy(checked_frame.get("behavior", {})),
                "gaze": deepcopy(checked_frame.get("gaze", {"target": "wander"})),
                "sequence": checked_frame.get("sequence"),
            }
        elif kind in {"text_matrix", "terminal"}:
            rendered[channel_id] = {
                "kind": kind,
                "affect": mapped_affect,
                "utterance": utterance,
            }
        elif kind == "mouth":
            rendered[channel_id] = {
                "kind": kind,
                "affect": mapped_affect,
                "speaking": checked_frame.get("behavior", {}).get("speaking", 0),
                "utterance_mode": utterance["mode"],
            }
        elif kind == "audio":
            rendered[channel_id] = {"kind": kind, "sound": utterance.get("sound", "none")}
        else:
            rendered[channel_id] = {
                "kind": kind,
                "affect": affect,
                "gaze": deepcopy(checked_frame.get("gaze")),
                "sequence": checked_frame.get("sequence"),
            }

    return {
        "protocol": "emote/1",
        "kind": "render_intent",
        "surface_id": checked_capabilities["surface"]["id"],
        "frame_seq": checked_frame["seq"],
        "channels": rendered,
        "warnings": warnings,
    }
