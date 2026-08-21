// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/leds.h against the nPM1300's constant-current sinks (D4, D3, D2).
 *
 * Read this before trusting a green run, because most of what you want from an
 * LED test is not here and cannot be.
 *
 * Nothing in this file can see light. leds_set() returning 0 proves a register
 * reached U2 and nothing more. The anodes sit on +3V3, which reaches them only
 * through jumper J3 (JP_3V3), and BUCK2 senses its own output upstream of J3 -
 * so the PMIC reports the rail healthy whether or not that jumper is bridged.
 * That is not theoretical: the first board these ran against passed every check
 * here with all three LEDs dark, and bridging J3 fixed it.
 *
 * Do not try to close the loop with the fuel gauge. Two attempts died there.
 * SENSOR_CHAN_GAUGE_AVG_CURRENT decodes the raw IBAT code against a full scale
 * chosen from the charger's private ibat_stat, that status flaps between
 * consecutive samples, and the decoded idle current therefore jumps in exact
 * powers of two - measured across one session with nothing changing on the
 * board: -656, -3503, -7006, -14013, -28027 uA. Averaging idle readings either
 * side of a lit LED does not rescue it; it turns the drift into a delta and
 * reports a confident pass on a dark board.
 *
 * What this suite does not prove:
 *   - that any light is emitted;
 *   - that channel n is the LED silkscreened D4/D3/D2 - swapped D3 and D2 pass;
 *   - that leds_set_mask() maps bit n to channel n, which is the only real
 *     logic in src/hal/leds_npm1300.c and is checked by nothing, anywhere;
 *   - that J3 is bridged, or +3V3 is present at the anodes;
 *   - that a part is populated, or fitted the right way round.
 *
 * All of that needs a person looking at the board:
 *   ./scripts/bringup.sh LEDs
 * which drives these same HAL entry points slowly enough to follow. Test 60
 * below is a deliberate skip so that gap appears in the Twister report rather
 * than only in this comment.
 *
 * Runs last of the HAL suites, and leaves every sink off.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>

#include <leds.h>

/* Long enough for an eye, short enough not to pad the suite. */
#define LED_DWELL_MS 150U

/* Channel index is the PMIC's LED number; the designator is what is silkscreened. */
static const char *const led_ref[LEDS_COUNT] = { "D4", "D3", "D2" };

ZTEST_SUITE(careloop_hal_4_leds, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_hal_4_leds, test_hal_leds_10_init)
{
    /*
     * Also proves the sinks were actively cleared. The led_npm13xx driver's own
     * init writes the three mode registers and stops - it never touches
     * LEDSET/LEDCLR - so a sink left on by the previous image survives a warm
     * reset unless leds_init() clears it.
     */
    zassert_ok(leds_init(),
               "leds_init() failed - U2's LED register interface is "
               "unreachable. careloop_3_i2c's bus scan is the place to look");
}

ZTEST(careloop_hal_4_leds, test_hal_leds_20_bounds_and_geometry)
{
    /*
     * The only assertions in this file with real teeth: pure API contract, no
     * hardware dependency, and they fail if the board definition changes under
     * the HAL rather than silently driving the wrong number of sinks.
     */
    zassert_equal(LEDS_COUNT, 3U,
                  "the nPM1300 has exactly three sinks; LEDS_COUNT is %u",
                  LEDS_COUNT);
    zassert_equal(LEDS_MASK_ALL, 0x07U,
                  "LEDS_MASK_ALL must cover exactly LEDS_COUNT channels");

    zassert_equal(leds_set(LEDS_COUNT, true), -ERR_LEDS_INVALID_CHANNEL,
                  "channel %u was accepted on a three-channel controller",
                  LEDS_COUNT);
    zassert_equal(leds_set(255U, true), -ERR_LEDS_INVALID_CHANNEL,
                  "channel 255 was accepted");

    zassert_equal(leds_set_mask(LEDS_MASK_ALL | BIT(LEDS_COUNT)),
                  -ERR_LEDS_INVALID_CHANNEL,
                  "a mask with a bit above LEDS_COUNT was accepted");
    zassert_equal(leds_set_mask(0xFFU), -ERR_LEDS_INVALID_CHANNEL,
                  "an all-ones mask was accepted");
}

ZTEST(careloop_hal_4_leds, test_hal_leds_30_walk)
{
    /* For the operator, not for ztest - and only if someone is looking. */
    for (uint8_t ch = 0U; ch < LEDS_COUNT; ch++) {
        int rc = leds_set(ch, true);

        zassert_ok(rc, "leds_set(%u, true) for %s failed (%d)%s", ch,
                   led_ref[ch], rc,
                   rc == -ERR_LEDS_NOT_CONTROLLABLE
                       ? " - sink not in host mode, check nordic,ledN-mode in "
                         "careloop.dts"
                       : "");

        printk("  led%u (%s) on\n", ch, led_ref[ch]);
        k_sleep(K_MSEC(LED_DWELL_MS));

        zassert_ok(leds_set(ch, false), "leds_set(%u, false) for %s failed", ch,
                   led_ref[ch]);
    }
}

ZTEST(careloop_hal_4_leds, test_hal_leds_40_mask_walk)
{
    static const uint8_t patterns[] = { 0x01U, 0x02U, 0x04U, 0x07U, 0x00U };

    /*
     * leds_set_mask()'s bit-to-channel loop is the only real logic in the HAL's
     * LED path, and nothing here can check it: a mask that walks the LEDs
     * backwards passes every assertion below. Only the operator running
     * ./scripts/bringup.sh LEDs can tell.
     */
    for (size_t i = 0U; i < ARRAY_SIZE(patterns); i++) {
        zassert_ok(leds_set_mask(patterns[i]),
                   "leds_set_mask(0x%02x) failed", patterns[i]);

        printk("  mask 0x%02x\n", patterns[i]);
        k_sleep(K_MSEC(LED_DWELL_MS));
    }
}

ZTEST(careloop_hal_4_leds, test_hal_leds_50_mask_is_whole_state)
{
    /*
     * A clear bit means off, not "leave alone". Return codes only - whether
     * channels 1 and 2 actually went dark is unobservable from here.
     */
    zassert_ok(leds_set_mask(LEDS_MASK_ALL), "leds_set_mask(ALL) failed");
    k_sleep(K_MSEC(LED_DWELL_MS));

    zassert_ok(leds_set_mask(BIT(0)), "leds_set_mask(BIT(0)) failed");
    k_sleep(K_MSEC(LED_DWELL_MS));

    zassert_ok(leds_set_mask(0U), "leds_set_mask(0) failed");
}

ZTEST(careloop_hal_4_leds, test_hal_leds_60_visual_check_is_manual)
{
    printk("  no firmware check can see light on this board\n");
    printk("  run ./scripts/bringup.sh LEDs and watch D4, D3, D2\n");
    printk("  if nothing lights, check jumper J3 (JP_3V3) before the LEDs\n");

    /*
     * Skipped rather than passed, so the gap shows up in the Twister report
     * where somebody reading a green run will see it.
     */
    ztest_test_skip();
}

ZTEST(careloop_hal_4_leds, test_hal_leds_70_leaves_all_off)
{
    zassert_ok(leds_set_mask(0U),
               "failed to turn the LEDs off at the end of the suite");

    printk("  all sinks off\n");
}
