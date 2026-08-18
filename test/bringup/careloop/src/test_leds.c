// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Indicator LEDs D4, D3 and D2 - nPM1300 sinks LED0, LED1 and LED2.
 *
 * Read what these tests are worth before trusting a green run. led_on()
 * returning 0 proves that a register reached the PMIC and nothing more. It
 * says nothing about light, and it passes just as happily with an unpopulated
 * footprint, a part fitted backwards, or no supply on the anodes at all.
 *
 * That is not theoretical. The first board these ran against passed every
 * check here while all three LEDs stayed dark: +3V3 reaches the anodes only
 * through jumper J3 (JP_3V3), and J3 was open. The PMIC did not notice either,
 * because BUCK2 senses its output at VOUT2 on the upstream side of J3 - the
 * regulator reported itself enabled and healthy while the rail behind it was
 * disconnected. Bridging J3 fixed it.
 *
 * So the walk below is for the operator, not for ztest. It lights one LED at
 * a time in silkscreen order and someone has to be looking at the board. It is
 * also the only way to catch D3 and D2 swapped.
 *
 * Do not try to close the loop with the nPM1300 fuel gauge. Two attempts died
 * here. SENSOR_CHAN_GAUGE_AVG_CURRENT decodes the raw ADC code against a
 * full-scale chosen from the charger's ibat_stat, that status flaps between
 * consecutive samples, and the decoded idle current therefore jumps in exact
 * powers of two - measured across one session with nothing changing on the
 * board: -656, -3503, -7006, -14013, -28027 uA. Averaging idle readings either
 * side of the load does not rescue it; it turns the drift into a delta and
 * reports a confident pass on a board whose LEDs are dark. The driver keeps
 * ibat_stat private, so gating on it means duplicating driver internals.
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>

#define LED_COUNT 3U

/* Long enough for an eye, short enough not to pad the suite. */
#define LED_DWELL_MS 200U

static const struct device *const leds =
    DEVICE_DT_GET(DT_NODELABEL(npm1300_leds));

/* Channel index is the PMIC's LED number; the designator is what is silkscreened. */
static const char *const led_ref[LED_COUNT] = { "D4", "D3", "D2" };

ZTEST_SUITE(careloop_7_leds, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_7_leds, test_leds_ready)
{
    zassert_true(device_is_ready(leds),
                 "nPM1300 LED driver not ready - init writes all three MODE "
                 "registers, so this fails with the PMIC register interface");
}

ZTEST(careloop_7_leds, test_leds_host_control)
{
    int rc;

    for (uint32_t i = 0U; i < LED_COUNT; i++) {
        rc = led_on(leds, i);
        zassert_ok(rc, "led_on(%u) for %s failed (%d)%s", i, led_ref[i], rc,
                   rc == -EPERM ? " - this LED is not in host mode" : "");

        printk("  led%u (%s) on\n", i, led_ref[i]);
        k_sleep(K_MSEC(LED_DWELL_MS));

        rc = led_off(leds, i);
        zassert_ok(rc, "led_off(%u) for %s failed (%d)", i, led_ref[i], rc);
    }

    /* The PMIC has exactly three sinks; a fourth would mean the wrong part. */
    zassert_equal(led_on(leds, LED_COUNT), -EINVAL,
                  "led_on(%u) was accepted on a three-channel controller",
                  LED_COUNT);
}
