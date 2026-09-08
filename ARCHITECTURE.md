# Architecture

YouAndEye is a local expressive surface. Agents send semantic intent; the device owns pixels, timing,
blinks, gaze motion, and its neutral idle life.

```text
MCP client ──STDIO──> host service ─┬─USB serial──> classic ESP32 ──► two GC9D01 eyes
                          │         │                     └─────────► sleeping/fallback SSD1306
local app ──loopback HTTP─┤         └─USB serial──> ESP32-S3 ───────► optional CO5300 AMOLED mouth
                          └── local profile store
```

## Boundaries

- `host/youandeye/` validates `emote/1`, arbitrates sources, translates frames into bounded commands, and
  owns the serial lease.
- `firmware/` is the accepted classic Heltec runtime. Its shared components own eye pose tables, motion, and
  RGB565 rendering; the built-in OLED remains a self-contained fallback surface.
- `firmware/amoled-mouth/` is the optional round-mouth runtime. It owns a 21-affect pose vocabulary, staggered
  lip interpolation, anticipation, moving holds, speech visemes, restrained cartoon accents, text scrolling,
  icon rendering, completion feedback, and the CO5300 panel. It does not accept pixels or coordinates.
- `schema/` and `protocol/examples/` are the machine-readable contract.
- `simulator/` is the browser reference for poses and motion constants.
- Agent profiles are local host data keyed by stable semantic identity. They select only curated color,
  temperament, and mouth options; they never contain pixels or renderer geometry.
- Semantic performances are bounded host-owned lists of emotional beats. The host schedules them while each
  surface still owns interpolation, blinks, gaze motion, scrolling, and the final neutral rendering.

The release firmware is USB-only. It starts no access point, stores no Wi-Fi credentials, and exposes no
device HTTP server. The optional host HTTP adapter binds only to loopback.

## State flow

1. An MCP tool call or loopback HTTP request supplies an affect, behavior, gaze, utterance, TTL, and priority.
2. The host validates the complete frame before accepting any field.
3. A bounded arbiter selects one live source/session and retains replay state for a limited window.
4. Capability downmix maps the semantic frame independently to eye and mouth commands.
5. Each serial adapter verifies USB identity and its product-specific firmware `STATUS` signature before
   writing commands.
6. Firmware eases toward intensity-scaled geometry while continuing local blink, gaze, lip, idle behavior, and
   short affect-aware acting cues such as hesitation, attention, relief, gaze aversion, mouth anticipation,
   state-specific micro-motion, and locally timed speech visemes. Untouched neutral also develops a slow,
   bounded attention fade; the next semantic intent resets it immediately.
7. Expired intent decays to the selected profile's neutral baseline on both controllers.

The AMOLED path is an optional enhancement. Discovery happens without opening the port. If it is absent,
ambiguous, running the wrong firmware, or fails during delivery, the host replays the complete frame to the
Heltec and restores its OLED. This keeps existing one-board clients and hardware fully compatible.

On first contact, an unrecognized agent reports `profile_required` while continuing to use the safe default.
The profile lifecycle is `draft → previewed → approved → active`; updates return only the candidate to draft,
leaving the last approved profile active until its replacement is approved. Profiles are selected from
`YOUANDEYE_AGENT_ID`, falling back to the semantic source identity.

## Concurrency and failure behavior

- One scheduler thread handles all host-side expirations; frames do not create timers.
- Serial I/O is serialized separately from semantic state, so a slow device cannot block state inspection.
- Each serial connection has its own lease, releases after idle, and reacquires on demand.
- PlatformIO and local hardware tools use a marker plus an OS-level lease lock, so upload waits for actual
  serial release rather than a fixed delay.
- Repeated busy, absent, wrong-firmware, timeout, or I/O failures open a short retry circuit.
- A disconnect never freezes a reaction pose: firmware autonomy continues and the next successful command
  reconciles authoritative state.
- Starting a new scene may replace or reject an active scene. `neutral` and an explicit `perform` cancellation
  interrupt safely, and completion, cancellation, timeout, and failure all restore the selected neutral profile.
- Blink timing remains binocular; only a restrained 0–3% closure-depth difference is randomized so the face
  feels organic without repeating the previously rejected inter-eye lag.

## Design invariant

No agent-facing function accepts pixels, coordinates, display dimensions, animation keyframes, Wi-Fi
credentials, or arbitrary serial commands. New surfaces should implement the same semantic contract and
keep their hardware rendering below it.
