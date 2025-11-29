/*
 * BLE Manager Implementation
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ble/ble_manager.h>
#include <ble/ble_advertising.h>
#include <ble/ble_connection.h>
#include <ble/ble_security.h>
#include <gatt/gatt_server.h>
#include <network_config.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_manager, LOG_LEVEL_INF);

static ble_network_event_cb_t network_event_callback = NULL;

static void ble_manager_event_handler(enum ble_network_event event, struct bt_conn *conn)
{
    if (network_event_callback) {
        network_event_callback(event, conn);
    }
}

int ble_network_init(ble_network_event_cb_t event_cb)
{
    int err;

    LOG_INF("Initializing BLE network layer");

    /* Store event callback */
    network_event_callback = event_cb;

    /* Initialize Bluetooth stack */
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    LOG_INF("Bluetooth initialized");

    /* Initialize GATT server */
    err = gatt_server_init();
    if (err) {
        LOG_ERR("GATT server init failed (err %d)", err);
        return err;
    }

    /* Initialize BLE security */
    err = ble_security_init();
    if (err) {
        LOG_ERR("BLE security init failed (err %d)", err);
        return err;
    }

    /* Initialize BLE connection management */
    err = ble_connection_init(ble_manager_event_handler);
    if (err) {
        LOG_ERR("BLE connection init failed (err %d)", err);
        return err;
    }

    /* Initialize BLE advertising */
    err = ble_advertising_init();
    if (err) {
        LOG_ERR("BLE advertising init failed (err %d)", err);
        return err;
    }

    /* Load stored bonds and settings */
    settings_load();

    LOG_INF("BLE network layer initialized successfully");
    return 0;
}

int ble_network_start_advertising(void)
{
    return ble_advertising_start();
}

int ble_network_stop_advertising(void)
{
    return ble_advertising_stop();
}

bool ble_network_is_connected(void)
{
    return ble_connection_is_connected();
}

struct bt_conn *ble_network_get_connection(void)
{
    return ble_connection_get_current();
}

int ble_network_disconnect(void)
{
    return ble_connection_disconnect();
}
