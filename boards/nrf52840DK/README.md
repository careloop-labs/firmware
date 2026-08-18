# nrf52840DK — intentionally empty

There is no board definition here, and there should not be one.

`nrf52840dk` already ships with Zephyr, at
`$ZEPHYR_BASE/boards/nordic/nrf52840dk`. This repo's `boards/` is on
`BOARD_ROOT` (set in each bring-up app's `CMakeLists.txt`), so Zephyr scans it
