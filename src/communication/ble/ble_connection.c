// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

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

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));

        /*
         * Restart advertising by hand. BT_LE_ADV_CONN_FAST_1 carries
         * BT_LE_ADV_OPT_CONN, which does not auto-resume, and disconnected()
         * is never called for a connection that failed to establish - so
         * without this the device goes silent for good after one bad attempt.
         *
         * Deferred, not direct: bt_le_adv_start() from inside this
         * callback returns -ENOMEM because the conn object is still held.
         */
        ble_advertising_restart();

        return;
    }

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected to %s", addr);

    current_conn = bt_conn_ref(conn);

    /* Stop advertising when connected */
    ble_advertising_stop();

    int sec_err = bt_conn_set_security(conn, NETWORK_SECURITY_LEVEL);

    if (sec_err) {
        LOG_ERR("Failed to request security level %d (err %d, ERR_BLE_SECURITY_REQUEST 0x%x) - "
                "dropping the link",
                (int)NETWORK_SECURITY_LEVEL, sec_err, ERR_BLE_SECURITY_REQUEST);

        /*
         * Every attribute in this GATT database is still declared with
         * plain BT_GATT_PERM_READ/WRITE, so an unencrypted link can read
         * and write all of it. Until those permissions require
         * encryption, refusing to hold a link that cannot be secured is
         * the only thing between an unpaired peer and Control.
         */
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
    }

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

    /* Restart advertising when disconnected. Deferred: calling
     * bt_le_adv_start() here fails with -ENOMEM, because this conn is not
     * released until the callback returns.
     */
    ble_advertising_restart();

    if (event_callback) {
        event_callback(BLE_NETWORK_EVENT_DISCONNECTED, conn);
    }
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        /* Level 1 means unencrypted. Reaching it is not an error and is
         * reported through this same callback, so log it distinctly - an
         * "encrypted" link that is actually L1 is the failure most easily
         * mistaken for success.
         */
        LOG_INF("Security changed: %s level %u (%s)", addr, level,
                level >= BT_SECURITY_L2 ? "encrypted" : "NOT encrypted");
    } else {
        LOG_ERR("Security failed: %s level %u err %d %s", addr, level, err,
                bt_security_err_to_str(err));
    }

    /*
     * Enforce the policy rather than just reporting it. This callback used
     * to log and return, so a peer that refused to pair simply stayed
     * connected at level 1 with the whole GATT database readable and
     * writable - observed on hardware as "Security failed ... level 1
     * err 2" followed by a link that lived on until the peer hung up.
     */
    if (err != BT_SECURITY_ERR_SUCCESS || level < NETWORK_SECURITY_LEVEL) {
        LOG_WRN("Disconnecting %s: level %u is below the required level %d", addr, level,
                (int)NETWORK_SECURITY_LEVEL);
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
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
