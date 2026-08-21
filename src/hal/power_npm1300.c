// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/power.h for the nPM1300 PMIC (U2).
 * Verified against npm13xx_charger in NCS v3.4.0 (Zephyr 4.4.0) and the
 * nPM1300 product specification's BCHGCHARGESTATUS/BCHGERRREASON layouts.
 *
 * power_init() cannot fail because the PMIC is dead - the MCU only runs
 * because BUCK1 is up. It fails when the I2C register interface is
 * unreachable, which is why it probes with a real fetch instead of trusting
 * device_is_ready(): that only reports how boot-time init went, not whether
 * the bus answers now.
 *
 * charger_millidegc is SENSOR_CHAN_DIE_TEMP. SENSOR_CHAN_GAUGE_TEMP returns
 * -ENOTSUP - no thermistor is fitted, the devicetree says thermistor-ohms = 0,
 * and that is enum index 0, which is the condition the driver actually tests.
 * It is never read here.
 *
 * Coherence trap: external_power_present comes from
 * SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS through sensor_channel_get(), which
 * hands back the byte the fetch already cached. The obvious alternative - the
 * SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT attribute, which is what the
 * npm13xx_ek sample reaches for - issues its own I2C read, so it would report
 * an instant the rest of the snapshot does not.
 *
 * current_milliamp deserves distrust near idle, and the power service must not
 * read small values as real. The driver decodes the raw IBAT code against a
 * full scale chosen from the charger's private ibat_stat, and that status flaps
 * between consecutive samples: 62.5 mA charging (current-microamp 50 mA x
 * 125/100) against 224 mA discharging (dischg-limit-microamp 200 mA x 112/100),
 * a 3.6x step decided by a status bit alone. Measured on this board at idle,
 * consecutive samples read -656, -3503, -7006, -14013 and -28027 uA - exact
 * doublings, with nothing on the board changing. An idle ibat_stat decodes
 * against a full scale of 0, so the channel can also read exactly 0 while the
 * cell is genuinely under load.
 *
 * Faults latch in software as well as in hardware. BCHGERRREASON latches until
 * TASKCLEARCHGERR, which nothing here issues - power_init() observes only, and
 * hal/power.h exposes no clear entry point - but DIETEMPHIGHCHGPAUSED is a live
 * status bit. Without the software latch POWER_FAULT_OVER_TEMPERATURE would
 * mean "now" while every other bit means "since boot".
 */

#include <power.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

#define POWER_NODE DT_NODELABEL(npm1300_charger)

/*
 * The application also builds for the nRF52840 DK, which has no such node.
 * Everything still compiles there; power_init() reports the PMIC absent.
 */
#if DT_NODE_HAS_STATUS_OKAY(POWER_NODE)
static const struct device *const charger = DEVICE_DT_GET(POWER_NODE);
#else
static const struct device *const charger = NULL;
#endif

/* BCHGCHARGESTATUS, offset 0x34. */
#define CHG_STATUS_BATTERY_DETECTED BIT(0)
#define CHG_STATUS_COMPLETED BIT(1)
#define CHG_STATUS_TRICKLE BIT(2)
#define CHG_STATUS_CONSTANT_CURRENT BIT(3)
#define CHG_STATUS_CONSTANT_VOLTAGE BIT(4)
#define CHG_STATUS_RECHARGE BIT(5)
#define CHG_STATUS_DIE_TEMP_HIGH_PAUSED BIT(6)
#define CHG_STATUS_SUPPLEMENT_ACTIVE BIT(7)

/* BCHGERRREASON, offset 0x36. Bit 7 is unassigned. */
#define CHG_ERR_NTC_SENSOR BIT(0)
#define CHG_ERR_VBAT_SENSOR BIT(1)
#define CHG_ERR_VBAT_LOW BIT(2)
#define CHG_ERR_VTRICKLE BIT(3)
#define CHG_ERR_MEAS_TIMEOUT BIT(4)
#define CHG_ERR_CHARGE_TIMEOUT BIT(5)
#define CHG_ERR_TRICKLE_TIMEOUT BIT(6)

#define VBUS_STATUS_PRESENT BIT(0)

enum read_index {
	READ_VOLTAGE = 0,
	READ_CURRENT,
	READ_DIE_TEMP,
	READ_STATUS,
	READ_ERROR,
	READ_VBUS,
	READ_COUNT,
};

static const enum sensor_channel read_channels[READ_COUNT] = {
	[READ_VOLTAGE] = SENSOR_CHAN_GAUGE_VOLTAGE,
	[READ_CURRENT] = SENSOR_CHAN_GAUGE_AVG_CURRENT,
	[READ_DIE_TEMP] = SENSOR_CHAN_DIE_TEMP,
	[READ_STATUS] = (enum sensor_channel)SENSOR_CHAN_NPM13XX_CHARGER_STATUS,
	[READ_ERROR] = (enum sensor_channel)SENSOR_CHAN_NPM13XX_CHARGER_ERROR,
	[READ_VBUS] = (enum sensor_channel)SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
};

static struct {
	bool initialized;
	uint32_t fault_flags;
	uint8_t logged_error;
} state;

/*
 * Active stages first: COMPLETED can still be set from the cycle that just
 * finished, and what is happening now is the more specific answer.
 */
static enum power_charge_state decode_charge_state(uint8_t status)
{
	if ((status & CHG_STATUS_TRICKLE) != 0U) {
		return POWER_CHARGE_STATE_TRICKLE;
	}

	if ((status & CHG_STATUS_CONSTANT_CURRENT) != 0U) {
		return POWER_CHARGE_STATE_CONSTANT_CURRENT;
	}

	if ((status & CHG_STATUS_CONSTANT_VOLTAGE) != 0U) {
		return POWER_CHARGE_STATE_CONSTANT_VOLTAGE;
	}

	if ((status & CHG_STATUS_COMPLETED) != 0U) {
		return POWER_CHARGE_STATE_COMPLETE;
	}

	return POWER_CHARGE_STATE_NONE;
}

static uint32_t decode_faults(uint8_t error, uint8_t status)
{
	uint32_t flags = 0U;

	/*
	 * A dead NTC reads as a temperature fault rather than a measurement
	 * one: what it means is that the safe charge window is no longer being
	 * enforced. It should never appear on this board - with no thermistor
	 * fitted the driver disables battery temperature monitoring at init.
	 */
	if ((error & CHG_ERR_NTC_SENSOR) != 0U) {
		flags |= POWER_FAULT_BATTERY_TEMPERATURE;
	}

	if ((error & (CHG_ERR_VBAT_LOW | CHG_ERR_VTRICKLE)) != 0U) {
		flags |= POWER_FAULT_BATTERY_VOLTAGE;
	}

	if ((error & (CHG_ERR_VBAT_SENSOR | CHG_ERR_MEAS_TIMEOUT)) != 0U) {
		flags |= POWER_FAULT_MEASUREMENT;
	}

	if ((error & (CHG_ERR_CHARGE_TIMEOUT | CHG_ERR_TRICKLE_TIMEOUT)) != 0U) {
		flags |= POWER_FAULT_CHARGE_TIMEOUT;
	}

	/* Not in BCHGERRREASON - the thermal pause is a status bit. */
	if ((status & CHG_STATUS_DIE_TEMP_HIGH_PAUSED) != 0U) {
		flags |= POWER_FAULT_OVER_TEMPERATURE;
	}

	return flags;
}

int power_init(void)
{
	int err;

	if ((charger == NULL) || !device_is_ready(charger)) {
		LOG_ERR("nPM1300 charger not ready");
		return -ERR_POWER_NOT_READY;
	}

	/*
	 * The only writes this makes are the ADC task registers that start a
	 * measurement; no rail, charge setting or regulator mode changes.
	 */
	err = sensor_sample_fetch(charger);
	if (err != 0) {
		LOG_ERR("nPM1300 register interface unreachable (%d)", err);
		return -ERR_POWER_NOT_READY;
	}

	state.initialized = true;

	return 0;
}

int power_read(struct power_status *status)
{
	struct sensor_value values[READ_COUNT];
	struct power_status snapshot;
	uint8_t raw_status;
	uint8_t raw_error;
	int err;

	if (!state.initialized) {
		return -ERR_POWER_NOT_INITIALIZED;
	}

	if (status == NULL) {
		return -ERR_POWER_READ_FAILED;
	}

	/* One fetch, then every channel out of the cache it filled. */
	err = sensor_sample_fetch(charger);
	if (err != 0) {
		LOG_ERR("sample fetch failed (%d)", err);
		return -ERR_POWER_READ_FAILED;
	}

	for (size_t i = 0U; i < READ_COUNT; i++) {
		err = sensor_channel_get(charger, read_channels[i], &values[i]);
		if (err != 0) {
			LOG_ERR("chan %d get failed (%d)", (int)read_channels[i], err);
			return -ERR_POWER_READ_FAILED;
		}
	}

	raw_status = (uint8_t)values[READ_STATUS].val1;
	raw_error = (uint8_t)values[READ_ERROR].val1;

	state.fault_flags |= decode_faults(raw_error, raw_status);

	/*
	 * POWER_FAULT_* is coarser than the register, so keep the raw byte.
	 * Only on change: BCHGERRREASON stays latched, and logging it every
	 * read would repeat one fault for the rest of the run.
	 */
	if (raw_error != state.logged_error) {
		LOG_WRN("BCHGERRREASON 0x%02x, BCHGCHARGESTATUS 0x%02x", raw_error, raw_status);
		state.logged_error = raw_error;
	}

	snapshot.battery_millivolt = (int32_t)sensor_value_to_milli(&values[READ_VOLTAGE]);
	snapshot.current_milliamp = (int32_t)sensor_value_to_milli(&values[READ_CURRENT]);
	snapshot.charger_millidegc = (int32_t)sensor_value_to_milli(&values[READ_DIE_TEMP]);
	snapshot.charge_state = decode_charge_state(raw_status);
	snapshot.fault_flags = state.fault_flags;
	snapshot.external_power_present =
		((uint8_t)values[READ_VBUS].val1 & VBUS_STATUS_PRESENT) != 0U;
	snapshot.battery_detected = (raw_status & CHG_STATUS_BATTERY_DETECTED) != 0U;

	*status = snapshot;

	return 0;
}
