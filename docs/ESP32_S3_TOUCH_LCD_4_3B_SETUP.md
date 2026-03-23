# ESP32-S3-Touch-LCD-4.3B Environment Setup

## Source References
- Waveshare hardware wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B
- Espressif ESP-IDF overview: https://www.espressif.com/en/products/sdks/esp-idf
- ESP-IDF programming guide (ESP32-S3): https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/
- ESP-IDF LCD peripheral API: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/lcd/index.html
- ESP-IDF I2C API: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2c.html

## Hardware Notes Applied to This Project
- MCU: ESP32-S3R8 dual-core Xtensa LX7.
- Display: 4.3 inch IPS RGB LCD, resolution `800x480`.
- Touch: GT911 capacitive touch controller on I2C (`0x5D`).
- RGB panel data/timing GPIO mapping is implemented in `main/hardware_init.c`.

## Local Toolchain Status
- Project is configured to run with a local ESP-IDF installation.
- Ensure `idf.py` is available from your terminal session before building.
- On Windows, use ESP-IDF PowerShell/CMD or source/export your ESP-IDF environment in shell.
- If PowerShell blocks `export.ps1` because of execution policy, use:
  - `.\scripts\idfw.cmd <idf.py args...>`
  This wrapper runs PowerShell with `-ExecutionPolicy Bypass` for the current command only.

## Local Workspace Defaults
For consistent local builds, use:
- `XDG_CACHE_HOME=.cache`
- Build directory override: `-B .idfbuild`

The helper scripts in `scripts/` apply these defaults.

## Build/Flash Workflow
1. Initialize environment helper (optional):
   - `source scripts/setup_idf_env.sh`
2. Configure target:
   - `XDG_CACHE_HOME=.cache idf.py -B .idfbuild -DIDF_TARGET=esp32s3 reconfigure`
3. Build:
   - `XDG_CACHE_HOME=.cache idf.py -B .idfbuild build`
4. Flash + monitor:
   - `XDG_CACHE_HOME=.cache idf.py -B .idfbuild -p <PORT> flash monitor`
5. Or use project wrapper:
   - `./scripts/build_flash_prompt.sh`
6. Windows wrappers:
   - `.\scripts\idfw.cmd -DIDF_TARGET=esp32s3 reconfigure`
   - `.\scripts\idfw.cmd build`
   - `.\scripts\idfw.cmd -p COM9 flash monitor`

## Dependency Mode
To support this offline/restricted environment, project manifests are set to local paths for LVGL and board touch components instead of online registry resolution.

## External Demo Examples (User Path)
Requested path:
- `C:\Espressif\OriginalFiles\ESP32-S3-Touch-LCD-4.3B-BOX-Demo\ESP-IDF`

If this path is not available on your machine yet, copy/import those example files into this workspace first, then map each example into this project's `docs/` and `main/` modules.
