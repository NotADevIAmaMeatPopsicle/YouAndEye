# Project status

Release candidate verified 2026-09-06.

| Area | Result |
|---|---|
| Python contract/host suite | 61/61 passing |
| Native motion test | Passing, including 60-second deterministic idle run |
| Native RGB565 renderer test | Passing at 240×320 and 160×160, including fast 160×160 path |
| Classic Heltec firmware build | Passing; 76,944 B RAM (23.5%), 355,665 B flash (27.1%) |
| Firmware network surface | Removed; USB serial only |
| Host serial ownership | Idle release, typed failures, retry circuit, cooperative marker, and OS lease lock |

The accepted physical rig previously passed the six-state visual check, synchronized-blink review,
device-owned beat cycle, and a 60-second zero-miss soak. The security-hardened USB-only build was compiled
but not flashed during this release-cleanup pass.

Current scope is the classic Heltec WiFi Kit 32, Waveshare DualEye module, built-in OLED mouth, browser
simulator, and local MCP/HTTP host bridge. Other board ports and experimental renderers are not part of this
public release.
