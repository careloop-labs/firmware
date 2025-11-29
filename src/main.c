#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include "hal_sensor.h"

/* Device-specific HAL adapters register via hal_sensor_system_init() */

/* MPU6050 now accessed via HAL (accel + gyro logical sensors) */

LOG_MODULE_REGISTER(careloop, LOG_LEVEL_INF);

static inline void log_vec_scaled(const char *tag, float mag, float x, float y, float z)
{
    int32_t mag_m = (int32_t)(mag * 1000.0f);
    int32_t x_m = (int32_t)(x * 1000.0f);
    int32_t y_m = (int32_t)(y * 1000.0f);
    int32_t z_m = (int32_t)(z * 1000.0f);
    LOG_INF("%s mag=%d.%03d x=%d.%03d y=%d.%03d z=%d.%03d",
            tag,
            (int)(mag_m / 1000), (int)abs(mag_m % 1000),
            (int)(x_m / 1000), (int)abs(x_m % 1000),
            (int)(y_m / 1000), (int)abs(y_m % 1000),
            (int)(z_m / 1000), (int)abs(z_m % 1000));
}

int main(void)
{
    LOG_INF("CareLoop HAL Heart Rate Monitor");
    
    hal_error_t ret;
    hal_sensor_t *hr_sensor;
    hal_sensor_t *accel_sensor;
    hal_sensor_t *gyro_sensor;
    hal_sensor_reading_t reading;
    
    /* Initialize sensor subsystem (register + init) */
    ret = hal_sensor_system_init();
    if (ret != HAL_OK) {
        LOG_ERR("Sensor system init failed: %d", ret);
        return -1;
    }
    
    /* Get heart rate sensor */
    hr_sensor = hal_sensor_get(HAL_SENSOR_TYPE_HEART_RATE);
    if (!hr_sensor) {
        LOG_ERR("No heart rate sensor found");
        return -1;
    }
    accel_sensor = hal_sensor_get(HAL_SENSOR_TYPE_ACCEL);
    gyro_sensor = hal_sensor_get(HAL_SENSOR_TYPE_GYRO);
    if (!accel_sensor || !gyro_sensor) {
        LOG_WRN("ACCEL or GYRO sensors not available");
    }
    
    LOG_INF("Heart rate sensor ready");
    
    /* Main reading loop */
    while (1) {
        ret = hr_sensor->ops->read(&reading);
        if (ret == HAL_OK) {
            if (reading.quality >= HAL_QUALITY_FAIR) {
                LOG_INF("HR: %d (Q:%d%%)", reading.raw_value, reading.quality);
            }
        }
        if (accel_sensor && accel_sensor->ops->read) {
            if (accel_sensor->ops->read(&reading) == HAL_OK) {
                log_vec_scaled("ACC", reading.value, reading.x, reading.y, reading.z);
            }
        }
        if (gyro_sensor && gyro_sensor->ops->read) {
            if (gyro_sensor->ops->read(&reading) == HAL_OK) {
                log_vec_scaled("GYR", reading.value, reading.x, reading.y, reading.z);
            }
        }

        k_sleep(K_MSEC(200));
    }
    
    return 0;
}
