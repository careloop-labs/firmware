// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * The BMI270 capability src/hal/motion_bmi270.c adds on top of the vendor
 * driver: correctly scaled slope thresholds, no-motion, the FIFO, sensortime
 * and the sensor's own health.
 *
 * Runs after careloop_hal_2_motion, which leaves the IMU initialised and
 * configured. The digit in the suite name is what puts it there; ztest orders
 * by name and CONFIG_ZTEST_SHUFFLE is off.
 *
 * Several of these read the configuration back off the chip through
 * motion_get_diagnostics(), rather than trusting that a call which returned 0
 * left the hardware as intended. That is the whole point of the file: a
 * threshold is write-only from the caller's side, so the only way to prove
 * one landed is to go and look.
 *
 * The threshold cases are a regression test for a specific defect. The
 * vendor driver scales ANYMO_2.threshold against a ten-bit mask, but the
 * field is eleven bits spanning 0..1 g, so everything configured through it
 * arrived at about half the value asked for. _10_ pins the encoding against
 * two values the datasheet states outright.
 *
 * What this does not prove:
 *
 * - That any-motion or no-motion ever fire. Both need the board picked up or
 *   set down, so _90_ arms them and skips, the same bargain
 *   test_hal_motion_f0_any_motion_is_manual makes.
 * - That the activity classifier is correct. It reports "still" on a bench
 *   whether or not it is working.
 * - Anything about absolute timing accuracy. _70_ shows the two clocks agree
 *   with each other over a short span, which is a consistency check, not a
 *   calibration - Y3 on this board measures ~717 ppm out (see test_clocks.c).
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <motion.h>

/*
 * Field positions restated from BST-BMI270-DS000-07 rather than imported from
 * the implementation. That is the point: a test that decoded these with the
 * same constants the encoder used would agree with a wrong mask. These come
 * from the datasheet's register tables, independently.
 */
#define DS_MO1_DURATION_MASK  0x1FFFU  /* ANYMO_1/NOMO_1 bits 12..0 */
#define DS_MO1_SELECT_XYZ     0xE000U  /* bits 15..13 */
#define DS_MO2_THRESHOLD_MASK 0x07FFU  /* ANYMO_2/NOMO_2 bits 10..0 */
#define DS_MO2_ENABLE         0x8000U  /* bit 15 */
#define DS_SENSORTIME_MASK    0x00FFFFFFU

/* 50 Hz is the floor for the feature engine in power-optimised filter mode. */
#define FIFO_ACCEL_ODR_MHZ 50000U
#define FIFO_ACCEL_RANGE_G 8U
#define FIFO_BATCH_MS 500U

#define SETTLE_MS 100U

/* Long enough that a 50 Hz stream has produced several batches. */
#define STREAM_OBSERVE_MS 1500U

#define GRAVITY_TOLERANCE_MMS2 3000

static const struct motion_config stream_config = {
	.accel_odr_mhz = FIFO_ACCEL_ODR_MHZ,
	.accel_range_g = FIFO_ACCEL_RANGE_G,
	.accel_oversampling = 1U,
	.gyro_odr_mhz = 0U,
	.gyro_range_dps = 250U,
	.gyro_oversampling = 0U,
};

/*
 * Hand-rolled: the image builds with -Wdouble-promotion and Twister with
 * -Werror, so sqrt() is not available. Duplicated from test_hal_motion.c
 * rather than shared, for the same reason that file duplicates it.
 */
static uint32_t isqrt64(uint64_t value)
{
	uint64_t remainder = value;
	uint64_t result = 0;
	uint64_t bit = 1ULL << 38;

	while (bit > remainder) {
		bit >>= 2;
	}

	while (bit != 0U) {
		if (remainder >= (result + bit)) {
			remainder -= result + bit;
			result = (result >> 1) + bit;
		} else {
			result >>= 1;
		}
		bit >>= 2;
	}

	return (uint32_t)result;
}

static void configure_for_stream(void)
{
	zassert_ok(motion_configure(&stream_config), "motion_configure() at 50 Hz failed");
	k_sleep(K_MSEC(SETTLE_MS));
}

ZTEST_SUITE(careloop_hal_3_motion_fifo, NULL, NULL, NULL, NULL, NULL);

/*
 * Where the fault in _80_ comes from, bisected rather than guessed.
 *
 * INTERNAL_ERROR.feat_eng_disabled means the sensor's motion features have
 * stopped while sampling carries on, and the datasheet describes it only as
 * "disabled by host during sensor operation" - which does not say which host
 * action does it. Three candidates, and this separates them:
 *
 *   a) it was already set at init, so the part came up that way and nothing
 *      this firmware does is responsible. careloop_hal_2_motion's _10_init
 *      prints the fault at the earliest readable moment; compare against the
 *      "entry" line below.
 *   b) careloop_hal_2_motion set it. That suite suspends the accelerometer
 *      twice, in _80_suspend_and_resume and _90_gyro_odr_zero, and the
 *      feature engine runs on accelerometer data - so a clean entry line here
 *      rules this out, and a dirty one with a clean init line pins it.
 *   c) writing feature registers during operation sets it. This case writes
 *      one and re-reads, before any other case in this suite has written any.
 *
 * The assertion is deliberately narrow: only the suspend/resume transition,
 * because that is the one this HAL could and should defend against by
 * re-arming on resume. The rest is reported, since a part that arrives
 * unhealthy is not something a test can fix.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_05_fault_provenance)
{
	const struct motion_any_motion_config probe = {
		.threshold_mg = 100U,
		.duration_ms = 100U,
	};
	struct motion_fault entry;
	struct motion_fault after_write;
	struct motion_fault after_cycle;

	zassert_true((motion_get_capabilities() & MOTION_CAP_FAULT) != 0U,
		     "fault capability missing");

	zassert_ok(motion_get_fault(&entry), "could not read the fault at entry");
	printk("  entry        ERR_REG 0x%02x  INTERNAL_ERROR 0x%02x  feat_eng_disabled %d\n",
	       entry.raw, entry.internal_raw, entry.feature_engine_disabled);

	configure_for_stream();

	zassert_ok(motion_configure_any_motion(&probe), "any-motion probe rejected");
	zassert_ok(motion_get_fault(&after_write), "could not read the fault after a feature write");
	printk("  after write  ERR_REG 0x%02x  INTERNAL_ERROR 0x%02x  feat_eng_disabled %d\n",
	       after_write.raw, after_write.internal_raw, after_write.feature_engine_disabled);

	/* Powers the accelerometer down and back up - what the feature engine feeds on. */
	zassert_ok(motion_set_enabled(false), "suspend failed");
	zassert_ok(motion_set_enabled(true), "resume failed");
	k_sleep(K_MSEC(SETTLE_MS));

	zassert_ok(motion_get_fault(&after_cycle), "could not read the fault after suspend/resume");
	printk("  after cycle  ERR_REG 0x%02x  INTERNAL_ERROR 0x%02x  feat_eng_disabled %d\n",
	       after_cycle.raw, after_cycle.internal_raw, after_cycle.feature_engine_disabled);

	zassert_false(!after_write.feature_engine_disabled && after_cycle.feature_engine_disabled,
		      "suspending the accelerometer disabled the feature engine - the HAL has to "
		      "re-arm the slope features and the classifier on resume, because sampling "
		      "comes back and they do not");
}

/*
 * The encoding, checked against values the datasheet publishes rather than
 * against this firmware's own arithmetic.
 *
 * ANYMO_1 resets to 0xE005 and ANYMO_2 to 0x38AA, documented as a 100 ms
 * duration on all three axes and an 83 mg threshold. Asking for exactly that
 * has to reproduce exactly those words, which pins the field widths, the
 * scale factor, the axis-select bits and the out_conf placement all at once.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_10_threshold_encoding)
{
	const struct motion_any_motion_config datasheet_default = {
		.threshold_mg = 83U,
		.duration_ms = 100U,
	};
	struct motion_diagnostics diag;

	zassert_true((motion_get_capabilities() & MOTION_CAP_ANY_MOTION) != 0U,
		     "any-motion capability missing - is bosch,bmi270-base still in the dts?");

	zassert_ok(motion_configure_any_motion(&datasheet_default),
		   "any-motion 83 mg / 100 ms rejected");

	zassert_ok(motion_get_diagnostics(&diag), "could not read the configuration back");
	zassert_true(diag.slope_readback_valid, "slope readback did not reach the chip");

	printk("  anymo_1 0x%04x  anymo_2 0x%04x  (datasheet reset 0xE005 / 0x38AA)\n",
	       diag.any_motion_raw[0], diag.any_motion_raw[1]);

	zassert_equal(diag.any_motion_raw[0], 0xE005U,
		      "ANYMO_1 encoded as 0x%04x, datasheet says 100 ms on XYZ is 0xE005",
		      diag.any_motion_raw[0]);

	/* The enable bit is not set by configure, so compare without it. */
	zassert_equal(diag.any_motion_raw[1] & ~DS_MO2_ENABLE, 0x38AAU,
		      "ANYMO_2 encoded as 0x%04x, datasheet says 83 mg is 0x38AA",
		      diag.any_motion_raw[1]);
}

/*
 * The regression proper. 100 mg is 0.1 g, and 0.1 g of an eleven-bit field
 * spanning 1 g is 204.8 LSB. The defective path scaled against ten bits and
 * produced 102 - so anything near 102 here means the old arithmetic is back.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_20_threshold_is_not_halved)
{
	const struct motion_any_motion_config config = {
		.threshold_mg = 100U,
		.duration_ms = 100U,
	};
	struct motion_diagnostics diag;
	unsigned int threshold_lsb;

	zassert_ok(motion_configure_any_motion(&config), "any-motion 100 mg rejected");
	zassert_ok(motion_get_diagnostics(&diag), "could not read ANYMO_2 back");

	threshold_lsb = (unsigned int)(diag.any_motion_raw[1] & DS_MO2_THRESHOLD_MASK);

	printk("  100 mg -> %u LSB (expected 205, defective path gave 102)\n", threshold_lsb);

	zassert_within(threshold_lsb, 205U, 1U,
		       "100 mg encoded as %u LSB; 205 is correct and 102 is the ten-bit defect",
		       threshold_lsb);
}

/*
 * No-motion lives on a different feature page from any-motion, at a different
 * address. Writing the wrong page would land silently on whatever else
 * occupies 0x30 there, so the two are configured differently and read back
 * separately.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_30_no_motion_is_separate)
{
	const struct motion_no_motion_config no_motion = {
		.threshold_mg = 40U,
		.duration_ms = 200U,
	};
	struct motion_diagnostics diag;
	unsigned int duration_lsb;
	unsigned int threshold_lsb;

	zassert_true((motion_get_capabilities() & MOTION_CAP_NO_MOTION) != 0U,
		     "no-motion capability missing");

	zassert_ok(motion_configure_no_motion(&no_motion), "no-motion 40 mg / 200 ms rejected");

	zassert_ok(motion_get_diagnostics(&diag), "could not read the configuration back");

	printk("  nomo_1 0x%04x  nomo_2 0x%04x   anymo_1 0x%04x  anymo_2 0x%04x\n",
	       diag.no_motion_raw[0], diag.no_motion_raw[1], diag.any_motion_raw[0],
	       diag.any_motion_raw[1]);

	/*
	 * 200 ms at 20 ms per step is 10, and 40 mg is 82 LSB. Narrowed to
	 * unsigned int first: GENMASK yields unsigned long, and the image
	 * builds with -Werror=format.
	 */
	duration_lsb = (unsigned int)(diag.no_motion_raw[0] & DS_MO1_DURATION_MASK);
	threshold_lsb = (unsigned int)(diag.no_motion_raw[1] & DS_MO2_THRESHOLD_MASK);

	zassert_equal(duration_lsb, 10U, "NOMO_1 duration is %u, expected 10", duration_lsb);
	zassert_within(threshold_lsb, 82U, 1U, "NOMO_2 threshold is %u LSB, expected 82",
		       threshold_lsb);

	/*
	 * The two must not be the same word. If the page select were being
	 * ignored, both reads would land on the same register and agree.
	 */
	zassert_not_equal(diag.no_motion_raw[1], diag.any_motion_raw[1],
			  "no-motion and any-motion read identical - FEAT_PAGE is not taking");
}

/*
 * Below 50 Hz the feature engine runs on fewer samples than it is specified
 * for and the part says so in INTERNAL_STATUS, but carries on. Arming
 * something against a rate it cannot work at is refused here instead.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_40_rate_floor_enforced)
{
	struct motion_config too_slow = stream_config;
	int err;

	configure_for_stream();

	zassert_ok(motion_set_event_enabled(MOTION_EVENT_ANY_MOTION, true),
		   "could not arm any-motion at 50 Hz");

	too_slow.accel_odr_mhz = 25000U;
	err = motion_configure(&too_slow);

	printk("  25 Hz with a feature armed returned %d\n", err);

	zassert_equal(err, -ERR_MOTION_INVALID_CONFIG,
		      "25 Hz was accepted while any-motion was armed; the feature engine needs "
		      "at least 50 Hz and degrades silently below it");

	zassert_ok(motion_set_event_enabled(MOTION_EVENT_ANY_MOTION, false),
		   "could not disarm any-motion");

	/* The rejected call must not have moved the rate. */
	configure_for_stream();
}

/* Streaming has to actually produce samples, and they have to look like the
 * board is sitting still on a bench.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_50_stream_fills_window)
{
	struct motion_accel_sample samples[64];
	size_t count = 0U;
	uint32_t max_batch = 0U;
	uint64_t magnitude_sq;
	uint32_t magnitude;

	zassert_true((motion_get_capabilities() & MOTION_CAP_FIFO) != 0U,
		     "FIFO capability missing - the drain thread or its interrupt did not start");

	configure_for_stream();

	zassert_ok(motion_stream_max_batch_ms(&max_batch), "could not read the batch ceiling");
	printk("  batch ceiling %u ms at 50 Hz\n", max_batch);
	zassert_true(max_batch >= FIFO_BATCH_MS,
		     "ceiling %u ms is below the %u ms batch this test asks for", max_batch,
		     FIFO_BATCH_MS);

	zassert_ok(motion_stream_start(FIFO_BATCH_MS), "motion_stream_start() failed");

	k_sleep(K_MSEC(STREAM_OBSERVE_MS));

	zassert_ok(motion_window_read(samples, ARRAY_SIZE(samples), &count),
		   "motion_window_read() failed");

	printk("  window holds %u samples after %u ms\n", (unsigned int)count, STREAM_OBSERVE_MS);

	zassert_true(count > 0U,
		     "no samples reached the window - the watermark interrupt on P0.09 never "
		     "fired, or the FIFO was never enabled");

	magnitude_sq = ((uint64_t)samples[count - 1U].x_mms2 * samples[count - 1U].x_mms2) +
		       ((uint64_t)samples[count - 1U].y_mms2 * samples[count - 1U].y_mms2) +
		       ((uint64_t)samples[count - 1U].z_mms2 * samples[count - 1U].z_mms2);
	magnitude = isqrt64(magnitude_sq);

	printk("  newest sample %d %d %d mm/s2, |a| = %u\n", samples[count - 1U].x_mms2,
	       samples[count - 1U].y_mms2, samples[count - 1U].z_mms2, magnitude);

	/* Vector length, not per-axis: a stuck axis still reads plausibly alone. */
	zassert_within(magnitude, (uint32_t)MOTION_GRAVITY_MMS2, GRAVITY_TOLERANCE_MMS2,
		       "|a| = %u mm/s2, expected about %d - samples came through the FIFO but "
		       "are not gravity, so the frame parser has the payload offset wrong",
		       magnitude, MOTION_GRAVITY_MMS2);

	zassert_ok(motion_stream_stop(), "motion_stream_stop() failed");
}

/*
 * A capture over a known interval. The sample count is the real assertion:
 * it says the drain kept up, that the frame parser found every frame, and
 * that the rate is what was asked for.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_60_capture_is_complete)
{
	static struct motion_accel_sample storage[256];
	struct motion_capture_result result = {0};
	uint32_t expected;

	configure_for_stream();

	zassert_ok(motion_stream_start(FIFO_BATCH_MS), "motion_stream_start() failed");
	zassert_ok(motion_capture_start(storage, ARRAY_SIZE(storage)),
		   "motion_capture_start() failed");

	k_sleep(K_MSEC(STREAM_OBSERVE_MS));

	zassert_ok(motion_capture_stop(&result), "motion_capture_stop() failed");
	zassert_ok(motion_stream_stop(), "motion_stream_stop() failed");

	expected = (STREAM_OBSERVE_MS * (FIFO_ACCEL_ODR_MHZ / 1000U)) / 1000U;

	printk("  captured %u samples in %u ms (expected ~%u), period %u us, dropped %u%s\n",
	       (unsigned int)result.count, STREAM_OBSERVE_MS, expected, result.period_us,
	       result.dropped, result.overflowed ? ", OVERFLOWED" : "");

	zassert_equal(result.dropped, 0U,
		      "the sensor discarded %u samples - the drain thread did not keep up with a "
		      "%u ms batch",
		      result.dropped, FIFO_BATCH_MS);

	zassert_false(result.overflowed, "capture buffer overflowed; %u samples would not fit",
		      (unsigned int)ARRAY_SIZE(storage));

	zassert_equal(result.period_us, 20000U, "period reported as %u us, expected 20000 at 50 Hz",
		      result.period_us);

	/*
	 * A batch's worth of tolerance either side: the window opens and
	 * closes between drains, not on them.
	 */
	zassert_within(result.count, expected, (FIFO_ACCEL_ODR_MHZ / 1000U) * FIFO_BATCH_MS / 1000U,
		       "captured %u samples, expected about %u - the rate is not 50 Hz or frames "
		       "were missed",
		       (unsigned int)result.count, expected);
}

/*
 * Sensortime is the sensor's own clock and the only thing that can say
 * whether its sample timeline agrees with the host's. Checked over a fixed
 * sleep: the two counters are independent oscillators, so this is a
 * consistency check between them, not a measurement of either.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_70_sensortime_advances)
{
	struct motion_diagnostics before;
	struct motion_diagnostics after;
	uint32_t first;
	uint32_t second;
	uint32_t delta_ticks;
	uint64_t elapsed_us;
	const uint32_t sleep_ms = 500U;

	zassert_ok(motion_get_diagnostics(&before), "could not read sensortime");
	zassert_true(before.sensortime_valid, "sensortime not available");

	k_sleep(K_MSEC(sleep_ms));

	zassert_ok(motion_get_diagnostics(&after), "could not read sensortime again");

	first = before.sensortime;
	second = after.sensortime;
	delta_ticks = (second - first) & DS_SENSORTIME_MASK;

	/* 39.0625 us per tick is exactly 625/16, stated here independently. */
	elapsed_us = ((uint64_t)delta_ticks * 625U) / 16U;

	printk("  sensortime %u -> %u, %llu us over a %u ms sleep\n", first, second, elapsed_us,
	       sleep_ms);

	zassert_true(delta_ticks > 0U,
		     "sensortime did not move - the counter is not running, or the read is "
		     "returning a stale register");

	/* Ten percent covers scheduling slop and both oscillators' tolerance. */
	zassert_within((uint32_t)elapsed_us, sleep_ms * 1000U, (sleep_ms * 1000U) / 10U,
		       "sensortime advanced %llu us over %u ms - the 39.0625 us tick conversion "
		       "or the 24-bit wrap handling is wrong",
		       elapsed_us, sleep_ms);
}

/*
 * The sensor's own health. A BMI270 in a fatal state keeps acknowledging its
 * address and keeps returning well-formed samples, so nothing in the data
 * reveals it and every other case in this suite would still pass.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_80_reports_no_fault)
{
	struct motion_fault fault;

	zassert_true((motion_get_capabilities() & MOTION_CAP_FAULT) != 0U,
		     "fault capability missing");

	zassert_ok(motion_get_fault(&fault), "motion_get_fault() failed");

	printk("  ERR_REG 0x%02x  INTERNAL_ERROR 0x%02x  fatal %d  feat_eng %d  halted %d  "
	       "fifo %d  vendor 0x%02x\n",
	       fault.raw, fault.internal_raw, fault.fatal, fault.feature_engine_disabled,
	       fault.processing_halted, fault.fifo_error, fault.vendor_code);

	/*
	 * Judged on the decoded bits, not on the registers reading zero. Every
	 * board measured so far reports INTERNAL_ERROR 0x01 from init onwards -
	 * bit 0 is reserved in the datasheet's own field table, which defines
	 * only bits 1, 2 and 4 - while all three defined bits stay clear and
	 * every case above this one passes.
	 */

	/*
	 * feat_eng_disabled is called out separately because it is the one that
	 * does not show up as bad data: sampling carries on perfectly while
	 * every motion event silently stops arriving.
	 */
	zassert_false(fault.feature_engine_disabled,
		      "the feature engine is disabled (INTERNAL_ERROR 0x%02x) - any-motion, "
		      "no-motion and activity cannot fire, though sampling is unaffected",
		      fault.internal_raw);

	zassert_false(fault.processing_halted,
		      "the sensor stopped processing (INTERNAL_ERROR 0x%02x)", fault.internal_raw);

	zassert_true(motion_fault_is_clear(&fault),
		     "sensor reports a fault: ERR_REG 0x%02x, INTERNAL_ERROR 0x%02x, vendor code "
		     "0x%02x. fatal_err clears only on power-on or soft reset",
		     fault.raw, fault.internal_raw, fault.vendor_code);
}

/*
 * Activity classification and both slope events are armed for real, and then
 * this skips: firing any of them needs the board picked up, carried and set
 * down again. Waiting on bench vibration to do that is a coin flip, and a
 * flaky hardware test is worse than an honest gap - so the gap lands in the
 * Twister report instead of only in this comment.
 */
ZTEST(careloop_hal_3_motion_fifo, test_hal_fifo_90_events_are_manual)
{
	enum motion_activity activity = MOTION_ACTIVITY_UNKNOWN;

	configure_for_stream();

	zassert_ok(motion_set_activity_enabled(true), "could not enable activity classification");
	zassert_ok(motion_get_activity(&activity), "could not read activity");

	zassert_true(activity <= MOTION_ACTIVITY_UNKNOWN, "activity reported as %d, out of range",
		     (int)activity);

	zassert_ok(motion_set_event_enabled(MOTION_EVENT_ANY_MOTION, true),
		   "could not arm any-motion");
	zassert_ok(motion_set_event_enabled(MOTION_EVENT_NO_MOTION, true),
		   "could not arm no-motion");

	printk("  activity %d, any-motion and no-motion both armed on INT1\n", (int)activity);
	printk("  firing these needs the board picked up and set down - not assertable here\n");

	ztest_test_skip();
}
