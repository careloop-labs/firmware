// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * careloop_v2 boot test.
 *
 * Proves exactly one thing: the image flashes, the CPU resets into it, main()
 * is reached, and log output arrives over RTT. Nothing else - no peripherals,
 * no BLE. If this fails, no other bring-up result can be trusted.
 *
 * The heartbeat is part of the proof: a single banner only shows main() was
 * entered, while a repeating beat shows the kernel scheduler is running.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(boot, LOG_LEVEL_INF);

#define HEARTBEAT_PERIOD_S 1

int main(void)
{
    LOG_INF("careloop_v2 boot OK - reached main()");

    for (uint32_t beat = 0U;; beat++) {
        k_sleep(K_SECONDS(HEARTBEAT_PERIOD_S));
        LOG_INF("beat %u", beat);
    }

    return 0;
}
