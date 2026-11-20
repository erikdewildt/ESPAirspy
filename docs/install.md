# Install prebuilt ESPAirspy firmware

Use only the M5Stack Unit PoE-P4 U213 (ESP32-P4, 16 MiB flash) and an Airspy
HF+ Discovery. This firmware is not intended for other ESP32 boards.

## Download and verify

For a stable version, open **Releases** in the GitHub repository. Download
`ESPAirspy.bin` for ESP OTA or `ESPAirspy-full.bin` for first installation.
Download `SHA256SUMS` and `BUILD-INFO.txt` to identify and verify the build.

Pull requests to `main`, pushes to `main` and manual CI runs also produce
an `ESPAirspy-<commit>` archive under **Actions → CI → run → Artifacts**.
Those are test builds and expire after 30 days (or earlier if deleted).
A GitHub login with repository read access is required for artifact downloads;
see [GitHub's download instructions](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts).
Tagged releases provide the persistent version downloads.

After extracting an artifact, verify all files with `sha256sum --check
SHA256SUMS` on Linux. If downloading selected release files, compare their
individual hashes with `sha256sum ESPAirspy.bin` or, on macOS,
`shasum -a 256 ESPAirspy.bin`. On Windows use `Get-FileHash ESPAirspy.bin
-Algorithm SHA256`. Checksums detect corruption; obtain builds from the trusted
project repository.

## Existing installation: ESP OTA

1. Open `http://<device-ip>/` on the trusted local network.
2. Under **Firmware → Update ESP firmware**, select `ESPAirspy.bin` and choose
   **Upload and restart**. Do not use the separate Airspy receiver update.
3. After restart, verify the version, receiver connection and SpyServer listener
   on **Overview**. OTA requires the project's compatible partition layout.

## First installation: USB

Connect the board's **PROG/OTG** port using a USB data cable. With the project
checkout and ESP-IDF v5.5.4 environment active, run:

```sh
./flash.sh /dev/ttyACM0 merged /absolute/path/to/ESPAirspy-full.bin
```

Replace the serial port. On native Windows use `flash.bat COM5 merged
C:\path\to\ESPAirspy-full.bin` in the ESP-IDF command prompt.
The factory image is written at offset 0 and rewrites the bootloader,
partitions, OTA metadata and app. Back up any settings you need first.
The OTA app image alone cannot initialize a blank board.

Connect the receiver to **HOST** USB-C and wired Ethernet. Find the assigned
IP address in DHCP leases or the serial log, then connect your SDR client to
`<device-ip>:5555`. The dashboard is served over HTTP. For power, network and
troubleshooting details, see the repository's `docs/hardware.md`,
`docs/build.md`, `docs/web-ui.md` and `SECURITY.md`.
