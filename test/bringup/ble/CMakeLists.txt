cmake_minimum_required(VERSION 3.20.0)

# Standalone app: the careloop_v2 board and DT bindings live in the repo root,
# three levels up. Both must be set before find_package(Zephyr) or they are ignored.
get_filename_component(CARELOOP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
list(APPEND BOARD_ROOT ${CARELOOP_ROOT})
list(APPEND DTS_ROOT ${CARELOOP_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(careloop_ble)

# The product BLE stack, compiled verbatim - this app exists to test *that*
# code, not a copy of it. Globbed for the same reason the root CMakeLists globs:
# a service added under src/ and silently missing from this image is exactly the
# kind of drift this diagnostic is meant to catch.
#
# src/main.c and src/business/ are excluded deliberately - this app supplies its
# own main(), and smf_init()/smf_update() are called from nowhere in the tree.
file(GLOB_RECURSE CARELOOP_BLE_SOURCES ${CARELOOP_ROOT}/src/communication/*.c)

target_sources(app PRIVATE
    src/main.c
    ${CARELOOP_BLE_SOURCES}
)

# Two include roots, both needed: files under src/communication include each
# other as <ble/...>, <gatt/...>, <network_config.h>, while product-style
# consumers use "communication/include/...".
target_include_directories(app PRIVATE
    ${CARELOOP_ROOT}/src
    ${CARELOOP_ROOT}/src/communication/include
)
