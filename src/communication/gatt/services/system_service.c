/*
 * System Service Implementation
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gatt/services/system_service.h>
#include <gatt/gatt_common.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(system_service, LOG_LEVEL_INF);

/* Service data */
static uint8_t sys_alert_reset = 0;
static uint8_t sys_battery_lvl = 100;
static char sys_fw_ver[] = "v1.0.0";

static void sys_batt_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Battery level CCC configuration changed: 0x%04x", value);
}

static ssize_t write_alert_reset(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (len != 1) {
        LOG_ERR("Invalid length for alert reset");
        return GATT_ERR_INVALID_LENGTH;
    }
    
    sys_alert_reset = ((uint8_t *)buf)[0];
    LOG_INF("Alert reset triggered: %d", sys_alert_reset);
    
    /* Handle alert reset logic here */
    if (sys_alert_reset) {
        LOG_INF("Processing alert reset...");
        /* Reset alert conditions, clear error states, etc. */
        sys_alert_reset = 0; /* Clear after processing */
    }
    
    return len;
}

static ssize_t read_battery_lvl(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &sys_battery_lvl, sizeof(sys_battery_lvl));
}

static ssize_t read_fw_ver(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, sys_fw_ver, strlen(sys_fw_ver));
}

BT_GATT_SERVICE_DEFINE(system_svc,
    BT_GATT_PRIMARY_SERVICE(GATT_DECLARE_128_UUID(SYSTEM_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(GATT_DECLARE_128_UUID(SYS_ALERT_RESET_UUID),
        BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_WRITE,
        NULL, write_alert_reset, &sys_alert_reset),
    BT_GATT_CHARACTERISTIC(GATT_DECLARE_128_UUID(SYS_BATTERY_LVL_UUID),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        read_battery_lvl, NULL, &sys_battery_lvl),
    BT_GATT_CCC(sys_batt_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(GATT_DECLARE_128_UUID(SYS_FW_VER_UUID),
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_fw_ver, NULL, sys_fw_ver),
);

int system_service_init(void)
{
    LOG_INF("System service initialized");
    return 0;
}

void system_service_simulate_battery_notify(void)
{
    sys_battery_lvl--;
    if (sys_battery_lvl == 0) {
        sys_battery_lvl = 100; /* Reset to full charge */
    }
    
    int err = bt_gatt_notify(NULL, &system_svc.attrs[3], &sys_battery_lvl, sizeof(sys_battery_lvl));
    if (err) {
        LOG_ERR("Failed to send battery level notification (err %d)", err);
    } else {
        LOG_DBG("Battery level notification sent: %d%%", sys_battery_lvl);
    }
}
