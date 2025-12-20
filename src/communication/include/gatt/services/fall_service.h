/*
 * Fall Detection Service Header
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FALL_SERVICE_H_
#define FALL_SERVICE_H_

/**
 * @brief Initialize the fall detection service
 * 
 * @return 0 on success, negative error code on failure
 */
int fall_service_init(void);

/**
 * @brief Simulate fall event notification (for demo purposes)
 */
void fall_service_simulate_notify(void);

#endif /* FALL_SERVICE_H_ */
