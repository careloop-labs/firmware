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

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static void *bringup_setup(void)
{
    printk("\nCareLoop Hardware Bring-up\n");
    printk("==========================\n");

    return NULL;
}

ZTEST_SUITE(careloop_bringup, NULL, bringup_setup, NULL, NULL, NULL);
