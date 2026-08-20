// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

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

    /*
     * settings_load() must run here, not at the end of this function.
     *
     * With CONFIG_BT_SETTINGS=y and no preset identity, bt_init() returns
     * early - it logs "No ID address. App must call settings_load()" and does
     * *not* set BT_DEV_READY. That flag is only raised inside the settings
     * commit handler, so until this call returns the stack is not ready and
     * every API that checks BT_DEV_READY fails. The legacy advertising path
     * below happens not to call one, which is the only reason the old ordering
     * appeared to work; bt_le_ext_adv_create() returns -EAGAIN unconditionally,
     * so enabling CONFIG_BT_EXT_ADV would have broken init outright.
     *
     * This is also what loads stored bonds.
     */
    err = settings_load();
    if (err) {
        LOG_ERR("Failed to load settings (err %d) - bonds will not persist", err);
        return err;
    }

    /* Initialize GATT server */
    err = gatt_server_init();
    if (err) {
        LOG_ERR("GATT server init failed (err %d)", err);
        return err;
    }

    /* Initialize BLE security */
    err = ble_security_init(ble_manager_event_handler);
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
