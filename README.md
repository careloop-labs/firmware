# CareLoop Firmware

Zephyr-based firmware for the CareLoop wearable (nRF52840). The repository is
now flat — the former `v0/` tree was lifted into the root to simplify builds and
tooling.

## Quick start

1) Prerequisites: Zephyr toolchain (Zephyr SDK or NCS), `west` on PATH, and
   `ZEPHYR_BASE` pointing at your Zephyr install.
2) From the repo root:

```sh
west build -b nrf52840dk_nrf52840 -p auto .
```

3) Flash the board (optional):

```sh
west flash
```

## Repo layout

- boards/: Board overlays and DTS tweaks.
- dts/: Custom sensor bindings.
- src/: Application, HAL, and driver glue.
- test/: ztest coverage (heart-rate filter, etc.).
- tools/: PPG analysis / helper scripts.
- zephyr/module.yml: Declares the MAX30102 driver module.
- prj.conf, Kconfig, CMakeLists.txt: Zephyr app configuration.
- .vscode/tasks.json: VS Code build task using `./build.sh`.

## Sensors

- MAX30102: Heart-rate/PPG front-end (custom driver and HAL adapter).
- MPU6050: Accelerometer/gyro via Zephyr built-in driver, wrapped by HAL.

## Notes

- Defaults target `nrf52840dk_nrf52840`. Override by exporting `BOARD=...` or
  passing `-b` to `build.sh`.
- `build.sh` wraps `west build -p auto` so local builds stay clean without
  manual pristine handling.
