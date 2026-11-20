# Airspy HF+ Discovery firmware updating

The Airspy HF+ Discovery runs its own firmware. The ESPAirspy app uses a
separate OTA image and slot. The **Receiver** view shows the attached Airspy
version, read using the receiver's GET_VERSION_STRING request. The reference
receiver reported `R5.0.1-CD` on 2026-11-19.

## Update from the dashboard

1. Open **Receiver** and check the current receiver version and factory
   calibration. Obtain the firmware package from the
   [Airspy HF+ Discovery page](https://airspy.com/airspy-hf-discovery/) and
   consult the included instructions and
   [vendor changelog](https://airspy.com/downloads/hfplus_changelog.txt).
2. Open **Firmware → Airspy HF+ firmware** and, if desired, select **Download
   the receiver configuration** to save a local backup. Keep it private.
3. Select the vendor file for the reported hardware variant.
   Use `hfplus-firmware-cd.bin` for `-CD` or
   `hfplus-firmware-bb.bin` for `-BB`. **Verify file** checks the
   filename against the reported variant and accepts files from 1 to 65,280
   bytes. This check does not authenticate or inspect the file contents.
4. Select **Update Airspy firmware** and keep the ESP and receiver powered and
   connected. The ESP restarts during programming. When the dashboard returns,
   check the receiver connection, firmware version and factory calibration in
   **Receiver**.

Use the **Update ESP firmware** panel only for the ESPAirspy app image.
ESP OTA rollback applies to the ESP app slots. See [ESP build and flashing](build.md).

## Update and recovery behavior

The ESP stages the entire receiver image in a 64 KiB ESP flash scratch region
at `0xf00000` before requesting a receiver reboot. The ESP restarts to
enumerate the ROM SAM-BA bootloader (`03eb:6124`), claims its CDC data interface
and checks for an ATSAM3U1. It writes and reads back each firmware page using
SAM-BA and EEFC, sets boot-from-flash and restarts both devices. NVS records the
update phase across the ESP restarts. The dashboard polls
`GET /api/airspy/update-status`.

The updater preserves factory calibration by leaving receiver flash page 255
untouched. Dashboard text in 0.4.0 describes a backup and restore during
programming. The implemented update preserves that page in place, and the
dashboard provides a separate manual download.

An interrupted upload is rejected before receiver programming starts. If power
or USB is interrupted during programming, check the update status when the ESP
returns. The ESP recognizes the SAM-BA bootloader even outside a scheduled
update. **Firmware → Maintenance → Recover receiver (bootloader)** selects
boot-from-flash and restarts the devices. This requires a receiver still
reachable in SAM-BA mode and a bootable receiver image. If recovery fails and
the receiver remains in SAM-BA mode, use Airspy's official update procedure
for the matching receiver. The web upload requires the receiver to enumerate
in normal mode so it can identify the variant.

The SAM-BA command framing, per-page flash sequence and variant checks are
covered by the [host tests](../tests/run-host-tests.sh).

## Recovery record (2026-11-14)

A real update attempt was interrupted, leaving the receiver in SAM-BA mode.
Two independent problems surfaced and were fixed:

- The ESP32-P4 USB host's DWC2 driver rejects bulk-IN reads whose size is not
  an integer multiple of the endpoint's Max Packet Size; the SAM-BA code's
  small reads (2, 4, 255 bytes) triggered
  `IN transfer num_bytes not integer multiple of MPS`. The reads now round the
  buffer up to a whole MPS (512 for this High-Speed device).
- The receiver's own watchdog reboot and a soft root-port power toggle both
  disconnect the device but do not make it re-enumerate on the host; only a
  full ESP32 reboot (USB host reset) re-enumerates it. The flow therefore
  watchdog-resets the receiver and then reboots the ESP32, persisting progress
  in NVS so the update resumes across the restarts.

After these fixes the receiver was recovered to normal mode with its
`R5.0.0-CD` firmware and `-152 ppb` factory calibration, and a subsequent
web-interface update flashed `R5.0.1-CD` end to end while preserving the
calibration.
