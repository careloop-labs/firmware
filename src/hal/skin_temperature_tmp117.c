// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/skin_temperature.h for the TMP117 (U3), I2C 0x48.
 * Verified against the tmp11x driver in NCS v3.4.0 (Zephyr 4.4.0).
 *
 * device_is_ready() is a real identity check: the tmp11x driver reads the
 * device-ID register and refuses anything that is not a TMP11x.
 *
 * The conversion period is read out of the devicetree rather than fixed here.
 * The driver does not leave the sensor at its power-on defaults - init writes
 * both the conversion rate and the averaging count from the odr and
 * oversampling properties. Our node sets neither, so the binding defaults
 * apply: odr 0x200 (1000 ms) and oversampling 0x20 (8 samples), which happen
 * to be the same as the TMP117's power-on state. That is why the answer is
 * ~1000 ms. Change the dts and the number reported here follows, which is what
 * the header asks for.
 *
 * The cycle is the longer of the programmed rate and the time the averaging
 * itself needs - 64 averaged samples cannot be delivered every 125 ms, so the
 * sensor stretches the period instead. Both tables below, hence the MAX.
 *
 * sensor_sample_fetch() returns -EBUSY until DATA_READY sets; that is
 * ERR_SKIN_TEMPERATURE_NOT_READY, not an error to retry internally. Reading
 * the result clears DATA_READY, so a second read inside one period reports
 * not-ready rather than handing back the same conversion twice - which is
 * exactly the "fails rather than repeating" the header requires, and it comes
 * from the hardware rather than from bookkeeping here.
 *
 * init() deliberately does not probe with a read, unlike power_init(). At
 * 1000 ms the first conversion completes about a second after power-on, far
 * later than boot reaches this point, so a probe would return -EBUSY and
 * report a perfectly healthy sensor as broken. device_is_ready() already
 * proves the part answered and identified itself.
 */

#include <skin_temperature.h>

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(skin_temperature, LOG_LEVEL_INF);

#define SKIN_TEMPERATURE_NODE DT_NODELABEL(tmp117)

/*
 * The application also builds for the nRF52840 DK, which has no such node.
 * Everything still compiles there; skin_temperature_init() reports it absent.
 */
#if DT_NODE_HAS_STATUS_OKAY(SKIN_TEMPERATURE_NODE)
static const struct device *const sensor = DEVICE_DT_GET(SKIN_TEMPERATURE_NODE);
/* Both properties are enums of evenly spaced register field values. */
#define SKIN_TEMPERATURE_ODR_IDX (DT_PROP(SKIN_TEMPERATURE_NODE, odr) >> 7)
#define SKIN_TEMPERATURE_AVG_IDX (DT_PROP(SKIN_TEMPERATURE_NODE, oversampling) >> 5)
#else
static const struct device *const sensor = NULL;
#define SKIN_TEMPERATURE_ODR_IDX 0
#define SKIN_TEMPERATURE_AVG_IDX 0
#endif

/*
 * TMP117 datasheet, conversion cycle time in continuous-conversion mode.
 * 15.5 ms rounds up to 16: over-reporting the period costs one late sample,
 * under-reporting hands the caller duplicates.
 */
static const uint32_t odr_period_ms[] = {
	16U, 125U, 250U, 500U, 1000U, 4000U, 8000U, 16000U,
};

/* Floor imposed by the averaging alone, indexed 1, 8, 32, 64 samples. */
static const uint32_t averaging_floor_ms[] = {
	16U, 125U, 500U, 1000U,
};

BUILD_ASSERT(SKIN_TEMPERATURE_ODR_IDX < ARRAY_SIZE(odr_period_ms), "odr outside binding enum");
BUILD_ASSERT(SKIN_TEMPERATURE_AVG_IDX < ARRAY_SIZE(averaging_floor_ms),
	     "oversampling outside binding enum");

static bool initialized;

int skin_temperature_init(void)
{
	if ((sensor == NULL) || !device_is_ready(sensor)) {
		LOG_ERR("TMP117 not ready");
		return -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED;
	}

	initialized = true;

	return 0;
}

int skin_temperature_get_conversion_period_ms(uint32_t *period_ms)
{
	if (!initialized || (period_ms == NULL)) {
		return -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED;
	}

	*period_ms = MAX(odr_period_ms[SKIN_TEMPERATURE_ODR_IDX],
			 averaging_floor_ms[SKIN_TEMPERATURE_AVG_IDX]);

	return 0;
}

int skin_temperature_read(int32_t *millidegc)
{
	struct sensor_value value;
	int err;

	if (!initialized) {
		return -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED;
	}

	if (millidegc == NULL) {
		return -ERR_SKIN_TEMPERATURE_READ_FAILED;
	}

	err = sensor_sample_fetch_chan(sensor, SENSOR_CHAN_AMBIENT_TEMP);
	if (err == -EBUSY) {
		/* No conversion has completed since the last read. */
		return -ERR_SKIN_TEMPERATURE_NOT_READY;
	}

	if (err != 0) {
		LOG_ERR("sample fetch failed (%d)", err);
		return -ERR_SKIN_TEMPERATURE_READ_FAILED;
	}

	err = sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &value);
	if (err != 0) {
		LOG_ERR("channel get failed (%d)", err);
		return -ERR_SKIN_TEMPERATURE_READ_FAILED;
	}

	*millidegc = (int32_t)sensor_value_to_milli(&value);

	return 0;
}
