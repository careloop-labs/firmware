cmake_minimum_required(VERSION 3.20.0)

# Standalone app: the careloop board and DT bindings live in the repo root,
# three levels up. Both must be set before find_package(Zephyr) or they are
# ignored. The nRF52840 DK needs neither - it ships with Zephyr - but this app
# is built for both, so the roots are set unconditionally.
get_filename_component(CARELOOP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
list(APPEND BOARD_ROOT ${CARELOOP_ROOT})
list(APPEND DTS_ROOT ${CARELOOP_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(careloop_clocks)

target_sources(app PRIVATE src/main.c)

# clock_measure.h is shared verbatim with the ztest suite in
# test/bringup/careloop - the numbers are only worth taking if both boards are
# measured by the same code.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
