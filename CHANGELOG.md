# Changelog

All notable changes to ESPAirspy are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Documentation

- Archive outdated dashboard imagery and align the build, hardware and firmware
  update guides with 0.4.0. Label historical measurements with their test scope.

## [0.4.0] - 2026-11-19

### Added
- Responsive web dashboard with persistent light and dark themes, streaming
  counters and a sanitized diagnostic summary.

### Changed
- Poll live status independently from Streaming history, suspend background
  reads in hidden tabs and mark stale telemetry after a failed refresh.

## [0.3.0] - 2026-11-19

### Added
- I/Q transport counters for ring-buffer pressure, lost and transmitted samples,
  empty reads, send failures, and maximum frame send time in `/api/status`.

### Fixed
- Surface NVS write failures when saving settings.

## [0.2.1] - 2026-11-19

### Fixed
- Close the SpyServer connection when the I/Q buffer drops an input block, so
  the client receives a disconnect instead of treating a sample gap as
  continuous input. Clients can open a fresh stream with reset filter state.

## [0.2.0] - 2026-11-15

### Changed
- Renamed the Kconfig option `CONFIG_AIRSPY_EXPERIMENTAL_FFT_SERVER` to
  `CONFIG_AIRSPY_FFT_SERVER`; the on-device FFT spectrum stream is stable.
- The frequency-domain IQ balancer's FFT estimate now runs in a dedicated task
  instead of the USB callback, keeping the streaming path predictable
  (`iq_balance_freq_start_task`).
- Consolidated to a single committed configuration (`sdkconfig.p4`); the
  parallel `sdkconfig.defaults` was removed so they cannot drift.

### Investigated
- FFT/spectrum cost at the narrowest decimation stages. The reported
  `max_process_us` is wall-clock time and includes preemption by the USB task;
  the FFT itself is amortised (a few frames per second) while the per-block
  halfband decimation dominates, and that path is already `-O3` with mirrored
  ring buffers. A SIMD FFT (ESP-DSP) was re-assessed and is not warranted, so no
  new dependency was added.

### Added
- `CONTRIBUTING.md`, `SECURITY.md` and `.editorconfig`.
- A real dashboard screenshot in the `README.md`; documentation is generic
  (no session-specific IPs).

## [0.1.1] - 2026-11-15

### Fixed
- The IQ-balancer settings toggle now controls whichever corrector is compiled
  in (static or frequency-domain) and resets its filter state on toggle.

### Changed
- The web dashboard no longer loads Google Fonts; it uses system fonts, so it
  works fully offline on an isolated LAN.

### Added
- `POST /api/factory-reset` and a "Reset settings to defaults" button that clear
  the persisted settings and restart.
- The release CI container image (`espressif/idf:v5.5.4`) was validated.

## [0.1.0] - 2026-11-15

Initial open-source release.

### Added
- Airspy HF+ Discovery streaming to a network **SpyServer** (TCP 5555), one
  client, signed 16-bit I/Q at 768 / 384 / 192 / 96 / 48 / 24 / 12 kS/s.
- Six-stage 55-tap halfband decimation cascade with >79 dB alias rejection.
- Software complex mixer for sub-kHz tuning and frequency-delta correction.
- Factory oscillator calibration (ppb) read from the receiver and applied.
- Receiver gain model: AGC on/off, 0–48 dB HF attenuation, LNA (+6 dB) and
  AGC-threshold low/high, settable at runtime and persisted in NVS.
- Optional DC blocker and static / frequency-domain IQ-imbalance correctors.
- On-device FFT spectrum stream with independent I/Q and FFT tuning.
- Ethernet (DHCP or static IP), OTA firmware updates and Airspy receiver
  firmware updates through the web dashboard.
- Web dashboard with live telemetry, persisted 24-hour history, settings,
  firmware update and a Changelog page.
- 400 MHz operation on pre-v3 ESP32-P4 silicon via an ESP-IDF patch.
- GitHub Actions release workflow and easy flash scripts (`flash.sh`,
  `flash.bat`).

### Notes
- Single-client design; SDORxide 1.7.2 is the validated client.
- Tested on the M5Stack Unit PoE-P4 (U213) with an Airspy HF+ Discovery.
