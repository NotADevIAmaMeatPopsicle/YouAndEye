# Architecture

YouAndEye is a local expressive surface. Agents send semantic intent; the device owns pixels, timing,
blinks, gaze motion, and its neutral idle life.

```text
MCP client ──STDIO──> host service ──USB serial──> classic ESP32
                          │                           ├─ two GC9D01 eyes
local app ──loopback HTTP─┘                           └─ built-in SSD1306 mouth
```

## Boundaries

- `host/youandeye/` validates `emote/1`, arbitrates sources, translates frames into bounded commands, and
  owns the serial lease.
- `firmware/` is the accepted classic Heltec runtime. Its shared components own pose tables, motion, and
  RGB565 rendering; the board layer owns both LCDs and the OLED.
- `schema/` and `protocol/examples/` are the machine-readable contract.
- `simulator/` is the browser reference for poses and motion constants.

The release firmware is USB-only. It starts no access point, stores no Wi-Fi credentials, and exposes no
device HTTP server. The optional host HTTP adapter binds only to loopback.

## State flow

1. An MCP tool call or loopback HTTP request supplies an affect, behavior, gaze, utterance, TTL, and priority.
2. The host validates the complete frame before accepting any field.
3. A bounded arbiter selects one live source/session and retains replay state for a limited window.
4. Capability downmix maps the semantic frame to eye and OLED commands.
5. The serial adapter verifies the USB identity and firmware `STATUS` signature before writing commands.
6. Firmware eases toward the target while continuing local blink, gaze, and idle behavior.
7. Expired intent decays to the compiled-in neutral baseline.

## Concurrency and failure behavior

- One scheduler thread handles all host-side expirations; frames do not create timers.
- Serial I/O is serialized separately from semantic state, so a slow device cannot block state inspection.
- The connection is released after idle and reacquired on demand.
- PlatformIO and local hardware tools use a marker plus an OS-level lease lock, so upload waits for actual
  serial release rather than a fixed delay.
- Repeated busy, absent, wrong-firmware, timeout, or I/O failures open a short retry circuit.
- A disconnect never freezes a reaction pose: firmware autonomy continues and the next successful command
  reconciles authoritative state.

## Design invariant

No agent-facing function accepts pixels, coordinates, display dimensions, animation keyframes, Wi-Fi
credentials, or arbitrary serial commands. New surfaces should implement the same semantic contract and
keep their hardware rendering below it.
