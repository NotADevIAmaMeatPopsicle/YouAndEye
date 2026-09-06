# Native firmware checks

These tests execute hardware-independent C components on the host. They complement the PlatformIO build;
they do not replace a physical display check.

From the repository root on Windows, run the motion test:

```powershell
$testExe = Join-Path $env:TEMP 'youandeye-motion-test.exe'
uv run --with ziglang python -m ziglang cc -std=c11 -Wall -Wextra -Werror `
  -Ifirmware/components/emote_motion/include `
  -Ifirmware/components/emote_state/include `
  tests/native/test_emote_motion.c `
  firmware/components/emote_motion/emote_motion.c `
  firmware/components/emote_state/emote_state.c `
  -lm -o $testExe
& $testExe
```

Run the RGB565 renderer test:

```powershell
$testExe = Join-Path $env:TEMP 'youandeye-renderer-test.exe'
uv run --with ziglang python -m ziglang cc -std=c11 -Wall -Wextra -Werror `
  -Ifirmware/components/eye_renderer/include `
  -Ifirmware/components/emote_state/include `
  tests/native/test_eye_renderer.c `
  firmware/components/eye_renderer/eye_renderer.c `
  firmware/components/emote_state/emote_state.c `
  -lm -o $testExe
& $testExe
```

The first test protects synchronized blink timing, gaze arcs, idle motion, finite spring integration, and
channel ordering. The second rejects blank or collapsed core expressions at both target and stress-test
resolutions.
