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
 * The banner used to be the single suite's setup function. With twelve suites
 * there is no one place to hang it, and repeating it per suite would bury the
 * results - so print it from SYS_INIT, which runs before ztest starts.
 *
 * The expected order is printed with it because nothing else states it in one
 * place. ztest runs suites in alphabetical order of the linker symbol, not link
 * order, so the digit in each name is what fixes the sequence - and the two
 * families are numbered independently, so no single name tells you where it
 * sits globally. A report whose order differs from this block is the symptom of
 * a rename, and it is otherwise very hard to notice.
 *
 * Two layers, deliberately kept apart. careloop_<n>_* drive the Zephyr drivers
 * directly and localise a fault to bus, part or configuration.
 * careloop_hal_<n>_* drive src/hal, the code the product actually ships, and
 * verify the contracts in its headers. A failure in one layer but not the other
 * is the most useful signal this suite produces.
 */
static int bringup_banner(void)
{
    printk("\nCareLoop Hardware Bring-up\n");
    printk("==========================\n");
    printk("expected order:\n");
    printk("  board   1_mcu 2_clocks 3_i2c 4_temperature 5_imu 6_power 7_leds\n");
    printk("  hal     hal_0_uninit hal_1_temperature hal_2_motion"
           " hal_3_power hal_4_leds\n\n");

    return 0;
}

SYS_INIT(bringup_banner, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
