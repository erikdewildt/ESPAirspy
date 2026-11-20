# Contributing to ESPAirspy

Thanks for your interest! This project is a small, hardware-focused firmware.
Please read `AGENTS.md` first — it is the working agreement (versioning,
conventions, hardware notes).

## Before you start

- ESP-IDF **v5.5.4** is required. `./build.sh` applies the needed ESP-IDF
  patches automatically.
- Target hardware: M5Stack Unit PoE-P4 (U213, ESP32-P4) + Airspy HF+ Discovery.
- Keep all code, comments, UI strings and documentation in **English**.

## Building and testing

```sh
export IDF_PATH=/path/to/esp-idf-5.5.4
. "$IDF_PATH/export.sh"
./build.sh
```

Run the Linux host suite with `./tests/run-host-tests.sh`. It needs Bash, a C
compiler and UBSan. Run `node tests/web-ui-test.mjs` for dashboard changes. See [build instructions](docs/build.md) and [physical test
procedures](docs/performance.md). Report which tests used real hardware.

## Pull requests

- Target `main`. One logical change per PR; keep it reviewable.
- CI must pass host tests, dashboard fixtures and the firmware build. See
  [CI and release management](docs/releases.md).
- Bump `PROJECT_VER` in `CMakeLists.txt`, update `CHANGELOG.md` and the in-app
  Changelog for feature changes (see `AGENTS.md` for the versioning rules).
- Describe what you changed and how you verified it (build output, host tests,
  and — where possible — hardware observations).
- Do not commit build output, `sdkconfig`, `managed_components/` or secrets.

## Style

- ESP-IDF idioms, `esp_err_t` return checks, stable `ESP_LOGx` tags.
- No heap allocation or blocking calls in `bulk_cb` (the USB callback).
- Opt-in DSP features stay behind Kconfig (`main/Kconfig.projbuild`).

## Reporting issues

Include the firmware version (web dashboard or `/api/status`), the ESP-IDF
version, the Airspy firmware version, and a description of the hardware setup.
For security issues, see `SECURITY.md`.
