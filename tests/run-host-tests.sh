#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc=${CC:-cc}
run() {
    local test=$1
    shift
    "$cc" -std=c11 -O3 -Wall -Wextra -Werror -fsanitize=undefined \
        -fno-sanitize-recover=undefined -Imain "$@" "tests/${test}_test.c" -lm -o "$out/$test"
    "$out/$test"
    printf 'PASS %s\n' "$test"
}
for test in airspy_samba airspy_signal airspy_tuning airspy_update_check \
    iq_balance_freq iq_balance iq_mixer iq_decimator spy_fft_settings spy_fft spy_protocol; do
    run "$test" "main/$test.c"
done
run dc_block main/dc_block.c main/iq_balance.c
run history_store -Itests/nvs_fake main/history_store.c
run settings -Itests/nvs_fake main/settings.c
run iq_vfo main/iq_mixer.c main/iq_decimator.c
run spy_spectrum main/spy_spectrum.c main/iq_mixer.c main/iq_decimator.c main/spy_fft.c

# The optional POSIX shim is for test sandboxes that block send/recv.
socket_flags=()
if [[ ${ESPAIRSPY_HOST_SOCKET_SHIM:-0} == 1 ]]; then
    socket_flags=(-Wl,--wrap=send,--wrap=recv tests/spy_server_fake/socket_shim.c)
fi
run spy_server_fft -Itests/spy_server_fake -include tests/spy_server_fake/sdkconfig.h \
    main/spy_protocol.c main/spy_fft_settings.c main/iq_mixer.c \
    main/iq_decimator.c main/spy_fft.c "${socket_flags[@]}"
