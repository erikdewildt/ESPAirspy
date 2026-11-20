#!/usr/bin/env bash
# Flash ESPAirspy to an ESP32-P4 board over USB.
#
# Usage:
#   ./flash.sh <serial-port> [full|app|merged]
#
#   full    (default) bootloader + partition table + OTA metadata + app image
#   app     app image only (0x20000), keeps existing bootloader/partitions
#   merged  write build-p4/merged-binary.bin at 0x0 (factory image from a release)
#
# Examples:
#   ./flash.sh /dev/ttyACM0
#   ./flash.sh COM5 app            # (under Git Bash / WSL)
#
# Requires either the ESP-IDF environment (idf.py) or esptool on PATH. Run
# ./build.sh first for the "full"/"app" modes.
set -euo pipefail

cd -- "$(dirname -- "$0")"

PORT="${1:-}"
MODE="${2:-full}"
BUILD="${AIRSPY_BUILD_DIR:-build-p4}"
CFG="${AIRSPY_SDKCONFIG:-sdkconfig.p4}"
APP="$BUILD/ESPAirspy.bin"
BAUD="${ESPPORT_BAUD:-460800}"
CHIP=esp32p4

if [[ -z "$PORT" ]]; then
    echo "Usage: $0 <serial-port> [full|app|merged]" >&2
    echo "  e.g. $0 /dev/ttyACM0" >&2
    exit 2
fi

if [[ "$MODE" == "merged" ]]; then
    MERGED="${3:-$BUILD/merged-binary.bin}"
    if [[ ! -f "$MERGED" ]]; then
        echo "Merged image not found: $MERGED" >&2
        echo "Build it with: idf.py -B $BUILD -D SDKCONFIG=$CFG merge-bin" >&2
        exit 1
    fi
    ESPTOOL="${ESPTOOL:-esptool.py}"
    command -v "$ESPTOOL" >/dev/null || ESPTOOL=esptool
    exec "$ESPTOOL" --chip "$CHIP" -p "$PORT" -b "$BAUD" write_flash 0x0 "$MERGED"
fi

if command -v idf.py >/dev/null 2>&1; then
    if [[ "$MODE" == "app" ]]; then
        exec idf.py -B "$BUILD" -D "SDKCONFIG=$CFG" -p "$PORT" app-flash
    fi
    exec idf.py -B "$BUILD" -D "SDKCONFIG=$CFG" -p "$PORT" flash
fi

# No idf.py: fall back to esptool with explicit offsets.
ESPTOOL="${ESPTOOL:-esptool.py}"
command -v "$ESPTOOL" >/dev/null || ESPTOOL=esptool
if [[ ! -f "$APP" ]]; then
    echo "App image not found: $APP (run ./build.sh first)" >&2
    exit 1
fi
if [[ "$MODE" == "app" ]]; then
    exec "$ESPTOOL" --chip "$CHIP" -p "$PORT" -b "$BAUD" write_flash 0x20000 "$APP"
fi
exec "$ESPTOOL" --chip "$CHIP" -p "$PORT" -b "$BAUD" write_flash \
    0x2000 "$BUILD/bootloader/bootloader.bin" \
    0x8000 "$BUILD/partition_table/partition-table.bin" \
    0xf000 "$BUILD/ota_data_initial.bin" \
    0x20000 "$APP"
