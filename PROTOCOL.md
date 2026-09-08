# `emote/1` protocol

The canonical JSON Schemas live in [`schema/`](schema/). Checked-in examples live in
[`protocol/examples/`](protocol/examples/). Receivers validate a complete message and reject it atomically;
they never clamp malformed fields into a partially valid state.

## Frame

An `emote/1` frame is expiring state, not a queued animation:

| Field | Meaning |
|---|---|
| `seq` | Monotonic number within one source session |
| `source` | Stable source id and restart-scoped session id |
| `affect` | Named state plus intensity, valence, and arousal |
| `behavior` | Mode, autonomy, energy, engagement, and speaking amount |
| `gaze` | Social target or normalized tracking point |
| `utterance` | Text, icon, speech, or silence intent |
| `sequence` | Optional device-owned acting beat |
| `channel_policy` | Per-channel `auto`, `render`, or `mute` |
| `ttl_ms` | Hold time measured by the receiver |
| `decay` | Return-to-baseline curve and duration |
| `priority` | `ambient`, `normal`, `alert`, or `critical` |
| `cause` | Diagnostic context; never rendered automatically |

The stable affect vocabulary is `neutral`, `happy`, `surprised`, `thinking`, `suspicious`, `sad`, `excited`,
`love`, `error`, `listening`, `speaking`, `working`, `success`, `playful`, `encouraging`, `curious`,
`uncertain`, `concerned`, `delighted`, `embarrassed`, and `reassuring`. The core visual readability gate
remains neutral, thinking, happy, surprised, suspicious, and error.

Intensity is semantic, not decorative metadata. A value of `0` relaxes an affect toward neutral geometry;
the authored intensity reproduces the canonical pose, and higher values strengthen it within renderer-safe
limits. Surfaces may add small affect-aware gestures and local asymmetry, but those behaviors must not change
the requested state or desynchronize the two eyes in time.

## Arbitration

Priority weights are `ambient=10`, `normal=50`, `alert=80`, and `critical=100`.

- A higher band preempts immediately.
- At equal priority, the active source/session owns the surface until expiry.
- A preempted candidate resumes only if it is still live.
- Every band obeys TTL and decay; no expression is permanent.
- The host tracks at most 256 live/recent sessions and keeps completed sequence records for 15 minutes.
- A stale or over-capacity submission is rejected without changing active state.

## Capabilities and transports

Capability descriptors declare the surface, channels, supported affects and sequences, limits, and
transports. Unsupported affects downmix by valence/arousal and ultimately fall back to neutral.

| Transport | Role |
|---|---|
| MCP STDIO | Preferred local agent boundary: the original four tools plus profile and performance controls |
| Loopback HTTP | Local app integration through `/v1/frames`, `/v1/state`, and `/v1/capabilities` |
| USB serial | Verified physical transport beneath the host service |

MCP STDIO authenticates no individual caller; the trusted desktop client is the security boundary. The
HTTP adapter refuses non-loopback binds and rejects non-loopback browser origins. Release firmware has no
network transport.

## Local identity profiles

`youandeye.profile/1` is a host-side identity document. It is keyed by a stable agent id and uses curated
enums for iris palette, accent, blink style, gaze style, idle temperament, and mouth style. Energy is bounded
to `0.25–0.85`, and signature acknowledgements are limited to 24 characters. The lifecycle is:

```text
profile_required → draft → previewed → approved → active
                         ↖──── update ────┘
```

The safe default remains usable at every first-run boundary. Updating a profile does not replace the last
active revision until the revised candidate is previewed, approved, and activated. Local profile records are
stored outside the repository and are never sent to firmware as arbitrary drawing instructions.

## Semantic performances

`youandeye.performance/1` carries 1–16 ordered beats. Each beat names an affect, a pacing hint, an optional
caption or existing device-owned sequence, and bounded modifiers for warmth, confidence, urgency, and gaze
aversion. It contains no duration keyframes, coordinates, or pixels. The host derives timing from each
surface's bounded motion and scroll envelope, exposes running/completed/cancelled/timed-out/failed feedback,
and restores profile-neutral after every terminal path.

The classic-ESP32 adapter carries identity through a bounded `PROFILE` command and one-shot scene context
through `CONTEXT`. Those are private host-to-firmware details, not agent APIs: they accept only named styles,
semantic gaze choices, and bounded scalar modifiers. `MOUTH STATUS` reports `scrollComplete`, allowing the
host to advance after the physical OLED finishes; older or alternate surfaces use a conservative time bound.

## Safety limits

- Utterance text is capped at 128 Unicode code points before device transliteration.
- Agent tools expose named gaze targets, not raw pixels.
- The firmware rejects malformed or oversized command batches and requires acknowledgements.
- Flash-rate and animation timing remain device-owned.
- On expiry, disconnect, or host failure, the device returns to its autonomous neutral baseline.
