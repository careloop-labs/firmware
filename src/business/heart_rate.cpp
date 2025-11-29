#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#include "heart_rate.h"
#include "hr_filter.h"
#include "hal_sensor.h"


LOG_MODULE_REGISTER(hr_proc, LOG_LEVEL_DBG);


#define STACKSIZE 4096
#define THREAD0_PRIORITY 7

static void hr_thread_entry(void *p1, void *p2, void *p3)
{
    // mark as unused
	(void)p1;
	(void)p2;
	(void)p3;

	LOG_INF("HR thread started");

	hr_filter_init();
	static bool _hal_inited = false;
	static hal_sensor_t *_sensor = NULL;
	static hal_sensor_reading_t last_reading = {0};

	float ppg_value = NAN;
	float preProcessed_ppg_value = NAN;

	if (!_hal_inited) {
		_hal_inited = true;
		hal_error_t herr = hal_sensor_system_init();
		if (herr != HAL_OK) {
			LOG_ERR("hal_sensor_system_init failed: %d", herr);
		}
		_sensor = hal_sensor_get(HAL_SENSOR_TYPE_HEART_RATE);
		if (!_sensor) {
			LOG_ERR("heart rate sensor not registered");
		}
	}

	while (1) {
		//TODO: read sensor signal from hal
		if (_sensor && _sensor->ops && _sensor->ops->read) {
			hal_error_t r = _sensor->ops->read(&last_reading);
			if (r == HAL_OK) {
				ppg_value = last_reading.value;
				LOG_DBG("ppg: ts=%llu raw=%u val=%.3f q=%d",
					(unsigned long long)last_reading.timestamp,
					last_reading.raw_value,
					ppg_value,
					(int)last_reading.quality);
			} else {
				LOG_WRN("ppg read failed: %d", r);
			}
		}
		
		//TODO: filter signal
		if (!isnan(ppg_value)) {
			hr_filter_process(&ppg_value, &preProcessed_ppg_value, 1u); // passes an unsigned 1
		} else {
			preProcessed_ppg_value = NAN;
		}
		
    	//TODO: compute (salience)

    	//TODO: beats


		k_sleep(K_MSEC(100)); /* placeholder delay */
	}
}

K_THREAD_DEFINE(hr_thread_id, STACKSIZE, hr_thread_entry, NULL, NULL, NULL,
				THREAD0_PRIORITY, 0, 0);

