/*
 * CareLoop - Heart Rate Processing (Peak Detection)
 */
#ifndef HEART_RATE_H
#define HEART_RATE_H

#include <stdbool.h>
#include "hal_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HR_STATE_IDLE = 0,
    HR_STATE_RUNNING,
    HR_STATE_NO_CONTACT,
    HR_STATE_ERROR
} hr_state_t;

/* Start background processing thread for the given heart-rate sensor */
bool heart_rate_start(hal_sensor_t *hr_sensor);

/* Get the latest BPM value (returns true if valid) */
bool heart_rate_get_bpm(float *bpm_out);

/* Optional: get current processing state */
hr_state_t heart_rate_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HEART_RATE_H */
