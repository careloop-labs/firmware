// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 CareLoop Labs

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>

#include "state.h"

/*
 * Every run action returns SMF_EVENT_PROPAGATE.
 *
 * Zephyr changed the run signature from `void (*)(void *)` to
 * `enum smf_state_result (*)(void *)`, which is what this file was failing to
 * compile against. PROPAGATE is the faithful translation of the old behaviour,
 * not a new choice: under the previous API a child's run was always followed by
 * its parent's unless smf_set_handled() was called, and nothing here ever
 * called it. That matters because power_parent is where the battery and
 * charging transitions are meant to live - returning HANDLED from the children
 * would stop it running at all.
 */

LOG_MODULE_REGISTER(state_machine, LOG_LEVEL_INF);

static void boot_init_entry(void *obj);
static enum smf_state_result boot_init_run(void *obj);
static void boot_init_exit(void *obj);

static void idle_day_entry(void *obj);
static enum smf_state_result idle_day_run(void *obj);
static void idle_day_exit(void *obj);

static void imu_check_entry(void *obj);
static enum smf_state_result imu_check_run(void *obj);
static void imu_check_exit(void *obj);

static void ppg_measure_entry(void *obj);
static enum smf_state_result ppg_measure_run(void *obj);
static void ppg_measure_exit(void *obj);

static void sleep_continuous_entry(void *obj);
static enum smf_state_result sleep_continuous_run(void *obj);
static void sleep_continuous_exit(void *obj);

static void activity_mode_entry(void *obj);
static enum smf_state_result activity_mode_run(void *obj);
static void activity_mode_exit(void *obj);

static void low_battery_entry(void *obj);
static enum smf_state_result low_battery_run(void *obj);
static void low_battery_exit(void *obj);

static void charging_entry(void *obj);
static enum smf_state_result charging_run(void *obj);
static void charging_exit(void *obj);

static enum smf_state_result power_logic_run(void *obj);

static const struct smf_state power_parent = 
    SMF_CREATE_STATE(NULL, power_logic_run, NULL, NULL, NULL);

static const struct smf_state demo_states[] = {
    [BOOT_INIT]        = SMF_CREATE_STATE(boot_init_entry, boot_init_run, boot_init_exit, &power_parent, NULL),
    [IDLE_DAY]         = SMF_CREATE_STATE(idle_day_entry, idle_day_run, idle_day_exit, &power_parent, NULL),
    [IMU_CHECK]        = SMF_CREATE_STATE(imu_check_entry, imu_check_run, imu_check_exit, &power_parent, NULL),
    [PPG_MEASURE]      = SMF_CREATE_STATE(ppg_measure_entry, ppg_measure_run, ppg_measure_exit, &power_parent, NULL),
    [SLEEP_CONTINUOUS] = SMF_CREATE_STATE(sleep_continuous_entry, sleep_continuous_run, sleep_continuous_exit, &power_parent, NULL),
    [ACTIVITY_MODE]    = SMF_CREATE_STATE(activity_mode_entry, activity_mode_run, activity_mode_exit, &power_parent, NULL),
    [LOW_BATTERY]      = SMF_CREATE_STATE(low_battery_entry, low_battery_run, low_battery_exit, NULL, NULL),
    [CHARGING]         = SMF_CREATE_STATE(charging_entry, charging_run, charging_exit, NULL, NULL),
};

void smf_init(struct smf_obj *obj)
{
    smf_set_initial(SMF_CTX(obj), &demo_states[BOOT_INIT]);
}

void smf_update(struct smf_obj *obj)
{
    smf_run_state(SMF_CTX(obj));
}

/* Init functions */

static enum smf_state_result power_logic_run(void *obj)
{
    struct smf_obj *ctx = (struct smf_obj *)obj;

    //if (battery_is_low()) {
    //    smf_set_state(SMF_CTX(ctx), &demo_states[LOW_BATTERY]);
    //} else if (usb_is_connected()) {
    //    smf_set_state(SMF_CTX(ctx), &demo_states[CHARGING]);
    //}

    return SMF_EVENT_PROPAGATE;
}

static void boot_init_entry(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("BOOT_INIT entry");
}

static enum smf_state_result boot_init_run(void *obj)
{
    ARG_UNUSED(obj);
    /* TODO: add boot init logic, then transition when ready */
    smf_set_state(SMF_CTX(obj), &demo_states[IDLE_DAY]);

    return SMF_EVENT_PROPAGATE;
}

static void boot_init_exit(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("BOOT_INIT exit");
}

static void idle_day_entry(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("IDLE_DAY entry");
}

static enum smf_state_result idle_day_run(void *obj)
{
    ARG_UNUSED(obj);
    /* TODO: add idle logic; remain or transition based on events */

    return SMF_EVENT_PROPAGATE;
}

static void idle_day_exit(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("IDLE_DAY exit");
}

static void imu_check_entry(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("IMU_CHECK entry");
}

static enum smf_state_result imu_check_run(void *obj)
{
    ARG_UNUSED(obj);
    /* TODO: gate motion, decide next state. */
    smf_set_state(SMF_CTX(obj), &demo_states[PPG_MEASURE]);

    return SMF_EVENT_PROPAGATE;
}

static void imu_check_exit(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("IMU_CHECK exit");
}

static void ppg_measure_entry(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("PPG_MEASURE entry");
}

static enum smf_state_result ppg_measure_run(void *obj)
{
    ARG_UNUSED(obj);
    /* TODO: measure window, then return to idle. */
    smf_set_state(SMF_CTX(obj), &demo_states[IDLE_DAY]);

    return SMF_EVENT_PROPAGATE;
}

static void ppg_measure_exit(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("PPG_MEASURE exit");
}

static void sleep_continuous_entry(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("SLEEP_CONTINUOUS entry");
}

static enum smf_state_result sleep_continuous_run(void *obj)
{
    ARG_UNUSED(obj);

    return SMF_EVENT_PROPAGATE;
}

static void sleep_continuous_exit(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("SLEEP_CONTINUOUS exit");
}

static void activity_mode_entry(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("ACTIVITY_MODE entry");
}

static enum smf_state_result activity_mode_run(void *obj)
{
    ARG_UNUSED(obj);

    return SMF_EVENT_PROPAGATE;
}

static void activity_mode_exit(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("ACTIVITY_MODE exit");
}

static void low_battery_entry(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("LOW_BATTERY entry");
}

static enum smf_state_result low_battery_run(void *obj)
{
    ARG_UNUSED(obj);

    return SMF_EVENT_PROPAGATE;
}

static void low_battery_exit(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("LOW_BATTERY exit");
}

static void charging_entry(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("CHARGING entry");
}

static enum smf_state_result charging_run(void *obj)
{
    ARG_UNUSED(obj);

    return SMF_EVENT_PROPAGATE;
}

static void charging_exit(void *obj)
{
    ARG_UNUSED(obj);
    LOG_INF("CHARGING exit");
}