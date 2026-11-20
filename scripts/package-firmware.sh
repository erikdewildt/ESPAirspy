#!/usr/bin/env bash
# Use the same build and distribution contents locally and in GitHub Actions.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
version=$(sed -n 's/^set(PROJECT_VER "\([0-9][0-9.]*\)")$/\1/p' CMakeLists.txt)
[[ "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || {
    echo 'Invalid PROJECT_VER (expected MAJOR.MINOR.PATCH).' >&2
    exit 1
}
if [[ "${GITHUB_REF_TYPE:-}" == tag && "${GITHUB_REF_NAME:-}" != "v$version" ]]; then
    echo 'Release tag must match PROJECT_VER.' >&2
    exit 1
fi
./build.sh
build_dir="${AIRSPY_BUILD_DIR:-build-p4}"
idf.py -B "$build_dir" -D "SDKCONFIG=${AIRSPY_SDKCONFIG:-sdkconfig.p4}" merge-bin
# A new directory prevents stale binaries from entering a release.
mkdir dist
cp "$build_dir/ESPAirspy.bin" dist/ESPAirspy.bin
cp "$build_dir/merged-binary.bin" dist/ESPAirspy-full.bin
cp LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md dist/
cp components/ethernet_init/UPSTREAM-LICENSE dist/ETHERNET-LICENSE
cp docs/install.md dist/INSTALL.md
{
    printf 'Firmware: %s\n' "$version"
    printf 'Source commit: %s\n' "$(git rev-parse HEAD)"
    printf 'ESP-IDF: %s\n' "$(idf.py --version)"
} > dist/BUILD-INFO.txt
(cd dist && sha256sum ESPAirspy.bin ESPAirspy-full.bin LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md ETHERNET-LICENSE INSTALL.md BUILD-INFO.txt > SHA256SUMS)
