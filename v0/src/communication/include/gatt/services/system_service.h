/*
 * System Service Header
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYSTEM_SERVICE_H_
#define SYSTEM_SERVICE_H_

/**
 * @brief Initialize the system service
 * 
 * @return 0 on success, negative error code on failure
 */
int system_service_init(void);

/**
 * @brief Simulate battery level notification (for demo purposes)
 */
void system_service_simulate_battery_notify(void);

#endif /* SYSTEM_SERVICE_H_ */
