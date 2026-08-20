// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#ifndef BLE_ADVERTISING_H_
#define BLE_ADVERTISING_H_

#include <stdbool.h>

/**
 * @brief Initialize BLE advertising
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_advertising_init(void);

/**
 * @brief Start BLE advertising
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_advertising_start(void);

/**
 * @brief Request an advertising restart from a Bluetooth callback context
 *
 * Advertising cannot be restarted from inside a connection callback: the conn
 * object is not released until the callback returns, so bt_le_adv_start()
 * fails with -ENOMEM. This defers the restart to the system workqueue and
 * retries a bounded number of times. Safe to call from the BT RX thread.
 */
void ble_advertising_restart(void);

/**
 * @brief Stop BLE advertising
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_advertising_stop(void);

/**
 * @brief Check if currently advertising
 * 
 * @return true if advertising, false otherwise
 */
bool ble_advertising_is_active(void);

#endif /* BLE_ADVERTISING_H_ */
