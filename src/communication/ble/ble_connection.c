/*
 * BLE Connection Management Implementation
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ble/ble_connection.h>
#include <ble/ble_advertising.h>
#include <network_config.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_connection, LOG_LEVEL_INF);

static struct bt_conn *current_conn = NULL;
static ble_network_event_cb_t event_callback = NULL;

/* Connection state management */
static ATOMIC_DEFINE(conn_state, 2U);
#define STATE_CONNECTED    1U
#define STATE_DISCONNECTED 2U

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
        return;
    }

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected to %s", addr);

    current_conn = bt_conn_ref(conn);
    atomic_set_bit(conn_state, STATE_CONNECTED);

    /* Stop advertising when connected */
    ble_advertising_stop();

    if (event_callback) {
        event_callback(BLE_NETWORK_EVENT_CONNECTED, conn);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    atomic_set_bit(conn_state, STATE_DISCONNECTED);

    /* Restart advertising when disconnected */
    ble_advertising_start();

    if (event_callback) {
        event_callback(BLE_NETWORK_EVENT_DISCONNECTED, conn);
    }
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        LOG_INF("Security changed: %s level %u", addr, level);
    } else {
        LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
    }

    if (event_callback) {
        event_callback(BLE_NETWORK_EVENT_SECURITY_CHANGED, conn);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

int ble_connection_init(ble_network_event_cb_t event_cb)
{
    LOG_INF("Initializing BLE connection management");
    
    event_callback = event_cb;
    current_conn = NULL;
    
    LOG_INF("BLE connection management initialized");
    return 0;
}

bool ble_connection_is_connected(void)
{
    return (current_conn != NULL);
}

struct bt_conn *ble_connection_get_current(void)
{
    return current_conn;
}

int ble_connection_disconnect(void)
{
    if (!current_conn) {
        return -ENOTCONN;
    }

    return bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
