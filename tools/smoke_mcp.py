"""Exercise the real YouAndEye MCP STDIO boundary with an optional physical beat."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_TOOLS = {
    "express",
    "face_status",
    "face_capabilities",
    "neutral",
    "configure_profile",
    "perform",
}


def _structured(result: object) -> dict:
    if getattr(result, "isError", False):
        raise RuntimeError(str(getattr(result, "content", "MCP tool failed")))
    value = getattr(result, "structuredContent", None)
    if not isinstance(value, dict):
        raise TypeError("MCP tool did not return structured content")
    return value


async def run(port: str, exercise: bool, amoled_port: str = "auto") -> dict:
    environment = dict(os.environ)
    environment["YOUANDEYE_PORT"] = port
    environment["YOUANDEYE_AMOLED_PORT"] = amoled_port
    parameters = StdioServerParameters(
        command="uv",
        args=["run", "--extra", "serial", "youandeye-mcp"],
        cwd=ROOT,
        env=environment,
    )
    async with (
        stdio_client(parameters) as (read_stream, write_stream),
        ClientSession(read_stream, write_stream) as session,
    ):
        await session.initialize()
        listed = await session.list_tools()
        names = {tool.name for tool in listed.tools}
        if names != EXPECTED_TOOLS:
            raise RuntimeError(f"unexpected MCP tools: {sorted(names)}")
        capabilities = _structured(await session.call_tool("face_capabilities", {}))
        status_before = _structured(await session.call_tool("face_status", {}))
        result: dict = {
            "ok": True,
            "tools": sorted(names),
            "capabilities": capabilities,
            "status_before": status_before,
        }
        if exercise:
            result["performance"] = _structured(
                await session.call_tool(
                    "perform",
                    {
                        "action": "start",
                        "title": "MCP end-to-end smoke test",
                        "beats": [
                            {"affect": "listening", "pace": "glance"},
                            {
                                "affect": "thinking",
                                "caption": "WORKING...",
                                "caption_mode": "scroll",
                                "pace": "brief",
                                "modifiers": {
                                    "warmth": 0.55,
                                    "confidence": 0.7,
                                    "urgency": 0.4,
                                    "gaze_aversion": "brief",
                                },
                            },
                            {"affect": "success", "pace": "brief"},
                        ],
                    },
                )
            )
            result["status_after"] = _structured(
                await session.call_tool("face_status", {})
            )
        return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port", default="auto", help="Verified port or auto discovery."
    )
    parser.add_argument(
        "--exercise",
        action="store_true",
        help="Show a success beat, wait for completion, and restore neutral.",
    )
    parser.add_argument(
        "--amoled-port",
        default="auto",
        help="Verified round-mouth port or auto discovery.",
    )
    args = parser.parse_args()
    result = asyncio.run(run(args.port, args.exercise, args.amoled_port))
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
