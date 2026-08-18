// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Clock bring-up.
 *
 * Two crystals, and BLE needs both: Y4 (32.768 kHz, LFXO) times the sleep
 * windows between connection events, Y3 (32 MHz, HFXO) sets the carrier
 * frequency. The Bluetooth spec allows +-50 ppm on the active clock and
 * whatever accuracy the controller *declares* on the sleep clock - and
 * careloop_defconfig declares 20 ppm.
 *
 * These tests exist because the radio on careloop does not work in either
 * direction while an nRF52840 DK a few centimetres away works perfectly with
 * the identical image. A clock fault explains that symmetry; attenuation does
 * not, because attenuation is not symmetric between transmit and receive in
 * the way observed.
 *
 * The ratio alone names no culprit - it is one number from two oscillators,
 * and both internal RC oscillators are far too coarse (HFINT +-1.5%, LFRC
 * +-2% calibrated) to arbitrate an error of well under one part in a
 * thousand. HFXO *startup time* does name one: it involves only the 32 MHz
 * circuit - Y3, its load capacitors and the SoC's drive - with Y4 playing no
 * part at all.
 *
 * Measured 2026-08-17, this board against an nRF52840 DK running the identical
 * code from test/bringup/clocks:
 *
 *              HFXO startup   ratio
 *   careloop@2.0   2929 us   717 ppm
 *   nRF52840 DK     335 us     4 ppm
 *
 * The DK reading 4 ppm is what makes 717 ppm trustworthy rather than a bug in
 * the measurement. Y3 is the faulty part.
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <hal/nrf_clock.h>

#include "clock_measure.h"

ZTEST_SUITE(careloop_2_clocks, NULL, NULL, NULL, NULL, NULL);

/*
 * The sleep clock must be the crystal, not the RC oscillator. Running on RC
 * is not a failure the radio reports - it simply widens every receive window
 * until connections drop, and careloop_defconfig declares 20 ppm, which
 * the RC cannot come close to honouring.
 */
ZTEST(careloop_2_clocks, test_clock_lfclk_source)
{
    nrf_clock_lfclk_t src = NRF_CLOCK_LFCLK_RC;
    bool running = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_LFCLK, &src);

    printk("  LFCLK running=%d src=%s (LFCLKSTAT raw 0x%08x)\n",
           (int)running, clock_lfclk_src_name(src),
           (unsigned int)NRF_CLOCK->LFCLKSTAT);

    zassert_true(running, "LFCLK is not running");
    zassert_equal(src, NRF_CLOCK_LFCLK_XTAL,
                  "LFCLK source is %s, not XTAL - Y4 (32.768 kHz) did not "
                  "start and the SoC fell back to the internal RC",
                  clock_lfclk_src_name(src));
}

/*
 * HFXO must start, and must start quickly. This is the one test here that can
 * implicate Y3 on its own: the ratio test that follows cannot tell Y3 from Y4,
 * but a slow or failed HFXO start points squarely at the 32 MHz part or its
 * loading capacitors.
 */
ZTEST(careloop_2_clocks, test_clock_hfxo_starts)
{
    uint32_t us = clock_hfxo_start_us();
    nrf_clock_hfclk_t src = NRF_CLOCK_HFCLK_LOW_ACCURACY;

    zassert_not_equal(us, UINT32_MAX,
                      "HFXO did not start within %u ms - Y3 (32 MHz) is not "
                      "oscillating; the SoC is running on the internal RC and "
                      "the radio cannot work at all",
                      CLOCK_HFXO_START_TIMEOUT_MS);

    (void)nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, &src);

    printk("  HFXO start %u us, src now %s (HFCLKSTAT raw 0x%08x)\n",
           (unsigned int)us,
           src == NRF_CLOCK_HFCLK_HIGH_ACCURACY ? "XTAL" : "RC",
           (unsigned int)NRF_CLOCK->HFCLKSTAT);

    zassert_equal(src, NRF_CLOCK_HFCLK_HIGH_ACCURACY,
                  "HFCLK reports RC after a successful HFXO start");
}

/*
 * The measurement that matters.
 *
 * Repeated over three gate lengths: a real frequency ratio is independent of
 * how long you look, while a measurement artefact is not. If all three agree,
 * the disagreement is physical.
 */
ZTEST(careloop_2_clocks, test_clock_lfxo_hfxo_ratio)
{
    static const uint32_t gates_ms[] = { 250U, 1000U, 2000U };
    int64_t ppm[ARRAY_SIZE(gates_ms)];
    uint32_t us;

    us = clock_hfxo_start_us();
    zassert_not_equal(us, UINT32_MAX, "HFXO did not start - cannot measure");

    for (size_t i = 0U; i < ARRAY_SIZE(gates_ms); i++) {
        uint32_t gate = (CLOCK_LF_TICKS_PER_SEC * gates_ms[i]) / 1000U;
        uint32_t counts = 0U, elapsed = 0U;

        ppm[i] = clock_measure_ratio_ppm(gate, &counts, &elapsed);

        printk("  gate %4u ms: %u LF ticks, %u HF counts -> %lld ppm\n",
               (unsigned int)gates_ms[i], (unsigned int)elapsed,
               (unsigned int)counts, (long long)ppm[i]);
    }

    /* Report before asserting: the numbers are the point of the test, and a
     * zassert that fires first would hide the other two gates.
     */
    zassert_true(llabs((long long)ppm[ARRAY_SIZE(gates_ms) - 1]) <= CLOCK_BLE_ACTIVE_PPM,
                 "LFXO and HFXO disagree by %lld ppm; Bluetooth allows +-%d ppm "
                 "on the active clock and this board declares 20 ppm sleep-clock "
                 "accuracy.\n"
                 "  One of the two crystals is off, and this measurement cannot "
                 "say which - it is a ratio.\n"
                 "  Y4 (32.768 kHz) is the less likely culprit: the entire "
                 "pulling range of a tuning-fork crystal is about 150 ppm, so "
                 "load capacitance alone cannot produce this.\n"
                 "  Y3 (32 MHz) can: at 2.44 GHz this error is about %lld kHz, "
                 "wider than the 2 MHz channel spacing, which would put the "
                 "carrier outside the channel in both directions - exactly the "
                 "symmetric radio failure observed.\n"
                 "  To settle it, compare against a part known to be right: run "
                 "this same suite on an nRF52840 DK, or put a counter on Y3.",
                 (long long)ppm[ARRAY_SIZE(gates_ms) - 1], CLOCK_BLE_ACTIVE_PPM,
                 (long long)(ppm[ARRAY_SIZE(gates_ms) - 1] * 2440000 / 1000000));
}

/*
 * What the controller tells the peer about its sleep clock. A declaration
 * tighter than the hardware can honour is worse than a loose one: the peer
 * sizes its receive windows from this number and will stop listening before
 * a drifting peripheral actually transmits.
 */
ZTEST(careloop_2_clocks, test_clock_declared_sca)
{
    printk("  declared sleep-clock accuracy: %s\n", clock_declared_sca());
    printk("  LF reference: %u Hz\n", (unsigned int)CLOCK_LF_TICKS_PER_SEC);

    zassert_equal(CLOCK_LF_TICKS_PER_SEC, 32768U,
                  "system clock is %u Hz, expected 32768 - the ratio "
                  "measurement assumes RTC1 on LFCLK",
                  (unsigned int)CLOCK_LF_TICKS_PER_SEC);
}
