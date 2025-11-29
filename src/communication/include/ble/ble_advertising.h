/*
 * BLE Advertising Management
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
