# Project status

The dual-controller expression release is built and flashed for the verified classic-ESP32 eyes and
optional Waveshare round-AMOLED mouth. Per-agent personality remains bounded by the semantic/render split and
the six core readability gate.

| Area | Result |
|---|---|
| Python contract/host suite | 94/94 passing, including complete 21-affect mouth parity, dual-device fan-out, independent leases, and OLED fallback |
| Native motion test | Passing, including identity bounds, semantic modifier clamps, core geometry preservation, synchronized blink, long-timescale attention, and 60-second deterministic idle checks |
| Native RGB565 renderer test | Passing at 240×320 and 160×160, including fast 160×160 path |
| Classic Heltec firmware build | Passing; 77,032 B RAM (23.5%), 363,925 B flash (27.8%) |
| Round AMOLED firmware build | Passing; 21,152 B internal RAM (6.5%), 419,176 B flash (6.4%), 8 MB PSRAM detected live |
| Firmware network surface | Removed; USB serial only |
| Host serial ownership | Idle release, typed failures, retry circuit, cooperative marker, and OS lease lock |
| Expression Bench | All 21 affects have authored mouth poses; semantic mouth studio and selectable 128×64 OLED/466×466 AMOLED heads visually inspected |
| Current physical release | Both controllers flashed with verified upload hashes; a nine-beat semantic showcase completed across both surfaces and restored neutral, with the eye renderer reporting 31.3 FPS and AMOLED firmware 0.4.0 reporting `expressive-v2` |
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

The round mouth now gives every canonical affect a deliberate silhouette and local performance. Open smiles,
gasps, pucker, skeptical and sheepish asymmetry, worried and uncertain shapes, teeth, tongue, and restrained
thought/sparkle/heart/blush/sweat/alert accents are interpolated on-device. Speech cycles through six local
visemes, and text or icons still preempt cleanly and report scrolling completion.

The connected classic eye controller and round AMOLED mouth were both flashed for this release. A combined
attention → thought → curiosity → surprise → play → delight → affection → reassurance → success performance
completed through the semantic scene layer, exercised both surfaces, and restored neutral automatically. The
Heltec OLED remains asleep while the AMOLED is available so the eye controller can concentrate on rendering;
the existing fallback restores the built-in OLED when the external mouth is absent.

Current scope is the classic Heltec WiFi Kit 32, Waveshare DualEye module, built-in OLED fallback, optional
Waveshare ESP32-S3-Touch-AMOLED-1.75 mouth, browser simulator, and local MCP/HTTP host bridge. Other board
ports and experimental renderers are not part of this public release.

The user approved the violet/mint, lively, curious, playful identity and its attention → thinking → success
scene in the simulator. The matching profile is now active on the local face; its record and machine-specific
client configuration remain outside the repository. A dedicated human visual pass for the newest nuanced
micro-behaviors remains useful polish, not a release blocker.
