# Mouse bridge for ESP32-S31 (experimental)

An ESP-IDF version of the bridge. It behaves like the [Pico firmware](../mouse_bridge/mouse_bridge.ino): it pairs with a Bluetooth Classic mouse, remembers it, and shows up on any computer as a USB mouse. The Pico remains the recommended build.

**Status: early and untested.** It compiles in CI against ESP-IDF v6.1, but nothing here has run on hardware, including Classic HID on the S31 itself. It isn't part of releases: binaries are in the `mouse_bridge-esp32s31` artifact of each `build` run, kept for 7 days.

## Choosing a board

The bridge has to act as a USB *device*, and that needs the S31's USB OTG port. Espressif's three S31 boards:

| Board | Where OTG goes | Fits? |
|---|---|---|
| [ESP-Mosaico](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp-mosaico/user_guide.html) | Type-C port, which its factory firmware uses as a USB serial device | Looks suitable, untested |
| ESP32-S31-Function-CoreBoard-1 | Type-A host port with a 500 mA power switch | No |
| ESP32-S31-Korvo-1 | Type-A host port, 500 mA | No |

The other Type-C ports on the last two boards won't work: USB Serial/JTAG and the UART bridge can't present a mouse.

**Don't connect either Type-A port to a computer with an A-to-A cable.** Both ends may drive 5 V onto the same wire.

Open questions for the ESP-Mosaico:

- **Pins:** the LED and button defaults below are for the Function-CoreBoard-1, so they need changing.
- **Flashing and logs:** once the firmware uses the Type-C port as a mouse, these may have to go through the USB Serial/JTAG pins on the header (GPIO33 and GPIO34). See [ESP-IDF issue #18992](https://github.com/espressif/esp-idf/issues/18992).
- **Power:** the port can also supply power, so check that it takes power from the computer.
- **Availability:** price and stock aren't confirmed.

## Behavior

- **LED:** fast blink = pairing, slow blink = reconnecting, solid = connected
- **Forget the mouse:** hold BOOT for 3 seconds, then release to reboot into pairing mode
- **Reconnecting:** the bridge pages the saved mouse, and also accepts a connection from it
- **Report parsing:** reads button, movement and wheel positions from the mouse's HID report map, and falls back to boot protocol (no wheel) if it finds no layout

The LED and button pins and `WHEEL_INVERT` are at the top of [main/main.c](main/main.c). The defaults match the Function-CoreBoard-1. Logs go to the default console. Logic shared with the Pico firmware is in [bridge_logic.h](../mouse_bridge/bridge_logic.h).

## Build

Requires ESP-IDF v6.1 or newer. CI builds this on every pull request and on `main`.

```console
idf.py build
idf.py -p PORT flash monitor
```

## Test

The report parser and USB forwarding run on a PC, with no hardware: see [tests/fuzz](../../tests/fuzz/README.md) for the fuzzer and [Testing](../../CONTRIBUTING.md#testing) for the unit tests.
