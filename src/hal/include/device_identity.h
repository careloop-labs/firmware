// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Device identity - factory-programmed, read-only, unique per unit. Survives
 * a reflash, a mass erase and a settings wipe, with no provisioning step.
 *
 * Not a secret: unique but not unpredictable, and readable by anything on the
 * part. It identifies a unit, it does not authenticate one - never use it as
 * a key or token.
 *
 * Firmware version, model and hardware revision are build and product
 * metadata, not silicon properties, and live above this layer.
 */

#ifndef HAL_DEVICE_IDENTITY_H_
#define HAL_DEVICE_IDENTITY_H_

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* Device identity specific error codes. */
#define ERR_DEVICE_IDENTITY_BUFFER_TOO_SMALL (ERR_HAL_DEVICE_IDENTITY + 1)
#define ERR_DEVICE_IDENTITY_INVALID_ARG (ERR_HAL_DEVICE_IDENTITY + 2)

/** Length of the raw unique ID, bytes. The one value a different SoC changes. */
#define DEVICE_IDENTITY_UID_SIZE 8U

/** Buffer size for device_identity_get_serial(), including the NUL. */
#define DEVICE_IDENTITY_SERIAL_SIZE ((DEVICE_IDENTITY_UID_SIZE * 2U) + 1U)

/**
 * @brief Read the unique device ID, most significant byte first.
 *
 * @param uid Destination. Untouched on failure.
 * @param len Size of @p uid, at least DEVICE_IDENTITY_UID_SIZE.
 * @return 0, -ERR_DEVICE_IDENTITY_INVALID_ARG, or
 *         -ERR_DEVICE_IDENTITY_BUFFER_TOO_SMALL.
 */
int device_identity_get_uid(uint8_t *uid, size_t len);

/**
 * @brief Format the unique ID as an uppercase hex string.
 *
 * Formatting in one place keeps the same board from appearing under two
 * different spellings.
 *
 * @param serial Destination. Untouched on failure.
 * @param len Size of @p serial, at least DEVICE_IDENTITY_SERIAL_SIZE.
 * @return 0, -ERR_DEVICE_IDENTITY_INVALID_ARG, or
 *         -ERR_DEVICE_IDENTITY_BUFFER_TOO_SMALL.
 */
int device_identity_get_serial(char *serial, size_t len);

#endif /* HAL_DEVICE_IDENTITY_H_ */
