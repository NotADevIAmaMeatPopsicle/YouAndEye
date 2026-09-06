from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path
from typing import Any, Mapping

from jsonschema import Draft202012Validator


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_FILES = {
    "frame": "emote-frame.schema.json",
    "capabilities": "emote-capabilities.schema.json",
    "event": "emote-event.schema.json",
}


class ContractError(ValueError):
    """Raised when an emote/1 message does not match its canonical schema."""


@lru_cache(maxsize=len(SCHEMA_FILES))
def load_schema(kind: str) -> dict[str, Any]:
    try:
        filename = SCHEMA_FILES[kind]
    except KeyError as exc:
        raise ContractError(f"unsupported message kind: {kind!r}") from exc
    path = ROOT / "schema" / filename
    schema = json.loads(path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return schema


@lru_cache(maxsize=len(SCHEMA_FILES))
def _validator(kind: str) -> Draft202012Validator:
    return Draft202012Validator(load_schema(kind))


def validate_message(message: Mapping[str, Any], expected_kind: str | None = None) -> dict[str, Any]:
    if not isinstance(message, Mapping):
        raise ContractError("message must be a JSON object")
    kind = message.get("kind")
    if not isinstance(kind, str) or kind not in SCHEMA_FILES:
        raise ContractError(f"unsupported message kind: {kind!r}")
    if expected_kind is not None and kind != expected_kind:
        raise ContractError(f"expected {expected_kind!r}, received {kind!r}")

    errors = sorted(_validator(kind).iter_errors(message), key=lambda error: list(error.path))
    if errors:
        error = errors[0]
        location = ".".join(str(part) for part in error.absolute_path) or "$"
        raise ContractError(f"{location}: {error.message}")
    return json.loads(json.dumps(message))
