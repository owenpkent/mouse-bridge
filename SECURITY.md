# Security Policy

mouse-bridge is a USB input device: whatever it receives over Bluetooth, it types into the computer it is plugged into. Security issues therefore matter even for a small project.

## Reporting a vulnerability

Please **do not open a public issue**. Report privately through GitHub: [Report a vulnerability](https://github.com/owenpkent/mouse-bridge/security/advisories/new).

Include what you found, its impact, and how to reproduce it (board, mouse, firmware version, serial log if relevant). You can expect an acknowledgement within a few days. Reporters are credited in the release notes unless they prefer not to be.

## Scope

In scope:

- A Bluetooth device other than the paired mouse getting its input forwarded to the host (for example, spoofing the saved mouse address, or pairing without the owner's action).
- Anything that lets a nearby attacker inject keystrokes or pointer input through the bridge.
- Malformed Bluetooth data that crashes the firmware or corrupts its memory.
- Leaks of the stored link key.

Out of scope:

- Attacks that need physical access to the board (anyone holding it can reflash it).
- Weaknesses in the Bluetooth Classic protocol itself, or upstream in BTstack, arduino-pico or ESP-IDF (please report those upstream; we will still update once fixed).

## Supported versions

Only the latest release receives fixes.
