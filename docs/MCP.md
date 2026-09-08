# Agent interface

YouAndEye preserves its original four semantic tools and adds two bounded identity/performance tools through
a local STDIO MCP server. The MCP process validates intent,
arbitrates sources, and owns the verified USB serial connection; agents never control pixels or arbitrary
serial commands.

## Install

From the repository root:

```powershell
uv sync --extra serial
uv run --extra serial youandeye-mcp
```

Configure an MCP client to run the equivalent command from its own configuration format:

```text
uv run --directory <PATH_TO_YOUANDEYE> --extra serial youandeye-mcp
```

Optional environment variables:

| Variable | Default | Purpose |
|---|---|---|
| `YOUANDEYE_PORT` | `auto` | Explicit serial port or safe single-device discovery |
| `YOUANDEYE_USB_SERIAL` | unset | Optional exact adapter serial when identical devices are attached |
| `YOUANDEYE_BAUDRATE` | `115200` | Firmware serial rate |
| `YOUANDEYE_SOURCE_ID` | `agent` | Human-readable source label |
| `YOUANDEYE_AGENT_ID` | source id | Stable identity used to select a local profile |
| `YOUANDEYE_PROFILE_DB` | OS user data directory | Optional profile database override |
| `YOUANDEYE_YIELD_PATH` | repository-local | Override the cooperative upload marker location |

The included `tools/install_claude_desktop_mcp.ps1` can add the STDIO entry to Claude Desktop without
committing machine paths:

```powershell
powershell -ExecutionPolicy Bypass -File tools/install_claude_desktop_mcp.ps1 -DryRun
powershell -ExecutionPolicy Bypass -File tools/install_claude_desktop_mcp.ps1
```

Always inspect the dry-run output first. Restart the client after changing its configuration.

## Tools

### `express`

Shows one temporary semantic state. Common examples:

```text
express(affect="thinking", message="PLEASE WAIT...", text_mode="scroll")
express(affect="success", sequence="celebrate")
express(affect="listening", ttl_ms=10000)
express(affect="uncertain", intensity=0.4, ttl_ms=2500)
express(affect="reassuring", ttl_ms=3500)
```

Important arguments are `affect`, `intensity`, optional `message`, `text_mode`, optional `sequence`,
`ttl_ms`, `priority`, `gaze`, `behavior_mode`, and `autonomy`. A device-owned sequence and an explicit
message are mutually exclusive.

Prefer eyes-only calls for ordinary listening, thinking, uncertainty, and reassurance. Add mouth text when it
clarifies content, confirms an important action, or provides useful status; it should support the expression,
not carry the emotion by itself. Intensity changes the rendered geometry rather than merely tagging the frame.

### `face_status`

Reads the active semantic state and live firmware telemetry, including connection, renderer, display,
mouth mode, frame rate, and missed deadlines. It may open the serial connection.

### `face_capabilities`

Returns supported affects, sequences, channels, limits, and discovery state without opening the port.

### `neutral`

Clears pending host intent and immediately restores the autonomous neutral face.

### `configure_profile`

Manages the profile bound to `YOUANDEYE_AGENT_ID`. A first run reports `profile_required`, but the original
expression tools continue through the safe default. The approval flow is deliberately explicit:

```text
create → preview → ask the user → approve → activate
```

`update` creates a new draft while preserving the last approved identity. `reset` removes only the current
agent's local profile. The preview performs neutral, listening, thinking, and success before returning to the
correct neutral face. Available values are curated rather than arbitrary colors or animation parameters.

Profiles are stored locally at `%LOCALAPPDATA%\YouAndEye\profiles\profiles-v1.sqlite3` on Windows (or the
platform user-data equivalent). Set `YOUANDEYE_PROFILE_DB` only when an integration needs another local path.
The database and its journal files are ignored by Git.

### `perform`

Accepts a complete 1–16 beat scene in one call. Each beat supplies an affect, a pacing word (`glance`,
`brief`, `normal`, `held`, or `lingering`), an optional caption, and optional semantic modifiers: warmth,
confidence, urgency, and `none`/`brief`/`moderate` gaze aversion. The host calculates dwell and scrolling time.
On the current firmware it also reads the OLED's scroll-completion signal, with the calculated envelope kept
as a compatibility fallback for older or alternate surfaces.

`perform(action="start")` returns completion directly by default and derives its deadline from the complete
scene; set `wait_timeout_ms` only when a caller needs a shorter explicit ceiling. Use
`wait_for_completion=false` and `perform(action="status")` for asynchronous work. `perform(action="cancel")`, `neutral`, or a new `express`
call safely interrupts the scene. Completion, cancellation, timeout, and failure restore either the active
profile's neutral or the explicitly selected safe neutral policy.

### `youandeye://profile`

This read-only resource reports `profile_required`, draft/preview/approval state, active styling, and the safe
fallback. Agents should read it once at session start and never approve their own preview without the user.

## Serial ownership

Only one process can own the board's serial port. The MCP service holds it while active, releases it after
10 seconds of inactivity, and reacquires it on demand. PlatformIO uploads use a repository-local yield marker
plus an OS-level lease lock; upload waits until the service has actually closed the port.

For another hardware command, use the same handoff explicitly:

```powershell
uv run --extra serial youandeye-port-yield run -- <YOUR_COMMAND>
```

The marker expires automatically if a process crashes. Repeated connection failures also enter a short
cooldown so a busy or missing device cannot make every tool call stall.

## Typed failures

| Code | Meaning |
|---|---|
| `device_absent` | No approved CP210x device is present |
| `device_ambiguous` | More than one approved adapter was found; configure an exact device |
| `wrong_device` | The requested port does not match the required USB identity |
| `wrong_firmware` | The serial endpoint lacks the YouAndEye `STATUS` signature |
| `port_busy` | Another local process owns the serial resource |
| `port_yielded` | A local upload or hardware tool requested the port |
| `retry_cooldown` | Repeated failures temporarily paused reconnect attempts |
| `communication_timeout` | The verified firmware did not acknowledge in time |
| `serial_io` | An established serial operation failed |

After a recoverable error, call `face_status`; once connected, call `neutral` if the visible state is not
the one expected.

## Optional loopback HTTP

Local applications can run:

```powershell
uv run --extra serial youandeye-bridge --heltec-port auto
```

The bridge offers `POST /v1/frames`, `GET /v1/state`, `GET /v1/capabilities`, `GET /v1/device`, and
`GET /health` on `127.0.0.1:8765`. It refuses non-loopback binds and rejects non-loopback browser origins.
It is a local integration API, not an authenticated remote service.

## Trust boundary

STDIO MCP cannot distinguish individual upstream callers. Trusting a desktop client's MCP connection means
trusting that client to invoke the six tools. The microcontroller itself is USB-only and exposes no Wi-Fi,
access point, mDNS, or HTTP service.
