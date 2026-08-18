// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Crystal measurements, shared by the ztest suite and the standalone
 * diagnostic.
 *
 * One copy, because the entire value of these numbers is comparability: the
 * careloop result only means something next to the same measurement taken
 * on a board whose crystals are known good. Two implementations that drifted
 * apart would quietly destroy that.
 *
 * Header-only and static inline - each user is a single translation unit, and
 * this keeps the standalone app free of a build-system dependency on the
 * suite's directory.
 */

#ifndef CARELOOP_CLOCK_MEASURE_H_
#define CARELOOP_CLOCK_MEASURE_H_

#include <zephyr/kernel.h>
#include <hal/nrf_clock.h>
#include <hal/nrf_timer.h>

/*
 * TIMER4 is status = "disabled" in nrf52840.dtsi and neither careloop nor
 * the nRF52840 DK enables it, so no Zephyr driver owns it and driving it
 * through the HAL cannot collide with one. It counts PCLK16M, derived from
 * HFCLK - that is what makes it an HFXO-referenced ruler.
 */
#define CLOCK_MEAS_TIMER    NRF_TIMER4
#define CLOCK_MEAS_TIMER_HZ 16000000U

/* k_cycle_get_32() is RTC1, which runs from LFCLK: the other ruler. */
#define CLOCK_LF_TICKS_PER_SEC ((uint32_t)sys_clock_hw_cycles_per_sec())

/* Bluetooth Core: active clock accuracy must be within +-50 ppm. */
#define CLOCK_BLE_ACTIVE_PPM 50

/* nRF52840 datasheet, tHFXO,startup - typical. */
#define CLOCK_HFXO_TYP_START_US 360U

#define CLOCK_HFXO_START_TIMEOUT_MS 10U

static inline const char *clock_lfclk_src_name(nrf_clock_lfclk_t src)
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

/*
 * Bring HFXO up and report how long it took, in microseconds.
 * UINT32_MAX means it never started; 0 means it was already running.
 *
 * Startup time is diagnostic in its own right, and unlike the ratio below it
 * points at one specific crystal: a 32 MHz part with the wrong load
 * capacitance, too high an ESR, or too little drive takes far longer than the
 * ~360 us the datasheet quotes, or does not start at all.
 *
 * The RTC timebase used here comes from LFCLK, so if *that* crystal is the
 * inaccurate one this figure is off by its error - a fraction of a percent,
 * far below the discrepancy this is meant to detect.
 */
static inline uint32_t clock_hfxo_start_us(void)
{
    nrf_clock_hfclk_t src;
    uint32_t t0, t1;

    if (nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, &src) &&
        src == NRF_CLOCK_HFCLK_HIGH_ACCURACY) {
        return 0U;
    }

    nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED);
    t0 = k_cycle_get_32();
    nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTART);

    while (!nrf_clock_event_check(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED)) {
        if (k_cyc_to_ms_floor32(k_cycle_get_32() - t0) >
            CLOCK_HFXO_START_TIMEOUT_MS) {
            return UINT32_MAX;
        }
    }
    t1 = k_cycle_get_32();

    return k_cyc_to_us_floor32(t1 - t0);
}

/*
 * Measure the HFXO-referenced timer against the LFXO-referenced RTC over a
 * gate of `gate_ticks` LF ticks, and return the disagreement in ppm.
 *
 * Both ends are aligned to an RTC tick edge, so the +-1 tick quantisation
 * that would otherwise dominate (30.5 us, i.e. 30 ppm over a 1 s gate) is
 * reduced to the few CPU cycles between the edge and the capture.
 *
 * Sign convention: positive means the timer counted more than the RTC
 * expected, i.e. HFCLK is fast relative to LFCLK.
 *
 * HFXO must already be running - call clock_hfxo_start_us() first, or the
 * ruler is the internal RC and the result is meaningless.
 */
static inline int64_t clock_measure_ratio_ppm(uint32_t gate_ticks,
                                              uint32_t *counts_out,
                                              uint32_t *elapsed_out)
{
    uint32_t t0, elapsed, counts, rtc_end;
    int64_t expected;

    nrf_timer_mode_set(CLOCK_MEAS_TIMER, NRF_TIMER_MODE_TIMER);
    nrf_timer_bit_width_set(CLOCK_MEAS_TIMER, NRF_TIMER_BIT_WIDTH_32);
    nrf_timer_prescaler_set(CLOCK_MEAS_TIMER, NRF_TIMER_FREQ_16MHz);
    nrf_timer_task_trigger(CLOCK_MEAS_TIMER, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(CLOCK_MEAS_TIMER, NRF_TIMER_TASK_CLEAR);

    /* Align the start to an RTC edge. */
    t0 = k_cycle_get_32();
    while (k_cycle_get_32() == t0) {
    }
    t0 = k_cycle_get_32();

    nrf_timer_task_trigger(CLOCK_MEAS_TIMER, NRF_TIMER_TASK_START);

    do {
        elapsed = k_cycle_get_32() - t0;
    } while (elapsed < gate_ticks);

    nrf_timer_task_trigger(CLOCK_MEAS_TIMER, NRF_TIMER_TASK_CAPTURE0);
    rtc_end = k_cycle_get_32();
    nrf_timer_task_trigger(CLOCK_MEAS_TIMER, NRF_TIMER_TASK_STOP);

    counts = nrf_timer_cc_get(CLOCK_MEAS_TIMER, NRF_TIMER_CC_CHANNEL0);
    elapsed = rtc_end - t0;

    expected = ((int64_t)elapsed * (int64_t)CLOCK_MEAS_TIMER_HZ) /
               (int64_t)CLOCK_LF_TICKS_PER_SEC;

    if (counts_out) {
        *counts_out = counts;
    }
    if (elapsed_out) {
        *elapsed_out = elapsed;
    }

    if (expected == 0) {
        return 0;
    }

    return (((int64_t)counts - expected) * 1000000) / expected;
}

/* What the controller declares to the peer about its sleep clock. */
static inline const char *clock_declared_sca(void)
{
#if defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_20PPM)
    return "20 ppm";
#elif defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_50PPM)
    return "50 ppm";
#elif defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_100PPM)
    return "100 ppm";
#elif defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_250PPM)
    return "250 ppm";
#elif defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_500PPM)
    return "500 ppm";
#else
    return "unknown";
#endif
}

#endif /* CARELOOP_CLOCK_MEASURE_H_ */
