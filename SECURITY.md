# Security Policy

## Supported versions

ESPAirspy is pre-1.0. Security fixes are applied to the latest release.

## Reporting a vulnerability

Report security problems privately. If the repository offers private vulnerability
reporting, use **Security → Advisories → Report a vulnerability**. You can also
contact the maintainers privately. Include the following details.

- affected firmware version,
- steps to reproduce,
- impact assessment,
- any suggested fix.

We will acknowledge and coordinate a fix and disclosure.

## Security model (read this)

ESPAirspy is designed for a **trusted local network**. By default:

- The web interface (`http://<device-ip>/`) and the OTA update endpoint have
  **no authentication and no transport encryption**.
- SpyServer on TCP 5555 is unauthenticated and unencrypted.
- The Airspy receiver firmware update path writes to the receiver over USB/SAM-BA.

Keep both ports on a trusted LAN. Use a VPN or equivalent authenticated private
network for remote access. A web reverse proxy protects only HTTP. Keep direct
access to the device and raw SpyServer traffic restricted.

## Firmware installation and dependencies

HTTP management includes ESP OTA, receiver firmware update/recovery, settings
and restart. An image passing structural checks is not proof of publisher
identity. Install firmware only from a source you trust.

The supported profile pins ESP-IDF v5.5.4. Track [Espressif advisories](https://github.com/espressif/esp-idf/security/advisories)
against the actual SDK configuration before releasing binaries. The normal
build disables the ESP HTTP Server WebSocket feature and registers no
WebSocket endpoints. The two SDK patches are applied by the build wrapper.
