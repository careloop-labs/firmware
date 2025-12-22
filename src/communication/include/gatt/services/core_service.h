// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#ifndef CORE_SERVICE_H_
#define CORE_SERVICE_H_

/**
 * @brief Initialize the CareLoop Core service
 * 
 * @return 0 on success, negative error code on failure
 */
int core_service_init(void);

/**
 * @brief Notify control characteristic
 */
void core_service_notify_control(void);

/**
 * @brief Notify time sync characteristic
 */
void core_service_notify_time_sync(void);

/**
 * @brief Notify status/telemetry characteristic
 */
void core_service_notify_status(void);

/**
 * @brief Notify data pipe characteristic
 */
void core_service_notify_data_pipe(void);

/**
 * @brief Notify transfer control characteristic
 */
void core_service_notify_transfer_control(void);

#endif /* CORE_SERVICE_H_ */
