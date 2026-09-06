"""Local emote/1 validation, arbitration, and surface bridge."""

from .arbitration import Arbiter, SubmitResult
from .bridge import SurfaceBridge
from .contracts import ContractError, load_schema, validate_message
from .downmix import downmix_frame
from .face_service import ExpressionService, heltec_bridge

__all__ = [
    "Arbiter",
    "ContractError",
    "ExpressionService",
    "SubmitResult",
    "SurfaceBridge",
    "downmix_frame",
    "heltec_bridge",
    "load_schema",
    "validate_message",
]
