@echo off
rem Flash ESPAirspy to an ESP32-P4 board over USB (Windows).
rem
rem Usage: flash.bat COMx [full^|app^|merged]
rem
rem   full    (default) bootloader + partition table + OTA metadata + app image
rem   app     app image only (0x20000)
rem   merged  write build-p4\merged-binary.bin at 0x0 (factory image)
rem
rem Requires ESP-IDF (idf.py) in the environment, or esptool.py on PATH.
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "PORT=%~1"
set "MODE=%~2"
if "%MODE%"=="" set "MODE=full"
set "BUILD=%AIRSPY_BUILD_DIR%"
if "%BUILD%"=="" set "BUILD=build-p4"
set "CFG=%AIRSPY_SDKCONFIG%"
if "%CFG%"=="" set "CFG=sdkconfig.p4"
set "CHIP=esp32p4"
set "BAUD=460800"

if "%PORT%"=="" (
    echo Usage: flash.bat COMx [full^|app^|merged]
    exit /b 2
)

if /I "%MODE%"=="merged" (
    set "MERGED=%~3"
    if "!MERGED!"=="" set "MERGED=!BUILD!\merged-binary.bin"
    esptool.py --chip %CHIP% -p %PORT% -b %BAUD% write_flash 0x0 "!MERGED!"
    exit /b %errorlevel%
)

where idf.py >nul 2>nul
if %errorlevel%==0 (
    if /I "%MODE%"=="app" (
        idf.py -B "%BUILD%" -D "SDKCONFIG=%CFG%" -p "%PORT%" app-flash
    ) else (
        idf.py -B "%BUILD%" -D "SDKCONFIG=%CFG%" -p "%PORT%" flash
    )
    exit /b %errorlevel%
)

if /I "%MODE%"=="app" (
    esptool.py --chip %CHIP% -p %PORT% -b %BAUD% write_flash 0x20000 "%BUILD%\ESPAirspy.bin"
) else (
    esptool.py --chip %CHIP% -p %PORT% -b %BAUD% write_flash ^
        0x2000 "%BUILD%\bootloader\bootloader.bin" ^
        0x8000 "%BUILD%\partition_table\partition-table.bin" ^
        0xf000 "%BUILD%\ota_data_initial.bin" ^
        0x20000 "%BUILD%\ESPAirspy.bin"
)
exit /b %errorlevel%
