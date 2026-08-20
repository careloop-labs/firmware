// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/leds.h for the nPM1300's constant-current sinks.
 *
 * Channel 0 -> LED0 -> D4, channel 1 -> LED1 -> D3, channel 2 -> LED2 -> D2.
 * All red, on/off only, no series resistors - the sink sets the current.
 *
 * The devicetree puts all three in host mode. In the other modes led_on()
 * returns -EPERM, which is ERR_LEDS_NOT_CONTROLLABLE.
 *
 * No PMIC write changes all three sinks together, so leds_set_mask() is three
 * transactions and is not atomic.
 *
 * If nothing lights, suspect the supply first: +3V3 reaches the anodes only
 * through jumper J3 (JP_3V3), and BUCK2 senses upstream of it - so every call
 * here returns 0 while the board stays dark. That is how the first board
 * failed.
 *
 * The PPG emitter (LED1 in afe.kicad_sch) is the AFE-driven SFH7074, not one
 * of these.
 */

/* TODO: implement hal/leds.h against the npm13xx led driver. */
