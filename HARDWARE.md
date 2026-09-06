# Hardware

The supported build uses these exact hardware families:

| Part | Role |
|---|---|
| Classic Heltec WiFi Kit 32 | Original ESP32 controller plus built-in 128×64 SSD1306 OLED mouth |
| Waveshare 0.71-inch DualEye LCD Module | Two 160×160 GC9D01 IPS eye panels, SKU 29381 |

> [!IMPORTANT]
> Heltec reused the “WiFi Kit 32” name for newer ESP32-S3 boards. This firmware targets PlatformIO board
> `heltec_wifi_kit_32`, the classic ESP32 version. The S3 product is not a drop-in replacement.

## Proven wiring

| DualEye signal | Heltec connection |
|---|---:|
| `VCC` / `GND` | `3V3` / `GND` |
| `DIN` / `CLK` | GPIO 23 / GPIO 18 |
| `CS1` / `CS2` | GPIO 5 / GPIO 17 |
| `DC` | GPIO 21 |
| `RST1` / `RST2` | GPIO 22 / GPIO 19 |
| `BL1` / `BL2` | GPIO 25 / GPIO 26 |

The built-in OLED remains on SDA GPIO 4, SCL GPIO 15, reset GPIO 16, address `0x3C`. The eye bus uses the
verified 40 MHz baseline. The accepted side-by-side panel rotations are `1,3`.

## Electrical rules

- Disconnect USB and batteries before wiring.
- Use 3.3 V for the eye module in this build and keep all ESP32 signals at 3.3 V logic.
- Confirm pin 1 from the module marking or official pinout; do not trust cable colors.
- Check continuity and verify there is no short between `3V3` and `GND` before first power.
- Give the tiny SH1.0 connector strain relief before closing an enclosure.

## Identity and flashing

The supported controller normally appears through a Silicon Labs CP210x bridge with VID:PID `10C4:EA60`.
Port names are temporary observations, never hardware identity. Before every upload, confirm the classic
ESP32 target and current port. The host additionally verifies the YouAndEye `STATUS` signature before it
sends expression commands.

See the [DIY build guide](docs/DIY_BUILD_GUIDE.md) for parts, assembly, upload, first boot, and recovery.
