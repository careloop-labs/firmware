cmake_minimum_required(VERSION 3.20.0)

# Standalone app: the careloop board and DT bindings live in the repo root,
# three levels up. Both must be set before find_package(Zephyr) or they are ignored.
get_filename_component(CARELOOP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
list(APPEND BOARD_ROOT ${CARELOOP_ROOT})
list(APPEND DTS_ROOT ${CARELOOP_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(careloop_optical)

# The product optical HAL, compiled verbatim. Everything specific to this
# board lives in it - the emitter-to-TX mapping, the reset pulse width, the
# internal-oscillator bit - and none of it can be checked without looking at
# the emitters, so it is checked here or nowhere.
#
# Named explicitly rather than globbed over src/hal, for the same reason the
# LEDs app does: a glob drags in motion, power and skin_temperature and forces
# CONFIG_SENSOR and the whole MFD stack into what is meant to stay minimal.
target_sources(app PRIVATE
    src/main.c
    src/detector.c
    ${CARELOOP_ROOT}/src/hal/optical_afe4404.c
)

# hal/optical_afe4404.c includes <optical.h>, which includes "errors.h" from src/.
target_include_directories(app PRIVATE
    ${CARELOOP_ROOT}/src
    ${CARELOOP_ROOT}/src/hal/include
)
