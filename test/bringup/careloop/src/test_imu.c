// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * BMI270 IMU (U4).
 *
 * device_is_ready() covers the chip-ID check and the 8 KB configuration
 * upload the BMI270 needs before it will do anything at all.
 *
 * The accelerometer must then be explicitly enabled: bmi270_init leaves
 * PWR_CTRL clear and the part suspended, so setting an output data rate is
 * what actually powers the MEMS. Without it every sample reads zero.
 *
 * The magnitude check is the point of this file. A chip that answers on the
 * bus proves the digital side; only gravity proves the sensor.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#define IMU_ODR_HZ       100
#define IMU_RANGE_G      8
#define IMU_SETTLE_MS    50

/* 1 g in mm/s^2, and how far off a board lying still may be. */
#define GRAVITY_MMS2     9807
#define GRAVITY_TOL_MMS2 2500

static const struct device *const imu = DEVICE_DT_GET(DT_NODELABEL(bmi270));

ZTEST_SUITE(careloop_5_imu, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_5_imu, test_imu_ready)
{
    zassert_true(device_is_ready(imu),
                 "BMI270 not ready - chip ID mismatch or config upload failed");
}

ZTEST(careloop_5_imu, test_imu_acceleration_sane)
{
    struct sensor_value odr = { .val1 = IMU_ODR_HZ, .val2 = 0 };
    struct sensor_value range = { .val1 = IMU_RANGE_G, .val2 = 0 };
    struct sensor_value acc[3];
    int64_t sq = 0;
    int64_t lo = (int64_t)(GRAVITY_MMS2 - GRAVITY_TOL_MMS2);
    int64_t hi = (int64_t)(GRAVITY_MMS2 + GRAVITY_TOL_MMS2);

    zassert_ok(sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
                               SENSOR_ATTR_SAMPLING_FREQUENCY, &odr),
               "could not set accelerometer ODR");
    zassert_ok(sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
                               SENSOR_ATTR_FULL_SCALE, &range),
               "could not set accelerometer range");

    k_sleep(K_MSEC(IMU_SETTLE_MS));

    zassert_ok(sensor_sample_fetch(imu), "BMI270 sample fetch failed");
    zassert_ok(sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, acc),
               "BMI270 channel get failed");

    /* Compare squared magnitudes so no square root is needed. */
    for (int i = 0; i < 3; i++) {
        int64_t mms2 = sensor_value_to_milli(&acc[i]);

        sq += mms2 * mms2;
    }

    printk("  accel %lld %lld %lld mm/s2  |a|^2 %lld\n",
           sensor_value_to_milli(&acc[0]), sensor_value_to_milli(&acc[1]),
           sensor_value_to_milli(&acc[2]), sq);

    zassert_true(sq >= lo * lo && sq <= hi * hi,
                 "acceleration magnitude outside %lld..%lld mm/s2 - "
                 "a dead or stuck axis reads plausible per-axis but wrong here",
                 lo, hi);
}
