/*
 * GATT Server Management
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GATT_SERVER_H_
#define GATT_SERVER_H_

/**
 * @brief Initialize GATT server and all services
 * 
 * @return 0 on success, negative error code on failure
 */
int gatt_server_init(void);

#endif /* GATT_SERVER_H_ */
