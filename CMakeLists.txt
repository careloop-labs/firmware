cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(CareLoop)

# Recursively add all .c files in src/ as sources
file(GLOB_RECURSE APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

target_sources(app PRIVATE ${APP_SOURCES})

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# BLE
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/communication/include
)

# Business
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/business/include
)