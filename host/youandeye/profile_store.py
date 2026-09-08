from __future__ import annotations

import json
import os
import re
import sqlite3
from contextlib import closing
from copy import deepcopy
from datetime import UTC, datetime
from pathlib import Path
from threading import RLock
from typing import Any, Mapping, Protocol

from .contracts import ContractError, validate_message


PROFILE_PROTOCOL = "youandeye.profile/1"
PROFILE_DB_ENV = "YOUANDEYE_PROFILE_DB"
AGENT_ID_PATTERN = re.compile(r"^[A-Za-z0-9._:-]{1,64}$")

IRIS_PALETTES: dict[str, tuple[int, int, int]] = {
    "azure": (70, 160, 238),
    "teal": (54, 190, 184),
    "violet": (145, 108, 224),
    "amber": (232, 165, 62),
    "rose": (220, 105, 148),
    "emerald": (76, 188, 126),
}

ACCENT_PALETTES: dict[str, str] = {
    "cyan": "#57c9f2",
    "violet": "#9a7bea",
    "gold": "#e8ad4f",
    "coral": "#ed7f72",
    "mint": "#67d6a0",
    "blue": "#4da3ff",
}

DEFAULT_PROFILE_CHOICES: dict[str, dict[str, Any]] = {
    "appearance": {
        "iris_palette": "azure",
        "accent": "blue",
        "mouth_style": "expressive",
    },
    "temperament": {
        "default_energy": 0.5,
        "blink_style": "natural",
        "gaze_style": "attentive",
        "idle_temperament": "calm",
    },
    "signature": {
        "affect": "success",
        "message": "GOT IT",
        "text_mode": "static",
    },
}


class ProfileError(ContractError):
    """Raised when a profile lifecycle operation is invalid."""


class ProfileRepository(Protocol):
    def get(self, agent_id: str) -> dict[str, Any] | None: ...

    def get_active(self, agent_id: str) -> dict[str, Any] | None: ...

    def put(self, profile: Mapping[str, Any]) -> dict[str, Any]: ...

    def delete(self, agent_id: str) -> bool: ...


def validate_agent_id(agent_id: str) -> str:
    if not isinstance(agent_id, str) or not AGENT_ID_PATTERN.fullmatch(agent_id):
        raise ProfileError(
            "agent identity must be 1-64 characters using letters, digits, dot, underscore, colon, or hyphen"
        )
    return agent_id


def utc_now() -> str:
    return datetime.now(UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def default_profile_db_path() -> Path:
    override = os.environ.get(PROFILE_DB_ENV)
    if override:
        return Path(override).expanduser().resolve()
    if os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        root = Path(os.environ["LOCALAPPDATA"])
    elif os.environ.get("XDG_DATA_HOME"):
        root = Path(os.environ["XDG_DATA_HOME"])
    else:
        root = Path.home() / ".local" / "share"
    return root / "YouAndEye" / "profiles" / "profiles-v1.sqlite3"


def build_profile(
    agent_id: str,
    *,
    changes: Mapping[str, Any] | None = None,
    existing: Mapping[str, Any] | None = None,
    lifecycle: str = "draft",
) -> dict[str, Any]:
    agent_id = validate_agent_id(agent_id)
    now = utc_now()
    if existing is None:
        profile: dict[str, Any] = {
            "protocol": PROFILE_PROTOCOL,
            "kind": "profile",
            "agent_id": agent_id,
            "revision": 1,
            "lifecycle": lifecycle,
            **deepcopy(DEFAULT_PROFILE_CHOICES),
            "created_at": now,
            "updated_at": now,
        }
    else:
        profile = validate_message(existing, "profile")
        if profile["agent_id"] != agent_id:
            raise ProfileError("a profile cannot be moved to another agent identity")
        profile["revision"] += 1
        profile["lifecycle"] = lifecycle
        profile["updated_at"] = now

    if changes:
        allowed_groups = {"appearance", "temperament", "signature"}
        unknown_groups = set(changes) - allowed_groups
        if unknown_groups:
            raise ProfileError(f"unknown profile groups: {', '.join(sorted(unknown_groups))}")
        for group, values in changes.items():
            if not isinstance(values, Mapping):
                raise ProfileError(f"profile group {group!r} must be an object")
            current = dict(profile[group])
            unknown_fields = set(values) - set(current)
            if unknown_fields:
                raise ProfileError(
                    f"unknown {group} fields: {', '.join(sorted(unknown_fields))}"
                )
            current.update(values)
            profile[group] = current

    return validate_message(profile, "profile")


def with_lifecycle(profile: Mapping[str, Any], lifecycle: str) -> dict[str, Any]:
    checked = validate_message(profile, "profile")
    changed = deepcopy(checked)
    changed["lifecycle"] = lifecycle
    changed["revision"] += 1
    changed["updated_at"] = utc_now()
    return validate_message(changed, "profile")


def profile_visuals(profile: Mapping[str, Any]) -> dict[str, Any]:
    checked = validate_message(profile, "profile")
    appearance = checked["appearance"]
    temperament = checked["temperament"]
    iris_name = appearance["iris_palette"]
    accent_name = appearance["accent"]
    return {
        "agent_id": checked["agent_id"],
        "revision": checked["revision"],
        "iris_palette": iris_name,
        "iris_rgb": list(IRIS_PALETTES[iris_name]),
        "accent": accent_name,
        "accent_css": ACCENT_PALETTES[accent_name],
        "mouth_style": appearance["mouth_style"],
        "default_energy": temperament["default_energy"],
        "blink_style": temperament["blink_style"],
        "gaze_style": temperament["gaze_style"],
        "idle_temperament": temperament["idle_temperament"],
    }


class MemoryProfileStore:
    """Thread-safe ephemeral store used by tests and embedders that opt out of disk state."""

    def __init__(self) -> None:
        self._profiles: dict[str, dict[str, Any]] = {}
        self._active_profiles: dict[str, dict[str, Any]] = {}
        self._lock = RLock()

    def get(self, agent_id: str) -> dict[str, Any] | None:
        validate_agent_id(agent_id)
        with self._lock:
            profile = self._profiles.get(agent_id)
            return deepcopy(profile) if profile is not None else None

    def get_active(self, agent_id: str) -> dict[str, Any] | None:
        validate_agent_id(agent_id)
        with self._lock:
            profile = self._active_profiles.get(agent_id)
            return deepcopy(profile) if profile is not None else None

    def put(self, profile: Mapping[str, Any]) -> dict[str, Any]:
        checked = validate_message(profile, "profile")
        with self._lock:
            self._profiles[checked["agent_id"]] = deepcopy(checked)
            if checked["lifecycle"] == "active":
                self._active_profiles[checked["agent_id"]] = deepcopy(checked)
        return deepcopy(checked)

    def delete(self, agent_id: str) -> bool:
        validate_agent_id(agent_id)
        with self._lock:
            removed = self._profiles.pop(agent_id, None) is not None
            self._active_profiles.pop(agent_id, None)
            return removed


class ProfileStore:
    """Small local SQLite store keyed by stable semantic agent identity."""

    def __init__(self, path: str | os.PathLike[str] | None = None) -> None:
        self.path = Path(path) if path is not None else default_profile_db_path()
        self.path = self.path.expanduser().resolve()
        self._lock = RLock()
        self._initialize()

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.path, timeout=5.0)
        connection.execute("PRAGMA foreign_keys = ON")
        return connection

    def _initialize(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self._lock, closing(self._connect()) as connection:
            with connection:
                connection.execute(
                    """
                    CREATE TABLE IF NOT EXISTS profiles (
                        agent_id TEXT PRIMARY KEY,
                        document TEXT NOT NULL,
                        active_document TEXT,
                        updated_at TEXT NOT NULL
                    )
                    """
                )
                columns = {
                    row[1] for row in connection.execute("PRAGMA table_info(profiles)")
                }
                if "active_document" not in columns:
                    connection.execute(
                        "ALTER TABLE profiles ADD COLUMN active_document TEXT"
                    )

    def get(self, agent_id: str) -> dict[str, Any] | None:
        agent_id = validate_agent_id(agent_id)
        with self._lock, closing(self._connect()) as connection:
            row = connection.execute(
                "SELECT document FROM profiles WHERE agent_id = ?", (agent_id,)
            ).fetchone()
        if row is None:
            return None
        try:
            document = json.loads(row[0])
        except json.JSONDecodeError as exc:
            raise ProfileError("stored profile is not valid JSON") from exc
        return validate_message(document, "profile")

    def get_active(self, agent_id: str) -> dict[str, Any] | None:
        agent_id = validate_agent_id(agent_id)
        with self._lock, closing(self._connect()) as connection:
            row = connection.execute(
                "SELECT active_document FROM profiles WHERE agent_id = ?", (agent_id,)
            ).fetchone()
        if row is None or row[0] is None:
            return None
        try:
            document = json.loads(row[0])
        except json.JSONDecodeError as exc:
            raise ProfileError("stored active profile is not valid JSON") from exc
        return validate_message(document, "profile")

    def put(self, profile: Mapping[str, Any]) -> dict[str, Any]:
        checked = validate_message(profile, "profile")
        document = json.dumps(checked, ensure_ascii=False, separators=(",", ":"))
        with self._lock, closing(self._connect()) as connection:
            with connection:
                active_document = document if checked["lifecycle"] == "active" else None
                connection.execute(
                    """
                    INSERT INTO profiles(agent_id, document, active_document, updated_at)
                    VALUES (?, ?, ?, ?)
                    ON CONFLICT(agent_id) DO UPDATE SET
                        document = excluded.document,
                        active_document = COALESCE(excluded.active_document, profiles.active_document),
                        updated_at = excluded.updated_at
                    """,
                    (
                        checked["agent_id"],
                        document,
                        active_document,
                        checked["updated_at"],
                    ),
                )
        return deepcopy(checked)

    def delete(self, agent_id: str) -> bool:
        agent_id = validate_agent_id(agent_id)
        with self._lock, closing(self._connect()) as connection:
            with connection:
                cursor = connection.execute(
                    "DELETE FROM profiles WHERE agent_id = ?", (agent_id,)
                )
                return cursor.rowcount > 0


__all__ = [
    "ACCENT_PALETTES",
    "DEFAULT_PROFILE_CHOICES",
    "IRIS_PALETTES",
    "MemoryProfileStore",
    "ProfileError",
    "ProfileRepository",
    "ProfileStore",
    "build_profile",
    "default_profile_db_path",
    "profile_visuals",
    "validate_agent_id",
    "with_lifecycle",
]
