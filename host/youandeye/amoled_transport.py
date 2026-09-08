from __future__ import annotations

import re
from collections.abc import Mapping
from typing import Any

from .contracts import validate_message

AMOLED_TEXT_MAX_CHARS = 64
AMOLED_SEQUENCE_COMMANDS = {
    "attention": "ATTENTION",
    "acknowledge": "ACKNOWLEDGE",
    "celebrate": "SUCCESS",
    "reassure": "REASSURE",
    "error": "ERROR",
}


def _safe_text(value: object) -> str:
    text = re.sub(r"\s+", " ", str(value)).strip()
    return text[:AMOLED_TEXT_MAX_CHARS]


def _profile_command(frame: Mapping[str, Any]) -> str:
    extensions = frame.get("extensions", {})
    context = extensions.get("youandeye", {}) if isinstance(extensions, Mapping) else {}
    profile = context.get("profile", {}) if isinstance(context, Mapping) else {}
    if not isinstance(profile, Mapping):
        profile = {}
    style = str(profile.get("mouth_style", "expressive")).upper()
    accent = str(profile.get("accent", "blue")).upper()
    energy = float(profile.get("default_energy", frame.get("behavior", {}).get("energy", 0.5)))
    return f"PROFILE {style} {accent} {min(1.0, max(0.0, energy)):.2f}"


def commands_for_amoled(frame: Mapping[str, Any]) -> list[str]:
    """Translate an emote/1 frame to bounded, semantic mouth commands."""

    checked = validate_message(frame, "frame")
    policy = checked.get("channel_policy", {})
    utterance_muted = policy.get("utterance", "auto") == "mute" or policy.get(
        "mouth", "auto"
    ) == "mute"
    utterance = checked.get("utterance", {"mode": "none"})
    mode = utterance["mode"]
    text = _safe_text(utterance.get("text", ""))

    extensions = checked.get("extensions", {})
    context = extensions.get("youandeye", {}) if isinstance(extensions, Mapping) else {}
    modifiers = context.get("modifiers", {}) if isinstance(context, Mapping) else {}
    if not isinstance(modifiers, Mapping):
        modifiers = {}

    intensity = float(checked["affect"]["intensity"])
    warmth = float(modifiers.get("warmth", 0.5))
    confidence = float(modifiers.get("confidence", 0.5))
    urgency = float(modifiers.get("urgency", 0.3))
    commands = [
        _profile_command(checked),
        "AFFECT {} {:.2f} {:.2f} {:.2f} {:.2f}".format(
            checked["affect"]["state"].upper(),
            intensity,
            min(1.0, max(0.0, warmth)),
            min(1.0, max(0.0, confidence)),
            min(1.0, max(0.0, urgency)),
        ),
    ]

    sequence = checked.get("sequence")
    if (
        sequence in AMOLED_SEQUENCE_COMMANDS
        and not utterance_muted
        and mode == "none"
    ):
        commands.append(f"BEAT {AMOLED_SEQUENCE_COMMANDS[sequence]}")
    elif utterance_muted:
        commands.append("MOUTH BLANK")
    elif mode == "none":
        commands.append("MOUTH AUTO")
    elif mode == "scroll":
        commands.append(f"SCROLL {text}" if text else "MOUTH BLANK")
    elif mode in {"static", "speech"}:
        commands.append(f"TEXT {text}" if text else "MOUTH BLANK")
    elif mode == "icon":
        icon = _safe_text(utterance.get("icon", "")).replace("_", " ").upper()
        commands.append(f"ICON {icon}" if icon else "MOUTH BLANK")
    else:  # Defensive even though the schema rejects this first.
        raise ValueError(f"unsupported AMOLED utterance mode: {mode!r}")

    return commands


__all__ = ["AMOLED_TEXT_MAX_CHARS", "commands_for_amoled"]
