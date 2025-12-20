/*
 * CareLoop Hardware Abstraction Layer - MAX30102 Interface
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_MAX30102_H
#define HAL_MAX30102_H

#include "hal_sensor.h"

/**
 * @brief Initialize and register MAX30102 sensor with HAL
 * @return HAL_OK on success, negative error code on failure
 */
hal_error_t hal_max30102_init(void);

#endif /* HAL_MAX30102_H */
