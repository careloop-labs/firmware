// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/power.h for the nPM1300 PMIC (U2).
 *
 * power_init() cannot fail because the PMIC is dead - the MCU only runs
 * because BUCK1 is up. It fails when the I2C register interface is
 * unreachable.
 *
 * charger_millidegc is SENSOR_CHAN_DIE_TEMP. SENSOR_CHAN_GAUGE_TEMP returns
 * -ENOTSUP: no thermistor is fitted and the devicetree says
 * thermistor-ohms = 0.
 *
 * Take all values from a single sensor_sample_fetch() so the snapshot is
 * coherent. Log the raw BCHGERRREASON when decoding POWER_FAULT_* bits - the
 * header exports a coarser set than the register carries.
 */

/* TODO: implement hal/power.h against npm1300 / npm13xx_charger. */
