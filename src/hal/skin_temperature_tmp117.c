// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/skin_temperature.h for the TMP117 (U3), I2C 0x48.
 *
 * device_is_ready() is a real identity check: the tmp11x driver reads the
 * device-ID register and refuses anything that is not a TMP11x.
 *
 * Power-on default is continuous mode with 8-sample averaging, so
 * skin_temperature_get_conversion_period_ms() reports ~1000. If this file
 * ever reconfigures averaging or mode, that answer must change with it -
 * callers pace themselves off it.
 *
 * sensor_sample_fetch() returns -EBUSY until DATA_READY sets; that is
 * ERR_SKIN_TEMPERATURE_NOT_READY, not an error to retry internally.
 */

/* TODO: implement hal/skin_temperature.h against the tmp11x driver. */
