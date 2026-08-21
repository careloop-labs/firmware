// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * The HAL before anything has initialised it.
 *
 * Every HAL under src/hal keeps a file-scope "initialized" flag and none of
 * them exposes a deinit, so the -ERR_*_NOT_INITIALIZED paths are reachable
 * exactly once per boot: the first suite to call an init closes them for the
 * rest of the image. Collecting them here, in a suite whose name sorts before
 * every other careloop_hal_* suite, is what makes that reachable at all.
 *
 * The ordering rests on the same mechanism the board suites already rely on -
 * ztest runs suites in alphabetical order of the linker symbol, not link order,
 * because Zephyr's iterable sections are emitted with SORT_BY_NAME. The digit
 * in the suite name is load-bearing. CONFIG_ZTEST_SHUFFLE=n keeps it that way.
 *
 * If a later change calls a HAL init from SYS_INIT or from main(), these tests
 * fail rather than quietly passing on a code path that no longer exists. That
 * is the intended failure: a green run here has to mean the guards were
 * actually exercised.
 *
 * What this does not prove: anything about the hardware. Every assertion below
 * would hold with the PCB unpopulated - these are guard clauses, not devices.
 * The suites that follow are where the parts have to answer.
 *
 * Note the sentinel fills. Several HAL headers promise the destination is
 * "untouched on failure", which is not observable by a return code, so each
 * case fills its destination with 0xA5 and checks the bytes survive.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <leds.h>
#include <motion.h>
#include <power.h>
#include <skin_temperature.h>

/* Arbitrary non-zero fill; any HAL write would disturb it. */
#define SENTINEL_BYTE 0xA5

static void motion_noop_callback(enum motion_event event)
{
    ARG_UNUSED(event);
}

ZTEST_SUITE(careloop_hal_0_uninit, NULL, NULL, NULL, NULL, NULL);

ZTEST(careloop_hal_0_uninit, test_hal_uninit_10_power)
{
    struct power_status status;
    struct power_status untouched;

    memset(&status, SENTINEL_BYTE, sizeof(status));
    memset(&untouched, SENTINEL_BYTE, sizeof(untouched));

    zassert_equal(power_read(&status), -ERR_POWER_NOT_INITIALIZED,
                  "power_read() before power_init() must report "
                  "ERR_POWER_NOT_INITIALIZED");

    zassert_mem_equal(&status, &untouched, sizeof(status),
                      "power_read() wrote to its destination on failure - "
                      "hal/power.h promises it is untouched");
}

ZTEST(careloop_hal_0_uninit, test_hal_uninit_20_skin_temperature)
{
    int32_t millidegc = (int32_t)0xA5A5A5A5;
    uint32_t period_ms = 0xA5A5A5A5U;

    zassert_equal(skin_temperature_read(&millidegc),
                  -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED,
                  "skin_temperature_read() before init must report "
                  "ERR_SKIN_TEMPERATURE_NOT_INITIALIZED, not NOT_READY - the "
                  "two mean different things and callers act on the difference");

    zassert_equal(millidegc, (int32_t)0xA5A5A5A5,
                  "skin_temperature_read() wrote to its destination on failure");

    zassert_equal(skin_temperature_get_conversion_period_ms(&period_ms),
                  -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED,
                  "the period is a property of the fitted sensor, so it is not "
                  "answerable before init");

    zassert_equal(period_ms, 0xA5A5A5A5U,
                  "get_conversion_period_ms() wrote to its destination on "
                  "failure");

    /*
     * hal/skin_temperature.h documents only 0 and NOT_INITIALIZED for this
     * entry point, so a NULL argument is reported as NOT_INITIALIZED too.
     * That conflates two faults; it is pinned here so it stays a decision
     * rather than becoming an accident.
     */
    zassert_equal(skin_temperature_get_conversion_period_ms(NULL),
                  -ERR_SKIN_TEMPERATURE_NOT_INITIALIZED,
                  "NULL is reported as NOT_INITIALIZED by contract");
}

ZTEST(careloop_hal_0_uninit, test_hal_uninit_30_motion)
{
    struct motion_config config = {
        .accel_odr_mhz = 100000U,
        .accel_range_g = 8U,
    };
    struct motion_any_motion_config any_motion = {
        .threshold_mg = 100U,
        .duration_ms = 100U,
    };
    struct motion_any_motion_limits limits;
    struct motion_accel_sample accel;
    struct motion_gyro_sample gyro;

    zassert_equal(motion_get_capabilities(), 0U,
                  "an uninitialised IMU must advertise no capabilities - a "
                  "caller reads this to decide what to configure");

    zassert_equal(motion_configure(&config), -ERR_MOTION_NOT_INITIALIZED,
                  "motion_configure() before motion_init()");
    zassert_equal(motion_set_enabled(true), -ERR_MOTION_NOT_INITIALIZED,
                  "motion_set_enabled() before motion_init()");
    zassert_equal(motion_read_accel(&accel), -ERR_MOTION_NOT_INITIALIZED,
                  "motion_read_accel() before motion_init()");
    zassert_equal(motion_read_gyro(&gyro), -ERR_MOTION_NOT_INITIALIZED,
                  "motion_read_gyro() before motion_init()");
    zassert_equal(motion_set_event_callback(motion_noop_callback),
                  -ERR_MOTION_NOT_INITIALIZED,
                  "motion_set_event_callback() before motion_init()");
    zassert_equal(motion_set_event_enabled(MOTION_EVENT_DATA_READY, true),
                  -ERR_MOTION_NOT_INITIALIZED,
                  "motion_set_event_enabled() before motion_init()");
    zassert_equal(motion_configure_any_motion(&any_motion),
                  -ERR_MOTION_NOT_INITIALIZED,
                  "motion_configure_any_motion() before motion_init()");

    /*
     * This one is the tripwire. motion_get_any_motion_limits() tests
     * MOTION_HAS_ANY_MOTION *before* it tests initialized, so it answers
     * NOT_INITIALIZED only when the capability is compiled in. With
     * CONFIG_BMI270_TRIGGER off it would return UNSUPPORTED instead - which is
     * precisely the config-parity gap prj.conf now closes.
     */
    zassert_equal(motion_get_any_motion_limits(&limits),
                  -ERR_MOTION_NOT_INITIALIZED,
                  "expected NOT_INITIALIZED but got UNSUPPORTED - that means "
                  "MOTION_HAS_ANY_MOTION compiled to 0, i.e. "
                  "CONFIG_BMI270_TRIGGER_GLOBAL_THREAD is missing from prj.conf "
                  "or careloop.dts lost irq-gpios / \"bosch,bmi270-base\"");
}

ZTEST(careloop_hal_0_uninit, test_hal_uninit_40_motion_null_precedence)
{
    /*
     * motion_read_accel/gyro check their argument before they check init, so a
     * NULL here reports READ_FAILED and not NOT_INITIALIZED. power_read() has
     * the opposite precedence. Neither is wrong, but the difference is real and
     * a caller switching on the code needs it to stay put.
     */
    zassert_equal(motion_read_accel(NULL), -ERR_MOTION_READ_FAILED,
                  "motion_read_accel(NULL) checks the pointer before init");
    zassert_equal(motion_read_gyro(NULL), -ERR_MOTION_READ_FAILED,
                  "motion_read_gyro(NULL) checks the pointer before init");
}

ZTEST(careloop_hal_0_uninit, test_hal_uninit_50_leds)
{
    zassert_equal(leds_set(0U, true), -ERR_LEDS_NOT_INITIALIZED,
                  "leds_set() before leds_init()");
    zassert_equal(leds_set_mask(0U), -ERR_LEDS_NOT_INITIALIZED,
                  "leds_set_mask() before leds_init()");

    /*
     * Out of range *and* uninitialised reports the init failure: leds_set()
     * checks initialized first. Pinned because the opposite order is the more
     * obvious way to write it and would change what a caller sees.
     */
    zassert_equal(leds_set(LEDS_COUNT, true), -ERR_LEDS_NOT_INITIALIZED,
                  "the init check precedes the channel bounds check");
}
