// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * careloop BLE bring-up.
 *
 * Brings up the *product* BLE stack - everything under src/communication,
 * compiled verbatim rather than copied - and reports enough over RTT for a
 * person holding a phone to
 * check it against nRF Connect. This is LEVEL 5 of automatedTestBringUp.md,
 * the first tier the ztest suite cannot reach: a peripheral cannot scan,
 * connect or subscribe to itself, so the central has to be a phone.
 *
 * Until now none of that code had ever been compiled for this board -
 * build.sh defaults to the nRF52840 DK and the bring-up suite enables no
 * CONFIG_BT at all. Treat every result here as first-run.
 *
 * The clock line in the banner is not decoration. There is an unresolved
 * ~755 ppm disagreement between Y3 (32 MHz) and Y4 (32.768 kHz), and BLE
 * settles half of it for free: a 755 ppm HFXO error is ~1.8 MHz at 2.44 GHz,
 * wider than the 2 MHz channel spacing, so if a phone can see this board at
 * normal range then Y3 is fine and the error lies with Y4 or with the
 * original measurement.
 *
 * Run it with:
 *   ./scripts/bringup.sh ble
 *
 * Control build on known-good clocks:
 *   BOARD=nrf52840dk/nrf52840 ./scripts/bringup.sh ble
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_clock.h>

#include <ble/ble_manager.h>
#include <ble/ble_advertising.h>
#include <gatt/gatt_common.h>
#include <network_config.h>

LOG_MODULE_REGISTER(ble_bringup, LOG_LEVEL_INF);

/*
 * SEGGER RTT defaults to NO_BLOCK_SKIP: anything written before a viewer
 * attaches is discarded, not queued. `bringup.sh` flashes (which resets the
 * board), then releases the probe, then attaches the console - so the banner
 * has to outlast that gap or it is simply lost.
 */
#define RTT_ATTACH_GRACE_MS 3000U

#define HEARTBEAT_PERIOD_S 10

/*
 * Wipe every stored bond at startup.
 *
 * Off by default - the whole point of the bond checks below is that state
 * survives a reboot, and a build that clears it on boot can never show that.
 * Turn it on for a *fresh* pairing run:
 *
 *   BOARD=nrf52840dk/nrf52840 ./scripts/bringup.sh ble -- \
 *       -DEXTRA_CFLAGS=-DCLEAR_BONDS_ON_BOOT=1
 *
 * Without it, re-testing the pairing dialog also means deleting the device in
 * the phone's Bluetooth settings - a peer that still holds a bond will simply
 * encrypt from its side and never prompt.
 */
#ifndef CLEAR_BONDS_ON_BOOT
#define CLEAR_BONDS_ON_BOOT 0
#endif

/* Sleep-clock accuracy this board declares to the controller. Worth printing:
 * half the crystal question is knowing which config is on the board in hand.
 */
#if defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_20PPM)
#define DECLARED_SCA "20 ppm"
#elif defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_50PPM)
#define DECLARED_SCA "50 ppm"
#elif defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_100PPM)
#define DECLARED_SCA "100 ppm"
#elif defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_250PPM)
#define DECLARED_SCA "250 ppm"
#elif defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_500PPM)
#define DECLARED_SCA "500 ppm"
#else
#define DECLARED_SCA "unknown"
#endif

/*
 * The exact 16 bytes of the service UUID as they go out in the advertising
 * payload. BT_UUID_128_ENCODE emits little-endian, so this reads backwards
 * against the UUID string nRF Connect shows - which is correct, and is the
 * single most common thing to misread when a phone shows nothing.
 */
static const uint8_t adv_service_uuid[] = { CARELOOP_SERVICE_UUID };

static const char *lfclk_src_name(nrf_clock_lfclk_t src)
{
    switch (src) {
    case NRF_CLOCK_LFCLK_RC:
        return "RC";
    case NRF_CLOCK_LFCLK_XTAL:
        return "XTAL";
    case NRF_CLOCK_LFCLK_SYNTH:
        return "SYNTH";
    default:
        return "?";
    }
}

static void print_clocks(void)
{
    nrf_clock_lfclk_t lf_src = NRF_CLOCK_LFCLK_RC;
    nrf_clock_hfclk_t hf_src = NRF_CLOCK_HFCLK_LOW_ACCURACY;
    bool lf_run = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_LFCLK, &lf_src);
    bool hf_run = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, &hf_src);

    printk("clock    LFCLK %s src=%s | HFCLK %s src=%s | declared SCA %s\n",
           lf_run ? "run" : "STOPPED", lfclk_src_name(lf_src),
           hf_run ? "run" : "stopped",
           hf_src == NRF_CLOCK_HFCLK_HIGH_ACCURACY ? "XTAL" : "RC",
           DECLARED_SCA);

    /*
     * HFCLK reading "stopped/RC" here is normal and not a fault: the
     * controller requests HFXO around each radio event and releases it in
     * between, so a snapshot outside an event sees the internal RC.
     */
}

static void print_identity(void)
{
    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
    size_t count = ARRAY_SIZE(addrs);

    bt_id_get(addrs, &count);

    if (count == 0U) {
        printk("identity NONE - bt_id_get returned no addresses\n");
        return;
    }

    for (size_t i = 0U; i < count; i++) {
        char s[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(&addrs[i], s, sizeof(s));
        /* Must be identical on every boot - privacy is off, so this is the
         * random static address derived from FICR. It is what you match
         * against the address nRF Connect lists.
         */
        printk("identity %u: %s\n", (unsigned int)i, s);
    }
}

/*
 * Bond storage.
 *
 * This is the persistence test, and it only means anything *before* the first
 * connection of a boot: a bond listed here was read back out of NVS by the
 * settings_load() inside ble_network_init(), so it survived the power cycle.
 * A bond listed after pairing proves only that SMP ran.
 *
 * The distinction matters because CONFIG_BT_SETTINGS without a working
 * storage backend links, runs, and silently stores nothing - the failure
 * surfaces as an unexpected pairing prompt days later, not as an error here.
 */
struct bond_scan {
    unsigned int count;
    bool verbose;
};

static void bond_cb(const struct bt_bond_info *info, void *user_data)
{
    struct bond_scan *scan = user_data;

    scan->count++;

    if (scan->verbose) {
        char s[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(&info->addr, s, sizeof(s));
        printk("bond     %u: %s\n", scan->count, s);
    }
}

static unsigned int count_bonds(void)
{
    struct bond_scan scan = { .count = 0U, .verbose = false };

    bt_foreach_bond(BT_ID_DEFAULT, bond_cb, &scan);
    return scan.count;
}

static void print_bonds(const char *when)
{
    struct bond_scan scan = { .count = 0U, .verbose = true };

    bt_foreach_bond(BT_ID_DEFAULT, bond_cb, &scan);

    if (scan.count == 0U) {
        printk("bond     none stored (%s)\n", when);
    } else {
        printk("bond     %u stored (%s)\n", scan.count, when);
    }
}

static void print_adv_payload(void)
{
    printk("adv      flags 06 (LE General Discoverable, no BR/EDR)\n");
    printk("adv      service uuid128 on air (little-endian):\n");
    printk("        ");
    for (size_t i = 0U; i < ARRAY_SIZE(adv_service_uuid); i++) {
        printk(" %02x", adv_service_uuid[i]);
    }
    printk("\n");
    printk("adv      expect nRF Connect to render this as\n");
    printk("           12345678-A000-1000-8000-00805F9B34FB\n");
    printk("scan rsp complete local name \"%s\" (%u B)\n",
           NETWORK_DEVICE_NAME, (unsigned int)(sizeof(NETWORK_DEVICE_NAME) - 1));
}

/*
 * Receiver test.
 *
 * Advertising only proves the transmitter: the phone hearing us says nothing
 * about whether we can hear the phone. A central's CONNECT_IND that never
 * arrives looks identical to a central that never sent one, and the RTT log
 * cannot tell them apart - so listen to the room instead.
 *
 * Any BLE traffic will do. Phones, laptops, headphones and beacons are all
 * advertising constantly in a normal room, so "heard nothing at all" is a
 * strong statement about this radio's receive path rather than about the peer.
 */
static atomic_t rx_packets;
static atomic_t rx_peers;
static atomic_t rx_best_rssi = ATOMIC_INIT(-127);

/* Distinct-peer counting, cheap and approximate - enough to tell "one noisy
 * neighbour" from "a working receiver in a populated room".
 */
#define RX_PEER_TABLE 32
static bt_addr_le_t rx_seen[RX_PEER_TABLE];
static uint8_t rx_seen_count;

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf)
{
    ARG_UNUSED(adv_type);
    ARG_UNUSED(buf);

    atomic_inc(&rx_packets);

    if (rssi > (int8_t)atomic_get(&rx_best_rssi)) {
        atomic_set(&rx_best_rssi, rssi);
    }

    for (uint8_t i = 0U; i < rx_seen_count; i++) {
        if (bt_addr_le_cmp(&rx_seen[i], addr) == 0) {
            return;
        }
    }

    if (rx_seen_count < RX_PEER_TABLE) {
        char s[BT_ADDR_LE_STR_LEN];

        bt_addr_le_copy(&rx_seen[rx_seen_count++], addr);
        atomic_set(&rx_peers, rx_seen_count);

        /* Name every new peer. Running this on a known-good radio next to a
         * suspect one turns "hears nothing" into "hears the whole room except
         * that board", which is a far stronger statement.
         */
        bt_addr_le_to_str(addr, s, sizeof(s));
        printk("rx peer  %-30s %d dBm\n", s, rssi);
    }
}

static void start_rx_test(void)
{
    /* Passive: listen only, never transmit scan requests, so this disturbs
     * our own advertising as little as possible.
     */
    struct bt_le_scan_param param = {
        .type = BT_LE_SCAN_TYPE_PASSIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    int rc = bt_le_scan_start(&param, scan_cb);

    if (rc != 0) {
        printk("rx test  FAILED to start scanning (%d)\n", rc);
        return;
    }

    printk("rx test  passive scan running - counting any BLE traffic heard\n");
}

static void ble_event_handler(enum ble_network_event event, struct bt_conn *conn)
{
    switch (event) {
    case BLE_NETWORK_EVENT_CONNECTED:
        printk(">>> CONNECTED\n");
        break;
    case BLE_NETWORK_EVENT_DISCONNECTED:
        printk(">>> DISCONNECTED (advertising should resume)\n");
        break;
    case BLE_NETWORK_EVENT_BONDED:
        /* Until bt_conn_auth_info_cb was registered in ble_security.c this
         * branch was unreachable: nothing in the tree emitted the event.
         */
        printk(">>> BONDED - %u bond(s) now stored\n", count_bonds());
        break;
    case BLE_NETWORK_EVENT_SECURITY_CHANGED:
        /* Level 1 is an unencrypted link. Reaching it is reported through
         * this same event, so print the number rather than assume success.
         */
        printk(">>> SECURITY CHANGED - level %d\n",
               conn ? (int)bt_conn_get_security(conn) : -1);
        break;
    default:
        printk(">>> unknown BLE event %d\n", (int)event);
        break;
    }
}

int main(void)
{
    int rc;

    k_sleep(K_MSEC(RTT_ATTACH_GRACE_MS));

    printk("\nCARELOOP BLE BRING-UP\n");
    printk("build    %s %s\n", __DATE__, __TIME__);
    print_clocks();

    /* The product facade logs each of its five init stages itself. */
    rc = ble_network_init(ble_event_handler);
    if (rc != 0) {
        printk("RESULT: FAIL - ble_network_init returned %d\n", rc);
        return 0;
    }

    print_identity();

    /* Before any connection: whatever is listed here came out of NVS. */
    print_bonds("from storage, before any connection");

#if CLEAR_BONDS_ON_BOOT
    printk("bond     CLEAR_BONDS_ON_BOOT set - erasing all bonds\n");
    rc = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
    if (rc != 0) {
        printk("bond     FAILED to clear bonds (%d)\n", rc);
    }
    print_bonds("after clearing");
#endif

    print_adv_payload();

    rc = ble_network_start_advertising();
    if (rc != 0) {
        printk("RESULT: FAIL - advertising did not start (%d)\n", rc);
        return 0;
    }

    printk("RESULT: advertising as \"%s\" - scan for it in nRF Connect\n",
           NETWORK_DEVICE_NAME);

    start_rx_test();

    /*
     * Idle. Connection state changes arrive through the callbacks above; the
     * heartbeat exists so a silent board can be told from a hung one - and so
     * that "is it still advertising?" is answered by asking the advertiser
     * rather than inferred from not being connected.
     */
    for (;;) {
        struct bt_conn *conn;
        unsigned int peers;

        k_sleep(K_SECONDS(HEARTBEAT_PERIOD_S));

        conn = ble_network_get_connection();
        peers = (unsigned int)atomic_get(&rx_peers);

        printk("[%s] sec=%d bonds=%u adv=%s  rx: %u pkts / %u%s peers",
               ble_network_is_connected() ? "CONNECTED" : "idle",
               conn ? (int)bt_conn_get_security(conn) : 0,
               count_bonds(),
               ble_advertising_is_active() ? "yes" : "NO",
               (unsigned int)atomic_get(&rx_packets),
               peers,
               /* The peer table is bounded, so this figure stops rising once
                * full. Marking saturation keeps a ceiling from being read as
                * a measurement.
                */
               peers >= RX_PEER_TABLE ? "+" : "");

        if (atomic_get(&rx_packets) > 0) {
            printk("  best RSSI %d dBm\n", (int)atomic_get(&rx_best_rssi));
        } else {
            printk("  <- receiver has heard NOTHING\n");
        }
    }

    return 0;
}
