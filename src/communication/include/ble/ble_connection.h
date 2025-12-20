/*
 * BLE Connection Management
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_CONNECTION_H_
#define BLE_CONNECTION_H_

#include <stdbool.h>
#include <zephyr/bluetooth/conn.h>
#include "ble_manager.h"

/**
 * @brief Initialize BLE connection management
 * 
 * @param event_cb Event callback function
 * @return 0 on success, negative error code on failure
 */
int ble_connection_init(ble_network_event_cb_t event_cb);

/**
 * @brief Check if currently connected
 * 
 * @return true if connected, false otherwise
 */
bool ble_connection_is_connected(void);

/**
 * @brief Get current connection
 * 
 * @return Pointer to connection or NULL
 */
struct bt_conn *ble_connection_get_current(void);

/**
 * @brief Disconnect current connection
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_connection_disconnect(void);

#endif /* BLE_CONNECTION_H_ */
