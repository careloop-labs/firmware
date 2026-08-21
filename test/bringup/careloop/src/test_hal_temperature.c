// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/skin_temperature.h against the TMP117 (U3).
 *
 * Layers on careloop_4_temperature, which proves U3 answers and the tmp11x
 * driver accepted its device ID. This suite proves the shipping wrapper
 * converts and paces correctly. A failure here with careloop_4_temperature
 * green means src/hal/skin_temperature_tmp117.c, not the part.
 *
 * The timing is the whole difficulty. skin_temperature_read() returns
 * ERR_SKIN_TEMPERATURE_NOT_READY until a conversion completes, and the
 * configured cycle is 1000 ms - odr 0x200 and oversampling 0x20 in
 * careloop.dts, which the tmp11x driver writes at init rather than leaving the
 * part at its power-on defaults. The whole board suite runs in well under that,
 * so a test that inits and immediately reads *will* fail. Everything below
 * paces off skin_temperature_get_conversion_period_ms() and never off a
 * literal, which is the same discipline the header asks of callers.
 *
 * Reading also clears DATA_READY in the sensor, so a second read inside one
 * period reports not-ready rather than handing back the same conversion twice.
 * That is the header's "fails rather than repeating" contract, and it is
 * enforced by hardware rather than by bookkeeping in the HAL - test 40 pins it.
 *
 * What this does not prove: absolute accuracy. A TMP117 stuck at exactly
 * 25.000 C passes every assertion here. The bench range only catches a part
 * that is grossly wrong, disconnected, or reading a different register.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <skin_temperature.h>

/* Room-temperature bench limits, not product limits. */
#define TEMP_MIN_MC 5000
#define TEMP_MAX_MC 60000

/* What careloop.dts asks for: odr 0x200 (1000 ms), oversampling 0x20 (8). */
#define TEMP_EXPECTED_PERIOD_MS 1000U

/* The TMP117 conversion-cycle table spans 15.5 ms to 16 s; 15.5 rounds up. */
#define TEMP_PERIOD_FLOOR_MS 16U
#define TEMP_PERIOD_CEILING_MS 16000U

/* Slack on top of two full periods, to absorb polling granularity. */
#define TEMP_WAIT_SLACK_MS 200U

static uint32_t conversion_period_ms;

/*
 * Poll until a conversion lands. Every return before success must be exactly
 * NOT_READY - anything else is a real fault and is surfaced rather than spun
 * on, because a read that keeps failing for a different reason would otherwise
 * look identical to a slow sensor.
 */
static int read_when_ready(int32_t *millidegc, uint32_t *waited_ms)
{
    uint32_t poll_ms = conversion_period_ms / 20U;
    uint32_t budget_ms = (conversion_period_ms * 2U) + TEMP_WAIT_SLACK_MS;
    uint32_t waited = 0U;
    int rc;

    if (poll_ms == 0U) {
        poll_ms = 1U;
    }

    for (;;) {
        rc = skin_temperature_read(millidegc);
        if (rc != -ERR_SKIN_TEMPERATURE_NOT_READY) {
            break;
        }

        if (waited >= budget_ms) {
            break;
        }

        k_sleep(K_MSEC(poll_ms));
        waited += poll_ms;
    }

    if (waited_ms != NULL) {
        *waited_ms = waited;
    }

    return rc;
}

ZTEST_SUITE(careloop_hal_1_temperature, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_hal_1_temperature, test_hal_skin_temp_10_init)
{
    /*
     * Unlike power_init() this does not probe with a read, and must not: at
     * 1000 ms the first conversion has not completed by the time boot reaches
     * here, so a probe would return not-ready and report a healthy U3 as
     * broken. device_is_ready() is already the identity check.
     */
    zassert_ok(skin_temperature_init(),
               "skin_temperature_init() failed - U3 did not answer at 0x48, or "
               "the tmp11x driver rejected its device ID");
}

ZTEST(careloop_hal_1_temperature, test_hal_skin_temp_20_period_reported)
{
    zassert_ok(skin_temperature_get_conversion_period_ms(&conversion_period_ms),
               "the conversion period must be answerable once initialised - "
               "callers pace themselves off it");

    printk("  conversion period %u ms\n", conversion_period_ms);

    zassert_between_inclusive(conversion_period_ms, TEMP_PERIOD_FLOOR_MS,
                              TEMP_PERIOD_CEILING_MS,
                              "period %u ms is outside the TMP117 conversion "
                              "cycle table entirely", conversion_period_ms);

    zassert_equal(conversion_period_ms, TEMP_EXPECTED_PERIOD_MS,
                  "expected %u ms from odr 0x200 and oversampling 0x20 in "
                  "careloop.dts but got %u ms - if the dts changed, this "
                  "expectation changes with it",
                  TEMP_EXPECTED_PERIOD_MS, conversion_period_ms);
}

ZTEST(careloop_hal_1_temperature, test_hal_skin_temp_30_first_read_paces)
{
    int32_t millidegc = 0;
    uint32_t waited_ms = 0U;
    int rc;

    zassert_not_equal(conversion_period_ms, 0U,
                      "test 20 must run first - it reads the period this one "
                      "paces off");

    rc = read_when_ready(&millidegc, &waited_ms);

    zassert_ok(rc, "no conversion completed within two periods (%u ms); rc %d. "
                   "NOT_READY here means DATA_READY never set on U3",
               (conversion_period_ms * 2U) + TEMP_WAIT_SLACK_MS, rc);

    printk("  first sample %d.%03d C after %u ms\n", millidegc / 1000,
           (millidegc < 0 ? -millidegc : millidegc) % 1000, waited_ms);

    zassert_between_inclusive(millidegc, TEMP_MIN_MC, TEMP_MAX_MC,
                              "skin temperature %d mC outside bench range",
                              millidegc);
}

ZTEST(careloop_hal_1_temperature, test_hal_skin_temp_40_second_read_not_ready)
{
    int32_t millidegc = 0;
    int32_t sentinel = (int32_t)0xA5A5A5A5;
    int rc;

    /* Consume a fresh conversion so the immediate re-read is inside a period. */
    zassert_ok(read_when_ready(&millidegc, NULL),
               "could not acquire a sample to start from");

    rc = skin_temperature_read(&sentinel);

    zassert_equal(rc, -ERR_SKIN_TEMPERATURE_NOT_READY,
                  "a second read inside one conversion period must fail rather "
                  "than repeat the previous value - a duplicated sample is "
                  "indistinguishable from a stalled sensor once logged (rc %d)",
                  rc);

    zassert_equal(sentinel, (int32_t)0xA5A5A5A5,
                  "the destination was written on a failed read");
}

ZTEST(careloop_hal_1_temperature, test_hal_skin_temp_50_read_rejects_null)
{
    /*
     * Post-init, so this pins the argument check and not the init check. The
     * uninitialised precedence is covered in careloop_hal_0_uninit.
     */
    zassert_equal(skin_temperature_read(NULL),
                  -ERR_SKIN_TEMPERATURE_READ_FAILED,
                  "a NULL destination is a read failure once initialised");
}

ZTEST(careloop_hal_1_temperature, test_hal_skin_temp_60_three_samples)
{
    int32_t samples[3] = { 0 };

    for (size_t i = 0U; i < ARRAY_SIZE(samples); i++) {
        zassert_ok(read_when_ready(&samples[i], NULL),
                   "sample %u did not arrive within two periods", i);

        zassert_between_inclusive(samples[i], TEMP_MIN_MC, TEMP_MAX_MC,
                                  "sample %u = %d mC outside bench range", i,
                                  samples[i]);
    }

    printk("  three samples %d, %d, %d mC\n", samples[0], samples[1],
           samples[2]);

    /*
     * Range only, deliberately. The spread is printed but not asserted: at
     * 7.8125 mC resolution a still sensor on a still bench may legitimately
     * report the same code three times, so requiring movement would be flaky.
     * This case therefore cannot prove liveness - only that repeated reads keep
     * working and stay plausible.
     */
}
