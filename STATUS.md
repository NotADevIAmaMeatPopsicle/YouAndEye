# Project status

The identity and semantic-performance release is built, flashed, and active on the verified classic-ESP32
face. Per-agent personality remains bounded by the semantic/render split and the six core readability gate.

| Area | Result |
|---|---|
| Python contract/host suite | 80/80 passing |
| Native motion test | Passing, including identity bounds, semantic modifier clamps, core geometry preservation, synchronized blink, long-timescale attention, and 60-second deterministic idle checks |
| Native RGB565 renderer test | Passing at 240×320 and 160×160, including fast 160×160 path |
| Classic Heltec firmware build | Passing; 77,016 B RAM (23.5%), 362,073 B flash (27.6%) |
| Firmware network surface | Removed; USB serial only |
| Host serial ownership | Idle release, typed failures, retry circuit, cooperative marker, and OS lease lock |
| Expression Bench | All 21 affects load; six nuanced poses plus fresh/rested neutral visually inspected on both render paths; zero browser console errors or warnings |
| Current physical release | Flashed to the verified classic ESP32; upload hashes verified; six-tool MCP scene completed with physical scroll feedback; active-profile 60-second soak sampled 100 status windows at 27.5 fps minimum, 32.054 ms maximum frame time, and zero misses |
| Local identity profiles | Active: `profile_required`, preview, approval, activation, update, reset, stable identity selection, curated styling, firmware temperament, and local SQLite persistence |
| Semantic performances | Active: bounded beats, pacing hints, captions, modifiers, safe replace/reject/cancel, completion feedback, and automatic neutral restoration |

The physical rig passed the six-state visual check, synchronized-blink review, device-owned beat cycle, and a
60-second zero-miss soak. The USB-only build adds intensity-scaled expression
geometry; curious, uncertain, concerned, delighted, embarrassed, and reassuring poses; affect-specific gaze
and blink cadence; thinking/listening/success transition cues; restrained per-eye blink depth; and a gradual
neutral attention fade that resets immediately on new intent. The profile release adds curated color, energy,
blink, gaze, idle, and mouth personality without changing authored core geometry. A stable identity was
created, physically previewed, activated, reloaded in a fresh service, and used for an autonomous three-beat
scene that returned to its own neutral state with zero renderer misses.

Current scope is the classic Heltec WiFi Kit 32, Waveshare DualEye module, built-in OLED mouth, browser
simulator, and local MCP/HTTP host bridge. Other board ports and experimental renderers are not part of this
public release.

The user approved the violet/mint, lively, curious, playful identity and its attention → thinking → success
scene in the simulator. The matching profile is now active on the local face; its record and machine-specific
client configuration remain outside the repository. A dedicated human visual pass for the newest nuanced
micro-behaviors remains useful polish, not a release blocker.
