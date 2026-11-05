# TODO

Firmware compiles in CI only (no local toolchain yet). Nothing below has run on hardware. Community tracking: [issue #1](https://github.com/owenpkent/mouse-bridge/issues/1).

## Get hardware

- [ ] Raspberry Pi Pico 2 W (or Pico W, or Pimoroni Pico Plus 2 W for USB-C)
- [ ] Micro-USB data cable (not charge-only)

## Bring-up

- [ ] Flash the UF2 from the CI artifact (hold BOOTSEL, plug in, drop the file on the RPI-RP2 / RP2350 drive)
- [ ] Build once with `DEBUG 1` and watch the serial scan log
- [ ] Pair: mouse in pairing mode, LED fast blink goes solid
- [ ] Movement, wheel, left / right / middle all correct

## Open questions (from the design summary)

1. [ ] **Reconnect after power cycle.** The Pico initiates the connection to the bonded mouse. If this mouse only reconnects from pairing mode, the approach needs adjusting (look at making the Pico connectable and letting the mouse page it).
2. [ ] **Button mapping.** If left registers as right, set `BUTTON_INDEX_BASE 1`.
3. [ ] **Device detection.** If the mouse shows in the scan log but is not picked up, widen `bridge_is_pointing_device()` in `bridge_logic.h`.
4. [ ] **Hot-swap time.** Time from plugging into another computer to a working cursor (Pico boot plus Bluetooth reconnect).
5. [ ] **Scroll direction.** If backwards, set `WHEEL_INVERT 1`.
6. [ ] **Stuck click on link loss.** Hold a click, then cut the link two ways: power off the mouse, and move out of range. Time how long until the computer sees the release. Out of range waits for the Bluetooth supervision timeout, because a held button sends no reports to watch for. If it is too long, try shortening that timeout.

## Later

- [ ] Enclosure (print)
- [ ] If arduino-pico's HID host proves flaky: fall back to Bluepad32 on the same board (see [docs/project-summary.md](docs/project-summary.md))

## ESP32-S31 port (experimental)

- [x] Get [firmware/mouse_bridge_esp32s31](firmware/mouse_bridge_esp32s31/README.md) compiling in CI
- [ ] Find a board or breakout that exposes USB OTG as a device
- [ ] Confirm that Classic HID host and USB device both work on real hardware
- [ ] Repeat the bring-up and open questions above
