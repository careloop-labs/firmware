// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * careloop motion streamer.
 *
 * Prints accelerometer and gyroscope samples from the BMI270 (U4), forever,
 * through hal/motion.h.
 *
 * This is the tool for watching the IMU respond. Tilt the board and watch
 * gravity move between axes; rotate it and watch the gyro follow; set it down
 * and watch both settle. The ztest suite cannot do any of that -
 * careloop_hal_2_motion checks that the acceleration magnitude is gravity and
 * that the gyro is near zero at rest, which proves the part is alive and the
 * conversions are sane but says nothing about whether the axes are oriented the
 * way the product assumes.
 *
 * Two things this app can settle that nothing else on the board can:
 *
 *   Axis orientation and sign. Lay the board flat and Z should read about
 *   +9807 mm/s^2 with X and Y near zero. Stand it on each edge in turn and the
 *   +1 g should move to the axis you expect, with the sign you expect. A board
 *   assembled mirrored, or a HAL that swapped two axes, passes every assertion
 *   in the suite and fails here in a way you can see.
 *
 *   Gyroscope scale. Every gyro check in the suite is taken at rest, so it is a
 *   bias check only - a factor-of-two error in the radians-to-millidegrees
 *   conversion passes it untouched. Rotate the board through a known angle at a
 *   roughly steady rate and the printed mdps should be in the right ballpark.
 *   That is the only scale check that exists.
 *
 * The print rate is not the sample rate, and deliberately so. RTT is the
 * bottleneck, not the sensor: a line per sample at 100 Hz overruns the up-buffer
 * in under a second, and NO_BLOCK_SKIP then drops output silently, which reads
 * as a sensor that stopped. The sensor runs at MOTION_ODR_MHZ and this prints
 * every PRINT_PERIOD_MS, so what you see is a sub-sample of a faster stream.
 *
 * Streams until the board is reset. Note this leaves the IMU running, which is
 * not free on a cell - reset the board when you are done looking.
 *
 * Run it with:
 *   ./scripts/bringup.sh motion
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <motion.h>

/* Not "motion": src/hal/motion_bmi270.c owns that name. */
LOG_MODULE_REGISTER(motion_app, LOG_LEVEL_INF);

/*
 * SEGGER RTT defaults to NO_BLOCK_SKIP: anything written before a viewer
 * attaches is discarded, not queued. bringup.sh resets the board as it flashes
 * and only then releases the probe and attaches the console, so the banner has
 * to outlast that gap.
 */
#define RTT_ATTACH_GRACE_MS 3000U

/*
 * 100 Hz on both axes. 100 Hz puts the accelerometer in performance mode, where
 * the driver accepts only oversampling 1, 2 or 4 - 1 here, since this app wants
 * to see the raw response rather than a smoothed one.
 */
#define MOTION_ODR_MHZ 100000U
#define ACCEL_RANGE_G 8U
#define ACCEL_OSR 1U
#define GYRO_RANGE_DPS 500U

/* What the eye and RTT can keep up with; the sensor runs ten times faster. */
#define PRINT_PERIOD_MS 100U

/* Let the MEMS settle after configuring before believing the first sample. */
#define SETTLE_MS 100U

/* How often to repeat the column header, in samples. */
#define HEADER_EVERY 20U

static const struct motion_config stream_config = {
    .accel_odr_mhz = MOTION_ODR_MHZ,
    .accel_range_g = ACCEL_RANGE_G,
    .accel_oversampling = ACCEL_OSR,
    .gyro_odr_mhz = MOTION_ODR_MHZ,
    .gyro_range_dps = GYRO_RANGE_DPS,
};

/*
 * Integer square root, so a vector length does not drag floating point into a
 * diagnostic image. Worst case is three axes at 16 g full scale, about 7.4e10,
 * so the starting bit has to be a power of four above that - 4^19 is 2.7e11.
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

/*
 * Which axis currently holds gravity, and which way up. The single most useful
 * line in the output when checking orientation: tilt the board and this should
 * name the axis you expect.
 */
static const char *dominant_axis(const struct motion_accel_sample *a)
{
    int32_t ax = (a->x_mms2 < 0) ? -a->x_mms2 : a->x_mms2;
    int32_t ay = (a->y_mms2 < 0) ? -a->y_mms2 : a->y_mms2;
    int32_t az = (a->z_mms2 < 0) ? -a->z_mms2 : a->z_mms2;

    if ((ax >= ay) && (ax >= az)) {
        return (a->x_mms2 < 0) ? "-X" : "+X";
    }

    if ((ay >= ax) && (ay >= az)) {
        return (a->y_mms2 < 0) ? "-Y" : "+Y";
    }

    return (a->z_mms2 < 0) ? "-Z" : "+Z";
}

static void print_capabilities(uint32_t caps)
{
    printk("capabilities 0x%02x: gyro %s, oversampling %s, data-ready %s, "
           "any-motion %s\n", caps,
           (caps & MOTION_CAP_GYRO) ? "yes" : "no",
           (caps & MOTION_CAP_OVERSAMPLING) ? "yes" : "no",
           (caps & MOTION_CAP_DATA_READY) ? "yes" : "no",
           (caps & MOTION_CAP_ANY_MOTION) ? "yes" : "no");
}

int main(void)
{
    uint32_t index = 0U;
    int rc;

    k_sleep(K_MSEC(RTT_ATTACH_GRACE_MS));

    printk("CARELOOP MOTION STREAM\n");

    rc = motion_init();
    if (rc != 0) {
        printk("RESULT: FAIL - motion_init() returned %d\n", rc);
        printk("  U4 did not answer at 0x68, the chip ID did not match, or the "
               "driver's config upload failed\n");
        printk("  run ./scripts/bringup.sh suite for the I2C bus scan\n");
        return 0;
    }

    print_capabilities(motion_get_capabilities());

    rc = motion_configure(&stream_config);
    if (rc != 0) {
        printk("RESULT: FAIL - motion_configure() returned %d\n", rc);
        printk("  rates snap down to the sensor's ladder, but ranges must match "
               "exactly: 2/4/8/16 g and 125/250/500/1000/2000 dps\n");
        return 0;
    }

    k_sleep(K_MSEC(SETTLE_MS));

    printk("accel %u mHz / %u g / osr %u, gyro %u mHz / %u dps\n",
           stream_config.accel_odr_mhz, stream_config.accel_range_g,
           stream_config.accel_oversampling, stream_config.gyro_odr_mhz,
           stream_config.gyro_range_dps);
    printk("printing every %u ms - the sensor runs faster, this is a "
           "sub-sample\n", PRINT_PERIOD_MS);
    printk("lay the board flat: expect |a| near %d mm/s^2 on +Z\n",
           MOTION_GRAVITY_MMS2);
    printk("STREAMING - reset the board to stop\n\n");

    for (;;) {
        struct motion_accel_sample accel;
        struct motion_gyro_sample gyro;
        int accel_rc = motion_read_accel(&accel);
        int gyro_rc = motion_read_gyro(&gyro);

        if ((accel_rc != 0) || (gyro_rc != 0)) {
            LOG_ERR("read failed: accel %d, gyro %d", accel_rc, gyro_rc);
            k_sleep(K_MSEC(PRINT_PERIOD_MS));
            continue;
        }

        if ((index % HEADER_EVERY) == 0U) {
            printk("%6s%24s%8s%5s%27s\n", "n",
                   "accel x/y/z mm/s^2", "|a|", "up", "gyro x/y/z mdps");
        }

        printk("%6u %7d %7d %7d %7d %4s %8d %8d %8d\n", index, accel.x_mms2,
               accel.y_mms2, accel.z_mms2, accel_magnitude_mms2(&accel),
               dominant_axis(&accel), gyro.x_mdps, gyro.y_mdps, gyro.z_mdps);

        index++;
        k_sleep(K_MSEC(PRINT_PERIOD_MS));
    }

    return 0;
}
