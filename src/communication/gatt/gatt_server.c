// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#include <gatt/gatt_server.h>
#include <gatt/services/device_info_service.h>
#include <gatt/services/battery_service.h>
#include <gatt/services/core_service.h>
#include <gatt/services/vitals_service.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gatt_server, LOG_LEVEL_INF);

int gatt_server_init(void)
{
    int err;

    LOG_INF("Initializing GATT server");

    /* Initialize all GATT services */
    err = device_info_service_init();
    if (err) {
        LOG_ERR("Failed to initialize Device Info service (err %d)", err);
        return err;
    }

    err = battery_service_init();
    if (err) {
        LOG_ERR("Failed to initialize Battery service (err %d)", err);
        return err;
    }

    err = core_service_init();
    if (err) {
        LOG_ERR("Failed to initialize Core service (err %d)", err);
        return err;
    }

    LOG_INF("GATT server initialized successfully");
    return 0;
}
