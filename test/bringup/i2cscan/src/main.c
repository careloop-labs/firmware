// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * careloop I2C bus scan.
 *
 * Answers what the schematic cannot: which pins the shared I2C bus is
 * actually on. mcu.kicad_sch draws a QFN-48 symbol but the fitted part is
 * aQFN-73, so its pin names cannot be trusted and the candidates have to be
 * tried against real silicon. A correct pair finds all three known devices;
 * a wrong one finds nothing, because the bus floats and every address NAKs.
 *
 * Build once per candidate, e.g.
 *   west build --no-sysbuild -b careloop -d build/a -p always . \
 *       -- -DEXTRA_DTC_OVERLAY_FILE=$PWD/boards/pins_p102_p104.overlay
 *
 * This proves wiring, not function: an ACK only shows something answers at
 * that address. Identity is the self-test's job.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2cscan, LOG_LEVEL_INF);

/* Devices the schematic says are on this bus, whatever the pins turn out to be. */
#define ADDR_TMP117  0x48U
#define ADDR_BMI270  0x68U
#define ADDR_NPM1300 0x6bU

/*
 * Valid 7-bit range. 0x00-0x07 and 0x78-0x7f are reserved by the I2C spec
 * and must not be probed - some parts misbehave when addressed there.
 */
#define ADDR_FIRST 0x08U
#define ADDR_LAST  0x77U

#define RTT_ATTACH_GRACE_MS 500U
#define RESCAN_PERIOD_S     5U

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static const struct {
    uint8_t     addr;
    const char *name;
} expected[] = {
    { ADDR_TMP117,  "TMP117"  },
    { ADDR_BMI270,  "BMI270"  },
    { ADDR_NPM1300, "nPM1300" },
};

/*
 * Probe one address with a zero-length write. This is the standard scan
 * transaction: it addresses the device and looks only at the ACK, without
 * touching any register, so it cannot disturb a part that is already
 * configured.
 */
static bool probe(uint8_t addr)
{
    uint8_t dummy = 0U;

    return i2c_write(i2c, &dummy, 0U, addr) == 0;
}

static uint32_t scan_once(void)
{
    uint32_t found = 0U;

    printk("scanning 0x%02x-0x%02x\n", ADDR_FIRST, ADDR_LAST);

    for (uint8_t addr = ADDR_FIRST; addr <= ADDR_LAST; addr++) {
        if (!probe(addr)) {
            continue;
        }

        found++;

        const char *name = "unexpected";

        for (size_t i = 0U; i < ARRAY_SIZE(expected); i++) {
            if (expected[i].addr == addr) {
                name = expected[i].name;
                break;
            }
        }

        printk("  ACK 0x%02x  %s\n", addr, name);
    }

    for (size_t i = 0U; i < ARRAY_SIZE(expected); i++) {
        if (!probe(expected[i].addr)) {
            printk("  MISSING 0x%02x  %s\n", expected[i].addr, expected[i].name);
        }
    }

    return found;
}

int main(void)
{
    /*
     * SEGGER RTT defaults to NO_BLOCK_SKIP: anything written before a
     * viewer attaches is discarded, not queued.
     */
    k_sleep(K_MSEC(RTT_ATTACH_GRACE_MS));

    printk("CARELOOP I2C SCAN\n");

    if (!device_is_ready(i2c)) {
        printk("RESULT: FAIL - i2c0 not ready\n");
        return 0;
    }

    /*
     * Rescan forever rather than reporting once: it lets a probe attach
     * late, and it distinguishes a stable bus from one that only answers
     * intermittently, which is what a marginal pull-up looks like.
     */
    for (;;) {
        uint32_t found = scan_once();

        printk("RESULT: %s - %u device(s)\n", found > 0U ? "devices found" : "EMPTY BUS", found);
        k_sleep(K_SECONDS(RESCAN_PERIOD_S));
    }

    return 0;
}
