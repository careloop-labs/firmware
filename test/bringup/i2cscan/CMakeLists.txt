cmake_minimum_required(VERSION 3.20.0)

# Standalone app: the careloop_v2 board and DT bindings live in the repo root,
# three levels up. Both must be set before find_package(Zephyr) or they are ignored.
get_filename_component(CARELOOP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
list(APPEND BOARD_ROOT ${CARELOOP_ROOT})
list(APPEND DTS_ROOT ${CARELOOP_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(careloop_i2cscan)

target_sources(app PRIVATE src/main.c)
