#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if ! command -v idf.py >/dev/null; then
    echo 'Source the ESP-IDF 5.5.4 export.sh before building.' >&2
    exit 1
fi
if [[ "$(idf.py --version)" != 'ESP-IDF v5.5.4'* ]]; then
    echo 'This board configuration is validated with ESP-IDF v5.5.4.' >&2
    exit 1
fi
# 400 MHz on pre-v3 silicon needs a small ESP-IDF patch (the pre-v3 clock tree
# does not map 400 MHz). Apply it idempotently so a fresh IDF checkout still
# builds. See docs/build.md and patches/.
patch_file="${PWD}/patches/esp32p4-400mhz-pre-v3.patch"
target="$IDF_PATH/components/esp_hw_support/port/esp32p4/rtc_clk.c"
if [[ -f "$patch_file" && -f "$target" ]]; then
    if ! grep -q 'Pre-v3 silicon: the analog CPLL already supports 400 MHz' "$target"; then
        echo 'Patching ESP-IDF for 400 MHz on pre-v3 silicon...' >&2
        git -C "$IDF_PATH" apply "$patch_file" 2>/dev/null || patch -d "$IDF_PATH" -p1 < "$patch_file"
    fi
    if ! grep -q 'Pre-v3 silicon: the analog CPLL already supports 400 MHz' "$target"; then
        echo "ERROR: could not apply $patch_file to ESP-IDF ($IDF_PATH)." >&2
        exit 1
    fi
else
    echo "Warning: 400 MHz patch not applied (missing $patch_file or IDF at $IDF_PATH)." >&2
fi
# Backport the P4 PSRAM controller clock enable from upstream IDF v5.5.6.
# Local investigations used an older experimental driver patch. Undo only
# that exact change if present so controller errors are reported normally.
old_patch="$PWD/patches/esp32p4-psram-mspi749.patch"
if git -C "$IDF_PATH" apply --reverse --check "$old_patch" 2>/dev/null; then
    git -C "$IDF_PATH" apply --reverse "$old_patch"
fi
clock_patch="$PWD/patches/esp32p4-psram-core-clock.patch"
if ! git -C "$IDF_PATH" apply --reverse --check "$clock_patch" 2>/dev/null; then
    git -C "$IDF_PATH" apply "$clock_patch"
fi
# Keep the older IDF 5.4 sdkconfig/build directory separate from this target.
exec idf.py -B "${AIRSPY_BUILD_DIR:-build-p4}" -D "SDKCONFIG=${AIRSPY_SDKCONFIG:-sdkconfig.p4}" build
