// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/power.h against the nPM1300 (U2).
 *
 * Layers on careloop_6_power, which proves the PMIC register interface answers.
 * This suite proves the shipping wrapper decodes it correctly - the charge
 * state machine, the latched fault bits and the coherence of one snapshot. A
 * failure here with careloop_6_power green means src/hal/power_npm1300.c.
 *
 * Jig requirement, same as careloop_6_power: a cell must be fitted or VBAT
 * driven from a supply. A board powered only over USB with no cell reads near
 * zero and fails tests 20 and 50 correctly but confusingly.
 *
 * Note what power_init() can and cannot fail on. The MCU is only executing
 * because BUCK1 is up, so it cannot fail "because the PMIC is dead" - it fails
 * when the I2C register interface is unreachable.
 *
 * current_milliamp is never range-asserted here, and that is deliberate. The
 * driver decodes the raw IBAT code against a full scale chosen from the
 * charger's private ibat_stat, which flaps between consecutive samples: 62.5 mA
 * charging against 224 mA discharging, a 3.6x step decided by a status bit
 * alone. Measured on this board at idle, consecutive samples read -656, -3503,
 * -7006, -14013 and -28027 uA - exact doublings, with nothing changing. An idle
 * ibat_stat decodes against a full scale of 0, so the channel can also read
 * exactly 0 under real load. Any threshold here would be a coin flip.
 *
 * What this does not prove: the POWER_FAULT_* decode mapping. No thermistor is
 * fitted and there is no way to provoke a charge timeout or a die-temperature
 * pause on a healthy board, so only "no undecoded bits" and latch monotonicity
 * are testable. Nor does one run cover the charge state enum - the jig sits in
 * whichever state it sits in. Covering TRICKLE/CC/CV needs a deeply discharged
 * cell plus USB; COMPLETE needs a full one.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <power.h>

/* Bench limits for a single-cell Li-ion, not product thresholds. */
#define VBAT_MIN_MV 3000
#define VBAT_MAX_MV 4400

#define DIE_TEMP_MIN_MC (-10000)
#define DIE_TEMP_MAX_MC 85000

/* Every fault bit hal/power.h defines. Anything outside means the decode drifted. */
#define POWER_FAULT_KNOWN                                                     \
    (POWER_FAULT_BATTERY_TEMPERATURE | POWER_FAULT_BATTERY_VOLTAGE |          \
     POWER_FAULT_CHARGE_TIMEOUT | POWER_FAULT_MEASUREMENT |                   \
     POWER_FAULT_OVER_TEMPERATURE)

#define POWER_SETTLE_MS 50U

static const char *charge_state_name(enum power_charge_state state)
{
    switch (state) {
    case POWER_CHARGE_STATE_NONE:
        return "none";
    case POWER_CHARGE_STATE_TRICKLE:
        return "trickle";
    case POWER_CHARGE_STATE_CONSTANT_CURRENT:
        return "constant current";
    case POWER_CHARGE_STATE_CONSTANT_VOLTAGE:
        return "constant voltage";
    case POWER_CHARGE_STATE_COMPLETE:
        return "complete";
    default:
        return "unknown";
    }
}

ZTEST_SUITE(careloop_hal_3_power, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_hal_3_power, test_hal_power_10_init)
{
    zassert_ok(power_init(),
               "power_init() failed - U2's register interface is unreachable. "
               "This cannot mean the PMIC is dead: the MCU only runs because "
               "BUCK1 is up");
}

ZTEST(careloop_hal_3_power, test_hal_power_20_snapshot_sane)
{
    struct power_status status;

    zassert_ok(power_read(&status), "power_read() failed");

    printk("  battery %d mV, current %d mA, charger %d.%03d C\n",
           status.battery_millivolt, status.current_milliamp,
           status.charger_millidegc / 1000,
           (status.charger_millidegc < 0 ? -status.charger_millidegc
                                         : status.charger_millidegc) % 1000);
    printk("  charge state %s, faults 0x%08x, external %s, battery %s\n",
           charge_state_name(status.charge_state), status.fault_flags,
           status.external_power_present ? "present" : "absent",
           status.battery_detected ? "detected" : "absent");

    zassert_between_inclusive(status.battery_millivolt, VBAT_MIN_MV, VBAT_MAX_MV,
                              "battery %d mV outside %d..%d - is a cell fitted "
                              "or VBAT driven?", status.battery_millivolt,
                              VBAT_MIN_MV, VBAT_MAX_MV);

    zassert_between_inclusive(status.charger_millidegc, DIE_TEMP_MIN_MC,
                              DIE_TEMP_MAX_MC,
                              "U2 die temperature %d mC implausible",
                              status.charger_millidegc);

    zassert_true(status.charge_state <= POWER_CHARGE_STATE_COMPLETE,
                 "charge state %d is not a value hal/power.h defines",
                 (int)status.charge_state);

    /*
     * BCHGERRREASON carries seven bits and hal/power.h exports five coarser
     * ones. A bit outside the known set means decode_faults() invented one.
     */
    zassert_equal(status.fault_flags & ~((uint32_t)POWER_FAULT_KNOWN), 0U,
                  "fault_flags 0x%08x carries a bit outside POWER_FAULT_* - "
                  "the decode table has drifted from the register",
                  status.fault_flags);
}

ZTEST(careloop_hal_3_power, test_hal_power_30_read_rejects_null)
{
    zassert_equal(power_read(NULL), -ERR_POWER_READ_FAILED,
                  "a NULL destination is a read failure once initialised");
}

ZTEST(careloop_hal_3_power, test_hal_power_40_faults_latched_monotonic)
{
    struct power_status first;
    struct power_status second;

    zassert_ok(power_read(&first), "first power_read() failed");
    k_sleep(K_MSEC(POWER_SETTLE_MS));
    zassert_ok(power_read(&second), "second power_read() failed");

    printk("  faults 0x%08x then 0x%08x\n", first.fault_flags,
           second.fault_flags);

    /*
     * hal/power.h: a bit means "this happened since the last clear", not "this
     * is happening now". Nothing here issues TASKCLEARCHGERR, so no bit may
     * ever go away. DIETEMPHIGHCHGPAUSED is a live status bit in the hardware
     * and would drop out without the software latch in power_npm1300.c - this
     * is what pins that latch.
     */
    zassert_equal(second.fault_flags & first.fault_flags, first.fault_flags,
                  "a latched fault bit disappeared between reads (0x%08x then "
                  "0x%08x) - latched means it stays until cleared",
                  first.fault_flags, second.fault_flags);
}

ZTEST(careloop_hal_3_power, test_hal_power_50_battery_detected)
{
    struct power_status status;

    zassert_ok(power_read(&status), "power_read() failed");

    zassert_true(status.battery_detected,
                 "U2 reports no battery - fit a cell or drive VBAT. If test 20 "
                 "read a plausible voltage but this fails, the "
                 "BCHGCHARGESTATUS bit-0 decode is wrong");
}

ZTEST(careloop_hal_3_power, test_hal_power_60_vbus_charge_state_cohere)
{
    struct power_status status;

    zassert_ok(power_read(&status), "power_read() failed");

    /*
     * The one cross-field check the "one coherent snapshot" contract permits.
     * With nothing plugged in you cannot be in trickle, constant current or
     * constant voltage. The converse is jig-dependent - a full cell on USB sits
     * in NONE quite legitimately - so that direction is printed, not asserted.
     */
    if (!status.external_power_present) {
        zassert_equal(status.charge_state, POWER_CHARGE_STATE_NONE,
                      "charge state is %s with no external supply present - "
                      "the VBUS bit and the charge state disagree, so they did "
                      "not come from the same snapshot",
                      charge_state_name(status.charge_state));
    } else {
        printk("  external supply present, charging %s\n",
               charge_state_name(status.charge_state));
    }
}
