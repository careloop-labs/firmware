// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#include <gatt/services/device_info_service.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(device_info_service, LOG_LEVEL_INF);

int device_info_service_init(void)
{
    // TODO: Initialize Device Information GATT service (pseudocode)
    // - Set manufacturer name
    // - Set model number
    // - Set hardware revision
    // - Set firmware revision
    // - Set serial number
    LOG_INF("Device Info service initialized (stub)");
    return 0;
}
