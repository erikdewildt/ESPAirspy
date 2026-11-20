# Validation and known limits

## Current firmware: 0.4.0

The firmware and dashboard fixture tests were built and checked locally. On
2026-11-19, version 0.4.0 booted on a Unit PoE-P4 after ESP OTA. The desktop
dashboard showed an Airspy HF+ Discovery connected and SpyServer listening.
Sustained streaming throughput, mobile/tablet behavior and the impact of the
0.4.0 dashboard on live I/Q streaming have not been measured.

## Earlier hardware measurements

These bounded observations from 2026-11-14 apply to earlier builds, rather than
constituting performance guarantees for the current firmware:

- All seven output rates were exercised for ten seconds each after warm-up,
  with payload rates within 0.1% of nominal and no counted USB block loss.
- Relative tuning by +375 and -375 Hz matched spectral shifts in real RF
  captures. Absolute oscillator accuracy was not independently calibrated.
- SDROxide 1.7.2 received audio and spectrum at 96 kS/s. A five-minute
  observation reported no additional USB or spectrum dropped blocks and
  sampled CPU loads near 81% / 24%.
- Early PSRAM-enabled builds failed before application startup. The current
  build wrapper includes the later controller-clock backport; do not infer
  current PSRAM behavior from those early failures.

Use the [benchmark procedure](performance.md) to validate a new build. Record
its version, configuration, receiver firmware, client and measurement window.
Host tests verify DSP and protocol logic; they do not prove USB continuity,
RF sensitivity, client interoperability or long-duration hardware stability.
