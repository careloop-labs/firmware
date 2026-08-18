// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * nPM1300 PMIC (U2) - every rail on the board comes from here.
 *
 * Note what the readiness check does and does not prove. The MCU is only
 * executing because BUCK1 is already up, so this cannot fail "because the
 * PMIC is dead" - it fails when the register interface is unreachable.
 *
 * Battery voltage comes from the PMIC's own VBAT ADC. There is no resistor
 * divider into a SAADC pin anywhere on this board, so this is the only
 * measurement available.
 *
 * Jig requirement: a cell must be fitted or VBAT driven from a supply.
 * A board powered only over USB with no cell reads near zero and fails
 * here correctly but confusingly.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#define DIE_TEMP_MIN_MC (-10000)
#define DIE_TEMP_MAX_MC 85000

#define VBAT_MIN_MV 3000
#define VBAT_MAX_MV 4400

static const struct device *const pmic =
    DEVICE_DT_GET(DT_NODELABEL(npm1300));
static const struct device *const charger =
    DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

ZTEST_SUITE(careloop_6_power, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_6_power, test_power_pmic_ready)
{
    zassert_true(device_is_ready(pmic), "nPM1300 MFD not ready");
    zassert_true(device_is_ready(charger), "nPM1300 charger not ready");
}

ZTEST(careloop_6_power, test_power_die_temp_sane)
{
    struct sensor_value val;
    int32_t mc;

    zassert_ok(sensor_sample_fetch(charger), "charger sample fetch failed");
    zassert_ok(sensor_channel_get(charger, SENSOR_CHAN_DIE_TEMP, &val),
               "charger die temp read failed");

    mc = sensor_value_to_milli(&val);
    printk("  pmic die %d.%03d C\n", mc / 1000, (mc < 0 ? -mc : mc) % 1000);

    zassert_between_inclusive(mc, DIE_TEMP_MIN_MC, DIE_TEMP_MAX_MC,
                              "PMIC die temperature %d mC implausible", mc);
}

ZTEST(careloop_6_power, test_power_battery_voltage)
{
    struct sensor_value val;
    int32_t mv;

    zassert_ok(sensor_sample_fetch(charger), "charger sample fetch failed");
    zassert_ok(sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &val),
               "battery voltage read failed");

    mv = sensor_value_to_milli(&val);
    printk("  battery %d.%03d V\n", mv / 1000, (mv < 0 ? -mv : mv) % 1000);

    zassert_between_inclusive(mv, VBAT_MIN_MV, VBAT_MAX_MV,
                              "battery %d mV outside %d..%d - is a cell fitted?",
                              mv, VBAT_MIN_MV, VBAT_MAX_MV);
}
