# AGENTS.md — ESPAirspy

This file is the working agreement for anyone (human or AI) contributing to
ESPAirspy. Keep it up to date when the project conventions change.

## What this project is

ESPAirspy is firmware for the **M5Stack Unit PoE-P4 (U213, ESP32-P4)** that
turns an **Airspy HF+ Discovery** USB receiver into a stand-alone network SDR.
It speaks the **SpyServer** protocol (TCP 5555) and serves one client with
signed 16-bit I/Q at 768 / 384 / 192 / 96 / 48 / 24 / 12 kS/s, plus a web
dashboard. The code has been developed with the help of AI.

## Semantic Versioning (required)

The firmware uses [Semantic Versioning 2.0.0](https://semver.org/).

- The single source of truth is `PROJECT_VER` in the top-level `CMakeLists.txt`.
- **Every feature change MUST bump the version** and update `CHANGELOG.md`
  (and the in-app Changelog panel in `main/page.html`).
- `MAJOR.MINOR.PATCH`:
  - `MAJOR` — incompatible changes (protocol, partition layout, config format).
  - `MINOR` — backwards-compatible features.
  - `PATCH` — backwards-compatible fixes.
- Only bump on a change that ships; batch related edits into one version bump.
- Tag releases as `vX.Y.Z`. The GitHub release workflow builds and attaches the
  firmware for tags matching `v*`.

## Build

ESP-IDF **v5.5.4** is required. The wrapper checks the version and applies the
required ESP-IDF patches before building.

```sh
export IDF_PATH=/path/to/esp-idf-5.5.4
. "$IDF_PATH/export.sh"
./build.sh                       # default: sdkconfig.p4 -> build-p4/
# or a specific config:
AIRSPY_SDKCONFIG=sdkconfig.p4 AIRSPY_BUILD_DIR=build-p4 ./build.sh
```

The output is `build-p4/ESPAirspy.bin`. The single committed configuration is
`sdkconfig.p4` (a complete config); there is no separate `sdkconfig.defaults`,
so the two cannot drift.

## Host tests

The DSP and protocol modules have no ESP-IDF dependency and are tested on the
host with `./tests/run-host-tests.sh` on Linux. Run the suite before submitting
DSP or protocol changes. See `docs/performance.md` for physical tests.

## Firmware layout and conventions

- All source, comments, UI strings and documentation are **English**.
- Keep the existing code style: compact C11, ESP-IDF APIs, `esp_err_t` checks,
  `ESP_LOGx` with a stable `TAG`, no heap allocation in hot paths.
- DSP files are compiled `-O3` (see `main/CMakeLists.txt`).
- Opt-in DSP/experiment features are Kconfig-gated in `main/Kconfig.projbuild`
  and must not run unless enabled.
- The USB callback (`bulk_cb`) and the SpyServer sender run on different cores;
  keep ISR/task-critical work bounded. Don't add blocking calls to `bulk_cb`.
- Persistent user settings live in NVS via `main/settings.c`.
- When changing the web UI, keep it accessible and offline-friendly (no external
  runtime dependencies beyond the Google Fonts link).

## Hardware reference

- Board: M5Stack Unit PoE-P4 (U213), ESP32-P4 v1.3, 16 MiB flash, 32 MiB PSRAM.
- Pre-v3 silicon runs at 400 MHz only with `patches/esp32p4-400mhz-pre-v3.patch`
  (applied by `build.sh`).
- Airspy HF+ Discovery: USB VID:PID `03eb:800c`.

## Never commit

- `build*/`, `sdkconfig`, `sdkconfig.old`, generated `managed_components/`,
  `dependencies.lock` (see `.gitignore`).
- Secrets, calibration captures containing private data, or API tokens.
