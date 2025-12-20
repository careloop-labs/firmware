/*
 * Network Layer Configuration
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETWORK_CONFIG_H_
#define NETWORK_CONFIG_H_

/* Device Information */
#define NETWORK_DEVICE_NAME "Zephyr Heartrate Sensor by Elias"
#define NETWORK_DEVICE_APPEARANCE 833

/* Connection Parameters */
#define NETWORK_MAX_CONNECTIONS 1

/* Advertising Parameters */
#define NETWORK_ADV_FAST_INTERVAL_MIN BT_GAP_ADV_FAST_INT_MIN_2
#define NETWORK_ADV_FAST_INTERVAL_MAX BT_GAP_ADV_FAST_INT_MAX_2

/* Security Configuration */
#define NETWORK_SECURITY_LEVEL BT_SECURITY_L2
#define NETWORK_BONDABLE true

#endif /* NETWORK_CONFIG_H_ */
