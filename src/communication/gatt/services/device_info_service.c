// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#include <gatt/services/device_info_service.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(device_info_service, LOG_LEVEL_INF);

struct dis_string {
    const char *value;
    uint16_t len;
};

static ssize_t dis_read_string(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    const struct dis_string *str = attr->user_data;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, str->value, str->len);
}

static const struct dis_string manufacturer_name = {
    .value = "CareLoop Labs",
    .len = sizeof("CareLoop Labs") - 1,
};

static const struct dis_string model_number = {
    .value = "CL-Bracelet-FF1",
    .len = sizeof("CL-Bracelet-FF1") - 1,
};

static const struct dis_string hardware_revision = {
    .value = "HW-REV-A",
    .len = sizeof("HW-REV-A") - 1,
};

static const struct dis_string firmware_revision = {
    .value = "FW-0001",
    .len = sizeof("FW-0001") - 1,
};

static const struct dis_string serial_number = {
    .value = "CL-00000001",
    .len = sizeof("CL-00000001") - 1,
};

BT_GATT_SERVICE_DEFINE(device_info_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DIS),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_MANUFACTURER_NAME,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           dis_read_string, NULL, (void *)&manufacturer_name),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_MODEL_NUMBER,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           dis_read_string, NULL, (void *)&model_number),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_HARDWARE_REVISION,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           dis_read_string, NULL, (void *)&hardware_revision),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_FIRMWARE_REVISION,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           dis_read_string, NULL, (void *)&firmware_revision),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_SERIAL_NUMBER,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           dis_read_string, NULL, (void *)&serial_number)
);

int device_info_service_init(void)
{
    LOG_INF("Device Info service initialized");
    return 0;
}
