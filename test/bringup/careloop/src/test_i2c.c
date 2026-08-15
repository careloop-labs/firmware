// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Shared I2C bus and device presence.
 *
 * Runs before any driver-level test so that a bus fault is reported once,
 * as a bus fault, instead of as three unrelated sensor failures.
 *
 * Each device is probed repeatedly rather than once. A single probe would
 * have passed a board that was intermittently dropping the TMP117, which is
 * exactly the defect this suite exists to catch.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#define ADDR_TMP117  0x48U
#define ADDR_BMI270  0x68U
#define ADDR_NPM1300 0x6bU

#define PRESENCE_PROBES 64U

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

/* Zero-length write: addresses the device and checks only the ACK. */
static bool probe_once(uint8_t addr)
{
    uint8_t dummy = 0U;

    return i2c_write(i2c, &dummy, 0U, addr) == 0;
}

static void assert_present(uint8_t addr, const char *name)
{
    uint32_t fails = 0U;

    for (uint32_t i = 0U; i < PRESENCE_PROBES; i++) {
        if (!probe_once(addr)) {
            fails++;
        }
    }

    printk("  %-8s 0x%02x  %u/%u ACK\n", name, addr,
           PRESENCE_PROBES - fails, PRESENCE_PROBES);

    zassert_equal(fails, 0U, "%s at 0x%02x missed %u of %u probes",
                  name, addr, fails, PRESENCE_PROBES);
}

ZTEST(careloop_bringup, test_i2c_bus_ready)
{
    zassert_true(device_is_ready(i2c), "i2c0 not ready");
}

ZTEST(careloop_bringup, test_i2c_tmp117_present)
{
    assert_present(ADDR_TMP117, "tmp117");
}

ZTEST(careloop_bringup, test_i2c_bmi270_present)
{
    assert_present(ADDR_BMI270, "bmi270");
}

ZTEST(careloop_bringup, test_i2c_npm1300_present)
{
    assert_present(ADDR_NPM1300, "npm1300");
}
