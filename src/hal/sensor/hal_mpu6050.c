/*
 * CareLoop Hardware Abstraction Layer - MPU6050 Adapter
 * Provides unified HAL interface for accelerometer and gyroscope readings.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <math.h>

#include "hal_mpu6050.h"

LOG_MODULE_REGISTER(hal_mpu6050, LOG_LEVEL_INF);

#define MPU6050_NODE DT_NODELABEL(mpu6050)

/* Private state shared between accel and gyro logical sensors */
typedef struct {
    const struct device *dev;
    hal_sensor_stats_t accel_stats;
    hal_sensor_stats_t gyro_stats;
    bool initialized;
} mpu6050_priv_t;

static mpu6050_priv_t mpu_priv = {0};

/* Forward declare ops */
static hal_error_t mpu6050_accel_init(void);
static hal_error_t mpu6050_accel_read(hal_sensor_reading_t *reading);
static hal_device_status_t mpu6050_accel_status(void);
static hal_error_t mpu6050_accel_get_stats(hal_sensor_stats_t *stats);
static hal_error_t mpu6050_accel_reset_stats(void);

static hal_error_t mpu6050_gyro_init(void);
static hal_error_t mpu6050_gyro_read(hal_sensor_reading_t *reading);
static hal_device_status_t mpu6050_gyro_status(void);
static hal_error_t mpu6050_gyro_get_stats(hal_sensor_stats_t *stats);
static hal_error_t mpu6050_gyro_reset_stats(void);

/* Simple quality heuristic: always FAIR if device ready */
static inline hal_quality_t calc_quality(bool ok) {
    return ok ? HAL_QUALITY_FAIR : HAL_QUALITY_INVALID; /* refine later */
}

static hal_error_t ensure_device(void) {
    if (mpu_priv.dev && device_is_ready(mpu_priv.dev)) {
        return HAL_OK;
    }
    mpu_priv.dev = DEVICE_DT_GET(MPU6050_NODE);
    if (!mpu_priv.dev) {
        LOG_ERR("MPU6050 device not found in DT");
        return HAL_ERROR_HARDWARE;
    }
    if (!device_is_ready(mpu_priv.dev)) {
        LOG_ERR("MPU6050 device not ready");
        return HAL_ERROR_NOT_INITIALIZED;
    }
    return HAL_OK;
}

/* Shared init just checks device once */
static hal_error_t mpu6050_common_init(void) {
    hal_error_t ret = ensure_device();
    if (ret == HAL_OK) {
        if (!mpu_priv.initialized) {
            memset(&mpu_priv.accel_stats, 0, sizeof(mpu_priv.accel_stats));
            memset(&mpu_priv.gyro_stats, 0, sizeof(mpu_priv.gyro_stats));
            mpu_priv.initialized = true;
            LOG_INF("MPU6050 adapter initialized");
        }
    }
    return ret;
}

/* ACCEL ops */
static hal_error_t mpu6050_accel_init(void) { return mpu6050_common_init(); }

static hal_error_t mpu6050_accel_read(hal_sensor_reading_t *reading) {
    if (!reading) return HAL_ERROR_INVALID_PARAM;
    hal_error_t r = ensure_device();
    if (r != HAL_OK) return r;

    if (sensor_sample_fetch(mpu_priv.dev) != 0) {
        mpu_priv.accel_stats.error_count++;
        return HAL_ERROR_HARDWARE;
    }
    struct sensor_value accel[3];
    if (sensor_channel_get(mpu_priv.dev, SENSOR_CHAN_ACCEL_XYZ, accel) != 0) {
        mpu_priv.accel_stats.error_count++;
        return HAL_ERROR_HARDWARE;
    }
    /* Fill vector and magnitude (units per Zephyr driver scaling) */
    int32_t x = accel[0].val1;
    int32_t y = accel[1].val1;
    int32_t z = accel[2].val1;
    float mag = sqrtf((float)(x*x + y*y + z*z));

    reading->timestamp = hal_get_timestamp();
    reading->value = mag; /* magnitude */
    reading->x = (float)x;
    reading->y = (float)y;
    reading->z = (float)z;
    reading->raw_value = (uint32_t)(x & 0xFFFFFFFF); /* legacy */
    reading->quality = calc_quality(true);
    reading->error_code = HAL_OK;

    mpu_priv.accel_stats.total_samples++;
    mpu_priv.accel_stats.valid_samples++;
    mpu_priv.accel_stats.last_reading = reading->timestamp;
    if (mpu_priv.accel_stats.total_samples == 1) {
        mpu_priv.accel_stats.avg_quality = reading->quality;
    } else {
        mpu_priv.accel_stats.avg_quality = (mpu_priv.accel_stats.avg_quality * 9 + reading->quality) / 10;
    }
    return HAL_OK;
}

static hal_device_status_t mpu6050_accel_status(void) {
    return (mpu_priv.dev && device_is_ready(mpu_priv.dev)) ? HAL_DEVICE_STATUS_READY : HAL_DEVICE_STATUS_ERROR;
}

static hal_error_t mpu6050_accel_get_stats(hal_sensor_stats_t *stats) {
    if (!stats) return HAL_ERROR_INVALID_PARAM;
    *stats = mpu_priv.accel_stats;
    return HAL_OK;
}

static hal_error_t mpu6050_accel_reset_stats(void) {
    memset(&mpu_priv.accel_stats, 0, sizeof(mpu_priv.accel_stats));
    return HAL_OK;
}

/* GYRO ops */
static hal_error_t mpu6050_gyro_init(void) { return mpu6050_common_init(); }

static hal_error_t mpu6050_gyro_read(hal_sensor_reading_t *reading) {
    if (!reading) return HAL_ERROR_INVALID_PARAM;
    hal_error_t r = ensure_device();
    if (r != HAL_OK) return r;

    if (sensor_sample_fetch(mpu_priv.dev) != 0) {
        mpu_priv.gyro_stats.error_count++;
        return HAL_ERROR_HARDWARE;
    }
    struct sensor_value gyro[3];
    if (sensor_channel_get(mpu_priv.dev, SENSOR_CHAN_GYRO_XYZ, gyro) != 0) {
        mpu_priv.gyro_stats.error_count++;
        return HAL_ERROR_HARDWARE;
    }
    int32_t x = gyro[0].val1;
    int32_t y = gyro[1].val1;
    int32_t z = gyro[2].val1;
    float mag = sqrtf((float)(x*x + y*y + z*z));

    reading->timestamp = hal_get_timestamp();
    reading->value = mag; /* magnitude */
    reading->x = (float)x;
    reading->y = (float)y;
    reading->z = (float)z;
    reading->raw_value = (uint32_t)(x & 0xFFFFFFFF);
    reading->quality = calc_quality(true);
    reading->error_code = HAL_OK;

    mpu_priv.gyro_stats.total_samples++;
    mpu_priv.gyro_stats.valid_samples++;
    mpu_priv.gyro_stats.last_reading = reading->timestamp;
    if (mpu_priv.gyro_stats.total_samples == 1) {
        mpu_priv.gyro_stats.avg_quality = reading->quality;
    } else {
        mpu_priv.gyro_stats.avg_quality = (mpu_priv.gyro_stats.avg_quality * 9 + reading->quality) / 10;
    }
    return HAL_OK;
}

static hal_device_status_t mpu6050_gyro_status(void) {
    return (mpu_priv.dev && device_is_ready(mpu_priv.dev)) ? HAL_DEVICE_STATUS_READY : HAL_DEVICE_STATUS_ERROR;
}

static hal_error_t mpu6050_gyro_get_stats(hal_sensor_stats_t *stats) {
    if (!stats) return HAL_ERROR_INVALID_PARAM;
    *stats = mpu_priv.gyro_stats;
    return HAL_OK;
}

static hal_error_t mpu6050_gyro_reset_stats(void) {
    memset(&mpu_priv.gyro_stats, 0, sizeof(mpu_priv.gyro_stats));
    return HAL_OK;
}

/* Operations structs */
static const hal_sensor_ops_t accel_ops = {
    .init = mpu6050_accel_init,
    .read = mpu6050_accel_read,
    .configure = NULL,
    .calibrate = NULL,
    .get_status = mpu6050_accel_status,
    .get_stats = mpu6050_accel_get_stats,
    .reset_stats = mpu6050_accel_reset_stats
};

static const hal_sensor_ops_t gyro_ops = {
    .init = mpu6050_gyro_init,
    .read = mpu6050_gyro_read,
    .configure = NULL,
    .calibrate = NULL,
    .get_status = mpu6050_gyro_status,
    .get_stats = mpu6050_gyro_get_stats,
    .reset_stats = mpu6050_gyro_reset_stats
};

static hal_sensor_t accel_sensor = {
    .type = HAL_SENSOR_TYPE_ACCEL,
    .name = "MPU6050 Accelerometer",
    .ops = &accel_ops,
    .priv_data = &mpu_priv,
    .initialized = false
};

static hal_sensor_t gyro_sensor = {
    .type = HAL_SENSOR_TYPE_GYRO,
    .name = "MPU6050 Gyroscope",
    .ops = &gyro_ops,
    .priv_data = &mpu_priv,
    .initialized = false
};

hal_error_t hal_mpu6050_init(void) {
    LOG_INF("Registering MPU6050 accel+gyro sensors");
    hal_error_t r1 = hal_sensor_register(&accel_sensor);
    hal_error_t r2 = hal_sensor_register(&gyro_sensor);
    if (r1 != HAL_OK || r2 != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}
