// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/motion.h for the BMI270 IMU (U4), I2C 0x68.
 * Verified against the driver in NCS v3.4.0 (Zephyr 4.4.0).
 *
 * Traps, all of which are silent if ignored:
 *
 * 1. Any-motion needs "bosch,bmi270-base" in the devicetree compatible list.
 *    Without it the driver picks bmi270_feature_max_fifo, whose anymo_1 and
 *    anymo_2 pointers are NULL, and bmi270_feature_reg_write() dereferences
 *    them. Address 0 is readable flash on this part, so there is no fault -
 *    it writes two arbitrary bytes as a feature page and register address.
 *    Happens on disable too; that write is outside the enable guard.
 *
 * 2. SLOPE_TH and SLOPE_DUR only cache into data->anymo_1/2. Nothing reaches
 *    the chip until trigger_set(SENSOR_TRIG_MOTION). Enabling before
 *    configuring arms zero threshold and no axes - SELECT_XYZ shares the word
 *    with the duration. Enforce the ordering here; the driver will not.
 *
 * 3. Accel OSR reads data->acc_odr to choose its lookup table (>=100 Hz:
 *    1/2/4; below: 1..128). Write ODR before OSR or a caller changing both
 *    gets the table for the previous rate. The performance-mode switch also
 *    accepts unrecognised factors as CIC_AVG8 rather than erroring, so
 *    validate here.
 *
 * 4. acc_odr_to_reg() returns 0 below the slowest step and set_accel_odr_osr()
 *    reads that as "disable", clearing PWR_CTRL_ACC_EN. An out-of-range rate
 *    powers the sensor down instead of failing. Same below 25 Hz on the gyro.
 *
 * Ladders, Hz. Accel: 0.78 1.56 3.125 6.25 12.5 25 50 100 200 400 800 1600.
 * Gyro: 25 50 100 200 400 800 1600 3200. Ranges must be exact - 2/4/8/16 g,
 * 125/250/500/1000/2000 dps - or the driver returns -ENOTSUP.
 *
 * Any-motion conversions for motion_get_any_motion_limits():
 *   threshold  val2 is micro-g, lsbs = val2 * 1023 / 1e6, lsbs == 0 rejected.
 *              min ~1 mg, max 1000 mg, ~0.98 mg/LSB. (The driver comment says
 *              0.49 mg/LSB; the arithmetic beside it says otherwise.)
 *   duration   20 ms/LSB, BIT_MASK(12) => max 81900 ms, step 20 ms. The
 *              conversion applies no mask, so a larger value overflows into
 *              the axis-select bits. Range-check before converting.
 *
 * Triggers are pinned to one line each: DATA_READY -> INT2, MOTION -> INT1.
 * Different registers, so both can be armed at once. The handler runs on the
 * system workqueue - no I2C, no blocking.
 *
 * sample_fetch() accepts only SENSOR_CHAN_ALL and always reads 12 bytes, so
 * read_accel and read_gyro each cost the full burst.
 *
 * Not reachable through this driver: FIFO, temperature, step counter, tap,
 * wrist gesture, PM_DEVICE.
 */

/* TODO: implement hal/motion.h against the bmi270 driver. */





//TODO: implement addtion features of the BMI270 (not now, do this later)
// - step counter
// - FIFO  
