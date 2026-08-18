// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#ifndef NETWORK_CONFIG_H_
#define NETWORK_CONFIG_H_

/* The error bases the codes at the bottom of this file are derived from.
 * Previously missing: every ERR_BLE_* below expanded to an undefined symbol,
 * which stayed invisible only because nothing had used one yet.
 */
#include "errors.h"

/* Device Information.
 *
 * Only what the C code actually reads lives here. Appearance, max connections
 * and bondability are Kconfig settings (CONFIG_BT_DEVICE_APPEARANCE,
 * CONFIG_BT_MAX_CONN, CONFIG_BT_BONDABLE) and used to be mirrored here as
 * macros nothing ever read - two sources for one value, free to drift.
 */
#define NETWORK_DEVICE_NAME "CareLoop wearable"

/* Advertising Parameters */
#define NETWORK_ADV_FAST_INTERVAL_MIN BT_GAP_ADV_FAST_INT_MIN_2
#define NETWORK_ADV_FAST_INTERVAL_MAX BT_GAP_ADV_FAST_INT_MAX_2

/* Security Configuration */
#define NETWORK_SECURITY_LEVEL BT_SECURITY_L2

/* Security specific error codes.
 *
 * These are positive, unlike the negative errno values Zephyr returns, so a
 * caller can always tell a CareLoop fault from a stack fault by the sign.
 */
#define ERR_BLE_AUTH_CB_REGISTER (ERR_COMMUNICATION_SECURITY + 1)
#define ERR_BLE_SECURITY_REQUEST (ERR_COMMUNICATION_SECURITY + 2)
#define ERR_BLE_PAIRING_FAILED (ERR_COMMUNICATION_SECURITY + 3)

#endif /* NETWORK_CONFIG_H_ */
