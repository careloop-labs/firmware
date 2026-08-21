// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Battery and charging.
 *
 * Reports what the cell is doing, in fixed units, without interpretation.
 * State of charge, thresholds and filtering are product policy and belong in
 * the power service.
 *
 * Battery temperature is absent: this product fits no cell thermistor.
 */

#ifndef HAL_POWER_H_
#define HAL_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

/* Power specific error codes. */
#define ERR_POWER_NOT_READY (ERR_HAL_POWER + 1)   /* hardware did not answer at init */
#define ERR_POWER_NOT_INITIALIZED (ERR_HAL_POWER + 2)
#define ERR_POWER_READ_FAILED (ERR_HAL_POWER + 3)

/**
 * @brief Stages of a constant-current/constant-voltage charge.
 *
 * POWER_CHARGE_STATE_NONE covers both a full cell and nothing plugged in; use
 * external_power_present to tell them apart.
 */
enum power_charge_state {
    POWER_CHARGE_STATE_NONE = 0,
    POWER_CHARGE_STATE_TRICKLE,
    POWER_CHARGE_STATE_CONSTANT_CURRENT,
    POWER_CHARGE_STATE_CONSTANT_VOLTAGE,
    POWER_CHARGE_STATE_COMPLETE,
};

/*
 * Latched fault bits, deliberately coarser than any one charger's error
 * register so fault handling survives a change of part. A bit means "this
 * happened since the last clear", not "this is happening now".
 */
#define POWER_FAULT_BATTERY_TEMPERATURE (1U << 0) /* outside safe charge window */
#define POWER_FAULT_BATTERY_VOLTAGE (1U << 1)     /* outside safe range */
#define POWER_FAULT_CHARGE_TIMEOUT (1U << 2)
#define POWER_FAULT_MEASUREMENT (1U << 3)
#define POWER_FAULT_OVER_TEMPERATURE (1U << 4)    /* charging circuit too hot */

/**
 * @brief One coherent snapshot of the power subsystem.
 *
 * All fields describe the same instant, which is why there is no per-value
 * getter.
 */
struct power_status {
    int32_t battery_millivolt;

    /** Signed: positive into the cell, negative out of it. Averaged. */
    int32_t current_milliamp;

    /** Charging circuit, not the cell and not the wearer. */
    int32_t charger_millidegc;

    enum power_charge_state charge_state;

    /** Latched POWER_FAULT_* bits, 0 when nothing is latched. */
    uint32_t fault_flags;

    /** An external supply is powering the device. */
    bool external_power_present;

    bool battery_detected;
};

/**
 * @brief Verify the power hardware is reachable.
 *
 * Observes only; does not change any rail, charge setting or regulator mode.
 *
 * @return 0, -ERR_POWER_NOT_READY, or a negative errno.
 */
int power_init(void);

/**
 * @brief Take a fresh reading. Measures on every call; no caching.
 *
 * @param status Destination. Untouched on failure.
 * @return 0, -ERR_POWER_NOT_INITIALIZED, -ERR_POWER_READ_FAILED, or a
 *         negative errno.
 */
int power_read(struct power_status *status);

#endif /* HAL_POWER_H_ */
