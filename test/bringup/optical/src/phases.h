// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * Phases of the optical bring-up app, split by which half of the optical
 * path they exercise. main.c owns the emitters and the ordering; detector.c
 * owns everything that reads light back.
 */

#ifndef BRINGUP_OPTICAL_PHASES_H_
#define BRINGUP_OPTICAL_PHASES_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Run the photodetector phases.
 *
 * Assumes the caller has already brought the front end up and started the
 * sequence, and leaves every emitter dark on return.
 *
 * @param drive_ua Emitter current to use for the reflectance phase.
 * @return true if every phase that can produce a verdict passed.
 */
bool detector_run(uint32_t drive_ua);

#endif /* BRINGUP_OPTICAL_PHASES_H_ */
