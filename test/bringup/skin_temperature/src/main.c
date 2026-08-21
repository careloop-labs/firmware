// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * careloop skin temperature streamer.
 *
 * Prints one line per conversion from the TMP117 (U3), forever, through
 * hal/skin_temperature.h.
 *
 * Run it with:
 *   ./scripts/bringup.sh skin_temperature
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <skin_temperature.h>

/* Not "skin_temperature": src/hal/skin_temperature_tmp117.c owns that name. */
LOG_MODULE_REGISTER(temp_app, LOG_LEVEL_INF);

/*
 * SEGGER RTT defaults to NO_BLOCK_SKIP: anything written before a viewer
 * attaches is discarded, not queued. bringup.sh resets the board as it flashes
 * and only then releases the probe and attaches the console, so the banner has
 * to outlast that gap.
 */
#define RTT_ATTACH_GRACE_MS 3000U

/* Poll this many times per conversion period. */
#define POLLS_PER_PERIOD 20U

/* Give up on a conversion after this many periods and say so. */
#define STALL_PERIODS 3U

/* How often to repeat the column header, in samples. */
#define HEADER_EVERY 20U

static void print_sample(uint32_t index, int32_t millidegc, uint32_t waited_ms,
                         int32_t first_millidegc)
{
    int32_t delta = millidegc - first_millidegc;

    printk("%6u  %4d.%03d C  %+6d.%03d  %5u ms\n", index, millidegc / 1000,
           (millidegc < 0 ? -millidegc : millidegc) % 1000, delta / 1000,
           (delta < 0 ? -delta : delta) % 1000, waited_ms);
}

int main(void)
{
    uint32_t period_ms;
    uint32_t poll_ms;
    uint32_t stall_budget_ms;
    uint32_t index = 0U;
    int32_t first_millidegc = 0;
    bool have_first = false;
    int rc;

    k_sleep(K_MSEC(RTT_ATTACH_GRACE_MS));

    printk("CARELOOP SKIN TEMPERATURE STREAM\n");

    rc = skin_temperature_init();
    if (rc != 0) {
        printk("RESULT: FAIL - skin_temperature_init() returned %d\n", rc);
        printk("  U3 did not answer at 0x48, or the tmp11x driver rejected its "
               "device ID\n");
        printk("  run ./scripts/bringup.sh suite for the I2C bus scan\n");
        return 0;
    }

    rc = skin_temperature_get_conversion_period_ms(&period_ms);
    if (rc != 0) {
        printk("RESULT: FAIL - conversion period unavailable (%d)\n", rc);
        return 0;
    }

    /*
     * Everything below paces off the period the HAL reports, never off a
     * literal. Change odr or oversampling in careloop.dts and this app follows
     * without being edited - which is the whole reason the period is a runtime
     * question rather than a constant.
     */
    poll_ms = period_ms / POLLS_PER_PERIOD;
    if (poll_ms == 0U) {
        poll_ms = 1U;
    }
    stall_budget_ms = period_ms * STALL_PERIODS;

    printk("conversion period %u ms, polling every %u ms\n", period_ms, poll_ms);
    printk("STREAMING - reset the board to stop\n\n");

    for (;;) {
        int32_t millidegc = 0;
        uint32_t waited = 0U;

        /*
         * NOT_READY is the normal answer between conversions, not a fault. The
         * HAL deliberately reports it rather than repeating the previous
         * sample, because a duplicate is indistinguishable from a stalled
         * sensor once it is in a log.
         */
        do {
            rc = skin_temperature_read(&millidegc);
            if (rc != -ERR_SKIN_TEMPERATURE_NOT_READY) {
                break;
            }

            k_sleep(K_MSEC(poll_ms));
            waited += poll_ms;
        } while (waited < stall_budget_ms);

        if (rc == -ERR_SKIN_TEMPERATURE_NOT_READY) {
            LOG_ERR("no conversion in %u ms (%u periods) - DATA_READY never set",
                    stall_budget_ms, STALL_PERIODS);
            continue;
        }

        if (rc != 0) {
            LOG_ERR("read failed: %d", rc);
            k_sleep(K_MSEC(period_ms));
            continue;
        }

        if (!have_first) {
            first_millidegc = millidegc;
            have_first = true;
        }

        if ((index % HEADER_EVERY) == 0U) {
            printk("%6s  %10s  %10s  %8s\n", "n", "temp", "delta", "waited");
        }

        print_sample(index, millidegc, waited, first_millidegc);
        index++;
    }

    return 0;
}
