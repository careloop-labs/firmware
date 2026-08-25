// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/motion.h against the BMI270 (U4).
 *
 * Layers on careloop_5_imu, which proves U4 answers and reports gravity through
 * the Zephyr driver. This suite proves the shipping wrapper configures, paces
 * and converts correctly. The gravity check is duplicated on purpose: the two
 * reach it by different paths, and a failure here with careloop_5_imu green
 * means src/hal/motion_bmi270.c.
 *
 * Read this before interpreting a NOT_CONFIGURED result. careloop_5_imu ran
 * first and set ODR 100 Hz / 8 g on the chip directly, behind the HAL's back,
 * and left the accelerometer powered. So NOT_CONFIGURED here reflects the HAL's
 * own bookkeeping, not an idle sensor - the chip may well be sampling.
 *
 * Two cases are one-shot for the life of the image and their order is fixed by
 * the digit in the case name, which is what ztest sorts on:
 *
 *   _30_ must precede _40_  - once any configure succeeds, "configured" latches
 *   _b0_ must precede _c0_  - once any-motion is configured, that latches too
 *
 * Neither HAL exposes a way back, so a rename that reorders them silently
 * deletes the coverage. The digits are load-bearing, not decorative.
 *
 * What this does not prove:
 *   - the gyroscope scale factor. Every gyro assertion here is taken at rest,
 *     so it is a bias check. A factor-of-two error in the rad-to-mdps
 *     conversion, or a wrong SENSOR_PI, passes untouched. That needs a
 *     controlled rotation rate, which this bench does not have.
 *   - the effective ODR after snapping. motion_configure() snaps a request down
 *     to the sensor's ladder but the HAL exposes no getter for the result, so
 *     test 50 checks only that the request was accepted.
 *   - that any-motion fires. See test f0.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <motion.h>

/* Bench limits. Gravity is 9807 mm/s^2; allow for tilt, noise and offset. */
#define ACCEL_MAGNITUDE_TOLERANCE_MMS2 2500

/* A still board should not exceed this on any gyro axis. 20 dps is generous. */
#define GYRO_REST_MAX_MDPS 20000

#define ACCEL_ODR_MHZ 100000U
#define ACCEL_RANGE_G 8U
#define GYRO_ODR_MHZ 100000U
#define GYRO_RANGE_DPS 250U

/* 100 Hz puts the accel in performance mode, where only OSR 1/2/4 are legal. */
#define ACCEL_OSR 1U

#define MOTION_SETTLE_MS 50U
#define GYRO_SETTLE_MS 100U
#define DATA_READY_WINDOW_MS 200U

/*
 * Any-motion limits, from the part rather than from the vendor driver.
 *
 * The duration ceiling used to be 81900 ms here, which was the driver's
 * twelve-bit mask rather than the hardware: ANYMO_1.duration is thirteen bits
 * at 20 ms per step, so 8191 * 20 ms = 163820 ms, and the datasheet quotes
 * the range as 0 to 163 seconds. The HAL writes the register directly now and
 * so reaches all of it.
 */
#define ANY_MOTION_THRESHOLD_MIN_MG 1U
#define ANY_MOTION_THRESHOLD_MAX_MG 1000U
#define ANY_MOTION_DURATION_MAX_MS 163820U
#define ANY_MOTION_DURATION_STEP_MS 20U

static const struct motion_config running_config = {
    .accel_odr_mhz = ACCEL_ODR_MHZ,
    .accel_range_g = ACCEL_RANGE_G,
    .accel_oversampling = ACCEL_OSR,
    .gyro_odr_mhz = GYRO_ODR_MHZ,
    .gyro_range_dps = GYRO_RANGE_DPS,
};

static atomic_t data_ready_events;

/*
 * Runs on the system workqueue, shared with every other sensor on the board.
 * Counting is all it may do - no I2C, no printk, no blocking.
 */
static void motion_callback(enum motion_event event)
{
    if (event == MOTION_EVENT_DATA_READY) {
        atomic_inc(&data_ready_events);
    }
}

/*
 * Integer square root. Deliberately not sqrt() from libm: this file is built
 * with -Wdouble-promotion, and a vector length in mm/s^2 has no business
 * dragging floating point into a test image.
 *
 * Worst case here is three axes at 16 g full scale, about 7.4e10, so the
 * starting bit has to be a power of four above that - 4^19 is 2.7e11.
 */
static int32_t isqrt64(int64_t value)
{
    int64_t bit = 1LL << 38;
    int64_t root = 0;

    while (bit > value) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }

    return (int32_t)root;
}

static int32_t accel_magnitude_mms2(const struct motion_accel_sample *sample)
{
    int64_t sum = ((int64_t)sample->x_mms2 * sample->x_mms2) +
                  ((int64_t)sample->y_mms2 * sample->y_mms2) +
                  ((int64_t)sample->z_mms2 * sample->z_mms2);

    return isqrt64(sum);
}

ZTEST_SUITE(careloop_hal_2_motion, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_hal_2_motion, test_hal_motion_10_init)
{
    struct motion_fault fault;

    zassert_ok(motion_init(),
               "motion_init() failed - U4 did not answer at 0x68, the chip ID "
               "did not match, or the driver's config upload failed");

    /*
     * The earliest point the sensor's own health can be read: nothing has
     * configured or suspended anything yet. Printed rather than asserted
     * because the assertion belongs to careloop_hal_3_motion_fifo - what this
     * line is for is saying whether a fault seen later was already here,
     * which is the difference between a part that came up unhealthy and one
     * this firmware upset.
     */
    if (motion_get_fault(&fault) == 0) {
        printk("  fault at init   ERR_REG 0x%02x  INTERNAL_ERROR 0x%02x  feat_eng_disabled %d\n",
               fault.raw, fault.internal_raw, fault.feature_engine_disabled);
    }
}

ZTEST(careloop_hal_2_motion, test_hal_motion_20_capabilities)
{
    uint32_t caps = motion_get_capabilities();

    printk("  capabilities 0x%02x: gyro %s, oversampling %s, data-ready %s, "
           "any-motion %s\n", caps,
           (caps & MOTION_CAP_GYRO) ? "yes" : "no",
           (caps & MOTION_CAP_OVERSAMPLING) ? "yes" : "no",
           (caps & MOTION_CAP_DATA_READY) ? "yes" : "no",
           (caps & MOTION_CAP_ANY_MOTION) ? "yes" : "no");

    zassert_true(caps & MOTION_CAP_GYRO, "the BMI270 has a gyroscope");
    zassert_true(caps & MOTION_CAP_OVERSAMPLING,
                 "the BMI270 supports oversampling");

    /*
     * The config-parity tripwire. These two are compiled out unless
     * CONFIG_BMI270_TRIGGER is on, so without them this suite would be
     * verifying a HAL built differently from the one the product ships.
     */
    zassert_true(caps & MOTION_CAP_DATA_READY,
                 "no data-ready capability - CONFIG_BMI270_TRIGGER_GLOBAL_THREAD "
                 "is missing from prj.conf, or careloop.dts lost irq-gpios "
                 "index 1 (INT2)");
    zassert_true(caps & MOTION_CAP_ANY_MOTION,
                 "no any-motion capability - needs "
                 "CONFIG_BMI270_TRIGGER_GLOBAL_THREAD, irq-gpios index 0 "
                 "(INT1), and the \"bosch,bmi270-base\" compatible in "
                 "careloop.dts");
}

ZTEST(careloop_hal_2_motion, test_hal_motion_30_read_before_configure)
{
    struct motion_accel_sample accel;
    struct motion_gyro_sample gyro;

    /*
     * One-shot: must run before test 40, which is the first case that gets a
     * configure accepted - after that "configured" latches for the life of the
     * image. U4 is very likely sampling right now, left running by
     * careloop_5_imu; what this pins is that the HAL refuses to report a sample
     * it did not ask for.
     */
    zassert_equal(motion_read_accel(&accel), -ERR_MOTION_NOT_CONFIGURED,
                  "reading before configuring must fail");
    zassert_equal(motion_read_gyro(&gyro), -ERR_MOTION_NOT_CONFIGURED,
                  "reading before configuring must fail");
    zassert_equal(motion_set_enabled(true), -ERR_MOTION_NOT_CONFIGURED,
                  "enabling before configuring must fail - there is no rate to "
                  "enable at");
}

ZTEST(careloop_hal_2_motion, test_hal_motion_40_invalid_config_rejected)
{
    /* static const: nine of these would be a lot of ztest thread stack. */
    static const struct motion_config rejected[] = {
        /* Ranges must match the ladder exactly; rounding changes resolution. */
        { .accel_odr_mhz = ACCEL_ODR_MHZ, .accel_range_g = 3U },
        { .accel_odr_mhz = ACCEL_ODR_MHZ, .accel_range_g = 0U },
        /* Below the slowest accel rung (782 mHz) - must fail, not power down. */
        { .accel_odr_mhz = 100U, .accel_range_g = ACCEL_RANGE_G },
        { .gyro_odr_mhz = GYRO_ODR_MHZ, .gyro_range_dps = 300U },
        /* Below the slowest gyro rung (25000 mHz). */
        { .gyro_odr_mhz = 1000U, .gyro_range_dps = GYRO_RANGE_DPS },
        /* OSR 8 is illegal at 200 Hz: performance mode allows only 1/2/4. */
        { .accel_odr_mhz = 200000U, .accel_range_g = ACCEL_RANGE_G,
          .accel_oversampling = 8U },
        { .gyro_odr_mhz = GYRO_ODR_MHZ, .gyro_range_dps = GYRO_RANGE_DPS,
          .gyro_oversampling = 8U },
    };
    struct motion_accel_sample accel;

    for (size_t i = 0U; i < ARRAY_SIZE(rejected); i++) {
        zassert_equal(motion_configure(&rejected[i]),
                      -ERR_MOTION_INVALID_CONFIG,
                      "config %u was accepted: accel %u mHz / %u g / osr %u, "
                      "gyro %u mHz / %u dps / osr %u", i,
                      rejected[i].accel_odr_mhz, rejected[i].accel_range_g,
                      rejected[i].accel_oversampling, rejected[i].gyro_odr_mhz,
                      rejected[i].gyro_range_dps, rejected[i].gyro_oversampling);
    }

    zassert_equal(motion_configure(NULL), -ERR_MOTION_INVALID_CONFIG,
                  "a NULL config was accepted");

    /*
     * The same OSR of 8 that was rejected at 200 Hz is legal below 100 Hz,
     * where the accelerometer leaves performance mode and the driver switches
     * lookup tables. This pair is what pins that the validation is rate-aware
     * rather than a fixed list.
     */
    {
        const struct motion_config slow_osr8 = {
            .accel_odr_mhz = 50000U,
            .accel_range_g = ACCEL_RANGE_G,
            .accel_oversampling = 8U,
        };

        zassert_ok(motion_configure(&slow_osr8),
                   "oversampling 8 must be accepted at 50 Hz even though it is "
                   "rejected at 200 Hz");
    }

    /*
     * Every rejection above must have left no half-written state. The HAL
     * validates the whole config before it writes anything, so a rejected
     * request must not have disturbed the accepted one.
     */
    zassert_ok(motion_read_accel(&accel),
               "the accepted 50 Hz config did not take effect, so a rejected "
               "config left the HAL in a half-configured state");
}

ZTEST(careloop_hal_2_motion, test_hal_motion_50_configure_snaps_odr)
{
    struct motion_config between = running_config;

    /* 150 Hz sits between the 100 Hz and 200 Hz rungs; it snaps down to 100. */
    between.accel_odr_mhz = 150000U;
    zassert_ok(motion_configure(&between),
               "a rate between two ladder rungs must snap down, not fail");

    /* 60 Hz sits between 50 and 100. */
    between.accel_odr_mhz = 60000U;
    zassert_ok(motion_configure(&between),
               "60 Hz must snap down to the 50 Hz rung");

    /*
     * Only the return code is checked. The HAL exposes no getter for the
     * effective rate, so the snapped value is observable solely by measuring
     * the sample rate - which this suite does not do.
     */
}

ZTEST(careloop_hal_2_motion, test_hal_motion_60_accel_is_gravity)
{
    struct motion_accel_sample accel;
    int32_t magnitude;

    zassert_ok(motion_configure(&running_config), "configure failed");
    k_sleep(K_MSEC(MOTION_SETTLE_MS));

    zassert_ok(motion_read_accel(&accel), "motion_read_accel() failed");

    magnitude = accel_magnitude_mms2(&accel);

    printk("  accel %d, %d, %d mm/s^2, magnitude %d\n", accel.x_mms2,
           accel.y_mms2, accel.z_mms2, magnitude);

    /*
     * Magnitude rather than per-axis, and deliberately so: a dead or stuck axis
     * still reads plausibly on its own, and only the vector length catches it.
     * A magnitude near zero means the accelerometer is powered down.
     */
    zassert_between_inclusive(magnitude,
                              MOTION_GRAVITY_MMS2 - ACCEL_MAGNITUDE_TOLERANCE_MMS2,
                              MOTION_GRAVITY_MMS2 + ACCEL_MAGNITUDE_TOLERANCE_MMS2,
                              "acceleration magnitude %d mm/s^2 is not gravity "
                              "(%d) - is U4 powered, and is the board still?",
                              magnitude, MOTION_GRAVITY_MMS2);
}

ZTEST(careloop_hal_2_motion, test_hal_motion_70_gyro_near_zero_at_rest)
{
    struct motion_gyro_sample gyro;

    zassert_ok(motion_configure(&running_config), "configure failed");
    k_sleep(K_MSEC(GYRO_SETTLE_MS));

    zassert_ok(motion_read_gyro(&gyro), "motion_read_gyro() failed");

    printk("  gyro %d, %d, %d mdps\n", gyro.x_mdps, gyro.y_mdps, gyro.z_mdps);

    /*
     * A bias check, not a scale check. A wrong radians-to-millidegrees factor
     * passes this untouched, because zero times anything is zero.
     */
    zassert_between_inclusive(gyro.x_mdps, -GYRO_REST_MAX_MDPS,
                              GYRO_REST_MAX_MDPS,
                              "gyro X %d mdps at rest", gyro.x_mdps);
    zassert_between_inclusive(gyro.y_mdps, -GYRO_REST_MAX_MDPS,
                              GYRO_REST_MAX_MDPS,
                              "gyro Y %d mdps at rest", gyro.y_mdps);
    zassert_between_inclusive(gyro.z_mdps, -GYRO_REST_MAX_MDPS,
                              GYRO_REST_MAX_MDPS,
                              "gyro Z %d mdps at rest", gyro.z_mdps);
}

ZTEST(careloop_hal_2_motion, test_hal_motion_80_suspend_and_resume)
{
    struct motion_accel_sample accel;
    int32_t magnitude;

    zassert_ok(motion_configure(&running_config), "configure failed");
    k_sleep(K_MSEC(MOTION_SETTLE_MS));

    zassert_ok(motion_set_enabled(false), "suspend failed");

    zassert_equal(motion_read_accel(&accel), -ERR_MOTION_NOT_CONFIGURED,
                  "a read while suspended must fail rather than return the last "
                  "live sample");

    /* Idempotent: the HAL returns early when the state already matches. */
    zassert_ok(motion_set_enabled(false), "a redundant suspend must succeed");

    zassert_ok(motion_set_enabled(true), "resume failed");
    k_sleep(K_MSEC(MOTION_SETTLE_MS));

    zassert_ok(motion_read_accel(&accel), "read after resume failed");
    magnitude = accel_magnitude_mms2(&accel);

    printk("  magnitude after resume %d mm/s^2\n", magnitude);

    /*
     * Suspend works by writing a rate of 0, which clears PWR_CTRL. If resume
     * only flipped a flag and never restored the rate, the sensor stays powered
     * down and this reads near zero - which is exactly what that failure looks
     * like.
     */
    zassert_between_inclusive(magnitude,
                              MOTION_GRAVITY_MMS2 - ACCEL_MAGNITUDE_TOLERANCE_MMS2,
                              MOTION_GRAVITY_MMS2 + ACCEL_MAGNITUDE_TOLERANCE_MMS2,
                              "magnitude %d mm/s^2 after resume - a value near "
                              "zero means resume did not re-power the MEMS",
                              magnitude);
}

ZTEST(careloop_hal_2_motion, test_hal_motion_90_gyro_odr_zero)
{
    struct motion_config accel_only = running_config;
    struct motion_accel_sample accel;
    struct motion_gyro_sample gyro;

    accel_only.gyro_odr_mhz = 0U;

    zassert_ok(motion_configure(&accel_only),
               "a zero gyro rate must be accepted - it means leave the gyro "
               "powered down");
    k_sleep(K_MSEC(MOTION_SETTLE_MS));

    /* NOT_CONFIGURED is per axis, not for the whole device. */
    zassert_equal(motion_read_gyro(&gyro), -ERR_MOTION_NOT_CONFIGURED,
                  "the gyro is powered down, so reading it must fail");
    zassert_ok(motion_read_accel(&accel),
               "the accelerometer is still configured and must still read");

    /* Put the gyro back for the cases that follow. */
    zassert_ok(motion_configure(&running_config), "reconfigure failed");
}

ZTEST(careloop_hal_2_motion, test_hal_motion_a0_any_motion_limits)
{
    struct motion_any_motion_limits limits;

    zassert_ok(motion_get_any_motion_limits(&limits),
               "any-motion limits must be readable once initialised");

    printk("  any-motion threshold %u..%u mg, duration max %u ms step %u ms\n",
           limits.threshold_min_mg, limits.threshold_max_mg,
           limits.duration_max_ms, limits.duration_step_ms);

    /*
     * These are re-derived driver internals, pinned against NCS v3.4.0. If they
     * change after an SDK bump the answer is to re-derive them from the driver,
     * not to relax the test - a caller that clamps to the wrong limits arms the
     * sensor at a threshold it did not intend.
     */
    zassert_equal(limits.threshold_min_mg, ANY_MOTION_THRESHOLD_MIN_MG,
                  "threshold floor changed");
    zassert_equal(limits.threshold_max_mg, ANY_MOTION_THRESHOLD_MAX_MG,
                  "threshold ceiling changed");
    zassert_equal(limits.duration_max_ms, ANY_MOTION_DURATION_MAX_MS,
                  "duration ceiling changed - the register field is 12 bits at "
                  "20 ms per LSB, and a larger value overflows into the "
                  "axis-select bits");
    zassert_equal(limits.duration_step_ms, ANY_MOTION_DURATION_STEP_MS,
                  "duration granularity changed");

    /* No INVALID_ARG code exists for this entry point; NULL is UNSUPPORTED. */
    zassert_equal(motion_get_any_motion_limits(NULL), -ERR_MOTION_UNSUPPORTED,
                  "NULL is reported as UNSUPPORTED by contract");
}

ZTEST(careloop_hal_2_motion, test_hal_motion_b0_enable_before_configure)
{
    /*
     * One-shot: must run before test c0, which latches any_motion_configured
     * for the rest of the image. Arming against thresholds that were never
     * written either floods the callback or never fires, and neither is
     * distinguishable from working hardware - so the HAL refuses.
     */
    zassert_equal(motion_set_event_enabled(MOTION_EVENT_ANY_MOTION, true),
                  -ERR_MOTION_NOT_CONFIGURED,
                  "arming any-motion before configuring its thresholds must "
                  "fail - the thresholds only reach the chip through the "
                  "trigger_set() the driver performs on arming");
}

ZTEST(careloop_hal_2_motion, test_hal_motion_c0_any_motion_bounds)
{
    static const struct motion_any_motion_config rejected[] = {
        { .threshold_mg = 0U, .duration_ms = 100U },
        { .threshold_mg = ANY_MOTION_THRESHOLD_MAX_MG + 1U, .duration_ms = 100U },
        { .threshold_mg = 100U, .duration_ms = ANY_MOTION_DURATION_MAX_MS + 1U },
    };
    /* Boundaries the HAL must accept; the max takes the full-scale branch. */
    static const struct motion_any_motion_config accepted[] = {
        { .threshold_mg = ANY_MOTION_THRESHOLD_MIN_MG, .duration_ms = 100U },
        { .threshold_mg = ANY_MOTION_THRESHOLD_MAX_MG, .duration_ms = 100U },
        /* 30 ms is not a multiple of the 20 ms step; it rounds down. */
        { .threshold_mg = 100U, .duration_ms = 30U },
    };

    /* Out-of-range requests are rejected, never silently clamped. */
    for (size_t i = 0U; i < ARRAY_SIZE(rejected); i++) {
        zassert_equal(motion_configure_any_motion(&rejected[i]),
                      -ERR_MOTION_INVALID_CONFIG,
                      "any-motion config %u was accepted: %u mg, %u ms", i,
                      rejected[i].threshold_mg, rejected[i].duration_ms);
    }

    zassert_equal(motion_configure_any_motion(NULL),
                  -ERR_MOTION_INVALID_CONFIG, "a NULL config was accepted");

    for (size_t i = 0U; i < ARRAY_SIZE(accepted); i++) {
        zassert_ok(motion_configure_any_motion(&accepted[i]),
                   "any-motion config %u was rejected: %u mg, %u ms", i,
                   accepted[i].threshold_mg, accepted[i].duration_ms);
    }
}

ZTEST(careloop_hal_2_motion, test_hal_motion_d0_callback_and_enable)
{
    zassert_ok(motion_set_event_callback(motion_callback),
               "setting the event callback failed");

    zassert_ok(motion_set_event_enabled(MOTION_EVENT_DATA_READY, true),
               "arming data-ready failed - INT2 is P0.09");
    zassert_ok(motion_set_event_enabled(MOTION_EVENT_ANY_MOTION, true),
               "arming any-motion failed - INT1 is P1.15");

    zassert_ok(motion_set_event_enabled(MOTION_EVENT_ANY_MOTION, false),
               "disarming any-motion failed");
    zassert_ok(motion_set_event_enabled(MOTION_EVENT_DATA_READY, false),
               "disarming data-ready failed");
}

ZTEST(careloop_hal_2_motion, test_hal_motion_e0_data_ready_fires)
{
    atomic_val_t seen;

    zassert_ok(motion_configure(&running_config), "configure failed");
    zassert_ok(motion_set_event_callback(motion_callback), "callback failed");

    atomic_clear(&data_ready_events);

    zassert_ok(motion_set_event_enabled(MOTION_EVENT_DATA_READY, true),
               "arming data-ready failed");

    k_sleep(K_MSEC(DATA_READY_WINDOW_MS));

    seen = atomic_get(&data_ready_events);
    zassert_ok(motion_set_event_enabled(MOTION_EVENT_DATA_READY, false),
               "disarming data-ready failed");

    printk("  %d data-ready events in %u ms at 100 Hz\n", (int)seen,
           DATA_READY_WINDOW_MS);

    /*
     * The only firmware-observable proof that the INT2 line physically works -
     * everything else in this suite would pass with both interrupt pins
     * unconnected.
     */
    zassert_true(seen > 0,
                 "no data-ready interrupt in %u ms with the accelerometer at "
                 "100 Hz. INT2 is P0.09, an NFC pin: it is GPIO only if "
                 "nfct-pins-as-gpios in careloop.dts actually reached UICR, "
                 "which needs a full erase and a power cycle. Check "
                 "0x1000120C reads 0xFFFFFFFE. Both interrupt pins are marked "
                 "unverified in the dts", DATA_READY_WINDOW_MS);
}

ZTEST(careloop_hal_2_motion, test_hal_motion_f0_any_motion_is_manual)
{
    const struct motion_any_motion_config sensitive = {
        .threshold_mg = ANY_MOTION_THRESHOLD_MIN_MG,
        .duration_ms = ANY_MOTION_DURATION_STEP_MS,
    };

    zassert_ok(motion_configure_any_motion(&sensitive),
               "arming any-motion at its most sensitive setting failed");

    printk("  any-motion armed at %u mg / %u ms\n", sensitive.threshold_mg,
           sensitive.duration_ms);
    printk("  firing it needs the board picked up - not assertable here\n");

    /*
     * Waiting for ambient bench vibration to cross a 1 mg threshold is a coin
     * flip, and a flaky hardware test is worse than an honest gap. Skipped so
     * the gap lands in the Twister report rather than only in this comment.
     */
    ztest_test_skip();
}
