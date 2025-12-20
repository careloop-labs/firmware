/*
 * BLE Advertising Management Implementation
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ble/ble_advertising.h>
#include <gatt/gatt_common.h>
#include <network_config.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_advertising, LOG_LEVEL_INF);

static bool advertising_active = false;

/* Advertising data: only Vitals Service UUID for legacy advertising */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xA0, 0x00, 0x78, 0x56, 0x34, 0x12
    ),
#if defined(CONFIG_BT_EXT_ADV)
    BT_DATA(BT_DATA_NAME_COMPLETE, NETWORK_DEVICE_NAME, sizeof(NETWORK_DEVICE_NAME) - 1),
#endif
};

#if !defined(CONFIG_BT_EXT_ADV)
/* Put the device name in the scan response for legacy advertising */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, NETWORK_DEVICE_NAME, sizeof(NETWORK_DEVICE_NAME) - 1),
};
#endif

#if defined(CONFIG_BT_EXT_ADV)
static struct bt_le_ext_adv *ext_adv = NULL;
#endif

int ble_advertising_init(void)
{
    LOG_INF("Initializing BLE advertising");

#if defined(CONFIG_BT_EXT_ADV)
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0U,
        .secondary_max_skip = 0U,
        .options = (BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_CODED),
        .interval_min = NETWORK_ADV_FAST_INTERVAL_MIN,
        .interval_max = NETWORK_ADV_FAST_INTERVAL_MAX,
        .peer = NULL,
    };

    int err = bt_le_ext_adv_create(&adv_param, NULL, &ext_adv);
    if (err) {
        LOG_ERR("Failed to create Coded PHY extended advertising set (err %d)", err);
        LOG_INF("Creating a non-Coded PHY connectable non-scannable advertising set");
        adv_param.options &= ~BT_LE_ADV_OPT_CODED;
        err = bt_le_ext_adv_create(&adv_param, NULL, &ext_adv);
        if (err) {
            LOG_ERR("Failed to create extended advertising set (err %d)", err);
            return err;
        }
    }

    err = bt_le_ext_adv_set_data(ext_adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Failed to set extended advertising data (err %d)", err);
        return err;
    }
#endif

    advertising_active = false;
    LOG_INF("BLE advertising initialized");
    return 0;
}

int ble_advertising_start(void)
{
    int err;

    if (advertising_active) {
        LOG_WRN("Advertising already active");
        return 0;
    }

#if !defined(CONFIG_BT_EXT_ADV)
    LOG_INF("Starting Legacy Advertising (connectable and scannable)");
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }
#else
    LOG_INF("Starting Extended Advertising (connectable non-scannable)");
    err = bt_le_ext_adv_start(ext_adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        LOG_ERR("Failed to start extended advertising set (err %d)", err);
        return err;
    }
#endif

    advertising_active = true;
    LOG_INF("Advertising successfully started");
    return 0;
}

int ble_advertising_stop(void)
{
    int err;

    if (!advertising_active) {
        return 0;
    }

#if !defined(CONFIG_BT_EXT_ADV)
    err = bt_le_adv_stop();
#else
    err = bt_le_ext_adv_stop(ext_adv);
#endif

    if (err) {
        LOG_ERR("Failed to stop advertising (err %d)", err);
        return err;
    }

    advertising_active = false;
    LOG_INF("Advertising stopped");
    return 0;
}

bool ble_advertising_is_active(void)
{
    return advertising_active;
}
