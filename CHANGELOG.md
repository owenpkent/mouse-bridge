# Changelog

All notable changes are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-11-05

### Added

- Firmware for Raspberry Pi Pico 2 W and Pico W: pairs with a Bluetooth Classic mouse, saves it in flash, reconnects on boot, and forwards movement, buttons and wheel as a USB mouse.
- LED status (pairing, reconnecting, connected) and BOOTSEL hold to forget the mouse.
- Config flags `BUTTON_INDEX_BASE`, `WHEEL_INVERT`, `DEBUG`.
- CI builds for both boards; tagged releases attach the `.uf2` files.

### Known issues

- Untested on hardware. See [TODO.md](TODO.md).
- The LED pauses for 5 seconds during each pairing scan (arduino-pico 6.3.2's async scan does not link).

[Unreleased]: https://github.com/owenpkent/mouse-bridge/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/owenpkent/mouse-bridge/releases/tag/v0.1.0
