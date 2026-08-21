cmake_minimum_required(VERSION 3.20.0)

# Standalone app: the careloop board lives in the repo root, three levels up.
# Must be set before find_package(Zephyr) or it is ignored. Twister is given
# the same roots on its command line by scripts/bringup.sh, because sysbuild - which
# Twister does not use here - would otherwise configure before this runs.
get_filename_component(CARELOOP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
list(APPEND BOARD_ROOT ${CARELOOP_ROOT})
list(APPEND DTS_ROOT ${CARELOOP_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(careloop_bringup)

# The product HAL, compiled verbatim - the careloop_hal_* suites exist to test
# *that* code rather than a parallel copy of it. Globbed for the same reason
# test/bringup/ble globs src/communication: a new hal/*.c silently missing from
# this image is exactly the drift these tests are here to catch.
#
# src/hal only. A glob over src/ would pull in src/main.c, whose main() collides
# with the one ztest supplies.
file(GLOB_RECURSE CARELOOP_HAL_SOURCES ${CARELOOP_ROOT}/src/hal/*.c)

target_sources(app PRIVATE
    src/main.c
    src/test_mcu.c
    src/test_clocks.c
    src/test_i2c.c
    src/test_temperature.c
    src/test_imu.c
    src/test_power.c
    src/test_leds.c
    src/test_hal_uninit.c
    src/test_hal_temperature.c
    src/test_hal_motion.c
    src/test_hal_power.c
    src/test_hal_leds.c
    ${CARELOOP_HAL_SOURCES}
)

# clock_measure.h is shared verbatim with test/bringup/clocks, the standalone
# A/B that can also run on a DK. The careloop numbers are only worth taking
# if the control board is measured by exactly the same code.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# Both roots are needed: hal/*.c include their own headers as <power.h>,
# <motion.h> and so on, while those headers include "errors.h" from src/.
target_include_directories(app PRIVATE
    ${CARELOOP_ROOT}/src
    ${CARELOOP_ROOT}/src/hal/include
)
