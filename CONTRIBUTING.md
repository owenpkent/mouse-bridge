# Contributing

Thanks for helping. The most useful thing right now is **testing on real hardware**: the firmware builds, but few mice have been tried.

## Ways to help

- **Mouse reports.** Flash a release, pair your mouse, and file a [mouse report](https://github.com/owenpkent/mouse-bridge/issues/new?template=mouse-report.yml). Reports of mice that *don't* work are just as valuable.
- **Bug reports.** Use the [bug template](https://github.com/owenpkent/mouse-bridge/issues/new?template=bug-report.yml) and include a serial log (see below).
- **Code.** Fixes for the open questions in [TODO.md](TODO.md) are especially welcome.
- **Questions and ideas** go in [Discussions](https://github.com/owenpkent/mouse-bridge/discussions).

## Getting a serial log

1. Set `#define DEBUG 1` at the top of [mouse_bridge.ino](firmware/mouse_bridge/mouse_bridge.ino) and build (below).
2. Open the Pico's serial port at 115200 baud (Arduino IDE Serial Monitor, PuTTY, `screen`, etc.).
3. The log shows every device found in the scan, with its Class of Device, and each connect attempt.

## Building

No local toolchain is required: every pull request and push to `main` is compiled by CI, and the `.uf2` files appear as build artifacts on the run page. The experimental ESP32-S31 port has its own [build notes](firmware/mouse_bridge_esp32s31/README.md#build).

To build locally with [arduino-cli](https://arduino.github.io/arduino-cli/):

```sh
arduino-cli config add board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
arduino-cli compile --fqbn "rp2040:rp2040:rpipico2w:ipbtstack=ipv4btcble,usbstack=picosdk" firmware/mouse_bridge
```

Use `rpipicow` instead of `rpipico2w` for the original Pico W. Arduino IDE settings are in the [README](README.md#flash).

## Testing

The firmware's pure logic has tests that run on a PC, with no hardware. They need only CMake and GCC or Clang:

```sh
cmake -S tests/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

CI runs these on every pull request, plus a [fuzzer](tests/fuzz/README.md) over the ESP32-S31 report path for 60 seconds, and for 30 minutes each night. They can't show that a mouse works, so real hardware reports still matter most.

## Pull requests

- Keep each PR to one change, and say in the description what hardware you tested on ("compiles only" is fine, just say so).
- Keep the sketch to one `.ino` file with plain Arduino C++, plus `bridge_logic.h` for pure logic that has tests. BT callbacks should only update shared state; USB output stays in `loop()`.
- Update [README.md](README.md), [TODO.md](TODO.md) and [CHANGELOG.md](CHANGELOG.md) when behavior changes.
- CI must pass: the firmware builds, the host tests and the fuzzer.

## Scope

mouse-bridge does one thing: carry one Bluetooth mouse's pairing between computers. Cursor roaming across screens is out of scope (software like Mouse Without Borders, Input Leap or Deskflow does that well). Keyboard support may be considered if it stays simple.

## Code of conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md).

## License

By contributing you agree that your contribution is licensed under the [MIT License](LICENSE).
