# Third-party notices

YouAndEye is released under the [MIT License](LICENSE). The firmware includes this pinned third-party
component:

## Arduino GFX

The repository includes a minimal Arduino GFX Library 1.6.4 subset providing the GC9D01 display and
ESP32 SPI support. It remains under the upstream BSD license included at
[`third_party/Arduino_GFX/license.txt`](third_party/Arduino_GFX/license.txt). The Heltec SSD1306 mouth
driver is implemented directly by YouAndEye and does not use Arduino GFX. Unused display, bus, canvas,
and optional U8g2/CJK font sources are not vendored.

Copyright notices in third-party source files remain the property of their respective authors.
