from __future__ import annotations

from copy import deepcopy
from typing import Any, Mapping

from .arbitration import Arbiter, SubmitResult
from .contracts import validate_message
from .downmix import downmix_frame


class SurfaceBridge:
    def __init__(self, capabilities: Mapping[str, Any], baseline: Mapping[str, Any]) -> None:
        self.capabilities = validate_message(capabilities, "capabilities")
        self.baseline = validate_message(baseline, "frame")
        self.arbiter = Arbiter()

    def submit(self, frame: Mapping[str, Any], now_ms: int) -> SubmitResult:
        return self.arbiter.submit(frame, now_ms)

    def state(self, now_ms: int) -> dict[str, Any]:
        resolution = self.arbiter.resolve(now_ms)
        if resolution is None:
            frame = deepcopy(self.baseline)
            frame["surface_id"] = self.capabilities["surface"]["id"]
            phase = "baseline"
            weight = 1.0
            accepted_at_ms = None
            expires_at_ms = None
        else:
            frame = resolution["frame"]
            phase = resolution["phase"]
            weight = resolution["weight"]
            accepted_at_ms = resolution["accepted_at_ms"]
            expires_at_ms = resolution["expires_at_ms"]

        return {
            "protocol": "emote/1",
            "kind": "state",
            "surface_id": self.capabilities["surface"]["id"],
            "phase": phase,
            "weight": weight,
            "accepted_at_ms": accepted_at_ms,
            "expires_at_ms": expires_at_ms,
            "frame": frame,
            "render_intent": downmix_frame(frame, self.capabilities),
        }

    def next_completion_ms(self, now_ms: int) -> int | None:
        return self.arbiter.next_completion_ms(now_ms)
