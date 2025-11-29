/*
 * CareLoop Hardware Abstraction Layer - MPU6050 Adapter
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_MPU6050_H
#define HAL_MPU6050_H

#include "hal_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and register MPU6050 (accelerometer + gyro) sensors.
 * Registers two logical sensors with types HAL_SENSOR_TYPE_ACCEL and HAL_SENSOR_TYPE_GYRO.
 * @return HAL_OK on success or negative error code.
 */
hal_error_t hal_mpu6050_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MPU6050_H */
