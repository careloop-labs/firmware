/*
 * Vitals Service Implementation
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gatt/services/vitals_service.h>
#include <gatt/gatt_common.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vitals_service, LOG_LEVEL_INF);

/* Service data */
static uint8_t vitals_hr_spo2 = 75;

static void vitals_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Vitals CCC configuration changed: 0x%04x", value);
}

BT_GATT_SERVICE_DEFINE(vitals_svc,
    BT_GATT_PRIMARY_SERVICE(GATT_DECLARE_128_UUID(VITALS_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(GATT_DECLARE_128_UUID(VITALS_HR_SPO2_UUID),
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, &vitals_hr_spo2),
    BT_GATT_CCC(vitals_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int vitals_service_init(void)
{
    LOG_INF("Vitals service initialized");
    return 0;
}

void vitals_service_simulate_notify(void)
{
    vitals_hr_spo2++;
    if (vitals_hr_spo2 > 200) {
        vitals_hr_spo2 = 60; /* Reset to reasonable heart rate */
    }
    
    int err = bt_gatt_notify(NULL, &vitals_svc.attrs[1], &vitals_hr_spo2, sizeof(vitals_hr_spo2));
    if (err) {
        LOG_ERR("Failed to send vitals notification (err %d)", err);
    } else {
        LOG_DBG("Vitals notification sent: HR/SpO2 = %d", vitals_hr_spo2);
    }
}
