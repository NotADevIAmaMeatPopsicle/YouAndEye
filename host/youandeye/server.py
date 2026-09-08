from __future__ import annotations

import argparse
import json
import os
import re
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit

from .bridge import SurfaceBridge
from .contracts import ContractError
from .device import DeviceError, HeltecDevice
from .face_service import ExpressionService, heltec_bridge
from .profile_store import ProfileStore, default_profile_db_path


ROOT = Path(__file__).resolve().parents[2]
LOOPBACK_ORIGIN = re.compile(r"^http://(?:127\.0\.0\.1|localhost)(?::\d+)?$")
MAX_BODY_BYTES = 64 * 1024


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def default_bridge() -> SurfaceBridge:
    examples = ROOT / "protocol" / "examples"
    return SurfaceBridge(
        load_json(examples / "capabilities-concept-rig.json"),
        load_json(examples / "frame-neutral-baseline.json"),
    )


class BridgeServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(
        self,
        address: tuple[str, int],
        bridge: SurfaceBridge,
        face_service: ExpressionService | None = None,
    ) -> None:
        super().__init__(address, BridgeHandler)
        self.bridge = bridge
        self.face_service = face_service


class BridgeHandler(BaseHTTPRequestHandler):
    server: BridgeServer

    def log_message(self, format: str, *args: object) -> None:
        print(f"{self.address_string()} - {format % args}")

    def _origin(self) -> str | None:
        origin = self.headers.get("Origin")
        return origin if origin and LOOPBACK_ORIGIN.fullmatch(origin) else None

    def _send_json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode(
            "utf-8"
        )
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        origin = self._origin()
        if origin:
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Vary", "Origin")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self) -> None:
        origin = self._origin()
        if not origin:
            self._send_json(
                HTTPStatus.FORBIDDEN, {"ok": False, "error": "loopback origin required"}
            )
            return
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_header("Access-Control-Allow-Origin", origin)
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Max-Age", "600")
        self.end_headers()

    def do_GET(self) -> None:
        path = urlsplit(self.path).path
        if path == "/health":
            self._send_json(HTTPStatus.OK, {"ok": True, "protocol": "emote/1"})
        elif path == "/v1/capabilities":
            self._send_json(HTTPStatus.OK, self.server.bridge.capabilities)
        elif path == "/v1/state":
            self._send_json(HTTPStatus.OK, self.server.bridge.state(monotonic_ms()))
        elif path == "/v1/device":
            if self.server.face_service is None:
                self._send_json(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    {"ok": False, "error": "physical Heltec dispatch is not enabled"},
                )
                return
            try:
                self._send_json(HTTPStatus.OK, self.server.face_service.status())
            except DeviceError as exc:
                self._send_json(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    {"ok": False, "error": str(exc), "error_code": exc.code},
                )
        elif path == "/v1/profile":
            if self.server.face_service is None:
                self._send_json(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    {"ok": False, "error": "profile service is not enabled"},
                )
                return
            self._send_json(HTTPStatus.OK, self.server.face_service.profile_status())
        else:
            self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not found"})

    def do_POST(self) -> None:
        if urlsplit(self.path).path != "/v1/frames":
            self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not found"})
            return
        if self.headers.get("Origin") and not self._origin():
            self._send_json(
                HTTPStatus.FORBIDDEN, {"ok": False, "error": "loopback origin required"}
            )
            return
        if self.headers.get_content_type() != "application/json":
            self._send_json(
                HTTPStatus.UNSUPPORTED_MEDIA_TYPE,
                {"ok": False, "error": "application/json required"},
            )
            return
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            content_length = -1
        if content_length <= 0 or content_length > MAX_BODY_BYTES:
            self._send_json(
                HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                {"ok": False, "error": "invalid body size"},
            )
            return
        try:
            body = self.rfile.read(content_length)
            message = json.loads(body.decode("utf-8"))
            if self.server.face_service is not None:
                receipt = self.server.face_service.submit_frame(message)
                result = None
            else:
                result = self.server.bridge.submit(message, monotonic_ms())
                receipt = None
        except (UnicodeDecodeError, json.JSONDecodeError, ContractError) as exc:
            self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(exc)})
            return
        except DeviceError as exc:
            self._send_json(
                HTTPStatus.SERVICE_UNAVAILABLE,
                {"ok": False, "error": str(exc), "error_code": exc.code},
            )
            return

        if receipt is not None:
            status = HTTPStatus.ACCEPTED if receipt["ok"] else HTTPStatus.CONFLICT
            self._send_json(status, receipt)
        else:
            assert result is not None
            if result.accepted:
                status = HTTPStatus.ACCEPTED
            elif result.reason == "session capacity reached":
                status = HTTPStatus.TOO_MANY_REQUESTS
            else:
                status = HTTPStatus.CONFLICT
            self._send_json(
                status,
                {
                    "ok": result.accepted,
                    "activated": result.activated,
                    "reason": result.reason,
                    "active_source": result.active_source,
                    "active_session": result.active_session,
                },
            )


def monotonic_ms() -> int:
    return time.monotonic_ns() // 1_000_000


def make_server(
    host: str = "127.0.0.1",
    port: int = 8765,
    bridge: SurfaceBridge | None = None,
    face_service: ExpressionService | None = None,
) -> BridgeServer:
    if host not in {"127.0.0.1", "localhost", "::1"}:
        raise ValueError("the v1 bridge is loopback-only")
    selected_bridge = (
        face_service.bridge
        if face_service is not None
        else (bridge or default_bridge())
    )
    return BridgeServer((host, port), selected_bridge, face_service)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the local-only YouandEye emote/1 bridge."
    )
    parser.add_argument(
        "--host", default="127.0.0.1", choices=("127.0.0.1", "localhost", "::1")
    )
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument(
        "--heltec-port",
        nargs="?",
        const="auto",
        help="Also dispatch accepted frames to the verified Heltec; omit the value to auto-discover.",
    )
    parser.add_argument(
        "--amoled-port",
        nargs="?",
        const="auto",
        help="Also dispatch mouth intent to a verified YouAndEye AMOLED controller.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    face_service = None
    if args.heltec_port is not None:
        source_id = os.environ.get("YOUANDEYE_SOURCE_ID", "agent")
        mouth_device = None
        if args.amoled_port is not None:
            from .device import AmoledMouthDevice

            mouth_device = AmoledMouthDevice(port=args.amoled_port)
        face_service = ExpressionService(
            HeltecDevice(port=args.heltec_port),
            mouth_device=mouth_device,
            bridge=heltec_bridge(),
            source_id=source_id,
            agent_id=os.environ.get("YOUANDEYE_AGENT_ID", source_id),
            profile_store=ProfileStore(default_profile_db_path()),
        )
    server = make_server(args.host, args.port, face_service=face_service)
    print(f"YouandEye bridge listening on http://{args.host}:{server.server_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if face_service is not None:
            face_service.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
