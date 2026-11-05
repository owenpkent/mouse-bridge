# Bluetooth Mouse Bridge: Project Summary

## Goal

Use one Bluetooth mouse with multiple computers by moving a dongle from one computer to another, without re-pairing every time. Cursor gliding from screen to screen is not a goal.

## The core problem

A regular Bluetooth USB adapter (like the UGREEN Bluetooth 5.4 dongle) only supplies a radio. The pairing is stored in the computer's operating system, not on the dongle, so the mouse stays tied to one computer. This is also why Bluetooth mice don't work through KVM or KM switches.

What's needed is a **HID proxy**: a device that pairs with the mouse itself, stores the pairing, and presents itself to any computer as an ordinary wired USB mouse.

## What we found

**No mainstream retail product does this.** Retail Bluetooth adapters (UGREEN, TP-Link, Amazon Basics, Keychron) are all standard radios. KVM makers confirm Bluetooth devices bypass their switches.

Options surveyed:

| Option | Verdict |
|---|---|
| Generic "HID proxy" dongles on AliExpress | Unbranded, unverified |
| Old CSR BLE-USB bridge dongle | Linux-based setup; reported mouse forwarding issues |
| HID Remapper on Seeed XIAO nRF52840 | Good, but BLE only |
| ESP32-S3 with esp-usb-ble-hid (built on espp) | Good, but BLE only |
| ESP32-S31 with ESP-IDF | Has Classic and USB on one chip, but very new and needs new firmware (see Fallbacks) |
| Raspberry Pi Zero 2 W with Bluetooth-2-USB | Handles Classic, but bulky and slow to boot |
| **Raspberry Pi Pico 2 W / Pico W** | **Handles Classic and BLE, native USB, small, cheap** |

## Key constraint

The mouse is **Bluetooth Classic**, confirmed via Windows Device Manager. That rules out every BLE-only option, including the ESP32-S3. The exception among ESP32 chips with native USB is the newer ESP32-S31, which has both Classic Bluetooth and USB OTG.

## Decision

Build the bridge on a **Raspberry Pi Pico 2 W** (Pico W also works) using the arduino-pico core and its `BluetoothHIDMaster` library.

The ESP32-S31 doesn't change this: arduino-pico already provides a Classic HID host, and the bridge only has to appear as a plain USB mouse. An S31 build becomes worth evaluating if the bridge ever has to join an ESP-IDF based system, where it would share a toolchain.

The Adafruit "Pico Bluetooth Keyboard Bridge" project was evaluated but doesn't fit as-is: its Classic path only detects and parses keyboards, and mouse forwarding exists only on the BLE side.

## Hardware

- Raspberry Pi Pico 2 W (or Pico W, or Pimoroni Pico Plus 2 W for USB-C)
- Micro-USB data cable (not charge-only)
- Optional: a simple button-operated USB switch, if moving the dongle by hand gets tedious (no mouse-roaming features needed)

## Firmware: `mouse_bridge.ino`

Arduino sketch for the arduino-pico core (Earle Philhower).

**How it works**
- Runs the Bluetooth Classic HID host via `BluetoothHIDMaster`
- First boot: scans for a device whose Class of Device is a pointing device, connects, and saves its address to flash
- Later boots: reconnects to the saved address automatically
- Forwards movement, wheel, and buttons to the USB `Mouse` library
- BT callbacks only update shared state; USB output happens in `loop()` as the library docs recommend
- Movement is chunked to fit 8-bit USB mouse reports

**Controls**
- LED fast blink: pairing
- LED slow blink: reconnecting (move or click the mouse to wake it)
- LED solid: connected
- Hold BOOTSEL 3 seconds: forget the mouse and reboot into pairing mode

**Arduino IDE settings**
- Board: Raspberry Pi Pico 2 W (or Pico W)
- IP/Bluetooth Stack: an option that includes Bluetooth
- USB Stack: Pico SDK

**Config flags (top of sketch)**
- `BUTTON_INDEX_BASE`: set to 1 if left-click registers as right-click
- `DEBUG`: serial logging at 115200 baud

## Status

Firmware compiles in CI, not yet tested on hardware.

## Open questions to test

1. **Reconnect after power cycle.** Relies on the Pico initiating the connection to the bonded mouse. Most Classic mice accept this when awake; if this one only reconnects from pairing mode, the approach needs adjusting.
2. **Button mapping.** Confirm left, right, and middle register correctly.
3. **Device detection.** If the mouse shows in the serial scan log but isn't picked up, its Class of Device may need a wider match.
4. **Hot-swap time.** Measure how long it takes to work after plugging into a different computer (Pico boot plus Bluetooth reconnect).

## Fallbacks

- **Bluepad32** on the same Pico 2 W: lower-level (Pico SDK, C), but a more widely tested Bluetooth host stack with Classic mouse support.
- **ESP32-S31** with ESP-IDF: the hardware fits, but the datasheet is still preliminary (as of November 2026) and ESP-IDF support is recent. Starting from esp-usb-ble-hid, a BLE gamepad to Switch Pro Controller adapter built on esp-nimble-cpp, needs two main changes: moving from NimBLE to Bluedroid for Classic (ESP-IDF's `esp_hidh`), and adding a USB mouse output. Bluedroid is workable, but less convenient to use than esp-nimble-cpp. Not yet confirmed: whether Classic HID host and USB device both work on the S31 in ESP-IDF. An experimental, untested port is in [firmware/mouse_bridge_esp32s31](../firmware/mouse_bridge_esp32s31/README.md). It needs a board that exposes USB OTG as a device, which the Function-CoreBoard-1 and Korvo-1 don't appear to.
- **Software sharing** with no hardware: pair the mouse to one PC and use Mouse Without Borders (PowerToys), Input Leap, or Deskflow.

## Next steps

1. Get a Pico 2 W and flash `mouse_bridge.ino`
2. Pair and verify movement, buttons, and wheel
3. Test unplug, replug, and moving between computers
4. Optional: design and print an enclosure

## References

- arduino-pico Bluetooth HID Master: https://arduino-pico.readthedocs.io/en/latest/hidmaster.html
- Adafruit Pico Bluetooth Keyboard Bridge: https://www.digikey.com/en/maker/projects/pico-bluetooth-keyboard-bridge/2cd3ac8a6bd14e68b763ad6e4bf8312e
- Bluepad32: https://github.com/ricardoquesada/bluepad32
- esp-usb-ble-hid (espp, BLE only): https://github.com/finger563/esp-usb-ble-hid
- ESP32-S31 datasheet (preliminary): https://documentation.espressif.com/esp32-s31_datasheet_en.html
- ESP-IDF Bluetooth HID Host API: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp_hidh.html
- HID Remapper (BLE only): https://github.com/jfedor2/hid-remapper
- Bluetooth-2-USB (Pi Zero): https://github.com/quaxalber/bluetooth_2_usb
