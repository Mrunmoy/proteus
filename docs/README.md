# Waveshare Hardware Documentation

Downloaded 2026-06-06 from the official Waveshare wikis.

- **RP2350-PiZero** — https://www.waveshare.com/wiki/RP2350-PiZero
- **GamePi13** — https://www.waveshare.com/wiki/GamePi13

The RP2350-PiZero is a Pi-Zero-form-factor board built on the Raspberry Pi
RP2350 MCU. GamePi13 is a 1.3" handheld game HAT (240×240 ST7789VW LCD) that
the board plugs into.

---

## RP2350-PiZero/

### schematics/
| File | What it is |
|------|------------|
| RP2350-PiZero-schematic.pdf | Board schematic |
| RP2350-PiZero-Pos.pdf | Component placement / position diagram |
| RP2350-PiZero-resources.zip | Combined Waveshare resource pack for the board |
| Pico-2-schematic.pdf | Reference Raspberry Pi Pico 2 schematic |
| Pico-2-Pinout.pdf | Pico 2 / RP2350 pinout reference |

### datasheets/
| File | What it is |
|------|------------|
| RP2350-datasheet.pdf | RP2350 MCU datasheet (Raspberry Pi) |
| Pico-2-datasheet.pdf | Raspberry Pi Pico 2 board datasheet |

### sdk-guides/
| File | What it is |
|------|------------|
| Getting-started-with-pico-2.pdf | Getting started (C/C++ & MicroPython) |
| Hardware-design-with-rp2350.pdf | Hardware design guide for RP2350 |
| Raspberry-pi-pico-2-c-sdk.pdf | C/C++ SDK manual |
| Raspberry-pi-pico-2-python-sdk.pdf | MicroPython SDK manual |

### demos-firmware/
| File | What it is |
|------|------------|
| Raspberry_Pi_Pico_Demo.zip | Official Pico C/Python demo code |
| WAVESHARE-RP2350-micropython-v1.26.0.zip | Waveshare MicroPython firmware (.uf2) |
| WAVESHARE-RP2350-micropython-v1.26.0-16MB.zip | Same, for 16 MB flash variant |
| WAVESHARE_RP2350B.zip | RP2350B firmware build |
| RPI_PICO-micropython-v1.25.0.zip | Upstream Pico MicroPython firmware |

### tools/
| File | What it is |
|------|------------|
| Image2Lcd2.9.zip | Image-to-LCD-array converter (Windows) |
| Thonny-3.3.3.zip | Thonny MicroPython IDE (Windows) |
| Zimo221-image-tool.7z | Zimo image/font conversion tool |

---

## GamePi13/

### schematics-datasheets/
| File | What it is |
|------|------------|
| 1.3inch_LCD_Module-schematic.pdf | 1.3" LCD module schematic |
| RPi-1.3inch-MINI-LCD.pdf | 1.3" mini LCD HAT user manual/spec |
| ST7789VW-LCD-controller-datasheet.pdf | ST7789VW display controller datasheet |

> Note: Waveshare's GamePi13 wiki does not publish a dedicated full-board
> schematic PDF; the LCD module schematic + controller datasheet above are the
> hardware references they provide.

### demos/
| File | What it is |
|------|------------|
| Gamepi13-RP2040-Demo.zip | RP2040/Pico demo (MicroPython) |
| Gamepi_RP2040_C.zip | RP2040/Pico demo (C) |
| Button_press_detector.zip | Button input example |
| Raspberry_Pi_Pico_Demo.zip | Generic Pico demo |
| Circuitpython_GamePi13.zip | CircuitPython demo |
| Micropython_GamePi13.zip | MicroPython demo |
| Flappybird.zip | Flappy Bird sample game |
| pico-vscode.zip | Offline VS Code + Pico toolchain bundle (~2.2 GB) |

### firmware/
| File | What it is |
|------|------------|
| RPI_PICO-micropython-v1.25.0.uf2 | MicroPython firmware for Pico |
| Thonny-3.3.3.zip | Thonny IDE |
| Waveshare13.dtbo | Raspberry Pi device-tree overlay (display) |
| Audremap18.dtbo | Raspberry Pi device-tree overlay (audio remap) |

### tools/
| File | What it is |
|------|------------|
| Image2Lcd2.9.zip | Image-to-LCD-array converter |
| Zimo221-image-tool.7z | Zimo image/font tool |
| flash_download_tool.zip | Espressif flash download tool |

---

## NOT downloaded (optional, large OS installer images)

The GamePi13 wiki also links two Raspberry Pi OS Lite images (~hundreds of MB
each) for running the HAT on a Raspberry Pi SBC. Grab them only if you plan to
use GamePi13 on a Pi rather than the RP2350 board:

- https://files.waveshare.com/wiki/GamePi13/Doc/2024-03-15-raspios-bookworm-arm64-lite.img.xz
- https://files.waveshare.com/wiki/GamePi13/Doc/2024-03-15-raspios-bookworm-armhf-lite.img.xz
