# Mouse bridge for ESP32-S31 (experimental)

An ESP-IDF version of the bridge. It behaves like the [Pico firmware](../mouse_bridge/mouse_bridge.ino): it pairs with a Bluetooth Classic mouse, remembers it, and shows up on any computer as a USB mouse. The Pico remains the recommended build.

**Status: early and untested.** Nothing here has run on hardware, including Classic HID on the S31 itself.

## Hardware problem to solve first

The bridge has to act as a USB *device*, and that needs the S31's USB OTG port. The Espressif boards checked so far don't make this easy:

| Board | Where OTG goes |
|---|---|
| ESP32-S31-Function-CoreBoard-1 | Type-A host port with a 500 mA power switch |
| ESP32-S31-Korvo-1 | Type-A host port, 500 mA |

Their Type-C ports won't work: USB Serial/JTAG and the UART bridge can't present a mouse.

**Don't connect one of those Type-A ports to a computer with an A-to-A cable.** Both ends may drive 5 V onto the same wire.

A usable setup needs a board with OTG on a device-capable connector, or a breakout wired to the OTG D+ and D- pins, with VBUS not supplied by the board.

## Behavior

- **LED:** fast blink = pairing, slow blink = reconnecting, solid = connected
- **Forget the mouse:** hold BOOT for 3 seconds, then release to reboot into pairing mode
- **Reconnecting:** the bridge pages the saved mouse, and also accepts a connection from it
- **Report parsing:** reads button, movement and wheel positions from the mouse's HID report map, and falls back to boot protocol (no wheel) if it finds no layout

The LED and button pins and `WHEEL_INVERT` are at the top of [main/main.c](main/main.c). The defaults match the Function-CoreBoard-1. Logs go to the default console.

## Build

Requires ESP-IDF v6.1 or newer. CI builds this on every pull request and on `main`.

```console
idf.py build
idf.py -p PORT flash monitor
```

## Test

The report parser and USB forwarding run on a PC, with no hardware: see [tests/fuzz](../../tests/fuzz/README.md) for the fuzzer and [Testing](../../CONTRIBUTING.md#testing) for the unit tests.
