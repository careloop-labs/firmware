cmake_minimum_required(VERSION 3.20.0)

# Standalone app: the careloop board and DT bindings live in the repo root,
# three levels up. Both must be set before find_package(Zephyr) or they are ignored.
get_filename_component(CARELOOP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
list(APPEND BOARD_ROOT ${CARELOOP_ROOT})
list(APPEND DTS_ROOT ${CARELOOP_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(careloop_skin_temperature)

# The product temperature HAL, compiled verbatim - streaming through the same
# code the product runs is the point, not an implementation detail.
#
# Named explicitly rather than globbed over src/hal: a glob would drag in
# motion, power and leds and force the whole PMIC stack into what is meant to
# stay a minimal tool.
target_sources(app PRIVATE
    src/main.c
    ${CARELOOP_ROOT}/src/hal/skin_temperature_tmp117.c
)

# hal/skin_temperature_tmp117.c includes <skin_temperature.h>, which includes
# "errors.h" from src/.
target_include_directories(app PRIVATE
    ${CARELOOP_ROOT}/src
    ${CARELOOP_ROOT}/src/hal/include
)
