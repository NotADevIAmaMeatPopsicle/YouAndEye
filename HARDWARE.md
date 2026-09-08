# Hardware

The supported build uses these exact hardware families:

| Part | Role |
|---|---|
| Classic Heltec WiFi Kit 32 | Original ESP32 controller plus built-in 128×64 SSD1306 OLED mouth |
| Waveshare 0.71-inch DualEye LCD Module | Two 160×160 GC9D01 IPS eye panels, SKU 29381 |
| Optional Waveshare ESP32-S3-Touch-AMOLED-1.75 | Separate 1.75-inch 466×466 CO5300 AMOLED mouth controller |

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

## Optional round AMOLED mouth

The upgraded mouth uses Waveshare's ESP32-S3-Touch-AMOLED-1.75 family (SKUs 31261/31262/31264). The board
combines an ESP32-S3R8 at up to 240 MHz, 8 MB PSRAM, 16 MB flash, and a 1.75-inch 466×466, 16.7-million-color
CO5300 panel. The display is driven over QSPI on CS 12, clock 38, data 4/5/6/7, and reset 39. YouAndEye does
not initialize Wi-Fi, touch, audio, or sensors for this role. See the
[official Waveshare documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75).

This board has its own USB data cable and receives bounded semantic commands from the same local host as the
eyes. When its verified firmware signature is present, the Heltec OLED enters hardware sleep. If the round
mouth is removed or rejects a command, the host wakes the OLED and replays the current semantic frame.

## Electrical rules

- Disconnect USB and batteries before wiring.
- Use 3.3 V for the eye module in this build and keep all ESP32 signals at 3.3 V logic.
- Confirm pin 1 from the module marking or official pinout; do not trust cable colors.
- Check continuity and verify there is no short between `3V3` and `GND` before first power.
- Give the tiny SH1.0 connector strain relief before closing an enclosure.

## Identity and flashing

The supported Heltec normally appears through a Silicon Labs CP210x bridge with VID:PID `10C4:EA60`. The
AMOLED board uses Espressif native USB, normally VID:PID `303A:1001`. Port names are temporary observations,
never hardware identity. Before every upload, confirm the expected chip and current port. At runtime the host
also requires the correct YouAndEye firmware `STATUS` signature before sending expression commands.

See the [DIY build guide](docs/DIY_BUILD_GUIDE.md) for parts, assembly, upload, first boot, and recovery.
