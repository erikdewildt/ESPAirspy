# ESPAirspy web dashboard

The dashboard is a self-contained HTML document with inline CSS, JavaScript and
Canvas charts. ESP-IDF embeds `main/page.html` into the firmware at build time
and the device serves it at `/`. There is no frontend build, package install,
font download or runtime CDN use. The firmware API and its HTTP server remain
the data source.

## Views

| View | What it provides |
|---|---|
| Overview | Receiver center frequency, receiver and server state, active I/Q rate, firmware version, CPU, internal heap, PSRAM, flash and uptime |
| Streaming | Client, sample and send counters, measured network rate, estimated USB payload rate, system and network history, optional FFT counters |
| Receiver | Read-only USB completions, sample rates, RF and I/Q center, oscillator calibration, applied gain and clipping |
| System | Board profile, firmware and SDK versions, OTA slot, Ethernet link, memory and a sanitized diagnostic summary |
| Firmware | ESP OTA, Airspy file verification and receiver firmware update, receiver calibration backup, recovery and restart |
| Settings | DHCP or static IPv4, compiled DSP options, receiver controls and settings reset |
| Changelog | Current firmware version and release notes |

The dashboard uses the browser's color preference on first load and stores a
manual choice locally. Charts use the same palette. Color signals accompany
explicit status text. Native controls support keyboard focus and show a
confirmation before disruptive actions. On local HTTP, the diagnostics copy
button selects the allowlisted text for manual copying if the clipboard API is
unavailable. It excludes network addresses, serial numbers and calibration
data.

## API and refresh

- `GET /api/status` supplies the live snapshot. The next status poll is
  scheduled five seconds after a successful round, or ten seconds after
  failure. Status, listener readiness and publishing state are distinct.
- `GET /api/history?range=...` and `GET /api/spectrum-stats` run only while
  Streaming is visible. The periodic history interval is at least 15 seconds.
  Choosing a new range requests it on demand. An unavailable history or absent
  spectrum endpoint leaves live status operational.
- Background status rounds stop when the tab is hidden. A timeout or failure
  marks the last values stale. The browser offers its clock once if a live
  snapshot has no valid clock, using the existing `POST /api/time` route.
- `GET /api/settings` loads editable values on demand. Existing settings,
  update, restart, calibration and reset routes keep their request formats.
- USB payload throughput is calculated from consecutive snapshots during
  streaming. It excludes USB protocol overhead. The ring watermark is the
  estimated peak since boot, and send time includes scheduling. For definitions,
  see [performance](performance.md).

## Development and verification

Run `node tests/web-ui-test.mjs` for a dependency-free test of navigation,
theme persistence, API recovery, diagnostics privacy, hidden-tab behavior and
settings validation. It uses controlled fixtures and makes no device requests.
Build the embedded image using [the normal firmware build](build.md).
The dashboard has no separate release asset.

| Size or load | 0.3.0 | 0.4.0 |
|---|---:|---:|
| Embedded HTML, CSS and JS | 45,424 B | 55,953 B |
| ESP32-P4 app image | 685,120 B | 695,808 B |
| Periodic status/history/spectrum requests per minute on Overview, nominal | 36 | Up to 12 |
| Runtime RAM, HTTP response time and live I/Q streaming impact | NOT MEASURED | NOT MEASURED |

The request estimate assumes fast replies and a foreground tab and excludes
user actions. The build and fixture logic were verified locally. On 2026-11-19,
the desktop dashboard loaded on a Unit PoE-P4 running 0.4.0 after ESP OTA.
Overview showed the Airspy connected and SpyServer listening with no active
client. Mobile and tablet behavior on the device and the effect on live I/Q
streaming remain unverified.
