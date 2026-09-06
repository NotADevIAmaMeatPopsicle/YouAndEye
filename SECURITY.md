# Security policy

## Supported release

Security fixes are made on the default branch. This is a local-first DIY hardware project, so keep the host
bridge and physical device attached only to a computer you trust.

## Boundary

- Release firmware communicates over USB serial and does not start Wi-Fi, an access point, mDNS, or a
  device HTTP server.
- The optional host HTTP adapter binds to loopback by default and rejects non-loopback browser origins.
- The MCP server uses local STDIO. It exposes semantic expression tools, not arbitrary serial or pixel
  commands.
- Serial discovery requires the expected CP210x VID/PID, fails closed on ambiguity, and verifies a
  YouAndEye runtime signature before sending expression commands. An exact USB serial may be supplied with
  `YOUANDEYE_USB_SERIAL` when a machine has multiple otherwise identical adapters.
- Do not commit API keys, Wi-Fi credentials, machine-specific ports, device identifiers, or local paths.

## Reporting

Please use GitHub's private vulnerability-reporting feature if it is enabled for the repository. Otherwise,
open a minimal issue that does not disclose exploit details or personal data and ask for a private contact.
