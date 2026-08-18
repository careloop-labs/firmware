// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Crystal A/B.
 * 
 * That comparison is the whole point. careloop reports its LFXO and HFXO
 * disagreeing by ~697 ppm, but a ratio names no culprit - it is one number
 * from two oscillators. A DK measured with identical code turns "one of these
 * two is wrong" into "this board differs from a good one in this specific
 * way".
 *
 *   ./scripts/bringup.sh clocks                                  # careloop
 *   BOARD=nrf52840dk/nrf52840 PROBE=<dk-serial> ./scripts/bringup.sh clocks
 *
 * With two probes attached, PROBE is not optional - see scripts/bringup.sh.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <hal/nrf_clock.h>

#include "clock_measure.h"

/* RTT discards anything written before a viewer attaches, and bringup.sh flashes,
 * releases the probe, then attaches - so outlast that gap.
 */
#define RTT_ATTACH_GRACE_MS 3000U

#define REPEATS 5

int main(void)
{
    static const uint32_t gates_ms[] = { 250U, 1000U, 2000U };
    nrf_clock_lfclk_t lf_src = NRF_CLOCK_LFCLK_RC;
    nrf_clock_hfclk_t hf_src = NRF_CLOCK_HFCLK_LOW_ACCURACY;
    bool lf_run;
    uint32_t start_us[REPEATS];
    uint32_t worst = 0U, best = UINT32_MAX;
    uint64_t sum = 0U;

    k_sleep(K_MSEC(RTT_ATTACH_GRACE_MS));

    printk("\nCARELOOP CLOCK A/B\n");
    printk("board    %s\n", CONFIG_BOARD_TARGET);
    printk("build    %s %s\n", __DATE__, __TIME__);

    lf_run = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_LFCLK, &lf_src);
    printk("lfclk    %s src=%s (LFCLKSTAT 0x%08x) declared SCA %s\n",
           lf_run ? "run" : "STOPPED", clock_lfclk_src_name(lf_src),
           (unsigned int)NRF_CLOCK->LFCLKSTAT, clock_declared_sca());

    /*
     * HFXO startup, repeated.
     *
     * This is the measurement that can name a single crystal: the ratio below
     * cannot tell Y3 from Y4, but startup time belongs to the 32 MHz part
     * alone. Repeated because a marginal crystal is not reliably slow - it is
     * erratic, and one sample cannot show that.
     *
     * Each pass must stop HFXO first, or every reading after the first is 0.
     */
    printk("\nHFXO startup (datasheet typical %u us)\n", CLOCK_HFXO_TYP_START_US);
    for (int i = 0; i < REPEATS; i++) {
        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTOP);
        while (nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, &hf_src) &&
               hf_src == NRF_CLOCK_HFCLK_HIGH_ACCURACY) {
        }
        k_sleep(K_MSEC(20));

        start_us[i] = clock_hfxo_start_us();
        if (start_us[i] == UINT32_MAX) {
            printk("  run %d: DID NOT START within %u ms\n", i,
                   CLOCK_HFXO_START_TIMEOUT_MS);
            continue;
        }

        sum += start_us[i];
        worst = MAX(worst, start_us[i]);
        best = MIN(best, start_us[i]);
        printk("  run %d: %u us\n", i, (unsigned int)start_us[i]);
    }
    printk("  min %u us  max %u us  mean %u us  -> %s\n",
           (unsigned int)best, (unsigned int)worst,
           (unsigned int)(sum / REPEATS),
           (sum / REPEATS) > (2 * CLOCK_HFXO_TYP_START_US) ? "SLOW" : "ok");

    /* Ratio needs HFXO up as the reference. */
    (void)clock_hfxo_start_us();
    (void)nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, &hf_src);
    printk("\nhfclk    src=%s (HFCLKSTAT 0x%08x)\n",
           hf_src == NRF_CLOCK_HFCLK_HIGH_ACCURACY ? "XTAL" : "RC",
           (unsigned int)NRF_CLOCK->HFCLKSTAT);

    printk("\nLFXO vs HFXO ratio (BLE allows +-%d ppm)\n", CLOCK_BLE_ACTIVE_PPM);
    for (size_t i = 0U; i < ARRAY_SIZE(gates_ms); i++) {
        uint32_t gate = (CLOCK_LF_TICKS_PER_SEC * gates_ms[i]) / 1000U;
        uint32_t counts = 0U, elapsed = 0U;
        int64_t ppm = clock_measure_ratio_ppm(gate, &counts, &elapsed);

        printk("  gate %4u ms: %u LF ticks, %u HF counts -> %lld ppm"
               "  (%lld kHz at 2.44 GHz)\n",
               (unsigned int)gates_ms[i], (unsigned int)elapsed,
               (unsigned int)counts, (long long)ppm,
               (long long)(ppm * 2440000 / 1000000));
    }

    printk("\nRESULT: compare these three blocks against the other board.\n");
    printk("        A good board: HFXO startup near %u us, ratio within +-%d ppm.\n",
           CLOCK_HFXO_TYP_START_US, CLOCK_BLE_ACTIVE_PPM);

    /*
     * Leave an HFXO-referenced ruler running for the host.
     *
     * The ratio above needs a working LFXO, and once Y4 stopped starting there
     * was no on-board reference left worth having - the internal LFRC drifts
     * far too much (it turned a rock-steady 697 ppm reading into 81 / -357 /
     * -2830 ppm across three gate lengths).
     *
     * The host has a better clock than anything on this board. TIMER4 counts
     * PCLK16M, so it counts HFXO directly; a debugger can trigger CAPTURE and
     * read CC[0] over SWD without stopping the CPU. Two such reads separated
     * by a known wall-clock interval give HFXO's *absolute* error, which is
     * what actually decides whether the BLE carrier lands in its channel - and
     * unlike a ratio it names Y3 without needing Y4 to work.
     *
     * SWD transaction latency (sub-millisecond) largely cancels between the
     * two reads, so a 120 s gate resolves well under 50 ppm.
     *
     * The counter is 32-bit at 16 MHz and wraps every ~268 s; the host must
     * take its difference modulo 2^32 and keep the gate shorter than that.
     */
    /*
     * Hold HFXO through the clock driver, not with a bare HAL HFCLKSTART.
     *
     * Triggering the task directly is not enough: nothing then owns the
     * request, and the driver stops HFXO again the moment the CPU idles. The
     * SoC falls back to the 64 MHz internal RC, TIMER4 keeps counting a
     * PCLK16M that is now RC-derived, and the measurement silently becomes a
     * reading of the RC's +-1.5% error instead of the crystal's. That mistake
     * produced a confident -2764 ppm here before HFCLKSTAT was checked.
     */
    static struct onoff_client hfxo_cli;
    struct onoff_manager *hfxo_mgr =
        z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    int hfxo_res;

    sys_notify_init_spinwait(&hfxo_cli.notify);
    if (onoff_request(hfxo_mgr, &hfxo_cli) < 0) {
        printk("  FAILED to request HFXO - reading below would be the RC\n");
    }
    while (sys_notify_fetch_result(&hfxo_cli.notify, &hfxo_res) == -EAGAIN) {
    }

    while (!(nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, &hf_src) &&
             hf_src == NRF_CLOCK_HFCLK_HIGH_ACCURACY)) {
    }

    nrf_timer_task_trigger(CLOCK_MEAS_TIMER, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(CLOCK_MEAS_TIMER, NRF_TIMER_TASK_CLEAR);
    nrf_timer_task_trigger(CLOCK_MEAS_TIMER, NRF_TIMER_TASK_START);

    printk("\nTIMER4 free-running on HFXO for host-referenced measurement:\n");
    printk("  HFCLKSTAT 0x%08x (must stay SRC=XTAL for the reading to mean anything)\n",
           (unsigned int)NRF_CLOCK->HFCLKSTAT);
    printk("  TASKS_CAPTURE[0] = 0x%08x   CC[0] = 0x%08x   %u Hz nominal\n",
           (unsigned int)(uintptr_t)&CLOCK_MEAS_TIMER->TASKS_CAPTURE[0],
           (unsigned int)(uintptr_t)&CLOCK_MEAS_TIMER->CC[0],
           CLOCK_MEAS_TIMER_HZ);

    /* Idle forever - the CPU must keep running so HFXO stays requested. */
    for (;;) {
        k_sleep(K_SECONDS(10));
    }

    return 0;
}
