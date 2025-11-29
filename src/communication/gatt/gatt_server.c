/*
 * GATT Server Management Implementation
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gatt/gatt_server.h>
#include <gatt/services/vitals_service.h>
#include <gatt/services/fall_service.h>
#include <gatt/services/system_service.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gatt_server, LOG_LEVEL_INF);

int gatt_server_init(void)
{
    int err;

    LOG_INF("Initializing GATT server");

    /* Initialize all services */
    err = vitals_service_init();
    if (err) {
        LOG_ERR("Failed to initialize vitals service (err %d)", err);
        return err;
    }

    err = fall_service_init();
    if (err) {
        LOG_ERR("Failed to initialize fall service (err %d)", err);
        return err;
    }

    err = system_service_init();
    if (err) {
        LOG_ERR("Failed to initialize system service (err %d)", err);
        return err;
    }

    LOG_INF("GATT server initialized successfully");
    return 0;
}
