/*
 * Firmware Information Header
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FIRMWARE_INFO_H_
#define FIRMWARE_INFO_H_

#include <stdint.h>

/**
 * @brief Reboot reasons
 */
enum firmware_reboot_reason {
    FIRMWARE_REBOOT_USER_REQUEST,
    FIRMWARE_REBOOT_ERROR,
    FIRMWARE_REBOOT_WATCHDOG,
    FIRMWARE_REBOOT_UPDATE,
};

/**
 * @brief Print firmware information to log
 */
void firmware_info_print(void);

/**
 * @brief Get firmware major version
 * 
 * @return Major version number
 */
uint8_t firmware_get_version_major(void);

/**
 * @brief Get firmware minor version
 * 
 * @return Minor version number
 */
uint8_t firmware_get_version_minor(void);

/**
 * @brief Get firmware patch version
 * 
 * @return Patch version number
 */
uint8_t firmware_get_version_patch(void);

/**
 * @brief Get firmware build number
 * 
 * @return Build number
 */
uint16_t firmware_get_build_number(void);

/**
 * @brief Get firmware version as string
 * 
 * @return Version string (e.g., "1.0.0")
 */
const char *firmware_get_version_string(void);

/**
 * @brief Get build date
 * 
 * @return Build date string
 */
const char *firmware_get_build_date(void);

/**
 * @brief Get build time
 * 
 * @return Build time string
 */
const char *firmware_get_build_time(void);

/**
 * @brief Get device name
 * 
 * @return Device name string
 */
const char *firmware_get_device_name(void);

/**
 * @brief Get manufacturer name
 * 
 * @return Manufacturer string
 */
const char *firmware_get_manufacturer(void);

/**
 * @brief Get device model
 * 
 * @return Model string
 */
const char *firmware_get_model(void);

/**
 * @brief Get system uptime
 * 
 * @return Uptime in seconds
 */
uint32_t firmware_get_uptime_seconds(void);

/**
 * @brief Reboot the system
 * 
 * @param reason Reason for reboot
 */
void firmware_reboot(enum firmware_reboot_reason reason);

#endif /* FIRMWARE_INFO_H_ */
