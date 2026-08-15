// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * MCU identity and reset state.
 *
 * Runs first because everything else is meaningless if the part is not the
 * one the board definition was written for. The package check is not
 * academic: the KiCad project disagrees with itself about which nRF52840
 * variant is fitted, and FICR is the only authority.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <hal/nrf_power.h>
#include <nrfx.h>

#define FICR_PART_NRF52840  0x52840UL
#define FICR_PACKAGE_QIAA   0x2004UL   /* aQFN73 */
#define FICR_RAM_256KB      0x100UL

ZTEST(careloop_bringup, test_mcu_identity)
{
    uint32_t part = NRF_FICR->INFO.PART;
    uint32_t package = NRF_FICR->INFO.PACKAGE;
    uint32_t ram = NRF_FICR->INFO.RAM;

    printk("  part 0x%05x  package 0x%04x  ram %uKB\n",
           (unsigned int)part, (unsigned int)package, (unsigned int)ram);

    zassert_equal(part, FICR_PART_NRF52840,
                  "expected nRF52840, FICR.INFO.PART reads 0x%05x",
                  (unsigned int)part);

    zassert_equal(package, FICR_PACKAGE_QIAA,
                  "expected aQFN73 (QIAA), FICR.INFO.PACKAGE reads 0x%04x - "
                  "the board definition includes nrf52840_qiaa.dtsi",
                  (unsigned int)package);

    zassert_equal(ram, FICR_RAM_256KB, "expected 256KB RAM, FICR reads %u",
                  (unsigned int)ram);
}

/*
 * A board that resets itself is the single most misleading failure mode -
 * it looks like a hang, or like nothing at all. RESETREAS latches until
 * cleared, so a stale bit from a debugger reset is expected and ignored;
 * what must not appear is a watchdog or lockup reset.
 */
ZTEST(careloop_bringup, test_mcu_reset_reason)
{
    uint32_t reas = nrf_power_resetreas_get(NRF_POWER);

    printk("  resetreas 0x%08x\n", (unsigned int)reas);

    zassert_false(reas & POWER_RESETREAS_DOG_Msk,
                  "watchdog reset latched (0x%08x)", (unsigned int)reas);
    zassert_false(reas & POWER_RESETREAS_LOCKUP_Msk,
                  "CPU lockup reset latched (0x%08x)", (unsigned int)reas);

    nrf_power_resetreas_clear(NRF_POWER, reas);
}

/*
 * The DC/DC converter must stay off: L13, the 15 nH DCC inductor, is not
 * fitted. If some future change enables it the SoC may not start at all, so
 * assert the state rather than trusting review to catch it.
 */
ZTEST(careloop_bringup, test_mcu_dcdc_disabled)
{
    bool dcdc = nrf_power_dcdcen_get(NRF_POWER);

    zassert_false(dcdc,
                  "REG1 DC/DC is enabled but L13 (15nH) is not populated");
}
