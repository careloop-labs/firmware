/*
 * BLE Manager - Main BLE Interface
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_MANAGER_H_
#define BLE_MANAGER_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <stdbool.h>

/**
 * @brief BLE Network Events
 */
enum ble_network_event {
    BLE_NETWORK_EVENT_CONNECTED,
    BLE_NETWORK_EVENT_DISCONNECTED,
    BLE_NETWORK_EVENT_BONDED,
    BLE_NETWORK_EVENT_SECURITY_CHANGED,
};

/**
 * @brief BLE Network Event Callback
 */
typedef void (*ble_network_event_cb_t)(enum ble_network_event event, struct bt_conn *conn);

/**
 * @brief Initialize the BLE network layer
 * 
 * @param event_cb Event callback function (optional)
 * @return 0 on success, negative error code on failure
 */
int ble_network_init(ble_network_event_cb_t event_cb);

/**
 * @brief Start BLE advertising
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_network_start_advertising(void);

/**
 * @brief Stop BLE advertising
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_network_stop_advertising(void);

/**
 * @brief Check if BLE is connected
 * 
 * @return true if connected, false otherwise
 */
bool ble_network_is_connected(void);

/**
 * @brief Get the current BLE connection
 * 
 * @return Pointer to connection object or NULL if not connected
 */
struct bt_conn *ble_network_get_connection(void);

/**
 * @brief Disconnect current BLE connection
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_network_disconnect(void);

#endif /* BLE_MANAGER_H_ */
