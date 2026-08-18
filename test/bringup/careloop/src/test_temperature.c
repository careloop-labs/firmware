// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * TMP117 skin temperature sensor (U3).
 *
 * device_is_ready() is already a real identity check here: the tmp11x
 * driver reads the device-ID register during init and refuses anything that
 * is not a TMP11x, so a part that answers on the bus but is the wrong chip
 * fails before this test runs.
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

/* Room-temperature bench limits, not product limits. */
#define TEMP_MIN_MC 5000
#define TEMP_MAX_MC 60000

/*
 * The TMP117 powers up in continuous mode with 8-sample averaging, giving a
 * conversion cycle of about a second, and sample_fetch returns -EBUSY until
 * DATA_READY sets. The whole suite runs in under 100 ms, so the first fetch
 * after boot is always too early - poll rather than assume.
 */
#define TEMP_READY_TIMEOUT_MS 2000U
#define TEMP_POLL_MS          25U

static const struct device *const tmp117 = DEVICE_DT_GET(DT_NODELABEL(tmp117));

ZTEST_SUITE(careloop_4_temperature, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_4_temperature, test_temperature_ready)
{
    zassert_true(device_is_ready(tmp117),
                 "TMP117 not ready - driver rejected the device ID, or the "
                 "part did not answer at 0x48");
}

ZTEST(careloop_4_temperature, test_temperature_reading_sane)
{
    struct sensor_value val;
    int32_t mc;

    int rc = -EBUSY;

    for (uint32_t waited = 0U; waited < TEMP_READY_TIMEOUT_MS;
         waited += TEMP_POLL_MS) {
        rc = sensor_sample_fetch(tmp117);
        if (rc != -EBUSY) {
            break;
        }
        k_sleep(K_MSEC(TEMP_POLL_MS));
    }

    zassert_ok(rc, "TMP117 sample fetch failed (%d) after %u ms; -EBUSY means "
                   "DATA_READY never set", rc, TEMP_READY_TIMEOUT_MS);
    zassert_ok(sensor_channel_get(tmp117, SENSOR_CHAN_AMBIENT_TEMP, &val),
               "TMP117 channel get failed");

    mc = sensor_value_to_milli(&val);
    printk("  temperature %d.%03d C\n", mc / 1000, (mc < 0 ? -mc : mc) % 1000);

    zassert_between_inclusive(mc, TEMP_MIN_MC, TEMP_MAX_MC,
                              "temperature %d mC outside bench range", mc);
}
