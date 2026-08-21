// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Motion - acceleration and angular rate.
 *
 * Optional features are advertised as capability bits and their ranges
 * reported at runtime, so this header names no part and a service written
 * against it survives a change of sensor.
 */

#ifndef HAL_MOTION_H_
#define HAL_MOTION_H_

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

/* Motion specific error codes. */
#define ERR_MOTION_NOT_READY (ERR_HAL_MOTION + 1)   /* absent, or failed to start */
#define ERR_MOTION_NOT_INITIALIZED (ERR_HAL_MOTION + 2)
#define ERR_MOTION_NOT_CONFIGURED (ERR_HAL_MOTION + 3)
#define ERR_MOTION_READ_FAILED (ERR_HAL_MOTION + 4)
#define ERR_MOTION_INVALID_CONFIG (ERR_HAL_MOTION + 5)
#define ERR_MOTION_NO_TRIGGER (ERR_HAL_MOTION + 6)  /* no interrupt path on this board */
#define ERR_MOTION_UNSUPPORTED (ERR_HAL_MOTION + 7) /* fitted sensor lacks the feature */

/** Acceleration due to gravity, mm/s^2. */
#define MOTION_GRAVITY_MMS2 9807

/* Optional capabilities, from motion_get_capabilities(). */
#define MOTION_CAP_GYRO (1U << 0)
#define MOTION_CAP_OVERSAMPLING (1U << 1)
#define MOTION_CAP_DATA_READY (1U << 2)
#define MOTION_CAP_ANY_MOTION (1U << 3)

/** One accelerometer sample, mm/s^2 per axis, sensor frame. */
struct motion_accel_sample {
    int32_t x_mms2;
    int32_t y_mms2;
    int32_t z_mms2;
};

/** One gyroscope sample, millidegrees per second per axis, sensor frame. */
struct motion_gyro_sample {
    int32_t x_mdps;
    int32_t y_mdps;
    int32_t z_mdps;
};

/**
 * @brief Sampling configuration.
 *
 * Rates snap down to the sensor's ladder; ranges must match exactly or the
 * request is rejected, since rounding a range would change the resolution of
 * every sample after it. Rates are in millihertz because the useful rates for
 * an always-on wearable are below 1 Hz. A rate of 0 leaves that sensor
 * powered down.
 *
 * Oversampling is honoured only with MOTION_CAP_OVERSAMPLING; 0 means leave
 * the hardware default.
 */
struct motion_config {
    uint32_t accel_odr_mhz;
    uint16_t accel_range_g;
    uint16_t accel_oversampling;
    uint32_t gyro_odr_mhz;
    uint16_t gyro_range_dps;
    uint16_t gyro_oversampling;
};

/** Events a sensor can raise by itself. */
enum motion_event {
    MOTION_EVENT_DATA_READY,
    MOTION_EVENT_ANY_MOTION,
};

/**
 * @brief Called when the sensor raises an event.
 *
 * Runs in a driver thread shared with other sensors: do not block and do not
 * read the sensor from it.
 */
typedef void (*motion_event_cb_t)(enum motion_event event);

/** Thresholds arming MOTION_EVENT_ANY_MOTION. Bounded by the limits below. */
struct motion_any_motion_config {
    uint16_t threshold_mg;
    uint32_t duration_ms;
};

/** What the fitted sensor accepts for any-motion. */
struct motion_any_motion_limits {
    uint16_t threshold_min_mg;
    uint16_t threshold_max_mg;
    uint32_t duration_max_ms;
    /** Duration granularity; requests are rounded down to a multiple. */
    uint32_t duration_step_ms;
};

/**
 * @brief Bring up the IMU. Leaves it suspended.
 *
 * @return 0, -ERR_MOTION_NOT_READY if absent or not the expected part, or a
 *         negative errno.
 */
int motion_init(void);

/**
 * @brief Report which optional features this board and build provide.
 *
 * Valid after motion_init(); 0 before it. Calling an optional entry point
 * whose bit is clear returns -ERR_MOTION_UNSUPPORTED.
 *
 * @return Bitwise OR of MOTION_CAP_* values.
 */
uint32_t motion_get_capabilities(void);

/**
 * @brief Set rates and ranges, and power up the requested sensors.
 *
 * Samples taken within the first few cycles after a rate change have not
 * settled. May be called again to retune.
 *
 * @return 0, -ERR_MOTION_INVALID_CONFIG if a rate is below the sensor's
 *         slowest step or a range or oversampling factor is not offered,
 *         -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_INITIALIZED, or a negative
 *         errno.
 */
int motion_configure(const struct motion_config *config);

/**
 * @brief Suspend or resume sampling, keeping the configuration.
 *
 * Reads while suspended fail rather than returning the last live sample.
 *
 * @return 0, -ERR_MOTION_NOT_CONFIGURED, or a negative errno.
 */
int motion_set_enabled(bool enabled);

/**
 * @brief Read the current acceleration. Fetches on every call; no caching.
 *
 * @return 0, -ERR_MOTION_NOT_CONFIGURED, -ERR_MOTION_READ_FAILED, or a
 *         negative errno.
 */
int motion_read_accel(struct motion_accel_sample *sample);

/**
 * @brief Read the current angular rate.
 *
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_CONFIGURED if
 *         gyro_odr_mhz was 0, -ERR_MOTION_READ_FAILED, or a negative errno.
 */
int motion_read_gyro(struct motion_gyro_sample *sample);

/**
 * @brief Install or remove the event callback. NULL removes.
 *
 * One callback serves every event. Registering does not start any event; use
 * motion_set_event_enabled().
 *
 * @return 0, -ERR_MOTION_NO_TRIGGER, -ERR_MOTION_NOT_INITIALIZED, or a
 *         negative errno.
 */
int motion_set_event_callback(motion_event_cb_t callback);

/**
 * @brief Start or stop one event source.
 *
 * MOTION_EVENT_ANY_MOTION cannot be enabled before
 * motion_configure_any_motion(): arming it against thresholds that were never
 * set either floods the callback or never fires, neither distinguishable from
 * working hardware.
 *
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_CONFIGURED,
 *         -ERR_MOTION_NOT_INITIALIZED, or a negative errno.
 */
int motion_set_event_enabled(enum motion_event event, bool enabled);

/**
 * @brief Report the bounds for any-motion configuration.
 *
 * @return 0, -ERR_MOTION_UNSUPPORTED, or -ERR_MOTION_NOT_INITIALIZED.
 */
int motion_get_any_motion_limits(struct motion_any_motion_limits *limits);

/**
 * @brief Set the any-motion threshold and duration.
 *
 * Duration is rounded down to a multiple of duration_step_ms. A threshold
 * outside the reported bounds is rejected, not clamped.
 *
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_INVALID_CONFIG,
 *         -ERR_MOTION_NOT_INITIALIZED, or a negative errno.
 */
int motion_configure_any_motion(const struct motion_any_motion_config *config);

#endif /* HAL_MOTION_H_ */
