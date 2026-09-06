<div align="center">

# YouAndEye 👀

### A tiny DIY face for an AI with something to say

Two little IPS eyes, one pocket-size OLED mouth, and a classic ESP32 doing its best impression of being alive.

![YouAndEye concept sheet](docs/assets/youandeye-concept-sheet.png)

![Hardware tested](https://img.shields.io/badge/hardware-tested-2ea44f)
![Tests](https://img.shields.io/badge/tests-61%2F61-2ea44f)
![ESP32](https://img.shields.io/badge/ESP32-PlatformIO-00979d)
![MCP](https://img.shields.io/badge/interface-MCP-7c3aed)
![License](https://img.shields.io/badge/license-MIT-f2c744)

**[Build one](docs/DIY_BUILD_GUIDE.md)** · **[Meet the face API](docs/MCP.md)** ·
**[Open the expression bench](simulator/index.html)**

</div>

## Hello, little face

YouAndEye is an expressive physical face for local or cloud-connected AI agents. The agent says *what it
means*—happy, thinking, suspicious, listening—and the device decides how to perform it: eye shape, brows,
gaze, blink timing, mouth motion, text, and small character beats.

The face does not need a stream of animation frames from a computer. Once powered, it keeps breathing,
looking around, and blinking locally. Disconnect the agent and it still has a pulse.

## What is inside?

| Part | Job |
|---|---|
| Classic Heltec WiFi Kit 32 | Runs the renderer and contributes its built-in 128×64 OLED mouth |
| Waveshare 0.71-inch DualEye LCD | Two 160×160 GC9D01 round IPS eyes on one compact board |
| `firmware/` | Fifteen expressions, autonomous motion, mouth shapes, text, and synchronized beats |
| Expression Bench | Exact-size browser playground for tuning before flashing hardware |
| `emote/1` | Transport-independent semantic expression contract |
| Local MCP server | Four safe tools that let Codex or another MCP client wear the face |

The accepted build sustains about 30 FPS on the classic ESP32 with synchronized blinking and zero deadline
misses in its final 60-second hardware soak. Release firmware is USB-only: it starts no access point, stores
no Wi-Fi credentials, and exposes no device network service.

## Build one over a weekend

The complete [DIY build guide](docs/DIY_BUILD_GUIDE.md) includes:

- exact board-generation warnings and official source links
- a shopping list and the proven 11-wire pin map
- careful first-power and upload steps
- eye, mouth, serial, and MCP smoke tests
- enclosure ideas and friendly failure recovery

The short version is delightfully small:

```text
Waveshare DualEye ── SPI ──► classic Heltec WiFi Kit 32
       two eyes                    brain + OLED mouth
                                      │
                                   USB/MCP
                                      │
                                  your agent
```

> [!IMPORTANT]
> Heltec's current WiFi Kit 32 is an ESP32-S3 revision. This working build uses the older classic ESP32
> target named `heltec_wifi_kit_32` by PlatformIO. Read the buying note before ordering.

## Try the software first

No hardware is required to explore the expressions:

```powershell
python -m http.server 4173
```

Open `http://127.0.0.1:4173/simulator/`, choose an expression, and watch the same
semantic motion model used by the firmware.

## Build and test

```powershell
uv sync --extra serial
uv run --extra serial python -m unittest discover -s tests -p 'test_*.py' -v
python -m platformio run -d firmware -e heltec_wifi_kit_32
```

The firmware pins Espressif32 6.12.0 and carries a small, licensed Arduino GFX 1.6.4 subset containing
only the GC9D01 and ESP32 SPI code it uses. A fresh checkout therefore cannot silently pull incompatible
display code.

Hardware writes are deliberately separate from builds. The DIY guide walks through identifying the board
before supplying an upload port.

## Give the face a feeling

The preferred agent boundary is local STDIO MCP:

```text
express(affect="thinking", message="PLEASE WAIT...", text_mode="scroll")
express(affect="success", sequence="celebrate")
neutral()
```

| Tool | Meaning |
|---|---|
| `express` | Show a temporary affect, message, or device-owned character beat |
| `face_status` | Read connection state, active expression, FPS, timing, and display health |
| `face_capabilities` | Discover the face without opening its serial port |
| `neutral` | Clear pending intent and return to autonomous neutral |

See [the MCP guide](docs/MCP.md) for Codex setup, other MCP clients, the loopback-only HTTP bridge, and safety
behavior. The project is local-first and fully usable offline; a trusted desktop client can optionally make
the tools available to a cloud agent without turning the microcontroller into a public service.

## Design rules

- **Agents express intent. Surfaces perform it.** Models never receive raw pixel or keyframe controls.
- **Life belongs on the device.** Blinks, saccades, easing, and idle behavior survive host hiccups.
- **Simulator before solder.** Visual ideas graduate in the browser before reaching firmware.
- **Readable beats realistic.** Six core expressions must remain recognizable at actual eye size.
- **Fail softly.** Expired or interrupted host intent returns to an autonomous neutral face.

## Project map

| Path | What lives there |
|---|---|
| [`docs/DIY_BUILD_GUIDE.md`](docs/DIY_BUILD_GUIDE.md) | Parts, wiring, flashing, first boot, enclosure, and troubleshooting |
| [`simulator/`](simulator/) | Interactive expression and motion simulator |
| [`firmware/`](firmware/) | Accepted classic-ESP32 firmware |
| [`host/youandeye/`](host/youandeye/) | MCP, HTTP, arbitration, validation, and verified serial bridge |
| [`schema/`](schema/) | Canonical `emote/1` JSON schemas |
| [`docs/MCP.md`](docs/MCP.md) | Agent interface and client setup |
| [`STATUS.md`](STATUS.md) | Concise release status and verification evidence |

Engineers joining the project should begin with this README and [`ARCHITECTURE.md`](ARCHITECTURE.md). Humans
who just want a charming desk creature should begin with the [DIY guide](docs/DIY_BUILD_GUIDE.md).

## Known-good release

The accepted Heltec behavior provides fifteen affects, five coordinated character beats, an animated mouth,
smooth text scrolling, native icons, and synchronized binocular blinking. Its paired host stack adds the
local MCP/HTTP control plane. Current build sizes, tests, and limitations are recorded in [`STATUS.md`](STATUS.md).

## Make it yours

Try a cardboard face. Sculpt one from foam clay. Put the eyes in a robot, a puppet, or a tiny haunted radio.
The protocol deliberately separates personality from hardware, so a completely different shell can keep the
same emotional vocabulary.

Please document the exact hardware you test, and share photos of anything especially adorable or unsettling.

YouAndEye is free software under the [MIT License](LICENSE). Dependency terms are collected in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
