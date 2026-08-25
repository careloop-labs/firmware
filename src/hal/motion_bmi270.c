// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/motion.h for the BMI270 IMU (U4), I2C 0x68.
 * Verified against the driver in NCS v3.4.0 (Zephyr 4.4.0).
 *
 * The upstream Zephyr driver reaches about a third of this part. It gives
 * accelerometer and gyroscope samples, rates, ranges and oversampling, and
 * nothing else - no FIFO, no no-motion, no activity classification, no
 * sensortime, and an any-motion threshold that arrives at roughly half the
 * value asked for. Everything past that is done here, by direct I2C to the
 * same devicetree node.
 *
 * The vendor driver is therefore used unmodified and kept unmodified: the
 * delta a reviewer has to read is this file. Forking it would mean owning
 * the 8 KB configuration blob and the init sequence as well.
 *
 * Ownership split. The driver keeps init, the config-file upload, the chip-ID
 * check, ODR/range/OSR through sensor_attr_set(), and sample_fetch /
 * channel_get. This file owns the FIFO, both slope features, activity
 * classification, sensortime, the error registers, and both interrupt pins.
 *
 * Layout, in dependency order: the register layer, then the clock, then
 * interrupts and batching, then hal/motion.h on top of all three. Nothing
 * below the last section is visible outside this file.
 *
 * ---------------------------------------------------------------------------
 *
 * CONFIG_BMI270_TRIGGER is deliberately not set, and that is load-bearing.
 * With the driver's trigger code built in, bmi270_thread_cb() reads
 * INT_STATUS_0 on every INT1 edge - before checking whether any handler is
 * installed - and bmi270_init_int_pin() arms that pin at driver init whether
 * a trigger was ever set or not. INT_STATUS_0 is read-to-clear and is the
 * only thing distinguishing any-motion from no-motion, so leaving the
 * driver's copy in would race us for it, and win, since the system workqueue
 * outranks the drain thread. Turning it off removes the race rather than
 * documenting it.
 *
 * Two rules follow from owning the part this way. Each is silent if broken:
 *
 * 1. Advanced power save stays ENABLED while streaming. Table 6 makes it the
 *    condition for low power mode at all - turning it off to make register
 *    access easy would take the part from single-digit microamps to 210 uA.
 *    Config sequences therefore bracket themselves with aps_hold() and
 *    aps_release(), and the FIFO drain relies on PWR_CONF.fifo_self_wakeup
 *    instead, which exists precisely so a burst read can complete without
 *    leaving low power mode.
 *
 * 2. The drain never reads FIFO_LENGTH. It burst-reads a fixed span and stops
 *    at the uninitialised-frame header the part returns past the end of valid
 *    data. That is one I2C transaction rather than two, and it keeps the
 *    whole drain inside the single burst read that rule 1 allows.
 *
 * ---------------------------------------------------------------------------
 *
 * Traps in the vendor driver that still apply, because its sampling path is
 * still the one in use:
 *
 * 1. "bosch,bmi270-base" must stay in the devicetree compatible list. It
 *    selects which configuration blob the driver uploads: the base config is
 *    8 KB and contains the feature engine, the alternative is 328 bytes and
 *    contains none of it. Without the marker there is nothing on the chip for
 *    the slope features or the classifier to configure. It also buys the
 *    2 KB FIFO rather than the 6 KB one - the two are mutually exclusive, and
 *    the features are worth more here than the depth.
 *
 * 2. Accel OSR reads the driver's cached ODR to choose its lookup table
 *    (>=100 Hz: 1/2/4; below: 1..128). Write ODR before OSR or a caller
 *    changing both gets the table for the previous rate. The performance-mode
 *    switch also accepts unrecognised factors as CIC_AVG8 rather than
 *    erroring, so validate here.
 *
 * 3. acc_odr_to_reg() returns 0 below the slowest step and set_accel_odr_osr()
 *    reads that as "disable", clearing PWR_CTRL_ACC_EN. An out-of-range rate
 *    powers the sensor down instead of failing. Same below 25 Hz on the gyro.
 *    That same behaviour is what motion_set_enabled() uses to suspend, since
 *    the driver exposes no other way.
 *
 * Ladders, Hz. Accel: 0.78125 1.5625 3.125 6.25 12.5 25 50 100 200 400 800
 * 1600. Gyro: 25 50 100 200 400 800 1600 3200. Ranges must be exact -
 * 2/4/8/16 g, 125/250/500/1000/2000 dps - or the driver returns -ENOTSUP.
 *
 * sample_fetch() accepts only SENSOR_CHAN_ALL and always reads 12 bytes, so
 * read_accel and read_gyro each cost the full burst. Channel units differ:
 * accel comes back in m/s^2, gyro in radians/s. Samples arriving through the
 * FIFO bypass both, and are scaled here against 2^15 rather than the driver's
 * INT16_MAX.
 *
 * Register constants and the datasheet sections behind them: bmi270_regs.h.
 */

#include <string.h>

#include <motion.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "bmi270_regs.h"

LOG_MODULE_REGISTER(motion, LOG_LEVEL_INF);

#define MOTION_NODE DT_NODELABEL(bmi270)

/*
 * The application also builds for the nRF52840 DK, which has no such node.
 * Everything still compiles there; motion_init() reports the sensor absent.
 *
 * Both interrupt lines are required for the register layer to be built at
 * all, because everything it adds is either reported on one of them or
 * configured alongside something that is. A board describing the part with
 * fewer pins loses the extension at compile time rather than failing at
 * runtime in a way that reads as a dead sensor.
 */
#if DT_NODE_HAS_STATUS_OKAY(MOTION_NODE) && DT_ON_BUS(MOTION_NODE, i2c) &&                          \
	DT_PROP_HAS_IDX(MOTION_NODE, irq_gpios, 0) && DT_PROP_HAS_IDX(MOTION_NODE, irq_gpios, 1)

#define MOTION_HAS_NODE 1

static const struct device *const imu = DEVICE_DT_GET(MOTION_NODE);

/* INT2 carries data-ready and the FIFO watermark, INT1 the features. */
#define MOTION_HAS_DRDY 1

/*
 * The compatible marker gates the slope features and the classifier for the
 * reason in trap 1 above: without it the chip has no feature engine loaded.
 */
#if DT_NODE_HAS_COMPAT(MOTION_NODE, bosch_bmi270_base)
#define MOTION_HAS_SLOPE 1
#else
#define MOTION_HAS_SLOPE 0
#endif

#else /* no usable node */

#define MOTION_HAS_NODE 0
#define MOTION_HAS_DRDY 0
#define MOTION_HAS_SLOPE 0

static const struct device *const imu = NULL;

#endif

#define MOTION_HAS_ANY_MOTION MOTION_HAS_SLOPE
#define MOTION_HAS_EVENTS (MOTION_HAS_DRDY || MOTION_HAS_SLOPE)
/* Values sent to the driver, rounded up into each band so none reads as 0. */
static const uint32_t accel_odr_ladder_mhz[] = {
	782U,    1563U,   3125U,   6250U,   12500U,   25000U,
	50000U,  100000U, 200000U, 400000U, 800000U,  1600000U,
};

static const uint32_t gyro_odr_ladder_mhz[] = {
	25000U,  50000U,   100000U,  200000U,
	400000U, 800000U,  1600000U, 3200000U,
};

/* Above this the accel runs in performance mode, which narrows the OSR set. */
#define ACCEL_PERFORMANCE_MODE_MHZ 100000U

/*
 * The real hardware bounds, now that the registers are written directly.
 * One LSB is 1 g / 2048 = 0.488 mg, so 1 mg is the smallest distinguishable
 * request and 1000 mg is full scale - and both now mean what they say, which
 * they did not when these went through the vendor driver's SLOPE_TH path.
 *
 * Duration is a 13-bit field at 20 ms per step: 8191 * 20 ms = 163820 ms.
 */
#define ANY_MOTION_THRESHOLD_MIN_MG 1U
#define ANY_MOTION_THRESHOLD_MAX_MG 1000U
#define ANY_MOTION_DURATION_MAX_MS 163820U
#define ANY_MOTION_DURATION_STEP_MS 20U

/*
 * Below this rate the feature engine is not merely less accurate, it is
 * running on fewer samples than it was designed around: with the
 * accelerometer filter in its power-optimised mode - which the driver
 * selects for anything under 100 Hz - datasheet 4.8.1 requires at least
 * 50 Hz, and the part sets INTERNAL_STATUS.odr_50hz_error and carries on
 * regardless. The 20 ms duration step stretches with the rate too, so a
 * configured one-second hold silently becomes two at 25 Hz.
 */
#define MOTION_FEATURE_MIN_ODR_MHZ 50000U

static struct {
	bool initialized;
	bool configured;
	bool running;
	bool any_motion_configured;
	bool any_motion_enabled;
	bool no_motion_configured;
	bool no_motion_enabled;
	bool activity_enabled;
	bool data_ready_enabled;
	bool ext_ready;
	bool stream_ready;
	struct motion_config config;
	motion_event_cb_t callback;
} state;

/* True while anything that depends on the feature engine is armed. */
static bool features_armed(void)
{
	return state.any_motion_enabled || state.no_motion_enabled || state.activity_enabled;
}

/* Raised to the application from wherever an event is decoded. */
static void notify(enum motion_event event)
{
	motion_event_cb_t callback = state.callback;

	if (callback != NULL) {
		callback(event);
	}
}

/*
 * Which of the two slope features. They are the same comparison against the
 * same slope, mirrored, and the hardware gives them identical fields - only
 * the feature page and the INT_STATUS_0 bit differ.
 */
enum bmi270_ext_slope {
	BMI270_EXT_SLOPE_ANY,
	BMI270_EXT_SLOPE_NO,
};

#if MOTION_HAS_NODE

/* Accelerometer bytes per FIFO frame in header mode, including the header. */
#define BMI270_EXT_ACCEL_FRAME_BYTES 7U

/* Usable FIFO capacity in accelerometer-only frames. */
#define BMI270_EXT_ACCEL_FRAME_CAPACITY (BMI270_FIFO_SIZE_BYTES / BMI270_EXT_ACCEL_FRAME_BYTES)

/* One accelerometer sample as the chip stores it, before scaling. */
struct bmi270_ext_frame {
	int16_t x;
	int16_t y;
	int16_t z;
};

/*
 * Result of draining the FIFO. `dropped` is the sensor's own count of samples
 * it discarded because the drain was late; it is reported separately rather
 * than inferred, and always refers to samples that preceded the ones here -
 * which is what makes these safe to treat as contiguous.
 */
struct bmi270_ext_drain {
	struct bmi270_ext_frame *frames;
	size_t capacity;
	size_t count;
	uint32_t dropped;
	uint32_t sensortime;
	bool sensortime_valid;
	bool config_changed;
};

/* ===================================================================== */
/* Registers the vendor driver never touches                             */
/* ===================================================================== */

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(MOTION_NODE);

/*
 * Largest watermark we accept, and the read span sized from it.
 *
 * Half the FIFO is a deliberate ceiling: the other half is the grace period
 * for a drain that is late. The slack on top of the watermark covers the
 * frames that keep arriving during the burst plus the trailing sensortime
 * frame, so a drain still reaches the end of the data in one transaction.
 */
#define EXT_WATERMARK_MAX_BYTES 1024U
#define EXT_READ_SLACK_BYTES 64U
#define EXT_READ_BUF_BYTES (EXT_WATERMARK_MAX_BYTES + EXT_READ_SLACK_BYTES)

/* Not on the stack: a drain thread would need a kilobyte of it otherwise. */
static uint8_t read_buf[EXT_READ_BUF_BYTES];

static struct {
	bool initialized;
	uint16_t watermark_bytes;
	uint8_t feat_int1_map;
	uint8_t data_map;
} ext;

/* --- Register primitives -------------------------------------------------- */

static int reg_read(uint8_t reg, uint8_t *value)
{
	return i2c_reg_read_byte_dt(&bus, reg, value);
}

static int reg_write(uint8_t reg, uint8_t value)
{
	return i2c_reg_write_byte_dt(&bus, reg, value);
}

/*
 * Advanced power save has to come off before a register sequence and go back
 * on after - see rule 1 in the header comment. The 450 us is the wake-up
 * allowance the datasheet gives for a part that may be in suspend.
 */
static int aps_hold(uint8_t *saved)
{
	uint8_t pwr_conf;
	int err;

	err = reg_read(BMI270_R_PWR_CONF, &pwr_conf);
	if (err != 0) {
		return err;
	}

	*saved = pwr_conf;

	if ((pwr_conf & BMI270_PWR_CONF_ADV_PWR_SAVE) == 0U) {
		return 0;
	}

	err = reg_write(BMI270_R_PWR_CONF, pwr_conf & ~BMI270_PWR_CONF_ADV_PWR_SAVE);
	if (err != 0) {
		return err;
	}

	k_usleep(BMI270_TRANSC_DELAY_SUSPEND_US);

	return 0;
}

static int aps_release(uint8_t saved)
{
	return reg_write(BMI270_R_PWR_CONF, saved);
}

/*
 * Feature registers live in paged windows at 0x30..0x3F. Writes have to be
 * 16-bit and word-aligned (datasheet 4.8.1); every address this module uses
 * is even, so a plain little-endian pair is correct.
 */
static int feature_write(uint8_t page, uint8_t addr, uint16_t value)
{
	uint8_t payload[2];
	int err;

	err = reg_write(BMI270_R_FEAT_PAGE, page);
	if (err != 0) {
		return err;
	}

	sys_put_le16(value, payload);

	return i2c_burst_write_dt(&bus, addr, payload, sizeof(payload));
}

static int feature_read(uint8_t page, uint8_t addr, uint16_t *value)
{
	uint8_t payload[2];
	int err;

	err = reg_write(BMI270_R_FEAT_PAGE, page);
	if (err != 0) {
		return err;
	}

	err = i2c_burst_read_dt(&bus, addr, payload, sizeof(payload));
	if (err != 0) {
		return err;
	}

	*value = sys_get_le16(payload);

	return 0;
}

/* --- Slope features ------------------------------------------------------- */

struct slope_layout {
	uint8_t page;
	uint8_t addr_1;
	uint8_t addr_2;
	uint8_t status_bit;
	uint8_t out_conf;
};

static const struct slope_layout slope_layouts[] = {
	[BMI270_EXT_SLOPE_ANY] = {
		.page = BMI270_FEAT_PAGE_ANYMO,
		.addr_1 = BMI270_FEAT_ADDR_ANYMO_1,
		.addr_2 = BMI270_FEAT_ADDR_ANYMO_2,
		.status_bit = BMI270_INT_FEAT_ANY_MOTION,
		.out_conf = 6U,
	},
	[BMI270_EXT_SLOPE_NO] = {
		.page = BMI270_FEAT_PAGE_NOMO,
		.addr_1 = BMI270_FEAT_ADDR_NOMO_1,
		.addr_2 = BMI270_FEAT_ADDR_NOMO_2,
		.status_bit = BMI270_INT_FEAT_NO_MOTION,
		.out_conf = 5U,
	},
};

/*
 * 1 g spans the full 11-bit field, so one LSB is 1/2048 g = 0.48828 mg.
 * Rounded rather than truncated, and clamped so a full-scale request cannot
 * wrap into the out_conf bits above it.
 */
static uint16_t bmi270_ext_threshold_mg_to_lsb(uint16_t threshold_mg)
{
	uint32_t lsb = (((uint32_t)threshold_mg * BMI270_MO2_LSB_PER_G) + 500U) / 1000U;

	return (uint16_t)MIN(lsb, BMI270_MO2_THRESHOLD_MAX_LSB);
}

static int bmi270_ext_slope_configure(enum bmi270_ext_slope slope, uint16_t threshold_mg,
			       uint32_t duration_ms)
{
	const struct slope_layout *layout;
	uint16_t word_1;
	uint16_t word_2;
	uint32_t duration_lsb;
	uint8_t saved;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if ((size_t)slope >= ARRAY_SIZE(slope_layouts)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	layout = &slope_layouts[slope];

	duration_lsb = duration_ms / BMI270_MO1_DURATION_STEP_MS;
	if (duration_lsb > BMI270_MO1_DURATION_MAX_LSB) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	/* All three axes: a wearable has no meaningful resting orientation. */
	word_1 = (uint16_t)(duration_lsb & BMI270_MO1_DURATION_MASK) | BMI270_MO1_SELECT_XYZ;

	word_2 = bmi270_ext_threshold_mg_to_lsb(threshold_mg) & BMI270_MO2_THRESHOLD_MASK;
	word_2 |= (uint16_t)(BMI270_MO2_OUT_CONF_BIT(layout->out_conf) << BMI270_MO2_OUT_CONF_POS) &
		  BMI270_MO2_OUT_CONF_MASK;

	err = aps_hold(&saved);
	if (err != 0) {
		return err;
	}

	err = feature_write(layout->page, layout->addr_1, word_1);
	if (err == 0) {
		/*
		 * Written without the enable bit. Arming is a separate step so
		 * a threshold can be retuned while the feature runs without a
		 * window where it is armed against half-written settings.
		 */
		err = feature_write(layout->page, layout->addr_2, word_2);
	}

	(void)aps_release(saved);

	if (err != 0) {
		LOG_ERR("slope %d config failed (%d)", (int)slope, err);
	}

	return err;
}

static int bmi270_ext_slope_set_enabled(enum bmi270_ext_slope slope, bool enabled)
{
	const struct slope_layout *layout;
	uint16_t word_2;
	uint8_t int1_map;
	uint8_t saved;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if ((size_t)slope >= ARRAY_SIZE(slope_layouts)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	layout = &slope_layouts[slope];

	err = aps_hold(&saved);
	if (err != 0) {
		return err;
	}

	err = feature_read(layout->page, layout->addr_2, &word_2);
	if (err == 0) {
		if (enabled) {
			word_2 |= BMI270_MO2_ENABLE;
		} else {
			word_2 &= (uint16_t)~BMI270_MO2_ENABLE;
		}

		err = feature_write(layout->page, layout->addr_2, word_2);
	}

	/*
	 * Both slope features share INT1, so the map is kept as a whole and
	 * only the one bit moved - writing it wholesale would disarm the other.
	 */
	if (err == 0) {
		int1_map = ext.feat_int1_map;
		if (enabled) {
			int1_map |= layout->status_bit;
		} else {
			int1_map &= (uint8_t)~layout->status_bit;
		}

		err = reg_write(BMI270_R_INT1_MAP_FEAT, int1_map);
		if (err == 0) {
			ext.feat_int1_map = int1_map;
		}
	}

	(void)aps_release(saved);

	return err;
}

static int bmi270_ext_slope_read_raw(enum bmi270_ext_slope slope, uint16_t *word1, uint16_t *word2)
{
	const struct slope_layout *layout;
	uint8_t saved;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (((size_t)slope >= ARRAY_SIZE(slope_layouts)) || (word1 == NULL) || (word2 == NULL)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	layout = &slope_layouts[slope];

	err = aps_hold(&saved);
	if (err != 0) {
		return err;
	}

	err = feature_read(layout->page, layout->addr_1, word1);
	if (err == 0) {
		err = feature_read(layout->page, layout->addr_2, word2);
	}

	(void)aps_release(saved);

	return err;
}

/* --- Activity ------------------------------------------------------------- */

static int bmi270_ext_activity_set_enabled(bool enabled)
{
	uint16_t sc_26;
	uint8_t saved;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	err = aps_hold(&saved);
	if (err != 0) {
		return err;
	}

	/*
	 * SC_26 also holds the step counter and detector enables and the step
	 * watermark, so it is read-modify-written rather than assigned.
	 */
	err = feature_read(BMI270_FEAT_PAGE_SC_26, BMI270_FEAT_ADDR_SC_26, &sc_26);
	if (err == 0) {
		if (enabled) {
			sc_26 |= BMI270_SC26_EN_ACTIVITY;
		} else {
			sc_26 &= (uint16_t)~BMI270_SC26_EN_ACTIVITY;
		}

		err = feature_write(BMI270_FEAT_PAGE_SC_26, BMI270_FEAT_ADDR_SC_26, sc_26);
	}

	(void)aps_release(saved);

	return err;
}

static int bmi270_ext_activity_get(enum motion_activity *activity)
{
	uint16_t act_out;
	uint8_t saved;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (activity == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	err = aps_hold(&saved);
	if (err != 0) {
		return err;
	}

	err = feature_read(BMI270_FEAT_PAGE_ACT_OUT, BMI270_FEAT_ADDR_ACT_OUT, &act_out);

	(void)aps_release(saved);

	if (err != 0) {
		return err;
	}

	/* The encodings match one for one; the cast is not a reinterpretation. */
	switch (act_out & BMI270_ACT_OUT_MASK) {
	case BMI270_ACT_OUT_STILL:
		*activity = MOTION_ACTIVITY_STILL;
		break;
	case BMI270_ACT_OUT_WALKING:
		*activity = MOTION_ACTIVITY_WALKING;
		break;
	case BMI270_ACT_OUT_RUNNING:
		*activity = MOTION_ACTIVITY_RUNNING;
		break;
	default:
		*activity = MOTION_ACTIVITY_UNKNOWN;
		break;
	}

	return 0;
}

/* --- Health --------------------------------------------------------------- */

static int bmi270_ext_get_fault(struct motion_fault *fault)
{
	uint8_t err_reg;
	uint8_t internal;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (fault == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	err = reg_read(BMI270_R_ERR_REG, &err_reg);
	if (err != 0) {
		return err;
	}

	err = reg_read(BMI270_R_INTERNAL_ERROR, &internal);
	if (err != 0) {
		return err;
	}

	/*
	 * Both registers are reported raw. Folding them into one code loses
	 * the distinction between "long processing time" and "the feature
	 * engine stopped", which are the same size of number and completely
	 * different problems.
	 */
	fault->raw = err_reg;
	fault->internal_raw = internal;
	fault->fatal = (err_reg & BMI270_ERR_FATAL) != 0U;
	fault->fifo_error = (err_reg & BMI270_ERR_FIFO) != 0U;
	fault->vendor_code = (uint8_t)FIELD_GET(BMI270_ERR_INTERNAL_MASK, err_reg);
	fault->feature_engine_disabled = (internal & BMI270_INT_ERR_FEAT_ENG_DISABLED) != 0U;
	fault->processing_halted = (internal & (BMI270_INT_ERR_1 | BMI270_INT_ERR_2)) != 0U;

	return 0;
}

static int bmi270_ext_read_feature_status(uint8_t *status)
{
	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (status == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	return reg_read(BMI270_R_INT_STATUS_0, status);
}

/* --- Sensortime ----------------------------------------------------------- */

static int bmi270_ext_sensortime(uint32_t *ticks)
{
	uint8_t raw[3];
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (ticks == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	err = i2c_burst_read_dt(&bus, BMI270_R_SENSORTIME_0, raw, sizeof(raw));
	if (err != 0) {
		return err;
	}

	*ticks = sys_get_le24(raw);

	return 0;
}

/* --- FIFO ----------------------------------------------------------------- */

static int bmi270_ext_fifo_configure(uint16_t watermark_bytes)
{
	uint8_t saved;
	uint8_t wtm[2];
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if ((watermark_bytes == 0U) || (watermark_bytes > EXT_WATERMARK_MAX_BYTES)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	err = aps_hold(&saved);
	if (err != 0) {
		return err;
	}

	sys_put_le16(watermark_bytes, wtm);
	err = i2c_burst_write_dt(&bus, BMI270_R_FIFO_WTM_0, wtm, sizeof(wtm));

	/*
	 * Streaming rather than stop-on-full: if a drain is ever late the
	 * newest samples are the ones worth keeping, and the skip frame tells
	 * us exactly how many of the older ones went. Stopping on full would
	 * instead leave a stale buffer and no record of the gap.
	 *
	 * fifo_time_en gives the trailing sensortime frame, which is the only
	 * way to tie these samples to the chip's own clock.
	 */
	if (err == 0) {
		err = reg_write(BMI270_R_FIFO_CONFIG_0, BMI270_FIFO_CFG0_TIME_EN);
	}

	/*
	 * Header mode is mandatory, not a preference: control frames - and so
	 * both the sensortime and the skip count - exist only in header mode.
	 * It costs one byte per frame.
	 */
	if (err == 0) {
		err = reg_write(BMI270_R_FIFO_CONFIG_1, BMI270_FIFO_CFG1_HEADER_EN);
	}

	/*
	 * Rule 1: this is what lets the drain burst-read under power save.
	 *
	 * Folded into the value aps_release() is going to write rather than
	 * written here - a separate write would put advanced power save back
	 * on partway through this sequence, while there are still registers
	 * to configure.
	 */
	if (err == 0) {
		saved |= BMI270_PWR_CONF_FIFO_SELF_WKUP;
	}

	(void)aps_release(saved);

	if (err == 0) {
		ext.watermark_bytes = watermark_bytes;
	}

	return err;
}

static int bmi270_ext_fifo_set_enabled(bool enabled)
{
	uint8_t config_1;
	uint8_t saved;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	err = aps_hold(&saved);
	if (err != 0) {
		return err;
	}

	err = reg_read(BMI270_R_FIFO_CONFIG_1, &config_1);
	if (err == 0) {
		if (enabled) {
			config_1 |= BMI270_FIFO_CFG1_ACC_EN;
		} else {
			config_1 &= (uint8_t)~BMI270_FIFO_CFG1_ACC_EN;
		}

		err = reg_write(BMI270_R_FIFO_CONFIG_1, config_1);
	}

	/*
	 * INT2 carries both the watermark and data-ready, so the map is
	 * tracked here and only the one bit moved - assigning the register
	 * would silently disarm whichever of the two was not being changed.
	 */
	if (err == 0) {
		uint8_t map = ext.data_map;

		if (enabled) {
			map |= BMI270_INT_DATA_FWM_INT2;
		} else {
			map &= (uint8_t)~BMI270_INT_DATA_FWM_INT2;
		}

		err = reg_write(BMI270_R_INT_MAP_DATA, map);
		if (err == 0) {
			ext.data_map = map;
		}
	}

	(void)aps_release(saved);

	return err;
}

static int bmi270_ext_data_ready_set_enabled(bool enabled)
{
	uint8_t map;
	uint8_t saved;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	err = aps_hold(&saved);
	if (err != 0) {
		return err;
	}

	map = ext.data_map;
	if (enabled) {
		map |= BMI270_INT_DATA_DRDY_INT2;
	} else {
		map &= (uint8_t)~BMI270_INT_DATA_DRDY_INT2;
	}

	err = reg_write(BMI270_R_INT_MAP_DATA, map);
	if (err == 0) {
		ext.data_map = map;
	}

	(void)aps_release(saved);

	return err;
}

static int bmi270_ext_read_data_status(uint8_t *status)
{
	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (status == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	return reg_read(BMI270_R_INT_STATUS_1, status);
}

static int bmi270_ext_fifo_flush(void)
{
	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	return reg_write(BMI270_R_CMD, BMI270_CMD_FIFO_FLUSH);
}

static int bmi270_ext_fifo_level(uint16_t *bytes)
{
	uint8_t raw[2];
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (bytes == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	err = i2c_burst_read_dt(&bus, BMI270_R_FIFO_LENGTH_0, raw, sizeof(raw));
	if (err != 0) {
		return err;
	}

	*bytes = sys_get_le16(raw) & BIT_MASK(13);

	return 0;
}

/*
 * Parse one burst. Returns the number of bytes consumed before the data ran
 * out, so a truncated trailing frame is left alone rather than half-read.
 */
static void parse_frames(const uint8_t *buf, size_t len, struct bmi270_ext_drain *drain)
{
	size_t i = 0U;

	while (i < len) {
		uint8_t header = buf[i];
		uint8_t mode;
		uint8_t parm;

		/* What the part returns once the FIFO has been read dry. */
		if (header == BMI270_FH_UNINIT) {
			break;
		}

		mode = (uint8_t)FIELD_GET(BMI270_FH_MODE_MASK, header);
		parm = (uint8_t)FIELD_GET(BMI270_FH_PARM_MASK, header);
		i++;

		if (mode == BMI270_FH_MODE_REGULAR) {
			size_t payload = 0U;
			size_t offset;

			if ((parm & BMI270_FH_PARM_AUX) != 0U) {
				payload += BMI270_FIFO_AUX_BYTES;
			}
			if ((parm & BMI270_FH_PARM_GYR) != 0U) {
				payload += BMI270_FIFO_GYR_BYTES;
			}
			if ((parm & BMI270_FH_PARM_ACC) != 0U) {
				payload += BMI270_FIFO_ACC_BYTES;
			}

			if ((payload == 0U) || ((i + payload) > len)) {
				break;
			}

			/* Payload order is AUX, then GYR, then ACC. */
			offset = i;
			if ((parm & BMI270_FH_PARM_AUX) != 0U) {
				offset += BMI270_FIFO_AUX_BYTES;
			}
			if ((parm & BMI270_FH_PARM_GYR) != 0U) {
				offset += BMI270_FIFO_GYR_BYTES;
			}

			if (((parm & BMI270_FH_PARM_ACC) != 0U) &&
			    (drain->count < drain->capacity)) {
				struct bmi270_ext_frame *frame = &drain->frames[drain->count];

				frame->x = (int16_t)sys_get_le16(&buf[offset]);
				frame->y = (int16_t)sys_get_le16(&buf[offset + 2U]);
				frame->z = (int16_t)sys_get_le16(&buf[offset + 4U]);
				drain->count++;
			}

			i += payload;
			continue;
		}

		if (mode != BMI270_FH_MODE_CONTROL) {
			/* Reserved encoding - the stream is no longer trustworthy. */
			break;
		}

		switch (parm) {
		case BMI270_FH_CTRL_SKIP:
			if ((i + BMI270_FH_CTRL_SKIP_LEN) > len) {
				return;
			}
			/*
			 * Samples the part discarded because this drain was
			 * late. Always precedes the frames that follow it,
			 * which is what keeps those contiguous.
			 */
			drain->dropped += buf[i];
			i += BMI270_FH_CTRL_SKIP_LEN;
			break;

		case BMI270_FH_CTRL_SENSORTIME:
			if ((i + BMI270_FH_CTRL_TIME_LEN) > len) {
				return;
			}
			drain->sensortime = sys_get_le24(&buf[i]);
			drain->sensortime_valid = true;
			i += BMI270_FH_CTRL_TIME_LEN;
			break;

		case BMI270_FH_CTRL_INPUT_CONFIG:
			if ((i + BMI270_FH_CTRL_INPUT_LEN) > len) {
				return;
			}
			/*
			 * A rate or range change took effect here, so samples
			 * before and after this point are not on one timeline.
			 */
			drain->config_changed = true;
			i += BMI270_FH_CTRL_INPUT_LEN;
			break;

		default:
			return;
		}
	}
}

static int bmi270_ext_fifo_drain(struct bmi270_ext_drain *drain)
{
	size_t to_read;
	int err;

	if (!ext.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if ((drain == NULL) || (drain->frames == NULL)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	drain->count = 0U;
	drain->dropped = 0U;
	drain->sensortime = 0U;
	drain->sensortime_valid = false;
	drain->config_changed = false;

	/*
	 * Rule 4: read past the watermark rather than asking how much is
	 * there. The overshoot both catches frames that landed during the
	 * burst and forces the FIFO empty, which is the only condition under
	 * which the part appends the sensortime frame.
	 */
	to_read = MIN((size_t)ext.watermark_bytes + EXT_READ_SLACK_BYTES, sizeof(read_buf));

	err = i2c_burst_read_dt(&bus, BMI270_R_FIFO_DATA, read_buf, to_read);
	if (err != 0) {
		LOG_ERR("FIFO burst read failed (%d)", err);
		return err;
	}

	parse_frames(read_buf, to_read, drain);

	return 0;
}

/* --- Init ----------------------------------------------------------------- */

static int bmi270_ext_init(void)
{
	int err;

	if (!device_is_ready(bus.bus)) {
		LOG_ERR("I2C bus not ready for BMI270 extension");
		return -ERR_MOTION_NOT_READY;
	}

	ext.initialized = true;
	ext.watermark_bytes = 0U;
	ext.feat_int1_map = 0U;
	ext.data_map = 0U;

	/*
	 * Drive both pins active high, to agree with the GPIO_ACTIVE_HIGH the
	 * devicetree declares. The reset value is active low, which is what
	 * the vendor driver leaves in place - workable, because Zephyr then
	 * catches the trailing edge of each pulse, but a pulse late and not
	 * what the board description says. Push-pull: there is no pull-up on
	 * either line.
	 */
	err = reg_write(BMI270_R_INT1_IO_CTRL, BMI270_INT_IO_OUTPUT_EN | BMI270_INT_IO_LVL);
	if (err == 0) {
		err = reg_write(BMI270_R_INT2_IO_CTRL,
				BMI270_INT_IO_OUTPUT_EN | BMI270_INT_IO_LVL);
	}

	/*
	 * Non-latched, matching the edge-triggered GPIO configuration on the
	 * host side. This is the reset value; written anyway so the mode is
	 * stated rather than inherited.
	 */
	if (err == 0) {
		err = reg_write(BMI270_R_INT_LATCH, 0U);
	}

	/* Nothing mapped until something is armed. */
	if (err == 0) {
		err = reg_write(BMI270_R_INT1_MAP_FEAT, 0U);
	}

	if (err == 0) {
		err = reg_write(BMI270_R_INT_MAP_DATA, 0U);
	}

	if (err != 0) {
		ext.initialized = false;
		LOG_ERR("BMI270 extension init failed (%d)", err);
	}

	return err;
}


/* ===================================================================== */
/* Relating the sensor's sample timeline to the host clock               */
/* ===================================================================== */
/*
 * Below this span the two clocks have not diverged enough for the difference
 * to be signal rather than the quantisation of either counter. One second of
 * host time at a 1 us resolution bounds the reported figure's own error to
 * roughly 1 ppm, which is finer than anything this is used to decide.
 */
#define DRIFT_MIN_SPAN_US 1000000ULL

static struct {
	bool started;
	uint32_t last_raw;
	uint64_t chip_elapsed_us;
	uint64_t host_origin_us;
	uint64_t host_latest_us;
} tick;

static void motion_time_reset(void)
{
	tick.started = false;
	tick.last_raw = 0U;
	tick.chip_elapsed_us = 0U;
	tick.host_origin_us = 0U;
	tick.host_latest_us = 0U;
}

static void motion_time_update(uint32_t raw_ticks, uint64_t host_us)
{
	uint32_t delta_ticks;

	raw_ticks &= BMI270_SENSORTIME_MASK;

	if (!tick.started) {
		tick.started = true;
		tick.last_raw = raw_ticks;
		tick.chip_elapsed_us = 0U;
		tick.host_origin_us = host_us;
		tick.host_latest_us = host_us;
		return;
	}

	/*
	 * Unsigned subtraction masked back to 24 bits handles the wrap without
	 * a branch: any number of whole wraps between two drains would be
	 * indistinguishable anyway, and at 655 s per wrap against a drain
	 * every few seconds that cannot arise.
	 */
	delta_ticks = (raw_ticks - tick.last_raw) & BMI270_SENSORTIME_MASK;
	tick.last_raw = raw_ticks;

	/* 39.0625 us per tick is exactly 625/16 - kept rational because the
	 * image builds with -Wdouble-promotion under -Werror.
	 */
	tick.chip_elapsed_us += ((uint64_t)delta_ticks * BMI270_SENSORTIME_US_NUM) /
				BMI270_SENSORTIME_US_DEN;
	tick.host_latest_us = host_us;
}

static uint64_t motion_time_host_elapsed_us(void)
{
	if (!tick.started) {
		return 0U;
	}

	return tick.host_latest_us - tick.host_origin_us;
}

static int motion_time_drift_ppm(int32_t *ppm)
{
	uint64_t host_span = motion_time_host_elapsed_us();
	int64_t difference;

	if ((ppm == NULL) || !tick.started || (host_span < DRIFT_MIN_SPAN_US)) {
		return -1;
	}

	difference = (int64_t)tick.chip_elapsed_us - (int64_t)host_span;

	/*
	 * Scaled against the host span, which is the reference by definition.
	 * int64 throughout: at 1e6 us per second a long run overflows 32 bits
	 * well before the multiply.
	 */
	*ppm = (int32_t)((difference * 1000000LL) / (int64_t)host_span);

	return 0;
}

/* ===================================================================== */
/* Interrupts, hardware batching, the rolling window and capture         */
/* ===================================================================== */

static const struct gpio_dt_spec feature_int = GPIO_DT_SPEC_GET_BY_IDX(MOTION_NODE, irq_gpios, 0);
static const struct gpio_dt_spec data_int = GPIO_DT_SPEC_GET_BY_IDX(MOTION_NODE, irq_gpios, 1);

#define DRAIN_STACK_SIZE 1024
#define DRAIN_PRIORITY K_PRIO_COOP(10)

/*
 * Worst case is one frame per seven bytes across the largest burst the
 * extension will read, so the scratch never overflows regardless of what the
 * FIFO held.
 */
#define DRAIN_MAX_FRAMES ((1024U + 64U) / BMI270_EXT_ACCEL_FRAME_BYTES)

/* Raw as the chip stores it: scaling to mm/s2 costs precision and is deferred
 * to the reader, who wants far fewer samples than the window holds.
 */
struct window_entry {
	int16_t x;
	int16_t y;
	int16_t z;
};

static struct bmi270_ext_frame drain_frames[DRAIN_MAX_FRAMES];
static struct window_entry window[MOTION_WINDOW_SAMPLES];

enum {
	IRQ_FLAG_FEATURE,
	IRQ_FLAG_DATA,
};

static K_SEM_DEFINE(drain_sem, 0, 1);
static K_MUTEX_DEFINE(sample_lock);
static K_THREAD_STACK_DEFINE(drain_stack, DRAIN_STACK_SIZE);
static struct k_thread drain_thread;
static struct gpio_callback feature_cb;
static struct gpio_callback data_cb;
static atomic_t irq_flags;

static struct {
	bool initialized;
	bool active;
	uint16_t range_g;
	uint32_t period_us;

	/* Rolling window, oldest at head once it has wrapped. */
	size_t head;
	size_t fill;

	/* Host clock at the most recent watermark interrupt. */
	uint64_t irq_host_us;

	/* Capture, valid only while storage is non-NULL. */
	struct motion_accel_sample *capture;
	size_t capture_capacity;
	size_t capture_count;
	uint64_t capture_first_us;
	uint32_t capture_dropped;
	bool capture_overflowed;
	bool capture_anchored;
} stream;

/* --- Scaling -------------------------------------------------------------- */

/*
 * Full scale is 2^15, not INT16_MAX: the datasheet gives 16384 LSB/g at
 * +/-2 g, so +/-2 g spans 32768 counts. The vendor driver divides by 32767
 * instead and carries a small systematic gain error as a result; samples
 * arriving through the FIFO do not.
 */
#define ACCEL_FULL_SCALE_LSB 32768

static int32_t raw_to_mms2(int16_t raw, uint16_t range_g)
{
	return (int32_t)(((int64_t)raw * (int64_t)range_g * MOTION_GRAVITY_MMS2) /
			 ACCEL_FULL_SCALE_LSB);
}

/* --- Interrupt ------------------------------------------------------------ */

static void feature_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	atomic_set_bit(&irq_flags, IRQ_FLAG_FEATURE);
	k_sem_give(&drain_sem);
}

static void data_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/*
	 * Taken here rather than in the drain thread: at this instant the
	 * newest sample in the FIFO has just been taken, and that is what the
	 * batch's timestamps are anchored to. Reading it after the burst
	 * would fold the burst's own duration into every sample.
	 */
	stream.irq_host_us = k_ticks_to_us_floor64(k_uptime_ticks());

	atomic_set_bit(&irq_flags, IRQ_FLAG_DATA);
	k_sem_give(&drain_sem);
}

/* --- Drain ---------------------------------------------------------------- */

static void window_push(const struct bmi270_ext_frame *frame)
{
	window[stream.head].x = frame->x;
	window[stream.head].y = frame->y;
	window[stream.head].z = frame->z;

	stream.head = (stream.head + 1U) % ARRAY_SIZE(window);
	if (stream.fill < ARRAY_SIZE(window)) {
		stream.fill++;
	}
}

static void capture_push(const struct bmi270_ext_frame *frame, uint64_t sample_us)
{
	if (stream.capture == NULL) {
		return;
	}

	if (stream.capture_count >= stream.capture_capacity) {
		stream.capture_overflowed = true;
		return;
	}

	if (!stream.capture_anchored) {
		stream.capture_first_us = sample_us;
		stream.capture_anchored = true;
	}

	stream.capture[stream.capture_count].x_mms2 = raw_to_mms2(frame->x, stream.range_g);
	stream.capture[stream.capture_count].y_mms2 = raw_to_mms2(frame->y, stream.range_g);
	stream.capture[stream.capture_count].z_mms2 = raw_to_mms2(frame->z, stream.range_g);
	stream.capture_count++;
}

static void drain_once(void)
{
	struct bmi270_ext_drain drain = {
		.frames = drain_frames,
		.capacity = ARRAY_SIZE(drain_frames),
	};
	uint64_t irq_us;
	uint64_t first_us;
	unsigned int key;
	int err;

	/*
	 * Read under a lock because it is 64-bit and written from an
	 * interrupt: a torn read here would splice the halves of two
	 * different timestamps together, which does not look like a small
	 * error in the output - it looks like a jump of hours.
	 */
	key = irq_lock();
	irq_us = stream.irq_host_us;
	irq_unlock(key);

	err = bmi270_ext_fifo_drain(&drain);
	if (err != 0) {
		return;
	}

	if (drain.sensortime_valid) {
		motion_time_update(drain.sensortime, irq_us);
	}

	if (drain.count == 0U) {
		return;
	}

	/*
	 * The newest sample was taken at the interrupt, so the batch runs
	 * backwards from there. Even spacing holds because anything the
	 * sensor discarded is reported as a skip count ahead of these frames,
	 * never interleaved with them.
	 */
	first_us = irq_us - ((uint64_t)(drain.count - 1U) * stream.period_us);

	k_mutex_lock(&sample_lock, K_FOREVER);

	for (size_t i = 0U; i < drain.count; i++) {
		window_push(&drain_frames[i]);
		capture_push(&drain_frames[i], first_us + ((uint64_t)i * stream.period_us));
	}

	if (drain.dropped != 0U) {
		stream.capture_dropped += drain.dropped;
		LOG_WRN("FIFO dropped %u samples - drain was late", drain.dropped);
	}

	if (drain.config_changed) {
		LOG_WRN("rate or range changed mid-stream; samples span two timelines");
	}

	k_mutex_unlock(&sample_lock);

	notify(MOTION_EVENT_BATCH);
}

/*
 * INT_STATUS_0 is read-to-clear and reports which feature fired. Both slope
 * features can be armed at once and this is what tells them apart, which is
 * the whole reason the vendor driver's trigger code cannot be built in.
 */
static void service_features(void)
{
	uint8_t status;

	if (bmi270_ext_read_feature_status(&status) != 0) {
		return;
	}

	if (state.any_motion_enabled && ((status & BMI270_INT_FEAT_ANY_MOTION) != 0U)) {
		notify(MOTION_EVENT_ANY_MOTION);
	}

	if (state.no_motion_enabled && ((status & BMI270_INT_FEAT_NO_MOTION) != 0U)) {
		notify(MOTION_EVENT_NO_MOTION);
	}
}

static void service_data(void)
{
	uint8_t status;

	if (bmi270_ext_read_data_status(&status) != 0) {
		return;
	}

	if (stream.active && ((status & BMI270_INT_STATUS1_FWM) != 0U)) {
		drain_once();
	}

	/*
	 * The data-ready bit reflects the sensor's state whether or not it is
	 * mapped to a pin, so it is reported only when someone asked for it -
	 * otherwise every watermark drain would also raise a phantom
	 * data-ready.
	 */
	if (state.data_ready_enabled && ((status & BMI270_INT_STATUS1_DRDY) != 0U)) {
		notify(MOTION_EVENT_DATA_READY);
	}
}

static void drain_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&drain_sem, K_FOREVER);

		if (atomic_test_and_clear_bit(&irq_flags, IRQ_FLAG_FEATURE)) {
			service_features();
		}

		if (atomic_test_and_clear_bit(&irq_flags, IRQ_FLAG_DATA)) {
			service_data();
		}
	}
}

/* --- Public --------------------------------------------------------------- */

static int configure_int_pin(const struct gpio_dt_spec *pin, struct gpio_callback *cb,
			     gpio_callback_handler_t handler)
{
	int err;

	err = gpio_pin_configure_dt(pin, GPIO_INPUT);
	if (err != 0) {
		return err;
	}

	gpio_init_callback(cb, handler, BIT(pin->pin));

	err = gpio_add_callback(pin->port, cb);
	if (err != 0) {
		return err;
	}

	return gpio_pin_interrupt_configure_dt(pin, GPIO_INT_EDGE_TO_ACTIVE);
}

static int motion_stream_init(void)
{
	int err;

	if (stream.initialized) {
		return 0;
	}

	if (!gpio_is_ready_dt(&feature_int) || !gpio_is_ready_dt(&data_int)) {
		LOG_ERR("BMI270 interrupt pins not ready");
		return -ERR_MOTION_NO_TRIGGER;
	}

	/*
	 * Nothing else configures these lines now, so both are set up here.
	 * Edge-triggered to match the non-latched, pulsed interrupts the
	 * sensor is configured to emit; the pins are driven active high, so
	 * "to active" is the leading edge of each pulse.
	 */
	err = configure_int_pin(&feature_int, &feature_cb, feature_isr);
	if (err != 0) {
		LOG_ERR("could not set up feature interrupt (%d)", err);
		return err;
	}

	err = configure_int_pin(&data_int, &data_cb, data_isr);
	if (err != 0) {
		LOG_ERR("could not set up data interrupt (%d)", err);
		return err;
	}

	k_thread_create(&drain_thread, drain_stack, K_THREAD_STACK_SIZEOF(drain_stack), drain_entry,
			NULL, NULL, NULL, DRAIN_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&drain_thread, "motion_drain");

	stream.initialized = true;

	return 0;
}

static uint32_t motion_stream_max_batch_ms_for(uint32_t odr_mhz)
{
	uint64_t frames;

	if (odr_mhz == 0U) {
		return 0U;
	}

	/*
	 * Only half the buffer is offered. The other half is the grace period
	 * for a drain that runs late - filling to the brim would mean the
	 * first moment of lateness is also the first lost sample.
	 */
	frames = BMI270_EXT_ACCEL_FRAME_CAPACITY / 2U;

	return (uint32_t)((frames * 1000000ULL) / odr_mhz);
}

static int motion_stream_begin(uint16_t range_g, uint32_t odr_mhz, uint32_t batch_ms)
{
	uint64_t frames;
	uint32_t watermark;
	int err;

	if (!stream.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if ((odr_mhz == 0U) || (range_g == 0U) || (batch_ms == 0U)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	if (batch_ms > motion_stream_max_batch_ms_for(odr_mhz)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	/* Watermark is a byte count, so the frame size has to come back in. */
	frames = ((uint64_t)batch_ms * odr_mhz) / 1000000ULL;
	if (frames == 0U) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	watermark = (uint32_t)(frames * BMI270_EXT_ACCEL_FRAME_BYTES);

	stream.range_g = range_g;
	stream.period_us = (uint32_t)(1000000000ULL / odr_mhz);

	err = bmi270_ext_fifo_configure((uint16_t)watermark);
	if (err != 0) {
		return err;
	}

	/* Start from empty so the first batch is not a mix of two rates. */
	err = bmi270_ext_fifo_flush();
	if (err != 0) {
		return err;
	}

	motion_time_reset();

	err = bmi270_ext_fifo_set_enabled(true);
	if (err != 0) {
		return err;
	}

	stream.active = true;

	return 0;
}

static int motion_stream_end(void)
{
	int err;

	if (!stream.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	stream.active = false;

	err = bmi270_ext_fifo_set_enabled(false);

	k_mutex_lock(&sample_lock, K_FOREVER);
	stream.capture = NULL;
	k_mutex_unlock(&sample_lock);

	(void)bmi270_ext_fifo_flush();

	return err;
}

static int motion_stream_window_read(struct motion_accel_sample *out, size_t max, size_t *count)
{
	size_t available;
	size_t start;

	if ((out == NULL) || (count == NULL)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	k_mutex_lock(&sample_lock, K_FOREVER);

	available = MIN(max, stream.fill);

	/*
	 * Oldest first. head points one past the newest, so the run of
	 * `available` samples ending there starts that far back around the
	 * ring.
	 */
	start = (stream.head + ARRAY_SIZE(window) - available) % ARRAY_SIZE(window);

	for (size_t i = 0U; i < available; i++) {
		const struct window_entry *entry = &window[(start + i) % ARRAY_SIZE(window)];

		out[i].x_mms2 = raw_to_mms2(entry->x, stream.range_g);
		out[i].y_mms2 = raw_to_mms2(entry->y, stream.range_g);
		out[i].z_mms2 = raw_to_mms2(entry->z, stream.range_g);
	}

	k_mutex_unlock(&sample_lock);

	*count = available;

	return 0;
}

static int motion_stream_capture_begin(struct motion_accel_sample *storage, size_t capacity)
{
	if ((storage == NULL) || (capacity == 0U)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	if (!stream.active) {
		return -ERR_MOTION_NOT_CONFIGURED;
	}

	k_mutex_lock(&sample_lock, K_FOREVER);

	if (stream.capture != NULL) {
		k_mutex_unlock(&sample_lock);
		return -ERR_MOTION_BUSY;
	}

	stream.capture = storage;
	stream.capture_capacity = capacity;
	stream.capture_count = 0U;
	stream.capture_first_us = 0U;
	stream.capture_dropped = 0U;
	stream.capture_overflowed = false;
	stream.capture_anchored = false;

	k_mutex_unlock(&sample_lock);

	return 0;
}

static int motion_stream_capture_end(struct motion_capture_result *result)
{
	k_mutex_lock(&sample_lock, K_FOREVER);

	if (result != NULL) {
		result->count = stream.capture_count;
		result->first_timestamp_us = stream.capture_first_us;
		result->period_us = stream.period_us;
		result->dropped = stream.capture_dropped;
		result->overflowed = stream.capture_overflowed;
	}

	stream.capture = NULL;
	stream.capture_count = 0U;

	k_mutex_unlock(&sample_lock);

	return 0;
}


#else /* no BMI270 described for this board */

/*
 * Stand-ins, so the entry points below stay free of preprocessor branches.
 * Every one is unreachable rather than merely unused: motion_init() fails
 * before anything can be armed, and each public function tests
 * state.ext_ready or state.stream_ready first.
 */
static int bmi270_ext_init(void) { return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_get_fault(struct motion_fault *fault) { ARG_UNUSED(fault); return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_slope_configure(enum bmi270_ext_slope s, uint16_t th, uint32_t d) { ARG_UNUSED(s); ARG_UNUSED(th); ARG_UNUSED(d); return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_slope_set_enabled(enum bmi270_ext_slope s, bool en) { ARG_UNUSED(s); ARG_UNUSED(en); return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_slope_read_raw(enum bmi270_ext_slope s, uint16_t *a, uint16_t *b) { ARG_UNUSED(s); ARG_UNUSED(a); ARG_UNUSED(b); return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_activity_set_enabled(bool en) { ARG_UNUSED(en); return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_activity_get(enum motion_activity *a) { ARG_UNUSED(a); return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_data_ready_set_enabled(bool en) { ARG_UNUSED(en); return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_sensortime(uint32_t *t) { ARG_UNUSED(t); return -ERR_MOTION_UNSUPPORTED; }
static int motion_stream_init(void) { return -ERR_MOTION_UNSUPPORTED; }
static int motion_stream_begin(uint16_t r, uint32_t o, uint32_t b) { ARG_UNUSED(r); ARG_UNUSED(o); ARG_UNUSED(b); return -ERR_MOTION_UNSUPPORTED; }
static int motion_stream_end(void) { return -ERR_MOTION_UNSUPPORTED; }
static int motion_stream_window_read(struct motion_accel_sample *o, size_t m, size_t *c) { ARG_UNUSED(o); ARG_UNUSED(m); ARG_UNUSED(c); return -ERR_MOTION_UNSUPPORTED; }
static int motion_stream_capture_begin(struct motion_accel_sample *s, size_t c) { ARG_UNUSED(s); ARG_UNUSED(c); return -ERR_MOTION_UNSUPPORTED; }
static int motion_stream_capture_end(struct motion_capture_result *r) { ARG_UNUSED(r); return -ERR_MOTION_UNSUPPORTED; }
static int motion_time_drift_ppm(int32_t *p) { ARG_UNUSED(p); return -ERR_MOTION_UNSUPPORTED; }
static int bmi270_ext_fifo_level(uint16_t *b) { ARG_UNUSED(b); return -ERR_MOTION_UNSUPPORTED; }
static uint32_t motion_stream_max_batch_ms_for(uint32_t o) { ARG_UNUSED(o); return 0U; }
static uint64_t motion_time_host_elapsed_us(void) { return 0U; }

#endif /* MOTION_HAS_NODE */

/* ===================================================================== */
/* hal/motion.h                                                          */
/* ===================================================================== */
/* Largest ladder step at or below the request; 0 if below the slowest. */
static uint32_t odr_snap(const uint32_t *ladder, size_t len, uint32_t request)
{
	uint32_t chosen = 0U;

	for (size_t i = 0U; i < len; i++) {
		if (ladder[i] > request) {
			break;
		}
		chosen = ladder[i];
	}

	return chosen;
}

static void mhz_to_sensor_value(uint32_t mhz, struct sensor_value *val)
{
	val->val1 = (int32_t)(mhz / 1000U);
	val->val2 = (int32_t)((mhz % 1000U) * 1000U);
}

static bool accel_range_valid(uint16_t range_g)
{
	return (range_g == 2U) || (range_g == 4U) || (range_g == 8U) || (range_g == 16U);
}

static bool gyro_range_valid(uint16_t range_dps)
{
	return (range_dps == 125U) || (range_dps == 250U) || (range_dps == 500U) ||
	       (range_dps == 1000U) || (range_dps == 2000U);
}

static bool accel_osr_valid(uint16_t osr, uint32_t effective_mhz)
{
	if (effective_mhz >= ACCEL_PERFORMANCE_MODE_MHZ) {
		return (osr == 1U) || (osr == 2U) || (osr == 4U);
	}

	switch (osr) {
	case 1U:
	case 2U:
	case 4U:
	case 8U:
	case 16U:
	case 32U:
	case 64U:
	case 128U:
		return true;
	default:
		return false;
	}
}

static bool gyro_osr_valid(uint16_t osr)
{
	return (osr == 1U) || (osr == 2U) || (osr == 4U);
}

static int attr_set_u32(enum sensor_channel chan, enum sensor_attribute attr, uint32_t value)
{
	struct sensor_value val = { .val1 = (int32_t)value, .val2 = 0 };

	return sensor_attr_set(imu, chan, attr, &val);
}

/* ODR first, always: the OSR lookup table is chosen from the rate in effect. */
static int apply_axis(enum sensor_channel chan, uint32_t odr_mhz, uint16_t range, uint16_t osr)
{
	struct sensor_value odr;
	int err;

	mhz_to_sensor_value(odr_mhz, &odr);

	err = sensor_attr_set(imu, chan, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	if (err != 0) {
		LOG_ERR("chan %d rate %u mHz rejected (%d)", (int)chan, odr_mhz, err);
		return err;
	}

	if (odr_mhz == 0U) {
		return 0;
	}

	err = attr_set_u32(chan, SENSOR_ATTR_FULL_SCALE, range);
	if (err != 0) {
		LOG_ERR("chan %d range %u rejected (%d)", (int)chan, range, err);
		return err;
	}

	if (osr != 0U) {
		err = attr_set_u32(chan, SENSOR_ATTR_OVERSAMPLING, osr);
		if (err != 0) {
			LOG_ERR("chan %d osr %u rejected (%d)", (int)chan, osr, err);
			return err;
		}
	}

	return 0;
}

static int apply_config(const struct motion_config *config)
{
	int err;

	err = apply_axis(SENSOR_CHAN_ACCEL_XYZ, config->accel_odr_mhz, config->accel_range_g,
			 config->accel_oversampling);
	if (err != 0) {
		return err;
	}

	return apply_axis(SENSOR_CHAN_GYRO_XYZ, config->gyro_odr_mhz, config->gyro_range_dps,
			  config->gyro_oversampling);
}

int motion_init(void)
{
	if ((imu == NULL) || !device_is_ready(imu)) {
		LOG_ERR("BMI270 not ready");
		return -ERR_MOTION_NOT_READY;
	}

	/*
	 * The extension takes over the interrupt registers the vendor driver
	 * leaves alone, so it has to run after the driver's own init - which
	 * POST_KERNEL ordering guarantees, since this is called from the
	 * application.
	 */
	if (bmi270_ext_init() == 0) {
		state.ext_ready = true;

		if (MOTION_HAS_EVENTS && (motion_stream_init() == 0)) {
			state.stream_ready = true;
		}
	} else {
		LOG_WRN("BMI270 extension unavailable; FIFO and features are off");
	}

	state.initialized = true;

	return 0;
}

uint32_t motion_get_capabilities(void)
{
	uint32_t caps;

	if (!state.initialized) {
		return 0U;
	}

	caps = MOTION_CAP_GYRO | MOTION_CAP_OVERSAMPLING;

	if (MOTION_HAS_DRDY) {
		caps |= MOTION_CAP_DATA_READY;
	}

	if (MOTION_HAS_SLOPE && state.ext_ready) {
		caps |= MOTION_CAP_ANY_MOTION | MOTION_CAP_NO_MOTION | MOTION_CAP_ACTIVITY;
	}

	if (state.ext_ready) {
		caps |= MOTION_CAP_FAULT;
	}

	if (state.stream_ready) {
		caps |= MOTION_CAP_FIFO;
	}

	return caps;
}

int motion_configure(const struct motion_config *config)
{
	struct motion_config effective;
	int err;

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (config == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	effective = *config;

	/* Validate everything before writing anything. */
	if (config->accel_odr_mhz != 0U) {
		effective.accel_odr_mhz = odr_snap(accel_odr_ladder_mhz,
						   ARRAY_SIZE(accel_odr_ladder_mhz),
						   config->accel_odr_mhz);
		if (effective.accel_odr_mhz == 0U) {
			return -ERR_MOTION_INVALID_CONFIG;
		}

		if (!accel_range_valid(config->accel_range_g)) {
			return -ERR_MOTION_INVALID_CONFIG;
		}

		/*
		 * Dropping below the feature engine's floor while something is
		 * armed would leave that event source running but degraded,
		 * with nothing in its behaviour to say so. Disarm first.
		 */
		if (features_armed() &&
		    (effective.accel_odr_mhz < MOTION_FEATURE_MIN_ODR_MHZ)) {
			LOG_ERR("cannot drop to %u mHz while motion features are armed",
				effective.accel_odr_mhz);
			return -ERR_MOTION_INVALID_CONFIG;
		}

		if ((config->accel_oversampling != 0U) &&
		    !accel_osr_valid(config->accel_oversampling, effective.accel_odr_mhz)) {
			return -ERR_MOTION_INVALID_CONFIG;
		}
	}

	if (config->gyro_odr_mhz != 0U) {
		effective.gyro_odr_mhz = odr_snap(gyro_odr_ladder_mhz,
						  ARRAY_SIZE(gyro_odr_ladder_mhz),
						  config->gyro_odr_mhz);
		if (effective.gyro_odr_mhz == 0U) {
			return -ERR_MOTION_INVALID_CONFIG;
		}

		if (!gyro_range_valid(config->gyro_range_dps)) {
			return -ERR_MOTION_INVALID_CONFIG;
		}

		if ((config->gyro_oversampling != 0U) &&
		    !gyro_osr_valid(config->gyro_oversampling)) {
			return -ERR_MOTION_INVALID_CONFIG;
		}
	}

	err = apply_config(&effective);
	if (err != 0) {
		state.configured = false;
		state.running = false;
		return err;
	}

	state.config = effective;
	state.configured = true;
	state.running = (effective.accel_odr_mhz != 0U) || (effective.gyro_odr_mhz != 0U);

	return 0;
}

int motion_set_enabled(bool enabled)
{
	static const struct motion_config suspended;
	int err;

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.configured) {
		return -ERR_MOTION_NOT_CONFIGURED;
	}

	if (enabled == state.running) {
		return 0;
	}

	/* A rate of 0 is what clears PWR_CTRL - see trap 4. */
	err = apply_config(enabled ? &state.config : &suspended);
	if (err != 0) {
		return err;
	}

	state.running = enabled;

	return 0;
}

static int read_channel(enum sensor_channel chan, struct sensor_value *out, uint32_t odr_mhz)
{
	int err;

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.configured || !state.running || (odr_mhz == 0U)) {
		return -ERR_MOTION_NOT_CONFIGURED;
	}

	err = sensor_sample_fetch(imu);
	if (err != 0) {
		LOG_ERR("sample fetch failed (%d)", err);
		return -ERR_MOTION_READ_FAILED;
	}

	err = sensor_channel_get(imu, chan, out);
	if (err != 0) {
		LOG_ERR("chan %d get failed (%d)", (int)chan, err);
		return -ERR_MOTION_READ_FAILED;
	}

	return 0;
}

int motion_read_accel(struct motion_accel_sample *sample)
{
	struct sensor_value val[3];
	int err;

	if (sample == NULL) {
		return -ERR_MOTION_READ_FAILED;
	}

	err = read_channel(SENSOR_CHAN_ACCEL_XYZ, val, state.config.accel_odr_mhz);
	if (err != 0) {
		return err;
	}

	/* The driver hands back m/s^2, so milli-units are already mm/s^2. */
	sample->x_mms2 = (int32_t)sensor_value_to_milli(&val[0]);
	sample->y_mms2 = (int32_t)sensor_value_to_milli(&val[1]);
	sample->z_mms2 = (int32_t)sensor_value_to_milli(&val[2]);

	return 0;
}

/*
 * Gyro channels are radians/s. sensor_rad_to_degrees() would round to whole
 * degrees, which at these rates throws away most of the signal, so scale to
 * millidegrees directly. Worst case is 2000 dps -> ~35e6 micro-rad, and
 * 35e6 * 180000 stays well inside int64.
 */
static int32_t gyro_rad_to_mdps(const struct sensor_value *rad)
{
	int64_t micro_rad = ((int64_t)rad->val1 * 1000000LL) + (int64_t)rad->val2;
	int64_t half = SENSOR_PI / 2;

	if (micro_rad >= 0) {
		return (int32_t)(((micro_rad * 180000LL) + half) / SENSOR_PI);
	}

	return (int32_t)(((micro_rad * 180000LL) - half) / SENSOR_PI);
}

int motion_read_gyro(struct motion_gyro_sample *sample)
{
	struct sensor_value val[3];
	int err;

	if (sample == NULL) {
		return -ERR_MOTION_READ_FAILED;
	}

	err = read_channel(SENSOR_CHAN_GYRO_XYZ, val, state.config.gyro_odr_mhz);
	if (err != 0) {
		return err;
	}

	sample->x_mdps = gyro_rad_to_mdps(&val[0]);
	sample->y_mdps = gyro_rad_to_mdps(&val[1]);
	sample->z_mdps = gyro_rad_to_mdps(&val[2]);

	return 0;
}

int motion_set_event_callback(motion_event_cb_t callback)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!MOTION_HAS_EVENTS) {
		return -ERR_MOTION_NO_TRIGGER;
	}

	state.callback = callback;

	return 0;
}

static int set_data_ready_enabled(bool enabled)
{
#if MOTION_HAS_DRDY
	int err;

	if (!state.stream_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	err = bmi270_ext_data_ready_set_enabled(enabled);
	if (err != 0) {
		LOG_ERR("data-ready %s failed (%d)", enabled ? "enable" : "disable", err);
		return err;
	}

	/*
	 * The interrupt service path needs this: INT_STATUS_1 reports data-ready
	 * whether or not it is mapped to a pin, so without a record of what was
	 * asked for, every watermark drain would also raise a phantom
	 * data-ready - and with the flag never set, no real one is raised at all.
	 */
	state.data_ready_enabled = enabled;

	return 0;
#else
	ARG_UNUSED(enabled);
	return -ERR_MOTION_UNSUPPORTED;
#endif
}

#if MOTION_HAS_SLOPE
/*
 * The feature engine samples the accelerometer, so arming anything against a
 * rate it cannot work at would produce an event source that looks configured
 * and behaves differently from how it reads. Refused rather than logged.
 */
static int slope_rate_is_usable(void)
{
	if (state.config.accel_odr_mhz < MOTION_FEATURE_MIN_ODR_MHZ) {
		LOG_ERR("motion features need at least %u mHz accel rate, have %u",
			MOTION_FEATURE_MIN_ODR_MHZ, state.config.accel_odr_mhz);
		return -ERR_MOTION_INVALID_CONFIG;
	}

	return 0;
}
#endif

static int set_slope_enabled(enum bmi270_ext_slope slope, enum motion_event event, bool configured,
			     bool *tracked, bool enabled)
{
#if MOTION_HAS_SLOPE
	int err;

	if (!state.ext_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	if (enabled) {
		if (!configured) {
			return -ERR_MOTION_NOT_CONFIGURED;
		}

		err = slope_rate_is_usable();
		if (err != 0) {
			return err;
		}
	}

	err = bmi270_ext_slope_set_enabled(slope, enabled);
	if (err != 0) {
		LOG_ERR("slope %d %s failed (%d)", (int)slope, enabled ? "enable" : "disable", err);
		return err;
	}

	*tracked = enabled;

	return 0;
#else
	ARG_UNUSED(slope);
	ARG_UNUSED(event);
	ARG_UNUSED(configured);
	ARG_UNUSED(tracked);
	ARG_UNUSED(enabled);
	return -ERR_MOTION_UNSUPPORTED;
#endif
}

int motion_set_event_enabled(enum motion_event event, bool enabled)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	switch (event) {
	case MOTION_EVENT_DATA_READY:
		return set_data_ready_enabled(enabled);
	case MOTION_EVENT_ANY_MOTION:
		return set_slope_enabled(BMI270_EXT_SLOPE_ANY, event, state.any_motion_configured,
					 &state.any_motion_enabled, enabled);
	case MOTION_EVENT_NO_MOTION:
		return set_slope_enabled(BMI270_EXT_SLOPE_NO, event, state.no_motion_configured,
					 &state.no_motion_enabled, enabled);
	default:
		return -ERR_MOTION_UNSUPPORTED;
	}
}

int motion_get_any_motion_limits(struct motion_any_motion_limits *limits)
{
	if (!MOTION_HAS_ANY_MOTION) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (limits == NULL) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	limits->threshold_min_mg = ANY_MOTION_THRESHOLD_MIN_MG;
	limits->threshold_max_mg = ANY_MOTION_THRESHOLD_MAX_MG;
	limits->duration_max_ms = ANY_MOTION_DURATION_MAX_MS;
	limits->duration_step_ms = ANY_MOTION_DURATION_STEP_MS;

	return 0;
}

/*
 * Shared by both slope features: they are the same comparison against the
 * same slope, mirrored, and the hardware gives them identical fields.
 */
static int configure_slope(enum bmi270_ext_slope slope, uint16_t threshold_mg,
			   uint32_t duration_ms, bool *configured)
{
#if !MOTION_HAS_SLOPE
	ARG_UNUSED(slope);
	ARG_UNUSED(threshold_mg);
	ARG_UNUSED(duration_ms);
	ARG_UNUSED(configured);
	return -ERR_MOTION_UNSUPPORTED;
#else
	uint32_t rounded_ms;
	int err;

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.ext_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	if ((threshold_mg < ANY_MOTION_THRESHOLD_MIN_MG) ||
	    (threshold_mg > ANY_MOTION_THRESHOLD_MAX_MG) ||
	    (duration_ms > ANY_MOTION_DURATION_MAX_MS)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	rounded_ms = (duration_ms / ANY_MOTION_DURATION_STEP_MS) * ANY_MOTION_DURATION_STEP_MS;

	/*
	 * Reaches the chip immediately. The old path through the vendor
	 * driver only cached these until a trigger was set, which is why the
	 * ordering used to be load-bearing; it no longer is.
	 */
	err = bmi270_ext_slope_configure(slope, threshold_mg, rounded_ms);
	if (err != 0) {
		LOG_ERR("slope %d config %u mg / %u ms rejected (%d)", (int)slope, threshold_mg,
			rounded_ms, err);
		return err;
	}

	*configured = true;

	return 0;
#endif
}

int motion_configure_any_motion(const struct motion_any_motion_config *config)
{
	if (config == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	return configure_slope(BMI270_EXT_SLOPE_ANY, config->threshold_mg, config->duration_ms,
			       &state.any_motion_configured);
}

int motion_configure_no_motion(const struct motion_no_motion_config *config)
{
	if (config == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	return configure_slope(BMI270_EXT_SLOPE_NO, config->threshold_mg, config->duration_ms,
			       &state.no_motion_configured);
}

/* --- Activity, health ------------------------------------------------------ */

int motion_set_activity_enabled(bool enabled)
{
	int err;

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!MOTION_HAS_SLOPE || !state.ext_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	/* Classification runs on the same feature engine as the slope events. */
	if (enabled && (state.config.accel_odr_mhz < MOTION_FEATURE_MIN_ODR_MHZ)) {
		LOG_ERR("activity needs at least %u mHz accel rate, have %u",
			MOTION_FEATURE_MIN_ODR_MHZ, state.config.accel_odr_mhz);
		return -ERR_MOTION_INVALID_CONFIG;
	}

	err = bmi270_ext_activity_set_enabled(enabled);
	if (err == 0) {
		state.activity_enabled = enabled;
	}

	return err;
}

int motion_get_activity(enum motion_activity *activity)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!MOTION_HAS_SLOPE || !state.ext_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	if (activity == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	return bmi270_ext_activity_get(activity);
}

int motion_get_fault(struct motion_fault *fault)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.ext_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	if (fault == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	return bmi270_ext_get_fault(fault);
}

/* --- Streaming ------------------------------------------------------------- */


int motion_stream_start(uint32_t batch_ms)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.stream_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	if (!state.configured || (state.config.accel_odr_mhz == 0U)) {
		return -ERR_MOTION_NOT_CONFIGURED;
	}

	return motion_stream_begin(state.config.accel_range_g, state.config.accel_odr_mhz,
				   batch_ms);
}

int motion_stream_stop(void)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.stream_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	return motion_stream_end();
}

int motion_stream_max_batch_ms(uint32_t *batch_ms)
{
	uint32_t limit;

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.stream_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	if (batch_ms == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	if (!state.configured || (state.config.accel_odr_mhz == 0U)) {
		return -ERR_MOTION_NOT_CONFIGURED;
	}

	limit = motion_stream_max_batch_ms_for(state.config.accel_odr_mhz);
	if (limit == 0U) {
		return -ERR_MOTION_NOT_CONFIGURED;
	}

	*batch_ms = limit;

	return 0;
}

int motion_window_read(struct motion_accel_sample *out, size_t max, size_t *count)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.stream_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	return motion_stream_window_read(out, max, count);
}

int motion_capture_start(struct motion_accel_sample *storage, size_t capacity)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.stream_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	return motion_stream_capture_begin(storage, capacity);
}

int motion_capture_stop(struct motion_capture_result *result)
{
	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.stream_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	return motion_stream_capture_end(result);
}

int motion_get_diagnostics(struct motion_diagnostics *diag)
{
	uint32_t ticks = 0U;

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (!state.ext_ready) {
		return -ERR_MOTION_UNSUPPORTED;
	}

	if (diag == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	memset(diag, 0, sizeof(*diag));

	/*
	 * Read whether or not anything has been configured yet. A word of
	 * zeroes is itself the answer to "did the threshold land", so an
	 * unconfigured feature is reported rather than skipped.
	 */
	if (MOTION_HAS_SLOPE) {
		int err = bmi270_ext_slope_read_raw(BMI270_EXT_SLOPE_ANY, &diag->any_motion_raw[0],
						    &diag->any_motion_raw[1]);

		if (err == 0) {
			err = bmi270_ext_slope_read_raw(BMI270_EXT_SLOPE_NO,
							&diag->no_motion_raw[0],
							&diag->no_motion_raw[1]);
		}

		diag->slope_readback_valid = (err == 0);
	}

	if (bmi270_ext_sensortime(&ticks) == 0) {
		diag->sensortime = ticks;
		diag->sensortime_valid = true;
	}

	{
		uint16_t level = 0U;

		if (bmi270_ext_fifo_level(&level) == 0) {
			diag->fifo_fill_bytes = level;
			diag->fifo_fill_valid = true;
		}
	}

	diag->clock_drift_valid = (motion_time_drift_ppm(&diag->clock_drift_ppm) == 0);
	diag->observed_span_us = motion_time_host_elapsed_us();

	return 0;
}

/*
 * TODO: step counter and detector.
 *
 * SC_26 already carries their enables and this firmware writes that register
 * for activity classification, so the remaining work is the step-count
 * readback (SC_OUT, page 0) and deciding what the product does with it.
 */
