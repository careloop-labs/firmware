/*
 * Vitals Service Header
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VITALS_SERVICE_H_
#define VITALS_SERVICE_H_

/**
 * @brief Initialize the vitals service
 * 
 * @return 0 on success, negative error code on failure
 */
int vitals_service_init(void);

/**
 * @brief Simulate vitals notification (for demo purposes)
 */
void vitals_service_simulate_notify(void);

#endif /* VITALS_SERVICE_H_ */
