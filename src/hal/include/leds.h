// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Indicator LEDs. On/off only - no brightness, no colour, no hardware
 * blinking. Patterns and their meanings are product policy.
 *
 * Channels are numbered rather than named so the mapping from meaning to
 * channel stays one level up. LEDS_COUNT is the one value a different board
 * would change; loop over it rather than naming channels individually.
 */

#ifndef HAL_LEDS_H_
#define HAL_LEDS_H_

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

/* LED specific error codes. */
#define ERR_LEDS_NOT_READY (ERR_HAL_LEDS + 1)
#define ERR_LEDS_NOT_INITIALIZED (ERR_HAL_LEDS + 2)
#define ERR_LEDS_INVALID_CHANNEL (ERR_HAL_LEDS + 3)
#define ERR_LEDS_NOT_CONTROLLABLE (ERR_HAL_LEDS + 4) /* exists, but not ours to drive */
#define ERR_LEDS_WRITE_FAILED (ERR_HAL_LEDS + 5)

/** Channels are 0..LEDS_COUNT-1. */
#define LEDS_COUNT 3U

/** Mask with every channel set. */
#define LEDS_MASK_ALL ((1U << LEDS_COUNT) - 1U)

/**
 * @brief Bring up the LED hardware and turn every channel off.
 *
 * LED state can survive a warm reset, so without this an LED left on by the
 * previous image reads as a signal from the new one.
 *
 * @return 0, -ERR_LEDS_NOT_READY, or a negative errno.
 */
int leds_init(void);

/**
 * @brief Turn one LED on or off.
 *
 * @param channel Channel index, 0 to LEDS_COUNT-1.
 * @return 0, -ERR_LEDS_INVALID_CHANNEL, -ERR_LEDS_NOT_CONTROLLABLE,
 *         -ERR_LEDS_WRITE_FAILED, or a negative errno.
 */
int leds_set(uint8_t channel, bool on);

/**
 * @brief Set all LEDs from a bitmask; bit n is channel n.
 *
 * Sets the whole state, so a clear bit turns its LED off. Not guaranteed
 * atomic - a fast pattern can be seen mid-update.
 *
 * @return 0, -ERR_LEDS_INVALID_CHANNEL if the mask has bits above LEDS_COUNT,
 *         or the first error hit while writing.
 */
int leds_set_mask(uint8_t mask);

#endif /* HAL_LEDS_H_ */
