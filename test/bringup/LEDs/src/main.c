// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * careloop LED exerciser.
 *
 * Drives D4, D3 and D2 - the nPM1300's LED0, LED1 and LED2 sinks - through a
 * repeating set of patterns, ending with a long solid phase that holds all
 * three on for LONG_ON_S seconds.
 *
 * This exists because the suite cannot see light. `bringup.careloop` verifies
 * that led_on() reaches the PMIC and stops there, which passes just as
 * happily on an unpopulated footprint, a part fitted backwards, or a rail
 * that is not there. Only a person looking at the board closes that loop, and
 * a 600 ms walk buried in a 6 second test run is easy to miss. Here every
 * phase is announced before it starts and the patterns are slow enough to
 * follow.
 *
 * It runs for about a minute and then stops with the LEDs off, rather than
 * looping forever: this is meant to be watched, and a board left blinking on
 * a bench after everyone has stopped looking only burns the cell. Cycles are
 * ~21.6 s, so a 60 s budget gets three whole ones and ends near 65 s. Reset
 * the board to run it again - the image stays flashed.
 *
 * The solid phase is the one to use with a meter: it is long enough to settle
 * on a reading and it counts down so you know how much of it is left.
 *
 * If nothing lights, suspect the supply before the LEDs. The anodes sit on
 * +3V3, which reaches them only through jumper J3 (JP_3V3) - and BUCK2 senses
 * its own output upstream of J3, so the PMIC reports the rail healthy whether
 * or not that jumper is bridged. That is exactly how the first board failed.
 *
 * Run it with:
 *   ./scripts/bringup.sh leds
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(leds, LOG_LEVEL_INF);

#define LED_COUNT 3U

/* How long all three stay on in the solid phase - the point of this app. */
#define LONG_ON_S 12U

#define WALK_MS       400U
#define BLINK_MS      150U
#define BLINK_REPEATS 6U
#define PHASE_GAP_MS  600U
#define CYCLE_GAP_S   3U

/*
 * Total run time. Whole cycles only - the check happens before a cycle
 * starts, so the last one always finishes rather than stopping mid-pattern.
 */
#define RUN_TIME_S 60U

/*
 * SEGGER RTT defaults to NO_BLOCK_SKIP: anything written before a viewer
 * attaches is discarded, not queued. `bringup.sh` resets the board as it
 * flashes and only then releases the probe and attaches the console, so this
 * has to outlast that gap - with a finite run there is no second chance to
 * catch the opening phase.
 */
#define RTT_ATTACH_GRACE_MS 3000U

static const struct device *const leds =
    DEVICE_DT_GET(DT_NODELABEL(npm1300_leds));

/* Channel index is the PMIC's LED number; the designator is on the silkscreen. */
static const char *const led_ref[LED_COUNT] = { "D4", "D3", "D2" };

/*
 * Report a failing channel once rather than on every pattern - a board with
 * one dead sink would otherwise bury the phase banners in repeats.
 */
static void set_led(uint32_t idx, bool on)
{
    static bool complained[LED_COUNT];

    int rc = on ? led_on(leds, idx) : led_off(leds, idx);

    if ((rc != 0) && !complained[idx]) {
        complained[idx] = true;
        LOG_ERR("led%u (%s) %s failed: %d%s", idx, led_ref[idx],
                on ? "on" : "off", rc,
                rc == -EPERM ? " - not in host mode" : "");
    }
}

static void all_leds(bool on)
{
    for (uint32_t i = 0U; i < LED_COUNT; i++) {
        set_led(i, on);
    }
}

/* One at a time, in silkscreen order - catches D3 and D2 wired the other way. */
static void phase_walk(void)
{
    printk("\n[walk] one at a time, %u ms each\n", WALK_MS);

    for (uint32_t i = 0U; i < LED_COUNT; i++) {
        printk("  led%u (%s)\n", i, led_ref[i]);
        set_led(i, true);
        k_sleep(K_MSEC(WALK_MS));
        set_led(i, false);
    }
}

/* Adds one LED at a time, so each step is visibly brighter than the last. */
static void phase_accumulate(void)
{
    printk("[fill] adding one at a time\n");

    for (uint32_t i = 0U; i < LED_COUNT; i++) {
        set_led(i, true);
        k_sleep(K_MSEC(WALK_MS));
    }

    k_sleep(K_MSEC(PHASE_GAP_MS));
    all_leds(false);
}

static void phase_blink(void)
{
    printk("[blink] all three, %u times\n", BLINK_REPEATS);

    for (uint32_t n = 0U; n < BLINK_REPEATS; n++) {
        all_leds(true);
        k_sleep(K_MSEC(BLINK_MS));
        all_leds(false);
        k_sleep(K_MSEC(BLINK_MS));
    }
}

/* The long one. Counts down so a meter reading has a known window. */
static void phase_solid(void)
{
    printk("[solid] all three ON for %u s - measure now\n", LONG_ON_S);

    all_leds(true);

    for (uint32_t left = LONG_ON_S; left > 0U; left--) {
        printk("  %u s remaining\n", left);
        k_sleep(K_SECONDS(1));
    }

    all_leds(false);
    printk("[solid] off\n");
}

int main(void)
{
    k_sleep(K_MSEC(RTT_ATTACH_GRACE_MS));

    printk("CARELOOP LED EXERCISER\n");

    if (!device_is_ready(leds)) {
        printk("RESULT: FAIL - nPM1300 LED driver not ready\n");
        printk("  the PMIC register interface is unreachable;"
               " run the bring-up suite for the I2C bus scan\n");
        return 0;
    }

    printk("driver ready, %u channels: %s=led0 %s=led1 %s=led2\n",
           LED_COUNT, led_ref[0], led_ref[1], led_ref[2]);
    printk("if nothing lights, check jumper J3 (JP_3V3) before the LEDs\n");

    printk("running for ~%u s\n", RUN_TIME_S);

    int64_t started = k_uptime_get();
    uint32_t cycle = 0U;

    /* Start a cycle only if the budget has not run out; never cut one short. */
    while ((k_uptime_get() - started) < (int64_t)(RUN_TIME_S * MSEC_PER_SEC)) {
        printk("\n=== cycle %u ===\n", cycle++);

        phase_walk();
        k_sleep(K_MSEC(PHASE_GAP_MS));

        phase_accumulate();
        k_sleep(K_MSEC(PHASE_GAP_MS));

        phase_blink();
        k_sleep(K_MSEC(PHASE_GAP_MS));

        phase_solid();
        k_sleep(K_SECONDS(CYCLE_GAP_S));
    }

    /* Belt and braces - every phase already ends dark, but never leave a
     * bench board drawing 10 mA because a pattern was edited badly.
     */
    all_leds(false);

    printk("\nDONE - %u cycles in %lld s, LEDs off. Reset the board to repeat.\n",
           cycle, (k_uptime_get() - started) / MSEC_PER_SEC);

    return 0;
}
