/*
 * Fall Detection Service Implementation
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gatt/services/fall_service.h>
#include <gatt/gatt_common.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fall_service, LOG_LEVEL_INF);

/* Service data */
static uint8_t fall_event = 0;
static uint8_t fall_thresh_cfg = 10;

static void fall_event_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Fall event CCC configuration changed: 0x%04x", value);
}

static ssize_t write_fall_thresh_cfg(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                     const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (len != 1) {
        LOG_ERR("Invalid length for fall threshold configuration");
        return GATT_ERR_INVALID_LENGTH;
    }
    
    fall_thresh_cfg = ((uint8_t *)buf)[0];
    LOG_INF("Fall threshold configuration updated: %d", fall_thresh_cfg);
    
    return len;
}

BT_GATT_SERVICE_DEFINE(fall_svc,
    BT_GATT_PRIMARY_SERVICE(GATT_DECLARE_128_UUID(FALL_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(GATT_DECLARE_128_UUID(FALL_EVENT_UUID),
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, &fall_event),
    BT_GATT_CCC(fall_event_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(GATT_DECLARE_128_UUID(FALL_THRESH_CFG_UUID),
        BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_WRITE,
        NULL, write_fall_thresh_cfg, &fall_thresh_cfg),
);

int fall_service_init(void)
{
    LOG_INF("Fall detection service initialized");
    return 0;
}

void fall_service_simulate_notify(void)
{
    fall_event = 1; /* Simulate a fall detected */
    
    int err = bt_gatt_notify(NULL, &fall_svc.attrs[1], &fall_event, sizeof(fall_event));
    if (err) {
        LOG_ERR("Failed to send fall event notification (err %d)", err);
    } else {
        LOG_DBG("Fall event notification sent: %d", fall_event);
    }
    
    /* Reset fall event after notification */
    fall_event = 0;
}
