# Round AMOLED mouth firmware

This optional firmware turns a Waveshare ESP32-S3-Touch-AMOLED-1.75 into YouAndEye's independent mouth and
caption surface. It drives the 466×466 CO5300 panel over QSPI, uses PSRAM for a flicker-free canvas, and keeps
all animation local. Wi-Fi, touch, microphone, speaker, and sensors are not initialized.

The expressive renderer authors a distinct mouth performance for all 38 canonical affects. It combines
high-resolution lip silhouettes with optional teeth and tongue layers, anticipation, staggered interpolation,
moving holds, six local speech visemes, and restrained thought, sparkle, heart, blush, sweat, tear, question,
sick-bubble, or alert accents.
The `minimal` and `text_friendly` profile styles remove decorative layers without changing the semantic state.

Build without touching hardware:

```powershell
python -m platformio run -d firmware/amoled-mouth -e waveshare_amoled_mouth
```

After verifying the current native-USB port belongs to the expected ESP32-S3 board, upload with:

```powershell
python -m platformio run -d firmware/amoled-mouth -e waveshare_amoled_mouth `
  --target upload --upload-port <AMOLED_PORT>
```

At 115200 baud, `STATUS` must report `product=youandeye-mouth`, `display=co5300`, `size=466x466`, and
`animation=concept-v3`.
The host requires that signature before it sends anything else.

The private serial vocabulary is intentionally semantic: curated `PROFILE`, `AFFECT`, `MOUTH`, `TEXT`,
`SCROLL`, `ICON`, and `BEAT` commands. No command accepts pixels, geometry, coordinates, frame timing, or
network configuration. Agents use the MCP tools documented in [`../../docs/MCP.md`](../../docs/MCP.md), not
this device protocol directly.
