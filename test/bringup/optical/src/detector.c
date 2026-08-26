// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Photodetector phases of the optical bring-up.
 *
 * There is exactly one detector on this board: the SFH7074's broadband PD3,
 * cathode to the AFE's INP, INM to ground. PD1 and PD2 - the two IR-cut
 * diodes - are not connected to anything, so nothing here can cross-check
 * one diode against another. Everything below reads the same photodiode
 * through four time slots.
 *
 * The three phases answer three separate questions, in increasing order of
 * how much has to be working:
 *
 *   1. baseline   Does the receiver convert at all? Needs the photodiode,
 *                 the TIA, the ADC and the timing engine, but no light and
 *                 no emitter. A stuck reading here is the one failure that
 *                 cannot be blamed on the room or the operator.
 *
 *   2. ambient    Does the detector respond to light? Needs the optics to be
 *                 unobstructed. Emitters stay dark, so this passes or fails
 *                 independently of the whole transmit path - which is what
 *                 makes it worth running even when the emitters are known
 *                 good.
 *
 *   3. reflectance Does light get from an emitter, off something, and back
 *                 into the detector? This is the actual PPG loop and the
 *                 only phase that proves the two halves are optically
 *                 coupled rather than merely alive.
 *
 * Phase 3 measures GREEN minus AMBIENT, not GREEN. A finger over the sensor
 * does two opposite things at once: it blocks room light, pushing the raw
 * reading down, and it reflects emitter light, pushing it up. Those partly
 * cancel, so raw GREEN can move either way and a threshold on it would be
 * measuring the room. The difference isolates the emitter's own contribution,
 * which only moves one way.
 *
 * Gain is auto-ranged rather than fixed because the right value depends
 * entirely on the room. At 500 kohm the broadband diode reaches a large
 * fraction of full scale under ordinary office light, so a fixed gain either
 * saturates on a bright bench or wastes range in a dim one, and both look
 * like a broken detector.
 *
 * Every threshold here is measured, not absolute, and that is the whole
 * reason phase 1 exists. An earlier version gated phase 2 on a fixed 1% of
 * full scale and passed with nobody in the room: the undisturbed ambient
 * reading already swung by 7% of full scale on its own, so the phase could
 * not fail. Mains lighting flickers at 100 Hz and the sequence runs at
 * 100 Hz, which aliases that flicker down to near-DC and produces exactly
 * that kind of large slow swing. So phase 1 measures the undisturbed
 * variation and the later phases must beat a multiple of it. A threshold
 * that a still, empty bench can clear is not a test.
 */

#include "phases.h"

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <optical.h>

LOG_MODULE_DECLARE(optical_app, LOG_LEVEL_INF);

#define SAMPLE_PERIOD_MS 20U /* 50 Hz against a 100 Hz sequence */

#define BASELINE_MS 2000U
#define AMBIENT_MS 8000U
#define REFLECT_MS 8000U
#define SETTLE_MS 3000U /* operator reaction time before a measured window */

/*
 * Gains to try, brightest-tolerant last. Auto-ranging walks this in order
 * and keeps the first that leaves headroom, so the list must stay descending
 * - the AFE's own register order is not monotonic and must not be used here.
 */
static const uint32_t gain_ladder_ohms[] = {
	2000000U, 1000000U, 500000U, 250000U, 100000U, 50000U, 25000U, 10000U,
};

/* Keep this much of full scale unused, so a signal added later still fits. */
#define HEADROOM_PERCENT 70

/*
 * How far above the undisturbed variation a response has to rise to count.
 * Three is chosen to sit clear of the flicker without demanding much: a hand
 * over the sensor changes the reading by most of full scale, hundreds of
 * times the noise, so a real response clears this by a wide margin and only
 * an absent operator fails it.
 */
#define NOISE_MULTIPLE 3

struct chan_stats {
	int32_t min;
	int32_t max;
	int64_t sum;
	uint32_t count;
	bool varied; /* false if every sample was bit-identical */
	int32_t first;
};

static const char *const channel_name[OPTICAL_CHANNEL_COUNT] = {
	[OPTICAL_CHANNEL_GREEN] = "GREEN",
	[OPTICAL_CHANNEL_RED] = "RED",
	[OPTICAL_CHANNEL_IR] = "IR",
	[OPTICAL_CHANNEL_AMBIENT] = "AMBIENT",
};

static void stats_reset(struct chan_stats *s)
{
	s->min = INT32_MAX;
	s->max = INT32_MIN;
	s->sum = 0;
	s->count = 0U;
	s->varied = false;
	s->first = 0;
}

static void stats_add(struct chan_stats *s, int32_t v)
{
	if (s->count == 0U) {
		s->first = v;
	} else if (v != s->first) {
		s->varied = true;
	}

	if (v < s->min) {
		s->min = v;
	}
	if (v > s->max) {
		s->max = v;
	}

	s->sum += v;
	s->count++;
}

static int32_t stats_mean(const struct chan_stats *s)
{
	if (s->count == 0U) {
		return 0;
	}

	return (int32_t)(s->sum / (int64_t)s->count);
}

static int32_t stats_range(const struct chan_stats *s)
{
	if (s->count == 0U) {
		return 0;
	}

	return s->max - s->min;
}

/*
 * Collect one window. Fills per-channel stats plus the GREEN-AMBIENT
 * difference, and ORs together every saturation flag seen - a channel that
 * clipped even once during the window has an untrustworthy min or max, so
 * the flag has to survive the whole window rather than reflect the last
 * sample.
 */
static int collect(uint32_t duration_ms, struct chan_stats chan[OPTICAL_CHANNEL_COUNT],
		   struct chan_stats *green_minus_ambient, uint8_t *saturated)
{
	uint32_t iterations = duration_ms / SAMPLE_PERIOD_MS;

	for (uint8_t i = 0; i < OPTICAL_CHANNEL_COUNT; i++) {
		stats_reset(&chan[i]);
	}
	stats_reset(green_minus_ambient);
	*saturated = 0U;

	for (uint32_t n = 0; n < iterations; n++) {
		struct optical_sample s;
		int err = optical_read(&s);

		if (err) {
			LOG_ERR("optical_read failed (%d)", err);
			return err;
		}

		for (uint8_t i = 0; i < OPTICAL_CHANNEL_COUNT; i++) {
			stats_add(&chan[i], s.code[i]);
		}

		stats_add(green_minus_ambient,
			  s.code[OPTICAL_CHANNEL_GREEN] - s.code[OPTICAL_CHANNEL_AMBIENT]);

		*saturated |= s.saturated;

		k_msleep(SAMPLE_PERIOD_MS);
	}

	return 0;
}

static void report(const char *label, const struct chan_stats *s)
{
	LOG_INF("    %-14s min %8d  max %8d  mean %8d  range %8d", label, s->min, s->max,
		stats_mean(s), stats_range(s));
}

/* Counts down so the operator knows how long to keep doing something. */
static void prompt_countdown(uint32_t seconds)
{
	for (uint32_t s = seconds; s > 0U; s--) {
		LOG_INF("    %u", s);
		k_sleep(K_SECONDS(1));
	}
}

/*
 * Pick the highest gain that leaves the ambient channel inside HEADROOM_PERCENT
 * of full scale. Walks down from the top rather than computing a gain from one
 * reading, because a saturated reading carries no magnitude information - it is
 * a rail - so there is nothing to compute from until the signal is back in
 * range.
 */
static int autorange(int32_t full_scale, uint32_t *chosen_ohms)
{
	int32_t limit = (int32_t)(((int64_t)full_scale * HEADROOM_PERCENT) / 100);

	for (size_t i = 0; i < ARRAY_SIZE(gain_ladder_ohms); i++) {
		struct chan_stats chan[OPTICAL_CHANNEL_COUNT];
		struct chan_stats diff;
		uint8_t saturated;
		int err;

		err = optical_set_gain_ohms(gain_ladder_ohms[i]);
		if (err) {
			LOG_ERR("set gain failed (%d)", err);
			return err;
		}

		/* Let the TIA settle over a few sequence periods before
		 * judging the result of the change. */
		k_msleep(100);

		err = collect(200U, chan, &diff, &saturated);
		if (err) {
			return err;
		}

		int32_t peak = MAX(abs(chan[OPTICAL_CHANNEL_AMBIENT].max),
				   abs(chan[OPTICAL_CHANNEL_AMBIENT].min));
		bool ambient_saturated = (saturated & BIT(OPTICAL_CHANNEL_AMBIENT)) != 0U;

		if (!ambient_saturated && peak < limit) {
			(void)optical_get_gain_ohms(chosen_ohms);
			LOG_INF("    gain %u ohm, ambient peak %d of %d", *chosen_ohms, peak,
				full_scale);
			return 0;
		}
	}

	/*
	 * Even the lowest gain clips. That is a real result, not an error -
	 * it means the detector is being flooded, which is what a bare sensor
	 * under a desk lamp looks like.
	 */
	(void)optical_get_gain_ohms(chosen_ohms);
	LOG_WRN("    ambient still saturating at the lowest gain (%u ohm) - shade the sensor",
		*chosen_ohms);

	return 0;
}

static bool phase_baseline(int32_t full_scale, int32_t *noise_floor)
{
	struct chan_stats chan[OPTICAL_CHANNEL_COUNT];
	struct chan_stats diff;
	uint8_t saturated;
	bool ok = true;

	LOG_INF("detector 1/3: baseline - emitters dark, do not touch the sensor");

	if (collect(BASELINE_MS, chan, &diff, &saturated) != 0) {
		return false;
	}

	for (uint8_t i = 0; i < OPTICAL_CHANNEL_COUNT; i++) {
		report(channel_name[i], &chan[i]);
	}

	/*
	 * A channel that never changes by even one count is the signature of a
	 * receiver that is not converting - a stalled timing engine, or the
	 * internal oscillator never enabled. Real conversions always carry
	 * noise in the low bits, so bit-identical samples over two seconds are
	 * not a quiet room, they are a dead ADC.
	 */
	if (!chan[OPTICAL_CHANNEL_AMBIENT].varied) {
		LOG_ERR("    FAIL ambient never changed (%d every sample) - the ADC is not "
			"converting; suspect the timing engine or OSC_ENABLE",
			chan[OPTICAL_CHANNEL_AMBIENT].first);
		ok = false;
	}

	/*
	 * With every emitter dark all four slots look at the same darkness, so
	 * they should agree. A wide spread means a slot is picking up something
	 * that is not light - most likely a timing phase overlapping an emitter
	 * window or an ADC reset.
	 */
	int32_t lo = INT32_MAX;
	int32_t hi = INT32_MIN;

	for (uint8_t i = 0; i < OPTICAL_CHANNEL_COUNT; i++) {
		lo = MIN(lo, stats_mean(&chan[i]));
		hi = MAX(hi, stats_mean(&chan[i]));
	}

	int32_t spread_limit = full_scale / 20; /* 5% of range */

	if ((hi - lo) > spread_limit) {
		LOG_WRN("    channels disagree by %d with all emitters dark (limit %d) - "
			"suspect the phase timing",
			hi - lo, spread_limit);
	}

	if (saturated != 0U) {
		LOG_WRN("    saturated channels mask 0x%02x even after auto-ranging", saturated);
	}

	/*
	 * What the later phases must beat. Taken from the ambient channel with
	 * nothing happening, so it captures whatever the room is already doing
	 * to the detector - flicker, drift, converter noise - as one number.
	 */
	*noise_floor = stats_range(&chan[OPTICAL_CHANNEL_AMBIENT]);
	LOG_INF("    undisturbed variation %d counts - later phases must beat %dx this",
		*noise_floor, NOISE_MULTIPLE);

	LOG_INF("    %s", ok ? "PASS the receiver is converting" : "FAIL");

	return ok;
}

static bool phase_ambient(int32_t full_scale, int32_t noise_floor)
{
	struct chan_stats chan[OPTICAL_CHANNEL_COUNT];
	struct chan_stats diff;
	uint8_t saturated;

	LOG_INF("detector 2/3: ambient response - emitters stay dark");
	LOG_INF("  WAVE YOUR HAND over the sensor, or cover and uncover it, for %u s",
		AMBIENT_MS / 1000U);
	prompt_countdown(SETTLE_MS / 1000U);
	LOG_INF("  ... measuring now");

	if (collect(AMBIENT_MS, chan, &diff, &saturated) != 0) {
		return false;
	}

	report("AMBIENT", &chan[OPTICAL_CHANNEL_AMBIENT]);

	/*
	 * A multiple of what the bench does on its own, floored at 1% of full
	 * scale so a freak dark room cannot make the bar trivially low.
	 * Covering the diode with a hand changes the photocurrent by orders of
	 * magnitude, so a genuine response clears this easily.
	 */
	int32_t threshold = MAX(noise_floor * NOISE_MULTIPLE, full_scale / 100);
	int32_t range = stats_range(&chan[OPTICAL_CHANNEL_AMBIENT]);

	if (range < threshold) {
		LOG_ERR("    FAIL ambient moved by %d, needed %d - either nothing passed over "
			"the sensor, or the detector is not seeing light (suspect PD3, its INP "
			"connection, or an obstructed optical path)",
			range, threshold);
		return false;
	}

	LOG_INF("    PASS ambient moved by %d (needed %d)", range, threshold);

	return true;
}

static bool phase_reflectance(int32_t full_scale, uint32_t drive_ua)
{
	struct chan_stats chan[OPTICAL_CHANNEL_COUNT];
	struct chan_stats diff;
	uint8_t saturated;
	int32_t uncovered_mean;

	LOG_INF("detector 3/3: reflectance - GREEN at %u uA", drive_ua);

	if (optical_set_current_ua(OPTICAL_EMITTER_GREEN, drive_ua) != 0) {
		LOG_ERR("    could not set green current");
		return false;
	}

	k_msleep(100);

	/* Reference with nothing in front of the sensor. */
	LOG_INF("  keep the sensor CLEAR - measuring the unloaded reference");
	if (collect(1000U, chan, &diff, &saturated) != 0) {
		return false;
	}
	uncovered_mean = stats_mean(&diff);
	int32_t reference_spread = stats_range(&diff);

	report("GREEN-AMBIENT", &diff);

	/*
	 * With nothing in front of the sensor this level is optical crosstalk:
	 * emitter light reaching the diode inside the package instead of via
	 * the target. It is reported because it is a fixed cost against the
	 * converter's range - whatever it takes, the reflected signal has to
	 * fit in what is left - and because a sudden change in it between
	 * boards points at the optical barrier rather than the electronics.
	 */
	LOG_INF("    crosstalk (no target): %d counts, %d%% of full scale", uncovered_mean,
		(int32_t)(((int64_t)uncovered_mean * 100) / full_scale));

	LOG_INF("  now PRESS A FINGER firmly over the sensor and hold it for %u s",
		REFLECT_MS / 1000U);
	prompt_countdown(SETTLE_MS / 1000U);
	LOG_INF("  ... measuring now");

	if (collect(REFLECT_MS, chan, &diff, &saturated) != 0) {
		return false;
	}

	report("GREEN-AMBIENT", &diff);
	report("GREEN raw", &chan[OPTICAL_CHANNEL_GREEN]);
	report("AMBIENT raw", &chan[OPTICAL_CHANNEL_AMBIENT]);

	int32_t covered_mean = stats_mean(&diff);
	int32_t delta = covered_mean - uncovered_mean;

	/*
	 * Measured against the reference window's own spread, not a fraction of
	 * full scale. The two means are compared directly, so the question is
	 * whether the shift is bigger than the wobble already present in the
	 * quantity being differenced - and that wobble is what the reference
	 * window just measured.
	 */
	int32_t threshold = MAX(reference_spread * NOISE_MULTIPLE, full_scale / 200);

	if (saturated != 0U) {
		LOG_WRN("    saturated channels mask 0x%02x - lower the gain or the current",
			saturated);
	}

	if (delta < threshold) {
		LOG_ERR("    FAIL reflected green changed by %d, needed %d - either no finger "
			"was applied, or emitter and detector are not optically coupled "
			"(check nothing is between them and that the finger covered the "
			"whole package)",
			delta, threshold);
		(void)optical_set_current_ua(OPTICAL_EMITTER_GREEN, 0U);
		return false;
	}

	LOG_INF("    PASS reflected green rose by %d (needed %d)", delta, threshold);

	/*
	 * Peak-to-peak over the window, next to the noise it has to be judged
	 * against. Deliberately not called the pulse and deliberately not
	 * gated on: at this PRF the aliased mains flicker lands in the same
	 * band as a heart rate, so this number is only suggestive of a PPG
	 * when it stands well clear of the reference spread. A still finger, a
	 * cold hand or a loose contact all suppress a real pulse without
	 * meaning the hardware is faulty.
	 */
	LOG_INF("    peak-to-peak %d against a reference spread of %d - suggestive of a pulse "
		"only if it clearly exceeds it",
		stats_range(&diff), reference_spread);

	(void)optical_set_current_ua(OPTICAL_EMITTER_GREEN, 0U);

	return true;
}

bool detector_run(uint32_t drive_ua)
{
	int32_t full_scale = 0;
	uint32_t gain_ohms = 0U;
	bool ok = true;
	int err;

	err = optical_get_full_scale_code(&full_scale);
	if (err) {
		LOG_ERR("full scale query failed (%d)", err);
		return false;
	}

	LOG_INF("--- photodetector (SFH7074 PD3, broadband; PD1/PD2 are not wired) ---");
	LOG_INF("  full scale %d counts", full_scale);

	LOG_INF("detector 0/3: auto-ranging the gain against the room");
	if (autorange(full_scale, &gain_ohms) != 0) {
		return false;
	}

	int32_t noise_floor = 0;

	ok &= phase_baseline(full_scale, &noise_floor);
	ok &= phase_ambient(full_scale, noise_floor);
	ok &= phase_reflectance(full_scale, drive_ua);

	return ok;
}
