# mouse-bridge

[![build](https://github.com/owenpkent/mouse-bridge/actions/workflows/build.yml/badge.svg)](https://github.com/owenpkent/mouse-bridge/actions/workflows/build.yml) [![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Use one **Bluetooth Classic** mouse with several computers by moving a small dongle between them, with no re-pairing.

A normal Bluetooth adapter is only a radio: the pairing lives in the computer's OS, so the mouse stays tied to one machine (and Bluetooth mice bypass KVM switches for the same reason). mouse-bridge is a **HID proxy** instead. A Raspberry Pi Pico 2 W pairs with the mouse, keeps the pairing in its own flash, and shows up on any computer as an ordinary wired USB mouse.

Status: firmware builds in CI but is **not yet tested on hardware**. If you have a Pico W or Pico 2 W and a Bluetooth mouse, a [mouse report](https://github.com/owenpkent/mouse-bridge/issues/new?template=mouse-report.yml) is the most useful contribution right now. See [TODO.md](TODO.md).

## Hardware

- Raspberry Pi Pico 2 W (Pico W also works; Pimoroni Pico Plus 2 W for USB-C)
- Micro-USB **data** cable
- Optional: a button USB switch, if moving the dongle by hand gets tedious

## Flash

**No tools needed:** download `mouse_bridge-rpipico2w.uf2` (or `-rpipicow.uf2`) from the latest [release](https://github.com/owenpkent/mouse-bridge/releases), or from the latest `build` run under [Actions](https://github.com/owenpkent/mouse-bridge/actions/workflows/build.yml) for unreleased changes. Hold BOOTSEL while plugging the Pico in, and drop the `.uf2` onto the drive that appears.

**From Arduino IDE:** install the arduino-pico core (Earle Philhower), open [firmware/mouse_bridge/mouse_bridge.ino](firmware/mouse_bridge/mouse_bridge.ino), then set:

| Tools menu | Value |
|---|---|
| Board | Raspberry Pi Pico 2 W (or Pico W) |
| IP/Bluetooth Stack | IPv4 + Bluetooth |
| USB Stack | Pico SDK |

## Use

| LED | Meaning |
|---|---|
| Fast blink | Pairing: put the mouse in pairing mode |
| Slow blink | Reconnecting: move or click the mouse to wake it |
| Solid | Connected |

- First boot scans for a pointing device, connects to the strongest one and saves it.
- Later boots reconnect to the saved mouse on their own.
- **Hold BOOTSEL for 3 seconds** to forget the mouse and go back to pairing.

## Config

Flags at the top of the sketch:

| Flag | Use |
|---|---|
| `BUTTON_INDEX_BASE` | Set to 1 if left-click registers as right-click |
| `WHEEL_INVERT` | Set to 1 if scrolling runs backwards |
| `DEBUG` | Serial log at 115200 baud (scan results, connect attempts) |

## How it works

- `BluetoothHIDMaster` runs a Bluetooth Classic HID host. BTstack stores the link key in flash; the sketch stores the mouse address (emulated EEPROM).
- BT callbacks only accumulate movement and button state. `loop()` sends USB reports, as the library docs recommend.
- Large movements are split into several reports to fit 8-bit USB mouse deltas.

## Compatible mice

Filled in from [mouse reports](https://github.com/owenpkent/mouse-bridge/issues?q=label%3Amouse-report). Bluetooth Classic mice are the target; BLE-only mice are not supported yet.

| Mouse | Board | Result | Notes |
|---|---|---|---|
| *none reported yet* | | | |

## Why not something else

Design notes, the options surveyed (ESP32, HID Remapper, Pi Zero, Adafruit's keyboard bridge) and fallbacks (Bluepad32, Mouse Without Borders) are in [docs/project-summary.md](docs/project-summary.md).

## Contributing

Mouse reports, bug reports and pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md). Questions go in [Discussions](https://github.com/owenpkent/mouse-bridge/discussions). Report security issues privately per [SECURITY.md](SECURITY.md).

## License

MIT, see [LICENSE](LICENSE).
