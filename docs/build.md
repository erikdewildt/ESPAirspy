# Build, flash and monitor

Use ESP-IDF **v5.5.4**, its installed ESP32-P4 RISC-V toolchain and the checked-in `sdkconfig.p4`. The project wrapper applies the pre-v3 clock and PSRAM fixes to the SDK checkout. Use an SDK clone you can write to. Initialize ESP-IDF's recursive submodules when cloning. ESPAirspy has no project submodules.

The [CI workflow](../.github/workflows/ci.yml) tests, builds and uploads images for pull requests to `main` and pushes to `main`. Tags matching the firmware version publish GitHub Releases. See [CI and releases](releases.md) and [prebuilt installation](install.md).

## Linux and macOS

Install the platform prerequisites from the [ESP-IDF v5.5.4 guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/get-started/linux-macos-setup.html). On Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install git wget flex bison gperf python3 python3-venv python3-pip cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 patch
```

On macOS, install Xcode Command Line Tools and the Homebrew or MacPorts packages listed in the linked guide. Use Bash for the commands below. Set the repository URL once the maintainer publishes it.

```sh
export ESPAIRSPY_REPO_URL='https://github.com/OWNER/REPOSITORY.git'
mkdir -p "$HOME/esp"
git clone --branch v5.5.4 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-5.5.4"
export IDF_PATH="$HOME/esp/esp-idf-5.5.4"
"$IDF_PATH/install.sh" esp32p4
. "$IDF_PATH/export.sh"
idf.py --version
git clone "$ESPAIRSPY_REPO_URL" ESPAirspy
cd ESPAirspy
./build.sh
idf.py -B build-p4 -D SDKCONFIG=sdkconfig.p4 merge-bin
```

`sdkconfig.p4` already selects `esp32p4`. Avoid calling `set-target` on that shared config. Use both `AIRSPY_SDKCONFIG` and `AIRSPY_BUILD_DIR` with a copied config for experiments. The project version comes from `PROJECT_VER` in `CMakeLists.txt` and is visible in the dashboard or `/api/status`. Build outputs depend on the toolchain, embedded timestamp and build paths. A repeatable build procedure does not imply byte-identical images.

| File | Use |
|---|---|
| `build-p4/ESPAirspy.bin` | App-only ESP OTA image |
| `build-p4/merged-binary.bin` | Factory image at offset 0, released as `ESPAirspy-full.bin` |
| `build-p4/ESPAirspy.elf` | Local serial decoding and debugging |

## Install on the board

- **First USB installation:** connect to the programming port and use `./flash.sh /dev/ttyACM0` on Linux or pass the actual `/dev/cu.usbmodem…` port on macOS. A full image rewrites bootloader, partition table, OTA metadata and app. To flash a prebuilt merged asset, pass its path as the third argument to `./flash.sh PORT merged IMAGE`.
- **ESP OTA:** with the existing compatible partition layout, upload `ESPAirspy.bin` under **Firmware → Update ESP firmware** and select **Upload and restart**. After the reboot, open **Overview** to confirm the running version, Airspy connection and SpyServer listener. The separate Airspy receiver firmware button expects a different vendor image. See [Receiver firmware updates](airspy-firmware-update.md).
- **Serial log:** run `idf.py -B build-p4 -D SDKCONFIG=sdkconfig.p4 -p PORT monitor` with the active IDF environment. The default monitor baud is 115200. Exit with Ctrl+].

## Windows

Use WSL2 and the Linux commands to build. Keep the SDK and workspace on the WSL filesystem for normal Unix permissions. For flashing on native Windows, copy the binaries to the project layout and use the ESP-IDF command prompt and `flash.bat COM5`, or `flash.bat COM5 merged PATH_TO_MERGED_IMAGE` for a release image. Replace COM5 with the actual programming port. OTA through the browser needs no serial passthrough. A native Windows source build and the WSL workflow are **NOT TESTED** in this review.

## Troubleshooting

| Symptom | Check |
|---|---|
| Wrong or missing IDF version | Activate the v5.5.4 checkout in a fresh shell and verify `idf.py --version`. |
| Submodule missing | In the SDK checkout run `git submodule update --init --recursive`. Use the Espressif release archive that includes submodules if cloning is unavailable. |
| SDK patch fails | Check that the SDK is writable and based on unmodified v5.5.4. Recreate the SDK clone if unrelated changes touched the patched files. |
| CMake reports an old SDK or target path | Use a fresh output directory. Keep the supplied ESP32-P4 config and enable the same IDF environment for build and merge. |
| No serial port | Check the programming connector, data cable, OS serial permissions and BOOT/reset state. |
| OTA accepts image but old version returns | Allow the board to reconnect and inspect serial boot logs for rollback, or check that the uploaded file was the intended app image. |
