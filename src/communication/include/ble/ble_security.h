// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#ifndef BLE_SECURITY_H_
#define BLE_SECURITY_H_

#include "ble_manager.h"

/**
 * @brief Initialize BLE security (bonding, authentication)
 *
 * @param event_cb Event callback function (optional). Receives
 *                 BLE_NETWORK_EVENT_BONDED when pairing completes with a
 *                 bond stored.
 * @return 0 on success, negative error code on failure
 */
int ble_security_init(ble_network_event_cb_t event_cb);

#endif /* BLE_SECURITY_H_ */
