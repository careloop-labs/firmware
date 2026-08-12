#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "communication/include/ble/ble_manager.h"

LOG_MODULE_REGISTER(careloop, LOG_LEVEL_INF);

static void ble_event_handler(enum ble_network_event event, struct bt_conn *conn)
{
    switch (event) {
    case BLE_NETWORK_EVENT_CONNECTED:
        LOG_INF("BLE Connected");
        break;
    case BLE_NETWORK_EVENT_DISCONNECTED:
        LOG_INF("BLE Disconnected");
        break;
    case BLE_NETWORK_EVENT_BONDED:
        LOG_INF("BLE Bonded");
        break;
    case BLE_NETWORK_EVENT_SECURITY_CHANGED:
        LOG_INF("BLE Security Changed");
        break;
    default:
        LOG_INF("BLE Unknown event: %d", event);
        break;
    }
}

int main(void)
{
    if (ble_network_init(ble_event_handler) != 0) {
        LOG_INF("Error in BLE initialization!");
    } else {
        LOG_INF("BLE initialized, starting advertising...");
        if (ble_network_start_advertising() != 0) {
            LOG_INF("Failed to start BLE advertising");
        } else {
            LOG_INF("BLE advertising started");
        }
    }

    LOG_INF("CareLoop v1 app starting");
    while (1) {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
