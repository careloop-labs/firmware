// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * careloop motion streamer.
 *
 * Prints accelerometer and gyroscope samples from the BMI270 (U4), forever,
 * through hal/motion.h, and alongside them the state of everything that only
 * a person handling the board can judge.
 *
 * This is the tool for watching the IMU respond. Tilt the board and watch
 * gravity move between axes; rotate it and watch the gyro follow; pick it up
 * and watch any-motion fire; set it down and watch no-motion fire and the
 * classifier fall back to "still"; walk with it and watch it say "walking".
 *
 * Four things this app can settle that nothing else on the board can. The
 * first two are about the sensor being wired up right, the last two about the
 * feature engine and the FIFO doing what they claim:
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
 *   That any-motion and no-motion actually fire. careloop_hal_3_motion_fifo
 *   arms both and then skips, because making them fire needs the board picked
 *   up and set down again and waiting on bench vibration to do that is a coin
 *   flip. Here a person does it. The event counters are the whole point: if
 *   picking the board up does not move "any", INT1 (P1.15) carries nothing -
 *   which no other test on this board can tell you, since every other check
 *   would pass with that pin unconnected.
 *
 *   That the classifier works. It reports "still" on a bench whether or not it
 *   is running, so the only evidence it is alive is walking across the room
 *   with the board and watching it change.
 *
 * The stats block also reports what the FIFO path is doing: samples delivered
 * per period, samples the sensor had to discard, and how far its clock has
 * drifted from the host's. Those numbers are the ones to read before trusting
 * a PPG window recorded against this timeline.
 *
 * Note the sensor is read two independent ways at once here, deliberately.
 * The per-sample lines are live register reads through motion_read_accel();
 * the stats come from the FIFO, drained in batches by the HAL's own thread.
 * They exercise different code, and disagreement between them is meaningful.
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
#include <zephyr/sys/atomic.h>

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
 *
 * Performance mode also means the feature engine runs at full performance
 * regardless of rate, so the 50 Hz floor that governs the product's low-power
 * mode does not bite here. The product runs these features at 50 Hz in
 * power-optimised mode instead; this app is not trying to reproduce its power
 * profile, only to show the features working.
 */
#define MOTION_ODR_MHZ 100000U
#define ACCEL_RANGE_G 8U
#define ACCEL_OSR 1U
#define GYRO_RANGE_DPS 500U

/* What the eye and RTT can keep up with; the sensor runs ten times faster. */
#define PRINT_PERIOD_MS 100U

/* Let the MEMS settle after configuring before believing the first sample. */
#define SETTLE_MS 100U

/* How often to repeat the column header and the stats block, in samples. */
#define HEADER_EVERY 20U

/* Half a second of hardware batching: frequent enough to watch, long enough
 * that a late drain would show up as a dropped count rather than never.
 */
#define FIFO_BATCH_MS 500U

/*
 * One stats period is HEADER_EVERY print periods, so 2 s at the values above.
 * The capture buffer has to hold that at the full sample rate with room to
 * spare, or it reports an overflow that is this app's fault rather than the
 * sensor's.
 */
#define CAPTURE_SAMPLES 512U

/*
 * Sensitive enough that lifting the board off the bench crosses it, without
 * being so twitchy that footsteps in the room do.
 */
#define ANY_MOTION_THRESHOLD_MG 100U
#define ANY_MOTION_DURATION_MS 100U

/* Two seconds of stillness before the board is called at rest. */
#define NO_MOTION_THRESHOLD_MG 60U
#define NO_MOTION_DURATION_MS 2000U

static const struct motion_config stream_config = {
    .accel_odr_mhz = MOTION_ODR_MHZ,
    .accel_range_g = ACCEL_RANGE_G,
    .accel_oversampling = ACCEL_OSR,
    .gyro_odr_mhz = MOTION_ODR_MHZ,
    .gyro_range_dps = GYRO_RANGE_DPS,
};

static struct motion_accel_sample capture_buffer[CAPTURE_SAMPLES];

/*
 * Written from the HAL's drain thread, read from main. Atomics rather than a
 * lock because the callback's contract is that it does not block - it runs on
 * the thread that is holding the I2C bus for the length of a burst.
 */
static atomic_t any_motion_events;
static atomic_t no_motion_events;
static atomic_t batch_events;

/* Never cleared - see the note in print_stream_stats() on why the running
 * total is the number that answers "is this pin connected".
 */
static atomic_t any_motion_total;
static atomic_t no_motion_total;

static void event_handler(enum motion_event event)
{
    switch (event) {
    case MOTION_EVENT_ANY_MOTION:
        atomic_inc(&any_motion_events);
        atomic_inc(&any_motion_total);
        break;
    case MOTION_EVENT_NO_MOTION:
        atomic_inc(&no_motion_events);
        atomic_inc(&no_motion_total);
        break;
    case MOTION_EVENT_BATCH:
        atomic_inc(&batch_events);
        break;
    default:
        break;
    }
}

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

static const char *activity_name(enum motion_activity activity)
{
    switch (activity) {
    case MOTION_ACTIVITY_STILL:
        return "still";
    case MOTION_ACTIVITY_WALKING:
        return "walking";
    case MOTION_ACTIVITY_RUNNING:
        return "running";
    default:
        return "unknown";
    }
}

static void print_capabilities(uint32_t caps)
{
    printk("capabilities 0x%02x: gyro %s, oversampling %s, data-ready %s, "
           "any-motion %s, no-motion %s, fifo %s, activity %s, fault %s\n", caps,
           (caps & MOTION_CAP_GYRO) ? "yes" : "no",
           (caps & MOTION_CAP_OVERSAMPLING) ? "yes" : "no",
           (caps & MOTION_CAP_DATA_READY) ? "yes" : "no",
           (caps & MOTION_CAP_ANY_MOTION) ? "yes" : "no",
           (caps & MOTION_CAP_NO_MOTION) ? "yes" : "no",
           (caps & MOTION_CAP_FIFO) ? "yes" : "no",
           (caps & MOTION_CAP_ACTIVITY) ? "yes" : "no",
           (caps & MOTION_CAP_FAULT) ? "yes" : "no");
}

static void print_armed_state(void);

/*
 * Arms both slope events and the classifier, then starts hardware batching.
 * Every step reports rather than aborting: a board where the FIFO will not
 * start is still worth streaming live samples from, and which step failed is
 * the diagnostic.
 */
static void start_features(void)
{
    const struct motion_any_motion_config any_motion = {
        .threshold_mg = ANY_MOTION_THRESHOLD_MG,
        .duration_ms = ANY_MOTION_DURATION_MS,
    };
    const struct motion_no_motion_config no_motion = {
        .threshold_mg = NO_MOTION_THRESHOLD_MG,
        .duration_ms = NO_MOTION_DURATION_MS,
    };
    uint32_t ceiling = 0U;
    int rc;

    rc = motion_set_event_callback(event_handler);
    if (rc != 0) {
        printk("  WARN - motion_set_event_callback() returned %d, no events will "
               "be counted\n", rc);
        return;
    }

    rc = motion_configure_any_motion(&any_motion);
    if (rc == 0) {
        rc = motion_set_event_enabled(MOTION_EVENT_ANY_MOTION, true);
    }
    if (rc != 0) {
        printk("  WARN - any-motion setup returned %d\n", rc);
    } else {
        printk("any-motion  %u mg / %u ms, on INT1\n", ANY_MOTION_THRESHOLD_MG,
               ANY_MOTION_DURATION_MS);
    }

    rc = motion_configure_no_motion(&no_motion);
    if (rc == 0) {
        rc = motion_set_event_enabled(MOTION_EVENT_NO_MOTION, true);
    }
    if (rc != 0) {
        printk("  WARN - no-motion setup returned %d\n", rc);
    } else {
        printk("no-motion   %u mg / %u ms, on INT1\n", NO_MOTION_THRESHOLD_MG,
               NO_MOTION_DURATION_MS);
    }

    rc = motion_set_activity_enabled(true);
    if (rc != 0) {
        printk("  WARN - motion_set_activity_enabled() returned %d\n", rc);
    } else {
        printk("classifier  on\n");
    }

    (void)motion_stream_max_batch_ms(&ceiling);

    rc = motion_stream_start(FIFO_BATCH_MS);
    if (rc != 0) {
        printk("  WARN - motion_stream_start() returned %d, no FIFO stats\n", rc);
        return;
    }

    printk("fifo        %u ms batches (ceiling %u ms at this rate), draining to "
           "the rolling window\n", FIFO_BATCH_MS, ceiling);

    rc = motion_capture_start(capture_buffer, ARRAY_SIZE(capture_buffer));
    if (rc != 0) {
        printk("  WARN - motion_capture_start() returned %d\n", rc);
    }

    print_armed_state();
}

/*
 * Reads the feature words and the interrupt map back off the chip.
 *
 * Everything above this reports whether a write returned an error, which is
 * not the same question. A threshold is write-only from the application's
 * side, so if events never arrive this is what separates "the configuration
 * never landed" from "it landed and the pin carries nothing" - and those have
 * completely different fixes.
 */
static void print_armed_state(void)
{
    struct motion_diagnostics diag;
    struct motion_fault fault;

    if (motion_get_diagnostics(&diag) == 0) {
        printk("readback    anymo 0x%04x 0x%04x   nomo 0x%04x 0x%04x   sensortime %u\n",
               diag.any_motion_raw[0], diag.any_motion_raw[1], diag.no_motion_raw[0],
               diag.no_motion_raw[1], diag.sensortime);
        printk("readback    %s\n",
               diag.slope_readback_valid ? "slope words read back from the chip"
                                         : "slope readback FAILED");
    }

    if (motion_get_fault(&fault) == 0) {
        printk("readback    ERR_REG 0x%02x  INTERNAL_ERROR 0x%02x%s\n", fault.raw,
               fault.internal_raw,
               fault.feature_engine_disabled ? "  [FEATURE ENGINE DISABLED]" : "");
    }
}

/*
 * Closes the current capture, reports it, and opens the next. Everything here
 * is per-period rather than cumulative, so a fault that starts halfway through
 * a session is visible instead of being averaged away.
 *
 * The sample count arrives in whole batches, so it steps rather than settling:
 * at a 500 ms batch inside a 2 s period it lands on 3 or 4 batches depending
 * on where the boundary falls, which reads as 150 or 200 at 100 Hz. That
 * swing is the quantisation, not loss - "dropped" is the number that counts
 * loss, and it is the sensor's own tally rather than anything inferred here.
 */
static void print_stream_stats(uint32_t period_ms)
{
    struct motion_capture_result result = {0};
    struct motion_fault fault;
    enum motion_activity activity = MOTION_ACTIVITY_UNKNOWN;
    struct motion_diagnostics diag;
    uint32_t expected;
    int rc;

    rc = motion_capture_stop(&result);
    if (rc != 0) {
        printk("  -- stream: capture stop returned %d\n", rc);
        return;
    }

    (void)motion_get_activity(&activity);

    expected = (period_ms * (MOTION_ODR_MHZ / 1000U)) / 1000U;

    /*
     * Event totals are cumulative as well as per-period, and the totals are
     * the ones to read. These interrupts are not latched, so a condition that
     * becomes true and stays true - a board left still, say - produces one
     * edge and then nothing. A per-period count of zero is what a working
     * no-motion looks like for as long as the board is not disturbed, and
     * only the total distinguishes that from a pin carrying nothing at all.
     */
    printk("  -- fifo: %u batches, %u samples (expect ~%u), dropped %u%s | "
           "activity %s | events: any %u (%u total), no %u (%u total)\n",
           (unsigned int)atomic_clear(&batch_events), (unsigned int)result.count,
           expected, result.dropped, result.overflowed ? ", OVERFLOWED" : "",
           activity_name(activity),
           (unsigned int)atomic_clear(&any_motion_events),
           (unsigned int)atomic_get(&any_motion_total),
           (unsigned int)atomic_clear(&no_motion_events),
           (unsigned int)atomic_get(&no_motion_total));

    /*
     * The IMU counts in its own oscillator and the host in Y4, so this is how
     * far the two timelines have pulled apart. It is the number that says
     * whether accelerometer and PPG samples can share an axis - and it fails
     * quietly until enough time has passed to mean anything.
     */
    if ((motion_get_diagnostics(&diag) == 0) && diag.clock_drift_valid) {
        printk("  -- clock: sensor %+d ppm against the host over %llu ms\n",
               diag.clock_drift_ppm, diag.observed_span_us / 1000U);
    }

    /*
     * A sensor in a fatal state keeps answering and keeps returning
     * well-formed samples, so nothing above would look wrong. Both registers
     * are printed raw: which bit is set is the whole diagnosis, and
     * feat_eng_disabled in particular explains events that never fire while
     * every sample still arrives.
     */
    if ((motion_get_fault(&fault) == 0) && !motion_fault_is_clear(&fault)) {
        printk("  -- FAULT: ERR_REG 0x%02x, INTERNAL_ERROR 0x%02x%s%s%s\n", fault.raw,
               fault.internal_raw, fault.fatal ? " [FATAL]" : "",
               fault.processing_halted ? " [PROCESSING HALTED]" : "",
               fault.feature_engine_disabled ? " [FEATURE ENGINE DISABLED]" : "");
    }

    rc = motion_capture_start(capture_buffer, ARRAY_SIZE(capture_buffer));
    if (rc != 0) {
        printk("  -- stream: capture restart returned %d\n", rc);
    }
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

    start_features();

    printk("printing every %u ms - the sensor runs faster, this is a "
           "sub-sample\n", PRINT_PERIOD_MS);
    printk("lay the board flat: expect |a| near %d mm/s^2 on +Z\n",
           MOTION_GRAVITY_MMS2);
    printk("pick the board up: 'any' should move. set it down and wait %u ms: "
           "'no' should move\n", NO_MOTION_DURATION_MS);
    printk("walk with it: activity should leave 'still'\n");
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
            /* Not on the first pass: there is nothing to report yet. */
            if (index != 0U) {
                print_stream_stats(HEADER_EVERY * PRINT_PERIOD_MS);
            }

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
