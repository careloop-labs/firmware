// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * BMI270 registers the upstream Zephyr driver does not reach.
 *
 * The driver's own bmi270.h is private to drivers/sensor/bosch/bmi270 and
 * cannot be included from application code, so the constants we need are
 * repeated here. Only what motion_bmi270_ext.c actually touches is listed -
 * this is not a second copy of the register map.
 *
 * Every value verified against BST-BMI270-DS000-07 (revision 07), section
 * numbers below refer to that document.
 */

#ifndef HAL_BMI270_REGS_H_
#define HAL_BMI270_REGS_H_

#include <zephyr/sys/util.h>

/* --- Direct registers ----------------------------------------------------- */

#define BMI270_R_ERR_REG        0x02U /* 5.2.2  */
#define BMI270_R_SENSORTIME_0   0x18U /* 4.6.15 - 3 bytes, LSB first */
#define BMI270_R_INT_STATUS_0   0x1CU /* feature interrupts */
#define BMI270_R_INT_STATUS_1   0x1DU /* data/FIFO/error interrupts */
#define BMI270_R_FIFO_LENGTH_0  0x24U /* 2 bytes, 13-bit fill level */
#define BMI270_R_FIFO_DATA      0x26U
#define BMI270_R_FEAT_PAGE      0x2FU /* 4.8.1 */
#define BMI270_R_ACC_CONF       0x40U
#define BMI270_R_FIFO_WTM_0     0x46U /* 2 bytes, 13-bit, in BYTES not frames */
#define BMI270_R_FIFO_CONFIG_0  0x48U /* 5.2.49, reset 0x02 */
#define BMI270_R_FIFO_CONFIG_1  0x49U /* 5.2.50, reset 0x10 */
#define BMI270_R_INT1_IO_CTRL   0x53U /* 5.2.58 */
#define BMI270_R_INT2_IO_CTRL   0x54U /* 5.2.59 */
#define BMI270_R_INT_LATCH      0x55U
#define BMI270_R_INT1_MAP_FEAT  0x56U
#define BMI270_R_INT2_MAP_FEAT  0x57U
#define BMI270_R_INT_MAP_DATA   0x58U
#define BMI270_R_INTERNAL_ERROR 0x5FU
#define BMI270_R_PWR_CONF       0x7CU
#define BMI270_R_CMD            0x7EU

/*
 * 5.2.2 ERR_REG. fatal_err clears only on POR or soft reset; the other two
 * clear on read. Bit 5 is not defined by the datasheet.
 *
 * fifo_err and aux_err cannot fire on this board - nothing uses the FIFO in
 * streaming-overflow mode and no auxiliary sensor is fitted - but they are
 * masked in rather than ignored so an unexpected one is still reported.
 */
#define BMI270_ERR_FATAL         BIT(0)
#define BMI270_ERR_INTERNAL_MASK GENMASK(4, 1)
#define BMI270_ERR_FIFO          BIT(6)
#define BMI270_ERR_AUX           BIT(7)

/*
 * 5.2.68 INTERNAL_ERROR. A separate register from ERR_REG and reported
 * separately: feat_eng_disabled means the motion features have stopped while
 * plain sampling carries on, so the only symptom is an event that never
 * arrives - which looks exactly like a wearer sitting still.
 */
#define BMI270_INT_ERR_1                 BIT(1) /* long processing time, halted */
#define BMI270_INT_ERR_2                 BIT(2) /* fatal, processing halted */
#define BMI270_INT_ERR_FEAT_ENG_DISABLED BIT(4)

/*
 * Only bits 1, 2 and 4 are defined; the rest are reserved, and the reserved
 * ones are not always zero. Every CareLoop board measured so far reads
 * INTERNAL_ERROR as 0x01 from the first readable moment after init, with all
 * three defined bits clear and every function of the part working - the FIFO,
 * both slope features, the classifier and sensortime included.
 *
 * So health is judged on the defined bits alone. Requiring the whole byte to
 * be zero makes the check fire permanently on hardware that is fine, and a
 * fault signal that is always on is worth less than none: it trains whoever
 * reads it to ignore the one time it matters.
 */
#define BMI270_INT_ERR_DEFINED_MASK                                                                \
	(BMI270_INT_ERR_1 | BMI270_INT_ERR_2 | BMI270_INT_ERR_FEAT_ENG_DISABLED)

/* Likewise for ERR_REG, where bit 5 is reserved. */
#define BMI270_ERR_DEFINED_MASK                                                                    \
	(BMI270_ERR_FATAL | BMI270_ERR_INTERNAL_MASK | BMI270_ERR_FIFO | BMI270_ERR_AUX)

/* 4.17 - the driver has this constant typo'd as OxB0 (letter O). */
#define BMI270_CMD_FIFO_FLUSH 0xB0U

/* 4.5 / 5.2.x PWR_CONF */
#define BMI270_PWR_CONF_ADV_PWR_SAVE   BIT(0)
#define BMI270_PWR_CONF_FIFO_SELF_WKUP BIT(1)

/*
 * 4.4 - with advanced power save enabled the part may be in suspend, and a
 * register access has to allow it to wake. The driver uses the same 450 us.
 */
#define BMI270_TRANSC_DELAY_SUSPEND_US 450U

/* --- FIFO ----------------------------------------------------------------- */

/* 4.7 - the base feature config gives 2048 bytes. See fifo notes below. */
#define BMI270_FIFO_SIZE_BYTES 2048U

/* 5.2.49 FIFO_CONFIG_0 */
#define BMI270_FIFO_CFG0_STOP_ON_FULL BIT(0)
#define BMI270_FIFO_CFG0_TIME_EN      BIT(1)

/* 5.2.50 FIFO_CONFIG_1 */
#define BMI270_FIFO_CFG1_HEADER_EN BIT(4)
#define BMI270_FIFO_CFG1_AUX_EN    BIT(5)
#define BMI270_FIFO_CFG1_ACC_EN    BIT(6)
#define BMI270_FIFO_CFG1_GYR_EN    BIT(7)

/*
 * 4.7.1 frame header: bits 7..6 fh_mode, 5..2 fh_parm, 1..0 fh_ext.
 *
 * A header of 0x80 is an uninitialised frame - it is what a read past the end
 * of the FIFO returns, and is the signal to stop parsing rather than an error.
 */
#define BMI270_FH_MODE_MASK    GENMASK(7, 6)
#define BMI270_FH_MODE_REGULAR 0x02U
#define BMI270_FH_MODE_CONTROL 0x01U
#define BMI270_FH_PARM_MASK    GENMASK(5, 2)
#define BMI270_FH_PARM_POS     2U
#define BMI270_FH_UNINIT       0x80U

/* fh_parm bits in a regular frame (4.7.1). Payload order is AUX, GYR, ACC. */
#define BMI270_FH_PARM_ACC BIT(0)
#define BMI270_FH_PARM_GYR BIT(1)
#define BMI270_FH_PARM_AUX BIT(2)

/* fh_parm opcodes in a control frame, with their payload lengths (4.7.1). */
#define BMI270_FH_CTRL_SKIP         0x00U
#define BMI270_FH_CTRL_SKIP_LEN     1U
#define BMI270_FH_CTRL_SENSORTIME   0x01U
#define BMI270_FH_CTRL_TIME_LEN     3U
#define BMI270_FH_CTRL_INPUT_CONFIG 0x02U
#define BMI270_FH_CTRL_INPUT_LEN    4U

#define BMI270_FIFO_AUX_BYTES 8U
#define BMI270_FIFO_GYR_BYTES 6U
#define BMI270_FIFO_ACC_BYTES 6U

/* --- Interrupt mapping ---------------------------------------------------- */

/* Applies to INT1_MAP_FEAT, INT2_MAP_FEAT and INT_STATUS_0 alike. */
#define BMI270_INT_FEAT_SIG_MOTION  BIT(0)
#define BMI270_INT_FEAT_STEP_COUNT  BIT(1)
#define BMI270_INT_FEAT_ACTIVITY    BIT(2)
#define BMI270_INT_FEAT_WRIST_WAKE  BIT(3)
#define BMI270_INT_FEAT_WRIST_GEST  BIT(4)
#define BMI270_INT_FEAT_NO_MOTION   BIT(5)
#define BMI270_INT_FEAT_ANY_MOTION  BIT(6)

/* INT_MAP_DATA (5.2.63). We own this register whole - see motion_bmi270_ext.c. */
#define BMI270_INT_DATA_FFULL_INT1 BIT(0)
#define BMI270_INT_DATA_FWM_INT1   BIT(1)
#define BMI270_INT_DATA_DRDY_INT1  BIT(2)
#define BMI270_INT_DATA_ERR_INT1   BIT(3)
#define BMI270_INT_DATA_FFULL_INT2 BIT(4)
#define BMI270_INT_DATA_FWM_INT2   BIT(5)
#define BMI270_INT_DATA_DRDY_INT2  BIT(6)
#define BMI270_INT_DATA_ERR_INT2   BIT(7)

/*
 * INT<x>_IO_CTRL (5.2.58). Both reset to 0x00, which means the pin is an
 * output driver that is switched off and, when enabled, drives ACTIVE LOW.
 *
 * The vendor driver writes output_en alone and so leaves the part driving
 * active low while careloop.dts declares the lines GPIO_ACTIVE_HIGH - which
 * works only because Zephyr then arms a rising edge and catches the trailing
 * edge of each pulse. Now that this firmware owns these registers the level
 * is set to match the devicetree, and the leading edge is what fires.
 */
#define BMI270_INT_IO_LVL       BIT(1) /* 0 = active low, 1 = active high */
#define BMI270_INT_IO_OPEN_DRAIN BIT(2)
#define BMI270_INT_IO_OUTPUT_EN BIT(3)
#define BMI270_INT_IO_INPUT_EN  BIT(4)

/* INT_STATUS_1 (read to clear). */
#define BMI270_INT_STATUS1_FFULL BIT(0)
#define BMI270_INT_STATUS1_FWM   BIT(1)
#define BMI270_INT_STATUS1_ERR   BIT(2)
#define BMI270_INT_STATUS1_DRDY  BIT(7)

/* --- Feature-page registers ----------------------------------------------- */

/*
 * 4.8.1: FEATURES registers live in paged windows at 0x30..0x3F, selected by
 * FEAT_PAGE. Writes must be 16-bit word oriented and start at an even
 * address; every register below is even, so a plain 2-byte little-endian
 * write is correct.
 *
 * Page and address per the FEATURES page map in section 5.
 */
#define BMI270_FEAT_PAGE_ACT_OUT 0U
#define BMI270_FEAT_ADDR_ACT_OUT 0x34U

#define BMI270_FEAT_PAGE_ANYMO 1U
#define BMI270_FEAT_ADDR_ANYMO_1 0x3CU
#define BMI270_FEAT_ADDR_ANYMO_2 0x3EU

#define BMI270_FEAT_PAGE_NOMO 2U
#define BMI270_FEAT_ADDR_NOMO_1 0x30U
#define BMI270_FEAT_ADDR_NOMO_2 0x32U

#define BMI270_FEAT_PAGE_SC_26 6U
#define BMI270_FEAT_ADDR_SC_26 0x32U

/*
 * ANYMO_1 and NOMO_1 share a layout, as do ANYMO_2 and NOMO_2.
 *
 * duration is THIRTEEN bits (12..0), not twelve: 20 ms/LSB gives the 0..163 s
 * range the datasheet quotes. The upstream driver masks twelve and so can
 * only reach half of it.
 */
#define BMI270_MO1_DURATION_MASK GENMASK(12, 0)
#define BMI270_MO1_SELECT_X      BIT(13)
#define BMI270_MO1_SELECT_Y      BIT(14)
#define BMI270_MO1_SELECT_Z      BIT(15)
#define BMI270_MO1_SELECT_XYZ                                                  \
	(BMI270_MO1_SELECT_X | BMI270_MO1_SELECT_Y | BMI270_MO1_SELECT_Z)

/*
 * threshold is ELEVEN bits (10..0) spanning 0..1 g, i.e. 1 g / 2048 =
 * 0.48828 mg per LSB. The datasheet's own reset value confirms it: ANYMO_2
 * resets to 0xAA and is documented as 83 mg, and 170 * 0.48828 = 83.0.
 *
 * The upstream driver scales against a TEN-bit mask, which maps full scale
 * onto 1023 LSB - a value the chip reads as 499.5 mg. Every threshold set
 * through that path lands at about half the value asked for. This is the
 * defect motion_bmi270_ext.c exists to bypass.
 */
#define BMI270_MO2_THRESHOLD_MASK GENMASK(10, 0)
#define BMI270_MO2_OUT_CONF_MASK  GENMASK(14, 11)
#define BMI270_MO2_OUT_CONF_POS   11U
#define BMI270_MO2_ENABLE         BIT(15)

/* Full-scale threshold in LSB, and the milli-g it corresponds to. */
#define BMI270_MO2_THRESHOLD_MAX_LSB 2047U
#define BMI270_MO2_LSB_PER_G         2048U

/* Duration granularity: the feature engine runs at 50 Hz (4.8.2). */
#define BMI270_MO1_DURATION_STEP_MS 20U
#define BMI270_MO1_DURATION_MAX_LSB 8191U

/*
 * out_conf selects which bit of INT_STATUS_0 the feature drives, encoded as
 * (bit index + 1); 0 disables the output entirely. Any-motion conventionally
 * takes bit 6 and no-motion bit 5, matching BMI270_INT_FEAT_*.
 */
#define BMI270_MO2_OUT_CONF_BIT(n) ((uint16_t)((n) + 1U))

/* SC_26 (page 6, 0x32) - step counter, step detector and activity. */
#define BMI270_SC26_WATERMARK_MASK GENMASK(9, 0)
#define BMI270_SC26_RESET_COUNTER  BIT(10)
#define BMI270_SC26_EN_DETECTOR    BIT(11)
#define BMI270_SC26_EN_COUNTER     BIT(12)
#define BMI270_SC26_EN_ACTIVITY    BIT(13)

/* ACT_OUT (page 0, 0x34) - 4.8.5. Unknown is the power-on value. */
#define BMI270_ACT_OUT_MASK    GENMASK(1, 0)
#define BMI270_ACT_OUT_STILL   0U
#define BMI270_ACT_OUT_WALKING 1U
#define BMI270_ACT_OUT_RUNNING 2U
#define BMI270_ACT_OUT_UNKNOWN 3U

/* --- Sensortime ----------------------------------------------------------- */

/*
 * 4.6.15 - free-running 24-bit counter, 39.0625 us per tick, so it wraps
 * every 2^24 * 39.0625 us = 655.36 s. Expressed as a rational rather than a
 * float because the image builds with -Wdouble-promotion under -Werror:
 * 39.0625 us is exactly 625/16 us.
 */
#define BMI270_SENSORTIME_MASK      0x00FFFFFFU
#define BMI270_SENSORTIME_PERIOD    0x01000000U
#define BMI270_SENSORTIME_US_NUM    625U
#define BMI270_SENSORTIME_US_DEN    16U

#endif /* HAL_BMI270_REGS_H_ */
