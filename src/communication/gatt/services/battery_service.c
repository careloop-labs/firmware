// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#include <gatt/services/battery_service.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery_service, LOG_LEVEL_INF);

static ssize_t battery_level_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset)
{
    //TODO: call the function to get real battery level
    static const uint8_t battery_level = 75;

    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             &battery_level, sizeof(battery_level));
}

BT_GATT_SERVICE_DEFINE(battery_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
    BT_GATT_CHARACTERISTIC(BT_UUID_BAS_BATTERY_LEVEL,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           battery_level_read, NULL, NULL)
);

int battery_service_init(void)
{
    LOG_INF("Battery service initialized");
    return 0;
}
