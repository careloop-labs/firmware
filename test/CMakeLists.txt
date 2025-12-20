cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(careloop_tests)

# Repository root
get_filename_component(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Test sources (add more ztest *.c/*.cpp here as you grow tests)
target_sources(app PRIVATE
    ${ROOT_DIR}/test/hr_filter_ztest.cpp
    ${ROOT_DIR}/src/business/hr_filter.cpp
)

target_include_directories(app PRIVATE
    ${ROOT_DIR}/src/business/include
)

# Zephyr toolchain provides C++ flags; ensure C++ is enabled via prj.conf (CONFIG_CPLUSPLUS=y)
