// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * CareLoop hardware bring-up suite.
 *
 * One image, one flash, every check. Split across src/test_*.c by hardware
 * domain; this file only owns the suite and the banner.
 *
 * Run it through Twister rather than by hand:
 *   ./scripts/bringup.sh suite
 *
 * Console is SEGGER RTT - the wearable exposes no UART pads - so Twister
 * reaches it through scripts/rtt_console.py as a --device-serial-pty.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/*
 * The banner used to be the single suite's setup function. With seven suites
 * there is no one place to hang it, and repeating it per suite would bury the
 * results - so print it from SYS_INIT, which runs before ztest starts.
 */
static int bringup_banner(void)
{
    printk("\nCareLoop Hardware Bring-up\n");
    printk("==========================\n");

    return 0;
}

SYS_INIT(bringup_banner, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
