/*
 * CareLoop Hardware Abstraction Layer - Common Definitions
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief HAL return codes
 */
typedef enum {
    HAL_OK = 0,
    HAL_ERROR = -1,
    HAL_ERROR_INVALID_PARAM = -2,
    HAL_ERROR_NOT_INITIALIZED = -3,
    HAL_ERROR_TIMEOUT = -4,
    HAL_ERROR_NO_DATA = -5,
    HAL_ERROR_HARDWARE = -6,
    HAL_ERROR_BUSY = -7
} hal_error_t;

/**
 * @brief HAL device status
 */
typedef enum {
    HAL_DEVICE_STATUS_UNKNOWN = 0,
    HAL_DEVICE_STATUS_READY,
    HAL_DEVICE_STATUS_BUSY,
    HAL_DEVICE_STATUS_ERROR,
    HAL_DEVICE_STATUS_SLEEP
} hal_device_status_t;

/**
 * @brief Common timestamp type (milliseconds since boot)
 */
typedef uint32_t hal_timestamp_t;

/**
 * @brief Quality score (0-100%)
 */
typedef uint8_t hal_quality_t;

#define HAL_QUALITY_EXCELLENT  90
#define HAL_QUALITY_GOOD       70
#define HAL_QUALITY_FAIR       50
#define HAL_QUALITY_POOR       30
#define HAL_QUALITY_INVALID    0

/**
 * @brief Get current timestamp in milliseconds
 */
static inline hal_timestamp_t hal_get_timestamp(void)
{
    return k_uptime_get_32();
}

#endif /* HAL_COMMON_H */
