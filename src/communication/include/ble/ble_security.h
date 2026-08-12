// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#ifndef BLE_SECURITY_H_
#define BLE_SECURITY_H_

/**
 * @brief Initialize BLE security (bonding, authentication)
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_security_init(void);

#endif /* BLE_SECURITY_H_ */
