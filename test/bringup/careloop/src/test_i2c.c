// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Shared I2C bus and device presence.
 *
 * Runs before any driver-level suite so that a bus fault is reported once,
 * as a bus fault, instead of as three unrelated sensor failures. The `3` in
 * the suite name is what enforces that - suites run in alphabetical order, not
 * link order - so it has to stay below the sensor suites' digits. See main.c.
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

/*
 * Valid 7-bit range for the sweep. 0x00-0x07 and 0x78-0x7f are reserved by
 * the I2C spec and must not be probed - some parts misbehave when addressed
 * there.
 */
#define ADDR_FIRST 0x08U
#define ADDR_LAST  0x77U

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static const struct {
    uint8_t     addr;
    const char *name;
} expected[] = {
    { ADDR_TMP117,  "tmp117"  },
    { ADDR_BMI270,  "bmi270"  },
    { ADDR_NPM1300, "npm1300" },
};

ZTEST_SUITE(careloop_3_i2c, NULL, NULL, NULL, NULL, NULL);

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

ZTEST(careloop_3_i2c, test_i2c_bus_ready)
{
    zassert_true(device_is_ready(i2c), "i2c0 not ready");
}

ZTEST(careloop_3_i2c, test_i2c_tmp117_present)
{
    assert_present(ADDR_TMP117, "tmp117");
}

ZTEST(careloop_3_i2c, test_i2c_bmi270_present)
{
    assert_present(ADDR_BMI270, "bmi270");
}

ZTEST(careloop_3_i2c, test_i2c_npm1300_present)
{
    assert_present(ADDR_NPM1300, "npm1300");
}

/*
 * Full sweep of the address space.
 *
 * The three tests above ask "is the part I expect still answering?"; this one
 * asks "what else is out there?" and is the only check that can see a device
 * nobody declared - a stray part, a wrong stuff option, or a footprint fitted
 * with something other than the BOM says.
 *
 * It asserts almost nothing on purpose. Presence of the three known devices is
 * already covered above with 64 probes each, which is the stronger test; a
 * second single-probe assertion here would only add a flaky duplicate. The
 * count check exists so that a completely floating bus - every address NAKing,
 * which is what a wrong pin assignment looks like - still fails loudly rather
 * than printing an empty map and passing.
 *
 * This replaces the standalone test/bringup/i2cscan app, whose other job was
 * finding which pins the bus is on. That is settled: SDA P1.02, SCL P1.04, in
 * boards/careloop/careloop_nrf52840_2_0_0.overlay.
 */
ZTEST(careloop_3_i2c, test_i2c_bus_scan)
{
    uint32_t found = 0U;

    printk("  scanning 0x%02x-0x%02x\n", ADDR_FIRST, ADDR_LAST);

    for (uint8_t addr = ADDR_FIRST; addr <= ADDR_LAST; addr++) {
        const char *name = "UNEXPECTED";

        if (!probe_once(addr)) {
            continue;
        }

        found++;

        for (size_t i = 0U; i < ARRAY_SIZE(expected); i++) {
            if (expected[i].addr == addr) {
                name = expected[i].name;
                break;
            }
        }

        printk("    ACK 0x%02x  %s\n", addr, name);
    }

    printk("  %u device(s) on the bus\n", found);

    zassert_true(found >= ARRAY_SIZE(expected),
                 "only %u device(s) ACKed across 0x%02x-0x%02x, expected at "
                 "least %u - a bus where nothing answers is what a wrong pin "
                 "assignment or a missing pull-up looks like",
                 found, ADDR_FIRST, ADDR_LAST, (unsigned int)ARRAY_SIZE(expected));
}
