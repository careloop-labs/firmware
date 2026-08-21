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
 *    MOTION_HAS_ANY_MOTION below tests for the marker, so a devicetree
 *    missing it loses the capability rather than corrupting the sensor.
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
 *    That same behaviour is what motion_set_enabled() uses to suspend, since
 *    the driver exposes no other way.
 *
 * Ladders, Hz. Accel: 0.78125 1.5625 3.125 6.25 12.5 25 50 100 200 400 800
 * 1600. Gyro: 25 50 100 200 400 800 1600 3200. Ranges must be exact -
 * 2/4/8/16 g, 125/250/500/1000/2000 dps - or the driver returns -ENOTSUP.
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
 * Channel units differ: accel comes back in m/s^2, gyro in radians/s. Only
 * the accel maps straight onto the HAL's units.
 *
 * Not reachable through this driver: FIFO, temperature, step counter, tap,
 * wrist gesture, PM_DEVICE.
 */

#include <motion.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(motion, LOG_LEVEL_INF);

#define MOTION_NODE DT_NODELABEL(bmi270)

/*
 * The application also builds for the nRF52840 DK, which has no such node.
 * Everything still compiles there; motion_init() reports the sensor absent.
 */
#if DT_NODE_HAS_STATUS_OKAY(MOTION_NODE)

static const struct device *const imu = DEVICE_DT_GET(MOTION_NODE);

#if IS_ENABLED(CONFIG_BMI270_TRIGGER) && DT_PROP_HAS_IDX(MOTION_NODE, irq_gpios, 1)
#define MOTION_HAS_DRDY 1
#else
#define MOTION_HAS_DRDY 0
#endif

/* The compatible test is trap 1: without the marker, arming would corrupt. */
#if IS_ENABLED(CONFIG_BMI270_TRIGGER) && DT_PROP_HAS_IDX(MOTION_NODE, irq_gpios, 0) && \
	DT_NODE_HAS_COMPAT(MOTION_NODE, bosch_bmi270_base)
#define MOTION_HAS_ANY_MOTION 1
#else
#define MOTION_HAS_ANY_MOTION 0
#endif

#else /* no node */

static const struct device *const imu = NULL;
#define MOTION_HAS_DRDY 0
#define MOTION_HAS_ANY_MOTION 0

#endif

#define MOTION_HAS_EVENTS (MOTION_HAS_DRDY || MOTION_HAS_ANY_MOTION)

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

#define ANY_MOTION_THRESHOLD_MIN_MG 1U
#define ANY_MOTION_THRESHOLD_MAX_MG 1000U
#define ANY_MOTION_DURATION_MAX_MS 81900U
#define ANY_MOTION_DURATION_STEP_MS 20U

static struct {
	bool initialized;
	bool configured;
	bool running;
	bool any_motion_configured;
	bool any_motion_enabled;
	struct motion_config config;
	motion_event_cb_t callback;
} state;

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

#if MOTION_HAS_EVENTS
static void trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	motion_event_cb_t callback = state.callback;

	ARG_UNUSED(dev);

	if (callback == NULL) {
		return;
	}

	switch ((int)trig->type) {
	case SENSOR_TRIG_DATA_READY:
		callback(MOTION_EVENT_DATA_READY);
		break;
	case SENSOR_TRIG_MOTION:
		callback(MOTION_EVENT_ANY_MOTION);
		break;
	default:
		break;
	}
}

/* The driver keeps these pointers, so they outlive the call by design. */
static const struct sensor_trigger drdy_trigger = {
	.type = SENSOR_TRIG_DATA_READY,
	.chan = SENSOR_CHAN_ACCEL_XYZ,
};

static const struct sensor_trigger any_motion_trigger = {
	.type = SENSOR_TRIG_MOTION,
	.chan = SENSOR_CHAN_ACCEL_XYZ,
};
#endif /* MOTION_HAS_EVENTS */

int motion_init(void)
{
	if ((imu == NULL) || !device_is_ready(imu)) {
		LOG_ERR("BMI270 not ready");
		return -ERR_MOTION_NOT_READY;
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

	if (MOTION_HAS_ANY_MOTION) {
		caps |= MOTION_CAP_ANY_MOTION;
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
	int err = sensor_trigger_set(imu, &drdy_trigger, enabled ? trigger_handler : NULL);

	if (err != 0) {
		LOG_ERR("data-ready trigger %s failed (%d)", enabled ? "enable" : "disable", err);
		return err;
	}

	return 0;
#else
	ARG_UNUSED(enabled);
	return -ERR_MOTION_UNSUPPORTED;
#endif
}

static int set_any_motion_enabled(bool enabled)
{
#if MOTION_HAS_ANY_MOTION
	int err;

	/* Trap 2: thresholds only reach the chip through trigger_set. */
	if (enabled && !state.any_motion_configured) {
		return -ERR_MOTION_NOT_CONFIGURED;
	}

	err = sensor_trigger_set(imu, &any_motion_trigger, enabled ? trigger_handler : NULL);
	if (err != 0) {
		LOG_ERR("any-motion trigger %s failed (%d)", enabled ? "enable" : "disable", err);
		return err;
	}

	state.any_motion_enabled = enabled;

	return 0;
#else
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
		return set_any_motion_enabled(enabled);
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

int motion_configure_any_motion(const struct motion_any_motion_config *config)
{
#if !MOTION_HAS_ANY_MOTION
	ARG_UNUSED(config);
	return -ERR_MOTION_UNSUPPORTED;
#else
	struct sensor_value threshold;
	uint32_t duration_ms;
	int err;

	if (!state.initialized) {
		return -ERR_MOTION_NOT_INITIALIZED;
	}

	if (config == NULL) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	if ((config->threshold_mg < ANY_MOTION_THRESHOLD_MIN_MG) ||
	    (config->threshold_mg > ANY_MOTION_THRESHOLD_MAX_MG) ||
	    (config->duration_ms > ANY_MOTION_DURATION_MAX_MS)) {
		return -ERR_MOTION_INVALID_CONFIG;
	}

	duration_ms = (config->duration_ms / ANY_MOTION_DURATION_STEP_MS) *
		      ANY_MOTION_DURATION_STEP_MS;

	/*
	 * Threshold is a micro-g fraction below full scale; any val1 > 0 tells
	 * the driver to clamp, which is how full scale is requested.
	 */
	if (config->threshold_mg >= ANY_MOTION_THRESHOLD_MAX_MG) {
		threshold.val1 = 1;
		threshold.val2 = 0;
	} else {
		threshold.val1 = 0;
		threshold.val2 = (int32_t)config->threshold_mg * 1000;
	}

	err = sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SLOPE_TH, &threshold);
	if (err != 0) {
		LOG_ERR("any-motion threshold %u mg rejected (%d)", config->threshold_mg, err);
		return err;
	}

	err = attr_set_u32(SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SLOPE_DUR, duration_ms);
	if (err != 0) {
		LOG_ERR("any-motion duration %u ms rejected (%d)", duration_ms, err);
		return err;
	}

	state.any_motion_configured = true;

	/* Re-arm so a retune while enabled actually reaches the chip. */
	if (state.any_motion_enabled) {
		err = sensor_trigger_set(imu, &any_motion_trigger, trigger_handler);
		if (err != 0) {
			LOG_ERR("any-motion re-arm failed (%d)", err);
			return err;
		}
	}

	return 0;
#endif
}

/*
 * TODO: additional BMI270 features - step counter, FIFO.
 *
 * Neither is reachable from here alone: the Zephyr driver implements no FIFO
 * path and exposes no step-counter channel, so both need driver work first,
 * plus a capability bit and entry points in motion.h.
 */
