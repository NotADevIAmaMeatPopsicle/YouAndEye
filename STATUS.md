# Project status

Expressive-behavior candidate flashed and machine-verified 2026-09-07. The user confirmed the post-flash face
looks good; the detailed nuanced-state and long-idle visual matrix remains open.

| Area | Result |
|---|---|
| Python contract/host suite | 65/65 passing |
| Native motion test | Passing, including intensity, acting-profile, synchronized blink, long-timescale attention, and 60-second deterministic idle checks |
| Native RGB565 renderer test | Passing at 240×320 and 160×160, including fast 160×160 path |
| Classic Heltec firmware build | Passing; 76,952 B RAM (23.5%), 358,621 B flash (27.4%) |
| Firmware network surface | Removed; USB serial only |
| Host serial ownership | Idle release, typed failures, retry circuit, cooperative marker, and OS lease lock |
| Expression Bench | All 21 affects load; six nuanced poses plus fresh/rested neutral visually inspected on both render paths; zero browser console errors or warnings |
| Current physical candidate | Flashed to the verified classic ESP32; upload hashes verified; MCP beat/neutral smoke and 60-second soak passed; user confirmed the face looks good |

The accepted physical rig previously passed the six-state visual check, synchronized-blink review,
device-owned beat cycle, and a 60-second zero-miss soak. The security-hardened USB-only build was compiled
but not flashed during the release-cleanup pass. The new local candidate adds intensity-scaled expression
geometry; curious, uncertain, concerned, delighted, embarrassed, and reassuring poses; affect-specific gaze
and blink cadence; thinking/listening/success transition cues; restrained per-eye blink depth; and a gradual
neutral attention fade that resets immediately on new intent. It is now running on the physical face and has
passed automated transport, motion, and soak checks plus the user's immediate whole-face review; nuanced-state
and long-idle human visual reads remain for the dedicated testing pass.

Current scope is the classic Heltec WiFi Kit 32, Waveshare DualEye module, built-in OLED mouth, browser
simulator, and local MCP/HTTP host bridge. Other board ports and experimental renderers are not part of this
public release.
