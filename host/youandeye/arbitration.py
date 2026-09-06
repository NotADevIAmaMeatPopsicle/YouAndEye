from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from threading import RLock
from typing import Any, Mapping

from .contracts import validate_message


PRIORITY = {"ambient": 10, "normal": 50, "alert": 80, "critical": 100}
DEFAULT_MAX_SESSIONS = 256
DEFAULT_REPLAY_RETENTION_MS = 15 * 60 * 1000


@dataclass(frozen=True)
class SubmitResult:
    accepted: bool
    activated: bool
    reason: str
    active_source: str | None
    active_session: str | None


@dataclass
class _Candidate:
    frame: dict[str, Any]
    accepted_at_ms: int
    expires_at_ms: int
    completes_at_ms: int

    @property
    def key(self) -> tuple[str, str]:
        source = self.frame["source"]
        return source["id"], source["session"]

    @property
    def priority(self) -> int:
        return PRIORITY[self.frame["priority"]]


class Arbiter:
    """Deterministic priority/session arbitration for one logical surface."""

    def __init__(
        self,
        *,
        max_sessions: int = DEFAULT_MAX_SESSIONS,
        replay_retention_ms: int = DEFAULT_REPLAY_RETENTION_MS,
    ) -> None:
        if max_sessions < 1:
            raise ValueError("max_sessions must be positive")
        if replay_retention_ms < 0:
            raise ValueError("replay_retention_ms cannot be negative")
        self._max_sessions = max_sessions
        self._replay_retention_ms = replay_retention_ms
        self._candidates: dict[tuple[str, str], _Candidate] = {}
        self._last_seq: dict[tuple[str, str], tuple[int, int]] = {}
        self._active_key: tuple[str, str] | None = None
        self._lock = RLock()

    def submit(self, frame: Mapping[str, Any], now_ms: int) -> SubmitResult:
        checked = validate_message(frame, "frame")
        source = checked["source"]
        key = (source["id"], source["session"])
        seq = checked["seq"]
        with self._lock:
            self._reselect(now_ms)
            self._prune_replay_records(now_ms)
            previous = self._last_seq.get(key)
            if previous is not None and seq <= previous[0]:
                return self._result(
                    False,
                    False,
                    f"stale sequence {seq}; last accepted is {previous[0]}",
                )
            if previous is None and len(self._last_seq) >= self._max_sessions:
                return self._result(False, False, "session capacity reached")

            expires_at = now_ms + checked["ttl_ms"]
            completes_at = expires_at + checked["decay"]["duration_ms"]
            retain_until = completes_at + self._replay_retention_ms
            self._last_seq[key] = (seq, retain_until)
            self._candidates[key] = _Candidate(
                deepcopy(checked), now_ms, expires_at, completes_at
            )

            previous_active = self._active_key
            self._reselect(now_ms, submitted_key=key)
            activated = self._active_key == key and previous_active != key
            return self._result(True, activated, "accepted")

    def resolve(self, now_ms: int) -> dict[str, Any] | None:
        with self._lock:
            self._reselect(now_ms)
            self._prune_replay_records(now_ms)
            if self._active_key is None:
                return None

            candidate = self._candidates[self._active_key]
            if now_ms < candidate.expires_at_ms:
                phase = "hold"
                weight = 1.0
            else:
                duration = candidate.frame["decay"]["duration_ms"]
                curve = candidate.frame["decay"]["curve"]
                if duration <= 0:
                    self._drop_active_and_reselect(now_ms)
                    return self.resolve(now_ms)
                progress = min(
                    1.0, max(0.0, (now_ms - candidate.expires_at_ms) / duration)
                )
                phase = "decay"
                if curve == "hold":
                    weight = 1.0
                elif curve == "linear":
                    weight = 1.0 - progress
                else:
                    weight = (1.0 - progress) ** 2

            return {
                "frame": deepcopy(candidate.frame),
                "accepted_at_ms": candidate.accepted_at_ms,
                "expires_at_ms": candidate.expires_at_ms,
                "phase": phase,
                "weight": round(weight, 6),
            }

    def next_completion_ms(self, now_ms: int) -> int | None:
        """Return the next candidate completion that can change resolved state."""

        with self._lock:
            self._reselect(now_ms)
            self._prune_replay_records(now_ms)
            if not self._candidates:
                return None
            return min(candidate.completes_at_ms for candidate in self._candidates.values())

    @property
    def candidate_count(self) -> int:
        with self._lock:
            return len(self._candidates)

    @property
    def tracked_session_count(self) -> int:
        with self._lock:
            return len(self._last_seq)

    def _prune_replay_records(self, now_ms: int) -> None:
        for key, (_, retain_until_ms) in list(self._last_seq.items()):
            if key not in self._candidates and now_ms >= retain_until_ms:
                del self._last_seq[key]

    def _reselect(self, now_ms: int, submitted_key: tuple[str, str] | None = None) -> None:
        active = self._candidates.get(self._active_key) if self._active_key else None
        if active is not None and now_ms >= active.completes_at_ms:
            del self._candidates[active.key]
            self._active_key = None
            active = None

        for key, candidate in list(self._candidates.items()):
            if key != self._active_key and now_ms >= candidate.expires_at_ms:
                del self._candidates[key]

        if self._active_key is None:
            eligible = list(self._candidates.values())
            if eligible:
                winner = max(eligible, key=lambda item: (item.priority, item.accepted_at_ms))
                self._active_key = winner.key
            return

        active = self._candidates[self._active_key]
        if submitted_key == self._active_key:
            higher = [item for item in self._candidates.values() if item.priority > active.priority]
        elif submitted_key is not None and submitted_key in self._candidates:
            submitted = self._candidates[submitted_key]
            higher = [submitted] if submitted.priority > active.priority else []
        else:
            higher = [item for item in self._candidates.values() if item.priority > active.priority]
        if higher:
            winner = max(higher, key=lambda item: (item.priority, item.accepted_at_ms))
            self._active_key = winner.key

    def _drop_active_and_reselect(self, now_ms: int) -> None:
        if self._active_key is not None:
            self._candidates.pop(self._active_key, None)
            self._active_key = None
        self._reselect(now_ms)

    def _result(self, accepted: bool, activated: bool, reason: str) -> SubmitResult:
        active_source, active_session = self._active_key or (None, None)
        return SubmitResult(accepted, activated, reason, active_source, active_session)
