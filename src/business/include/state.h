/*
 * State machine public interface (business layer only).
 * https://docs.zephyrproject.org/latest/services/smf/index.html#state-creation 
 */

#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/smf.h>

#include "errors.h"

/* State specific error codes */
#define ERR_STATE_RUNTIME (ERR_BUSINESS_STATES + 1) //TODO: add usefule ERRORS here after development
#define ERR_STATE_COLISSION (ERR_BUSINESS_STATES + 2)

/* High-level states. */
enum state {
	BOOT_INIT,
	IDLE_DAY,
	IMU_CHECK,
	PPG_MEASURE,
	SLEEP_CONTINUOUS,
	ACTIVITY_MODE,
	LOW_BATTERY,
	CHARGING
};

struct smf_obj {
	struct smf_ctx ctx;
	/* TODO: add per-instance data here (no dynamic allocation). */
};

void smf_init(struct smf_obj *obj); 
void smf_update(struct smf_obj *obj); 


#endif /* STATE_H */

