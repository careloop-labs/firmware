cmake_minimum_required(VERSION 3.20.0)

# Standalone app: the careloop_v2 board lives in the repo root, three levels up.
# Must be set before find_package(Zephyr) or it is ignored. Twister is given
# the same roots on its command line by scripts/hil, because sysbuild - which
# Twister does not use here - would otherwise configure before this runs.
get_filename_component(CARELOOP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
list(APPEND BOARD_ROOT ${CARELOOP_ROOT})
list(APPEND DTS_ROOT ${CARELOOP_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(careloop_bringup)

target_sources(app PRIVATE
    src/main.c
    src/test_mcu.c
    src/test_i2c.c
    src/test_temperature.c
    src/test_imu.c
    src/test_power.c
    src/test_leds.c
)
