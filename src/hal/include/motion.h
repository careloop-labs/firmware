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
#include <stddef.h>
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
#define ERR_MOTION_OVERFLOW (ERR_HAL_MOTION + 8)    /* samples were lost before they were read */
#define ERR_MOTION_FAULT (ERR_HAL_MOTION + 9)       /* the sensor reports itself unhealthy */
#define ERR_MOTION_BUSY (ERR_HAL_MOTION + 10)       /* a capture is already running */

/** Acceleration due to gravity, mm/s^2. */
#define MOTION_GRAVITY_MMS2 9807

/* Optional capabilities, from motion_get_capabilities(). */
#define MOTION_CAP_GYRO (1U << 0)
#define MOTION_CAP_OVERSAMPLING (1U << 1)
#define MOTION_CAP_DATA_READY (1U << 2)
#define MOTION_CAP_ANY_MOTION (1U << 3)
#define MOTION_CAP_NO_MOTION (1U << 4)
#define MOTION_CAP_FIFO (1U << 5)     /* hardware batching, and so streaming */
#define MOTION_CAP_ACTIVITY (1U << 6) /* sensor classifies its own motion */
#define MOTION_CAP_FAULT (1U << 7)    /* sensor reports its own health */

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
    MOTION_EVENT_NO_MOTION,
    /**
     * A batch has been drained into the rolling window, and into the capture
     * buffer if one is running. Raised after the data has landed, so a
     * handler may call motion_window_read() immediately.
     */
    MOTION_EVENT_BATCH,
};

/**
 * @brief What the sensor thinks the wearer is doing.
 *
 * Contextual metadata only. The classifier is the sensor's own and cannot be
 * inspected or tuned, so this must not be the sole trigger for a measurement.
 * MOTION_ACTIVITY_UNKNOWN is the power-on value and also what is reported
 * while classification is disabled.
 */
enum motion_activity {
    MOTION_ACTIVITY_STILL = 0,
    MOTION_ACTIVITY_WALKING,
    MOTION_ACTIVITY_RUNNING,
    MOTION_ACTIVITY_UNKNOWN,
};

/**
 * @brief Health as the sensor itself reports it.
 *
 * @c fatal means the part is not in an operational state and will not
 * recover without a reset - it keeps answering on the bus and keeps
 * returning plausible-looking samples throughout, which is exactly why this
 * has to be polled rather than inferred from the data.
 */
struct motion_fault {
    bool fatal;
    /**
     * The sensor's own motion features have stopped running, while plain
     * sampling carries on unaffected. Worth its own flag because it is the
     * one fault whose symptom is an event source that never fires - which is
     * indistinguishable from a quiet wearer.
     */
    bool feature_engine_disabled;
    /** The sensor stopped processing: it took too long, or it gave up. */
    bool processing_halted;
    bool fifo_error;
    /** Vendor-defined code; non-zero and undecoded means ask the vendor. */
    uint8_t vendor_code;
    /** Raw error registers, for logging a fault nobody has decoded yet. */
    uint8_t raw;
    uint8_t internal_raw;
};

/**
 * @brief True if @p fault records anything wrong.
 *
 * Judged on the decoded flags, deliberately, rather than on the raw registers
 * being zero. Parts read back reserved bits that are not errors and are not
 * documented, so demanding a zero register reports a permanent fault on
 * healthy hardware - and an indicator that is always lit is worse than none,
 * because it teaches whoever reads it to ignore the one time it means
 * something. The raw bytes stay in the struct so an undocumented bit can
 * still be logged and recognised if it ever moves.
 */
static inline bool motion_fault_is_clear(const struct motion_fault *fault)
{
    return !fault->fatal && !fault->feature_engine_disabled && !fault->processing_halted &&
           !fault->fifo_error && (fault->vendor_code == 0U);
}

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

/**
 * @brief Thresholds arming MOTION_EVENT_NO_MOTION.
 *
 * Same shape and same hardware limits as any-motion - it is the mirrored
 * comparison against the same slope, so motion_get_any_motion_limits()
 * describes both. Kept a distinct type so a caller cannot pass one where the
 * other was meant.
 */
struct motion_no_motion_config {
    uint16_t threshold_mg;
    uint32_t duration_ms;
};

/**
 * @brief Set the no-motion threshold and duration.
 *
 * Bounds and rounding are those of motion_get_any_motion_limits(). Like
 * any-motion, this must be called before the event is enabled.
 *
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_INVALID_CONFIG,
 *         -ERR_MOTION_NOT_INITIALIZED, or a negative errno.
 */
int motion_configure_no_motion(const struct motion_no_motion_config *config);

/**
 * @brief Read the sensor's own activity classification.
 *
 * Requires motion_set_activity_enabled(true) and reports
 * MOTION_ACTIVITY_UNKNOWN until the sensor has seen enough motion to decide.
 *
 * @param activity Destination. Untouched on failure.
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_INITIALIZED, or a
 *         negative errno.
 */
int motion_get_activity(enum motion_activity *activity);

/**
 * @brief Start or stop the sensor's activity classifier.
 *
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_INITIALIZED, or a
 *         negative errno.
 */
int motion_set_activity_enabled(bool enabled);

/**
 * @brief Read the sensor's self-reported health.
 *
 * Worth polling rather than inferring: a sensor in a fatal state still
 * acknowledges its address and still returns well-formed samples, so no
 * amount of looking at the data reveals it.
 *
 * @param fault Destination. Untouched on failure.
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_INITIALIZED, or a
 *         negative errno.
 */
int motion_get_fault(struct motion_fault *fault);

/* --- Streaming ------------------------------------------------------------ */

/** Samples held by the rolling window. At 50 Hz this is a little over 10 s. */
#ifndef MOTION_WINDOW_SAMPLES
#define MOTION_WINDOW_SAMPLES 512U
#endif

/**
 * @brief Begin hardware-batched sampling.
 *
 * The sensor buffers samples itself and the HAL drains them in bursts, so the
 * MCU wakes once per batch instead of once per sample. Every drained sample
 * enters the rolling window, and the capture buffer as well if one is
 * running; MOTION_EVENT_BATCH is raised once the data has landed.
 *
 * Draining runs on a HAL-owned thread, not the system workqueue, because it
 * holds the I2C bus for the length of a burst.
 *
 * @param batch_ms How much data to accumulate before draining. Rounded down
 *        to whole samples and clamped to what the hardware buffer holds -
 *        motion_stream_max_batch_ms() reports that ceiling for the configured
 *        rate. Smaller means more wake-ups; larger risks losing samples if
 *        the drain is ever late.
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_CONFIGURED if no
 *         accelerometer rate is set, -ERR_MOTION_INVALID_CONFIG, or a
 *         negative errno.
 */
int motion_stream_start(uint32_t batch_ms);

/**
 * @brief Stop batched sampling and discard anything still buffered.
 *
 * The rolling window keeps whatever it already holds. A running capture is
 * stopped first; collect its result with motion_capture_stop() beforehand or
 * it is lost.
 *
 * @return 0, or a negative errno.
 */
int motion_stream_stop(void);

/**
 * @brief Largest batch the hardware buffer can hold at the configured rate.
 *
 * A property of the fitted sensor and the current rate, so it is reported
 * rather than fixed: the buffer is a byte budget, and how much time that
 * buys depends on how fast samples arrive.
 *
 * @param batch_ms Destination. Untouched on failure.
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_CONFIGURED, or a
 *         negative errno.
 */
int motion_stream_max_batch_ms(uint32_t *batch_ms);

/**
 * @brief Copy the most recent samples out of the rolling window.
 *
 * Oldest first, newest last. Returns fewer than requested when the window
 * has not filled yet. Safe to call while streaming.
 *
 * @param out Destination for up to @p max samples.
 * @param max Capacity of @p out.
 * @param count Number actually written. Untouched on failure.
 * @return 0, -ERR_MOTION_NOT_CONFIGURED if streaming never started, or a
 *         negative errno.
 */
int motion_window_read(struct motion_accel_sample *out, size_t max, size_t *count);

/* --- Capture -------------------------------------------------------------- */

/**
 * @brief What a capture collected.
 *
 * Samples are evenly spaced: sample @c i was taken at
 * @c first_timestamp_us + i * @c period_us. That holds because a batch is
 * contiguous by construction - the sensor reports anything it had to discard
 * separately, as @c dropped, and only ever before the samples that follow it.
 *
 * @c dropped or @c overflowed being non-zero means the record has a hole in
 * it. For anything the record is evidence for, that has to be reported
 * rather than smoothed over.
 */
struct motion_capture_result {
    size_t count;
    uint64_t first_timestamp_us;
    uint32_t period_us;
    /** Samples the sensor discarded because the HAL drained too late. */
    uint32_t dropped;
    /** True if the caller's buffer filled and later samples were discarded. */
    bool overflowed;
};

/**
 * @brief Record every sample into a caller-owned buffer until stopped.
 *
 * The buffer belongs to the HAL until motion_capture_stop() returns, and
 * must not be read or freed before then. Sized by the caller because only
 * the caller knows how long the window is - at 50 Hz, one minute is 3000
 * samples.
 *
 * Requires streaming to be running.
 *
 * @param storage Destination for samples.
 * @param capacity Number of samples @p storage holds.
 * @return 0, -ERR_MOTION_BUSY if a capture is already running,
 *         -ERR_MOTION_NOT_CONFIGURED if not streaming,
 *         -ERR_MOTION_INVALID_CONFIG, or a negative errno.
 */
int motion_capture_start(struct motion_accel_sample *storage, size_t capacity);

/**
 * @brief End the capture and hand back the buffer.
 *
 * The buffer is the caller's again once this returns. Safe to call when no
 * capture is running; @p result then reports a count of 0.
 *
 * @param result Destination. May be NULL to discard the capture.
 * @return 0, or a negative errno.
 */
int motion_capture_stop(struct motion_capture_result *result);

/* --- Diagnostics ---------------------------------------------------------- */

/**
 * @brief What the sensor is actually configured with, read back off the chip.
 *
 * Everything else here reports whether a call returned an error, which is a
 * different question from whether the hardware ended up in the intended
 * state. A threshold in particular is write-only from the caller's side: if
 * an event never arrives, only a readback separates "the configuration never
 * landed" from "it landed and the interrupt line carries nothing", and those
 * have entirely different fixes.
 *
 * The raw words are deliberately not decoded into millig and milliseconds.
 * The point is to see what the part holds, and a decode that shared a bug
 * with the encode would agree with it.
 */
struct motion_diagnostics {
    /** ANYMO_1 and ANYMO_2 as stored, or 0 if any-motion is unsupported. */
    uint16_t any_motion_raw[2];
    /** NOMO_1 and NOMO_2 as stored, or 0 if no-motion is unsupported. */
    uint16_t no_motion_raw[2];
    /** True once thresholds have been written; the enable bit is in the word. */
    bool slope_readback_valid;

    /** The sensor's own free-running counter. */
    uint32_t sensortime;
    bool sensortime_valid;

    /**
     * Bytes currently held in the sensor's hardware buffer. Climbing towards
     * capacity across successive reads means the drain is falling behind,
     * which shows up as lost samples before it shows up anywhere else.
     */
    uint16_t fifo_fill_bytes;
    bool fifo_fill_valid;

    /**
     * How far the sensor's timeline has drifted from the host's, in ppm,
     * positive meaning the sensor runs fast. Needs streaming to have been
     * running long enough to mean anything, hence the validity flag.
     *
     * Worth reading before trusting a long recording: the part's ODR
     * accuracy is specified in whole percent, so this is a real effect, not
     * a rounding artefact.
     */
    int32_t clock_drift_ppm;
    bool clock_drift_valid;
    /** Host time the drift figure was measured over. */
    uint64_t observed_span_us;
};

/**
 * @brief Read back the sensor's configured state and clock.
 *
 * @param diag Destination. Fully populated on success; fields whose feature
 *        is unsupported read 0 with their validity flag clear.
 * @return 0, -ERR_MOTION_UNSUPPORTED, -ERR_MOTION_NOT_INITIALIZED, or a
 *         negative errno.
 */
int motion_get_diagnostics(struct motion_diagnostics *diag);

#endif /* HAL_MOTION_H_ */
