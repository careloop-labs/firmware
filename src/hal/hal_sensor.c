/*
 * CareLoop Hardware Abstraction Layer - Sensor Registry
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_sensor.h"
#include "hal_max30102.h"
#include "hal_mpu6050.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hal_sensor, LOG_LEVEL_DBG);

/* Maximum number of sensors that can be registered */
#define HAL_SENSOR_MAX_COUNT 8

/* Sensor registry */
static hal_sensor_t *sensor_registry[HAL_SENSOR_MAX_COUNT];
static uint8_t sensor_count = 0;

hal_error_t hal_sensor_register(hal_sensor_t *sensor)
{
    if (!sensor) {
        LOG_ERR("Invalid sensor pointer");
        return HAL_ERROR_INVALID_PARAM;
    }
    
    if (sensor_count >= HAL_SENSOR_MAX_COUNT) {
        LOG_ERR("Maximum sensor count exceeded");
        return HAL_ERROR;
    }
    
    if (sensor->type >= HAL_SENSOR_TYPE_COUNT) {
        LOG_ERR("Invalid sensor type: %d", sensor->type);
        return HAL_ERROR_INVALID_PARAM;
    }
    
    if (!sensor->ops) {
        LOG_ERR("Sensor operations not provided");
        return HAL_ERROR_INVALID_PARAM;
    }
    
    /* Check if sensor type already registered */
    for (int i = 0; i < sensor_count; ++i) {
        if (sensor_registry[i]->type == sensor->type) {
            LOG_WRN("Sensor type %d already registered, replacing", sensor->type);
            sensor_registry[i] = sensor;
            return HAL_OK;
        }
    }
    
    /* Add new sensor */
    sensor_registry[sensor_count] = sensor;
    sensor_count++;
    
    LOG_INF("Registered sensor: %s (type %d)", 
            sensor->name ? sensor->name : "Unknown", sensor->type);
    
    return HAL_OK;
}

hal_sensor_t* hal_sensor_get(hal_sensor_type_t type)
{
    if (type >= HAL_SENSOR_TYPE_COUNT) {
        LOG_ERR("Invalid sensor type: %d", type);
        return NULL;
    }
    
    for (int i = 0; i < sensor_count; i++) {
        if (sensor_registry[i]->type == type) {
            return sensor_registry[i];
        }
    }
    
    LOG_WRN("Sensor type %d not found", type);
    return NULL;
}

hal_error_t hal_sensor_init_all(void)
{
    hal_error_t ret = HAL_OK;
    int initialized_count = 0;
    
    LOG_INF("Initializing %d registered sensors", sensor_count);
    
    for (int i = 0; i < sensor_count; i++) {
        hal_sensor_t *sensor = sensor_registry[i];
        
        if (!sensor->ops->init) {
            LOG_WRN("Sensor %s has no init function", 
                    sensor->name ? sensor->name : "Unknown");
            continue;
        }
        
        LOG_DBG("Initializing sensor: %s", 
                sensor->name ? sensor->name : "Unknown");
        
        hal_error_t init_ret = sensor->ops->init();
        if (init_ret == HAL_OK) {
            sensor->initialized = true;
            initialized_count++;
            LOG_INF("Sensor %s initialized successfully", 
                    sensor->name ? sensor->name : "Unknown");
        } else {
            sensor->initialized = false;
            LOG_ERR("Failed to initialize sensor %s: %d", 
                    sensor->name ? sensor->name : "Unknown", init_ret);
            ret = HAL_ERROR;
        }
    }
    
    LOG_INF("Sensor initialization complete: %d/%d successful", 
            initialized_count, sensor_count);
    
    return ret;
}

/**
 * @brief Register all available sensors with the HAL registry.
 * This centralizes device-specific registration away from application code.
 */
hal_error_t hal_sensor_register_all(void)
{
    hal_error_t ret = HAL_OK;

#ifdef CONFIG_MAX30102
    if (hal_max30102_init() != HAL_OK) {
        LOG_ERR("Failed to register MAX30102");
        ret = HAL_ERROR;
    }
#endif

#ifdef CONFIG_MPU6050
    if (hal_mpu6050_init() != HAL_OK) {
        LOG_WRN("MPU6050 not registered");
        ret = HAL_ERROR;
    }
#endif

    return ret;
}

/**
 * @brief System-level initialization for sensors.
 * Registers all sensors and then initializes them via ops->init.
 */
hal_error_t hal_sensor_system_init(void)
{
    hal_error_t r = hal_sensor_register_all();
    if (r != HAL_OK) {
        LOG_WRN("Some sensors failed to register");
        // continue to attempt init for successfully registered sensors
    }
    return hal_sensor_init_all();
}
