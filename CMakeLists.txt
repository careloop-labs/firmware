cmake_minimum_required(VERSION 3.20.0)

# Set the DTS root to include our custom bindings
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(CareLoop)

# Main application sources
target_sources(app PRIVATE 
    src/main.c
)

# ----------------------------------------
# HAL - Hardware Abstraction Layer sources
# ----------------------------------------

if(CONFIG_MPU6050)
    # Only build MPU6050 adapter and driver when enabled
    target_sources(app PRIVATE
        src/hal/sensor/hal_mpu6050.c
    )
endif()

if(CONFIG_MAX30102)
    # Only build MAX30102 adapter and driver when enabled
    target_sources(app PRIVATE 
        src/hal/sensor/hal_max30102.c
        src/drivers/sensor/max30102/max30102.c
    )
    target_include_directories(app PRIVATE src/drivers/sensor/max30102)
endif()

# Common HAL sources
target_sources(app PRIVATE
    src/hal/hal_sensor.c
)

# HAL include directories
target_include_directories(app PRIVATE 
    src/hal/include
)

# ----------------------------------------
# Business logic sources (C++)
# ----------------------------------------

target_sources(app PRIVATE
    src/business/heart_rate.cpp
    src/business/hr_filter.cpp
)
target_include_directories(app PRIVATE src/business/include)