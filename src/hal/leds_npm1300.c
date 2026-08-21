// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/leds.h for the nPM1300's constant-current sinks.
 * Verified against led_npm13xx in NCS v3.4.0 (Zephyr 4.4.0).
 *
 * Channel 0 -> LED0 -> D4, channel 1 -> LED1 -> D3, channel 2 -> LED2 -> D2.
 * All red, on/off only, no series resistors - the sink sets the current.
 *
 * The devicetree puts all three in host mode. In the other modes led_on()
 * returns -EPERM, which is ERR_LEDS_NOT_CONTROLLABLE.
 *
 * No PMIC write changes all three sinks together, so leds_set_mask() is three
 * transactions and is not atomic.
 *
 * leds_init() has to clear the sinks itself. The driver's own init writes the
 * three mode registers and stops there, so a sink left on by the previous
 * image survives into this one - it never touches LEDSET/LEDCLR.
 *
 * Needs CONFIG_LED=y. LED_NPM13XX is default y off the devicetree node, but
 * its Kconfig is sourced inside `if LED`, and LED itself is a bare menuconfig
 * with no default - so without it the driver silently is not built and
 * DEVICE_DT_GET resolves to a device that never initialises.
 *
 * If nothing lights, suspect the supply first: +3V3 reaches the anodes only
 * through jumper J3 (JP_3V3), and BUCK2 senses upstream of it - so every call
 * here returns 0 while the board stays dark. That is how the first board
 * failed.
 *
 * Do not try to confirm that from firmware. Nothing on the MCU or the PMIC ADC
 * can see BUCK2's output, and the fuel gauge cannot stand in: idle
 * SENSOR_CHAN_GAUGE_AVG_CURRENT readings jump in exact powers of two with
 * nothing changing, so an A/B around a lit LED reports a confident pass on a
 * dark board. LED verification is operator-visual only.
 *
 * The PPG emitter (LED1 in afe.kicad_sch) is the AFE-driven SFH7074, not one
 * of these.
 */

#include <leds.h>

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(leds, LOG_LEVEL_INF);

#define LEDS_NODE DT_NODELABEL(npm1300_leds)

/*
 * The application also builds for the nRF52840 DK, which has no such node.
 * Everything still compiles there; leds_init() reports the driver absent.
 */
#if DT_NODE_HAS_STATUS_OKAY(LEDS_NODE)
static const struct device *const leds = DEVICE_DT_GET(LEDS_NODE);
#else
static const struct device *const leds = NULL;
#endif

/* The part has exactly three sinks, and the driver rejects any index above. */
BUILD_ASSERT(LEDS_COUNT == 3U, "nPM1300 drives three LEDs");

static bool initialized;

static int set_channel(uint8_t channel, bool on)
{
	int err = on ? led_on(leds, channel) : led_off(leds, channel);

	switch (err) {
	case 0:
		return 0;
	case -EPERM:
		/* Not host mode: the PMIC owns this sink for its own charging
		 * or error indication, so it is not ours to drive. */
		return -ERR_LEDS_NOT_CONTROLLABLE;
	case -EINVAL:
		return -ERR_LEDS_INVALID_CHANNEL;
	default:
		LOG_ERR("channel %u %s failed (%d)", channel, on ? "on" : "off", err);
		return -ERR_LEDS_WRITE_FAILED;
	}
}

int leds_init(void)
{
	if ((leds == NULL) || !device_is_ready(leds)) {
		LOG_ERR("nPM1300 LED driver not ready");
		return -ERR_LEDS_NOT_READY;
	}

	for (uint8_t channel = 0U; channel < LEDS_COUNT; channel++) {
		int err = set_channel(channel, false);

		/*
		 * A sink the PMIC owns cannot be cleared and is not a failure
		 * to bring up - the PMIC is driving it deliberately. Every
		 * channel that is ours is off by the time this returns.
		 */
		if ((err != 0) && (err != -ERR_LEDS_NOT_CONTROLLABLE)) {
			return err;
		}
	}

	initialized = true;

	return 0;
}

int leds_set(uint8_t channel, bool on)
{
	if (!initialized) {
		return -ERR_LEDS_NOT_INITIALIZED;
	}

	if (channel >= LEDS_COUNT) {
		return -ERR_LEDS_INVALID_CHANNEL;
	}

	return set_channel(channel, on);
}

int leds_set_mask(uint8_t mask)
{
	int first_err = 0;

	if (!initialized) {
		return -ERR_LEDS_NOT_INITIALIZED;
	}

	if ((mask & ~LEDS_MASK_ALL) != 0U) {
		return -ERR_LEDS_INVALID_CHANNEL;
	}

	/*
	 * Keep writing after a failure: the mask sets the whole state, and
	 * stopping early would leave the untouched channels showing part of the
	 * previous pattern.
	 */
	for (uint8_t channel = 0U; channel < LEDS_COUNT; channel++) {
		int err = set_channel(channel, (mask & BIT(channel)) != 0U);

		if ((err != 0) && (first_err == 0)) {
			first_err = err;
		}
	}

	return first_err;
}
