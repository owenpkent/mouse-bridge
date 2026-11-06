# Report fuzzer

A libFuzzer harness ([report_fuzz.c](report_fuzz.c)) for the bytes a Bluetooth mouse controls in the ESP32-S31 firmware. Each input is a HID report map plus one input report, and goes through:

- `mouse_parse_report_map`: every item type, sizes, and malformed maps
- `mouse_decode` and `mouse_decode_boot`: field extraction at any offset
- `mouse_forward` and the `bridge_logic.h` move splitting, with USB sends that fail

Any crash, ASan or UBSan report, timeout (10 s) or memory blow-up fails the run. So does breaking one of these rules:

- Decoded buttons and values stay in range for their field.
- Everything sent to USB is in range, and a report causes a bounded number of sends.
- The whole move arrives when sends work.
- Buttons recover after failed sends, so a click can't stick.

The Pico firmware gets its parsing from the arduino-pico library, so only its use of `bridge_logic.h` is covered here.

## When it runs

- **Pull requests:** 60 seconds ([fuzz.yml](../../.github/workflows/fuzz.yml))
- **Nightly:** 30 minutes at 05:41 UTC, from `main` only. GitHub turns off scheduled workflows after 60 days with no repository activity, and they can be turned back on from the Actions tab.
- **Host tests:** the Clang CI job replays `corpus/`, so past crashes stay fixed

## Run locally

Needs Clang with libFuzzer. It also runs on Windows with LLVM's Clang, if the folder holding the `clang_rt.asan` DLL is on `PATH`.

```console
cmake -S tests/host -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DMOUSE_BRIDGE_HOST_SANITIZE=ON -DMOUSE_BRIDGE_HOST_FUZZ=ON
cmake --build build-host --target report_fuzz
mkdir -p fuzz-corpus
build-host/report_fuzz fuzz-corpus tests/fuzz/corpus -max_total_time=60 -max_len=4096
```

The first folder is where libFuzzer writes new inputs, so this leaves `tests/fuzz/corpus` untouched.

## Reproduce a crash

CI uploads `crash-*`, `timeout-*` and `oom-*` files as the `fuzz-findings` artifact. To replay one:

```console
build-host/report_fuzz crash-<hash>
```

Once it's fixed, copy the file into `tests/fuzz/corpus/` so it stays a regression test.

## Seed corpus

Each file in `corpus/` is a 2-byte little-endian map length, the report map, then an input report. They cover a common mouse with a report ID, 12-bit movement, a keyboard and mouse combo, absolute X and Y, Push and Pop, 4-byte usages, and a long item. They're only starting points, and the fuzzer mutates them.
