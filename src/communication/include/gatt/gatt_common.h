// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#ifndef GATT_COMMON_H_
#define GATT_COMMON_H_

#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

/* * UUID Base for CareLoop Services 
 * Formatted for BT_UUID_128_ENCODE (little-endian order for the bytes usually)
 */
#define CARELOOP_UUID_BASE_VAL 0x12345678, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB

/* --- Standard SIG Services (16-bit) --- */
#define BT_UUID_DIS_VAL               0x180A  /* Device Information Service */
#define BT_UUID_BAS_VAL               0x180F  /* Battery Service */

/* --- CareLoop Core Service (A000) --- */
#define CARELOOP_SERVICE_UUID         BT_UUID_128_ENCODE(0x12345678, 0xA000, 0x1000, 0x8000, 0x00805F9B34FB)

/* Core Service Characteristic UUIDs */
#define CL_CHAR_CONTROL_UUID          BT_UUID_128_ENCODE(0x12345678, 0xA001, 0x1000, 0x8000, 0x00805F9B34FB)
#define CL_CHAR_TIME_SYNC_UUID        BT_UUID_128_ENCODE(0x12345678, 0xA002, 0x1000, 0x8000, 0x00805F9B34FB)
#define CL_CHAR_TELEMETRY_UUID        BT_UUID_128_ENCODE(0x12345678, 0xA003, 0x1000, 0x8000, 0x00805F9B34FB)
#define CL_CHAR_DATA_PIPE_UUID        BT_UUID_128_ENCODE(0x12345678, 0xA004, 0x1000, 0x8000, 0x00805F9B34FB)
#define CL_CHAR_TRANSFER_CTRL_UUID    BT_UUID_128_ENCODE(0x12345678, 0xA005, 0x1000, 0x8000, 0x00805F9B34FB)

/* --- Common GATT Error Responses --- */
#define GATT_ERR_INVALID_LENGTH       BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN)
#define GATT_ERR_INSUFFICIENT_PERM    BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_AUTHORIZATION)

#endif /* GATT_COMMON_H_ */