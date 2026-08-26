// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * careloop optical path exerciser.
 *
 * Two halves, in order. First the emitters: green, red and IR one at a time
 * through the AFE4404, then all three together. Then the detector phases in
 * detector.c, which read light back through the SFH7074's photodiode.
 *
 * The emitter half is a bring-up tool rather than a test - it answers "does
 * the AFE answer, and does anything light up" and stops there. A ztest cannot
 * close that loop: every write below returns 0 on a board where the boost is
 * dead, the emitters are unpopulated, or RESETZ is held low by a misconfigured
 * pin, because the AFE ACKs off RX_SUP and IO_SUP alone and ACKing is the only
 * thing firmware can observe. Only a person looking at the emitter closes it.
 *
 * The detector half does produce verdicts, because a photodiode reading is
 * something firmware can actually check. It still needs an operator - two of
 * its three phases ask for a hand or a finger over the sensor - so it prints
 * what it wants and when, and reports PASS or FAIL per phase.
 *
 * How to watch it, in order of how much it tells you:
 *
 *   Green and red are directly visible, but dimly - each emitter is on for
 *   100 us out of every 10 ms, so about 1% of the time. Look at the SFH7074
 *   package itself in a dim room, not at the board from across the bench.
 *
 *   IR is invisible to the eye. Most phone cameras see it as a faint white
 *   or violet dot; front-facing cameras usually work better than rear ones,
 *   which tend to have stronger IR-cut filters.
 *
 *   A current meter in series with the battery is the more reliable check,
 *   and the ALL phase is the one to use it on.
 *
 * If nothing lights, the order to suspect things in:
 *
 *   1. The +5V boost (U8). It feeds both TX_SUP and the emitter common
 *      anode, and the AFE answers on I2C without it. optical_init() enables
 *      it, but nothing here can read the rail back.
 *   2. Jumper J3. +3V3 reaches the AFE's RX_SUP only through it, and BUCK2
 *      senses upstream, so the PMIC reports the rail healthy either way.
 *      That is how the first board failed the LED bring-up.
 *   3. RESETZ (P0.26). Its net is called AFE_INT2, which is wrong - it is an
 *      output. Held low, the AFE never comes out of reset and does not ACK,
 *      so init would have failed loudly rather than reaching the phases.
 *
 * Currents are deliberately below each emitter's DC rating rather than at the
 * AFE's 50 mA full scale; the HAL enforces the ceiling, this only stays well
 * inside it. Even so, do not leave the ALL phase running against skin.
 *
 * Run it with:
 *   ./scripts/bringup.sh optical
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <optical.h>

#include "phases.h"

/*
 * Not "optical": src/hal/optical_afe4404.c already registers that module
 * name, and this image links both. Two LOG_MODULE_REGISTER of one name is a
 * duplicate symbol at link time.
 */
LOG_MODULE_REGISTER(optical_app, LOG_LEVEL_INF);

/* Long enough to settle a meter on, and to hunt for a dim dot in a dark room. */
#define PHASE_S 10U
#define PHASE_GAP_MS 800U

/* Comfortably inside every per-emitter ceiling the HAL enforces. */
#define DRIVE_UA 20000U

static const char *const emitter_name[OPTICAL_EMITTER_COUNT] = {
	[OPTICAL_EMITTER_GREEN] = "GREEN",
	[OPTICAL_EMITTER_RED] = "RED",
	[OPTICAL_EMITTER_IR] = "IR",
};

/* Appended to the phase banner, so each phase says how to observe it. */
static const char *const emitter_hint[OPTICAL_EMITTER_COUNT] = {
	[OPTICAL_EMITTER_GREEN] = "look at the SFH7074 in a dim room",
	[OPTICAL_EMITTER_RED] = "look at the SFH7074 in a dim room",
	[OPTICAL_EMITTER_IR] = "invisible - use a phone camera",
};

static int all_off(void)
{
	for (uint8_t i = 0; i < OPTICAL_EMITTER_COUNT; i++) {
		int err = optical_set_current_ua((enum optical_emitter)i, 0U);

		if (err) {
			LOG_ERR("clearing emitter %u failed (%d)", i, err);
			return err;
		}
	}

	return 0;
}

/* Counts down so it is obvious how much of a phase is left to look at. */
static void hold(uint32_t seconds)
{
	for (uint32_t s = seconds; s > 0U; s--) {
		LOG_INF("    %u", s);
		k_sleep(K_SECONDS(1));
	}
}

static int phase_single(enum optical_emitter emitter)
{
	int err;

	LOG_INF("phase: %s at %u uA - %s", emitter_name[emitter], DRIVE_UA,
		emitter_hint[emitter]);

	err = optical_set_current_ua(emitter, DRIVE_UA);
	if (err) {
		LOG_ERR("set current failed (%d)", err);
		return err;
	}

	hold(PHASE_S);

	err = optical_set_current_ua(emitter, 0U);
	if (err) {
		LOG_ERR("clear current failed (%d)", err);
		return err;
	}

	k_msleep(PHASE_GAP_MS);

	return 0;
}

static int phase_all(void)
{
	int err;

	LOG_INF("phase: ALL three at %u uA - the one to put a meter on", DRIVE_UA);

	for (uint8_t i = 0; i < OPTICAL_EMITTER_COUNT; i++) {
		err = optical_set_current_ua((enum optical_emitter)i, DRIVE_UA);
		if (err) {
			LOG_ERR("set emitter %u failed (%d)", i, err);
			return err;
		}
	}

	hold(PHASE_S);

	return all_off();
}

int main(void)
{
	int err;

	LOG_INF("careloop optical bring-up");

	err = optical_init();
	if (err) {
		/*
		 * Stop here rather than carrying on. Past this point every
		 * call would fail the same way, and a screen of identical
		 * errors buries the one that says what actually happened.
		 */
		LOG_ERR("optical_init failed (%d) - see the header of this file", err);
		return 0;
	}

	/*
	 * Report the limits the HAL will enforce before driving anything, so
	 * the numbers in the phases below can be checked against them without
	 * reading the source.
	 */
	uint32_t step_ua = 0U;

	if (optical_get_current_step_ua(&step_ua) == 0) {
		LOG_INF("current step %u uA", step_ua);
	}

	for (uint8_t i = 0; i < OPTICAL_EMITTER_COUNT; i++) {
		uint32_t max_ua = 0U;

		if (optical_get_max_current_ua((enum optical_emitter)i, &max_ua) == 0) {
			LOG_INF("  %-8s max %u uA", emitter_name[i], max_ua);
		}
	}

	err = optical_start();
	if (err) {
		LOG_ERR("optical_start failed (%d)", err);
		return 0;
	}

	LOG_INF("timing engine running, emitters dark");
	k_msleep(PHASE_GAP_MS);

	/*
	 * Green first: it is the brightest of the two visible emitters, so if
	 * exactly one phase is going to be seen on a marginal setup, this is
	 * the one.
	 */
	static const enum optical_emitter order[] = {
		OPTICAL_EMITTER_GREEN,
		OPTICAL_EMITTER_RED,
		OPTICAL_EMITTER_IR,
	};

	for (size_t i = 0; i < ARRAY_SIZE(order); i++) {
		if (phase_single(order[i]) != 0) {
			break;
		}
	}

	(void)phase_all();

	/*
	 * Detector phases run with the sequence still going and every emitter
	 * dark, which is the state phase_all() leaves behind. They own the
	 * gain from here on, so nothing above should assume it is still at the
	 * value optical_init() loaded.
	 */
	bool detector_ok = detector_run(DRIVE_UA);

	/*
	 * Stop rather than loop. This is meant to be watched, and emitters
	 * left pulsing on a bench after everyone has stopped looking only burn
	 * the cell. Reset the board to run it again - the image stays flashed.
	 */
	err = optical_stop();
	if (err) {
		LOG_ERR("optical_stop failed (%d)", err);
	}

	LOG_INF("done - emitters dark, timing engine stopped");
	LOG_INF("detector verdict: %s", detector_ok ? "PASS" : "FAIL - see above");

	return 0;
}
