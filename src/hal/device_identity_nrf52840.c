// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/device_identity.h for the nRF52840.
 *
 * The UID is FICR.DEVICEID, 64-bit, which is why
 * DEVICE_IDENTITY_UID_SIZE is 8.
 *
 * Byte order: the header promises most significant first, so DEVICEID[1]
 * precedes DEVICEID[0]. That matches the spelling the BLE Device Information
 * service already uses; changing it would give one board two serials.
 *
 * Part, package and RAM checks belong in the bring-up suite, which asserts
 * them against FICR directly - the KiCad project disagrees with itself about
 * which variant is fitted.
 */

#include <hal/device_identity.h>

/*
 * nrfx.h rather than <hal/nrf_ficr.h>: src/hal/include is on the include
 * path, so anything spelled <hal/...> resolves against this layer's own
 * headers first.
 */
#include <nrfx.h>

/** DEVICEID is two 32-bit words. */
#define DEVICEID_WORDS 2U
#define BYTES_PER_WORD 4U

static const char hex_digits[] = "0123456789ABCDEF";

/*
 * FICR is read-only and factory-programmed, so this needs no locking, no
 * initialisation and no cached copy.
 */
static void device_identity_uid_bytes(uint8_t *uid)
{
    const uint32_t words[DEVICEID_WORDS] = {
        NRF_FICR->DEVICEID[1],
        NRF_FICR->DEVICEID[0],
    };

    for (size_t w = 0U; w < DEVICEID_WORDS; w++) {
        for (size_t b = 0U; b < BYTES_PER_WORD; b++) {
            uid[(w * BYTES_PER_WORD) + b] =
                (uint8_t)(words[w] >> (24U - (8U * b)));
        }
    }
}

int device_identity_get_uid(uint8_t *uid, size_t len)
{
    if (uid == NULL) {
        return -ERR_DEVICE_IDENTITY_INVALID_ARG;
    }

    if (len < DEVICE_IDENTITY_UID_SIZE) {
        return -ERR_DEVICE_IDENTITY_BUFFER_TOO_SMALL;
    }

    device_identity_uid_bytes(uid);

    return 0;
}

int device_identity_get_serial(char *serial, size_t len)
{
    uint8_t uid[DEVICE_IDENTITY_UID_SIZE];

    if (serial == NULL) {
        return -ERR_DEVICE_IDENTITY_INVALID_ARG;
    }

    if (len < DEVICE_IDENTITY_SERIAL_SIZE) {
        return -ERR_DEVICE_IDENTITY_BUFFER_TOO_SMALL;
    }

    device_identity_uid_bytes(uid);

    for (size_t i = 0U; i < DEVICE_IDENTITY_UID_SIZE; i++) {
        serial[i * 2U] = hex_digits[uid[i] >> 4U];
        serial[(i * 2U) + 1U] = hex_digits[uid[i] & 0x0FU];
    }

    serial[DEVICE_IDENTITY_UID_SIZE * 2U] = '\0';

    return 0;
}
