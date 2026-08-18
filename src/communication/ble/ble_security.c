// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#include <ble/ble_security.h>
#include <network_config.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_security, LOG_LEVEL_INF);

static ble_network_event_cb_t event_callback = NULL;

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Confirm passkey for %s: %06u", addr, passkey);
    bt_conn_auth_passkey_confirm(conn);
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Pairing cancelled: %s", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .passkey_confirm = auth_passkey_confirm,
    .cancel = auth_cancel,
};

/*
 * Pairing outcome callbacks.
 *
 * drives the pairing *procedure* (show a passkey, confirm, cancel). Whether
 * pairing then succeeded, and whether a bond was actually written to storage,
 * is reported here and nowhere else. Without this registration
 * BLE_NETWORK_EVENT_BONDED can never be emitted, however well bonding works.
 */
static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    /* bonded == false means the pairing was transient: the link is encrypted
     * for this connection only and nothing was persisted, so the peer will
     * have to pair again after a power cycle.
     */
    LOG_INF("Pairing complete: %s (bond stored: %s)", addr, bonded ? "yes" : "no");

    if (bonded && event_callback) {
        event_callback(BLE_NETWORK_EVENT_BONDED, conn);
    }
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_ERR("Pairing failed: %s err 0x%02x %s (ERR_BLE_PAIRING_FAILED 0x%x)",
            addr, (unsigned int)reason, bt_security_err_to_str(reason),
            ERR_BLE_PAIRING_FAILED);
}

static void bond_deleted(uint8_t id, const bt_addr_le_t *peer)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(peer, addr, sizeof(addr));

    LOG_INF("Bond deleted: %s (id %u)", addr, (unsigned int)id);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
    .bond_deleted = bond_deleted,
};

int ble_security_init(ble_network_event_cb_t event_cb)
{
    int err;

    LOG_INF("Initializing BLE security");

    event_callback = event_cb;

    /* Register authentication callbacks */
    err = bt_conn_auth_cb_register(&auth_cb_display);
    if (err) {
        LOG_ERR("Failed to register auth callbacks (err %d)", err);
        return ERR_BLE_AUTH_CB_REGISTER;
    }

    /* Register pairing outcome callbacks */
    err = bt_conn_auth_info_cb_register(&auth_info_cb);
    if (err) {
        LOG_ERR("Failed to register auth info callbacks (err %d)", err);
        return ERR_BLE_AUTH_CB_REGISTER;
    }

    LOG_INF("BLE security initialized");
    return 0;
}
