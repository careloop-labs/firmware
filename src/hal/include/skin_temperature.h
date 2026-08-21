// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Skin temperature.
 *
 * Named skin_temperature because power.h also reports a temperature. Measures
 * the sensor's own package against the wearer; how that tracks body
 * temperature is for the layers above.
 */

#ifndef HAL_SKIN_TEMPERATURE_H_
#define HAL_SKIN_TEMPERATURE_H_

#include <stdint.h>

#include "errors.h"

/* Skin temperature specific error codes. */
#define ERR_SKIN_TEMPERATURE_NOT_READY (ERR_HAL_SKIN_TEMPERATURE + 1)
#define ERR_SKIN_TEMPERATURE_NOT_INITIALIZED (ERR_HAL_SKIN_TEMPERATURE + 2)
#define ERR_SKIN_TEMPERATURE_READ_FAILED (ERR_HAL_SKIN_TEMPERATURE + 3)

/**
 * @brief Bring up the sensor, including its identity check.
 *
 * @return 0, -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED if the sensor does not
 *         answer or is not the expected part, or a negative errno.
 */
int skin_temperature_init(void);

/**
 * @brief Nominal time between conversions, milliseconds.
 *
 * Reported at runtime because it is a property of the fitted sensor. A caller
 * that pins it at compile time samples at the wrong rate once the sensor
 * changes. Sampling faster only yields the same value twice, and the first
 * read after init is always too early.
 *
 * @param period_ms Destination. Untouched on failure.
 * @return 0, or -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED.
 */
int skin_temperature_get_conversion_period_ms(uint32_t *period_ms);

/**
 * @brief Read the most recent completed conversion. Does not block or cache.
 *
 * Fails rather than repeating the previous value, because a duplicated sample
 * is indistinguishable from a stalled sensor once logged.
 *
 * @param millidegc Destination, millidegrees Celsius. Untouched on failure.
 * @return 0, -ERR_SKIN_TEMPERATURE_NOT_READY if no fresh conversion is
 *         available, -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED,
 *         -ERR_SKIN_TEMPERATURE_READ_FAILED, or a negative errno.
 */
int skin_temperature_read(int32_t *millidegc);

#endif /* HAL_SKIN_TEMPERATURE_H_ */
