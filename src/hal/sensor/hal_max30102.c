#include "hal_sensor.h"
#include "max30102.h"
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>

LOG_MODULE_REGISTER(hal_max30102);

#define MAX30102_NODE DT_NODELABEL(max30102)

/* MAX30102 private data structure */
typedef struct {
    const struct device *dev;
    hal_sensor_config_t config;
    hal_sensor_stats_t stats;
    bool calibrated;
    uint32_t baseline_value;
} max30102_priv_t;

/* Private data instance */
static max30102_priv_t max30102_priv = {0};

/* Default configuration */
static const hal_sensor_config_t default_config = {
    .sample_rate_hz = 100,
    .led_current = 63,      /* ~27mA */
    .adc_range = 4096,
    .pulse_width_us = 411,
    .auto_calibrate = true
};

/**
 * @brief Initialize MAX30102 sensor
 */
static hal_error_t max30102_hal_init(void)
{
    LOG_DBG("Initializing MAX30102 HAL");
    
    /* Get device from device tree */
    max30102_priv.dev = DEVICE_DT_GET(MAX30102_NODE);
    if (!max30102_priv.dev) {
        LOG_ERR("Failed to get MAX30102 device");
        return HAL_ERROR_HARDWARE;
    }
    
    /* Check if device is ready */
    if (!device_is_ready(max30102_priv.dev)) {
        LOG_ERR("MAX30102 device not ready");
        return HAL_ERROR_HARDWARE;
    }
    
    /* Initialize configuration from DT with defaults */
    max30102_priv.config = default_config;
#if DT_NODE_HAS_PROP(MAX30102_NODE, sample_rate)
    max30102_priv.config.sample_rate_hz = DT_PROP(MAX30102_NODE, sample_rate);
#endif
#if DT_NODE_HAS_PROP(MAX30102_NODE, led_current_red)
    max30102_priv.config.led_current = DT_PROP(MAX30102_NODE, led_current_red);
#endif
#if DT_NODE_HAS_PROP(MAX30102_NODE, adc_range)
    max30102_priv.config.adc_range = DT_PROP(MAX30102_NODE, adc_range);
#endif
#if DT_NODE_HAS_PROP(MAX30102_NODE, pulse_width)
    max30102_priv.config.pulse_width_us = DT_PROP(MAX30102_NODE, pulse_width);
#endif

    /* Apply configuration via sensor API if supported */
    struct sensor_value sval;
    int rc;
    /* Sampling frequency */
#ifdef SENSOR_ATTR_SAMPLING_FREQUENCY
    sval.val1 = (int)max30102_priv.config.sample_rate_hz;
    sval.val2 = 0;
    rc = sensor_attr_set(max30102_priv.dev, SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &sval);
    if (rc) { LOG_WRN("MAX30102: sampling freq set not supported (%d)", rc); }
#endif
    /* LED currents and other attributes would be mapped to driver-specific attrs if available */
    
    /* Reset statistics */
    memset(&max30102_priv.stats, 0, sizeof(max30102_priv.stats));
    
    /* Initialize calibration state */
    max30102_priv.calibrated = false;
    max30102_priv.baseline_value = 0;
    
    LOG_INF("MAX30102 HAL initialized successfully");
    return HAL_OK;
}

/**
 * @brief Calculate signal quality based on raw value
 */
static hal_quality_t calculate_quality(uint32_t raw_value)
{
    /* Quality assessment based on signal strength */
    if (raw_value < 5000) {
        return HAL_QUALITY_INVALID;  /* No finger detected */
    } else if (raw_value > 250000) {
        return HAL_QUALITY_POOR;     /* Signal saturation */
    } else if (raw_value < 15000) {
        return HAL_QUALITY_POOR;     /* Weak signal */
    } else if (raw_value < 20000) {
        return HAL_QUALITY_FAIR;     /* Marginal signal */
    } else if (raw_value > 200000) {
        return HAL_QUALITY_FAIR;     /* High signal */
    } else {
        return HAL_QUALITY_EXCELLENT; /* Good signal range */
    }
}

/**
 * @brief Read sensor data
 */
static hal_error_t max30102_hal_read(hal_sensor_reading_t *reading)
{
    if (!reading) {
        LOG_ERR("Invalid reading pointer");
        return HAL_ERROR_INVALID_PARAM;
    }
    
    if (!max30102_priv.dev) {
        LOG_ERR("MAX30102 not initialized");
        return HAL_ERROR_NOT_INITIALIZED;
    }
    
    struct sensor_value red_val;
    int ret;
    
    /* Update statistics */
    max30102_priv.stats.total_samples++;
    
    /* Trigger sensor reading */
    ret = sensor_sample_fetch(max30102_priv.dev);
    if (ret) {
        LOG_ERR("Failed to fetch sample: %d", ret);
        max30102_priv.stats.error_count++;
        reading->error_code = HAL_ERROR_HARDWARE;
        return HAL_ERROR_HARDWARE;
    }
    
    /* Get RED channel reading (Heart Rate mode) */
    ret = sensor_channel_get(max30102_priv.dev, SENSOR_CHAN_RED, &red_val);
    if (ret) {
        LOG_ERR("Failed to get red channel: %d", ret);
        max30102_priv.stats.error_count++;
        reading->error_code = HAL_ERROR_HARDWARE;
        return HAL_ERROR_HARDWARE;
    }
    
    /* Fill reading structure */
    reading->timestamp = hal_get_timestamp();
    reading->raw_value = red_val.val1;
    reading->quality = calculate_quality(reading->raw_value);
    reading->error_code = HAL_OK;
    
    /* Convert raw value to meaningful units (placeholder for now) */
    reading->value = (float)reading->raw_value;
    reading->x = 0.0f;
    reading->y = 0.0f;
    reading->z = 0.0f;
    
    /* Update statistics */
    if (reading->quality > HAL_QUALITY_POOR) {
        max30102_priv.stats.valid_samples++;
    }
    
    max30102_priv.stats.last_reading = reading->timestamp;
    
    /* Update average quality (simple moving average) */
    if (max30102_priv.stats.total_samples == 1) {
        max30102_priv.stats.avg_quality = reading->quality;
    } else {
        max30102_priv.stats.avg_quality = 
            (max30102_priv.stats.avg_quality * 9 + reading->quality) / 10;
    }
    
    LOG_DBG("MAX30102 reading: raw=%d, quality=%d%%", 
            reading->raw_value, reading->quality);
    
    return HAL_OK;
}

/**
 * @brief Configure sensor
 */
static hal_error_t max30102_hal_configure(const hal_sensor_config_t *config)
{
    if (!config) {
        LOG_ERR("Invalid config pointer");
        return HAL_ERROR_INVALID_PARAM;
    }
    
    /* Store configuration */
    max30102_priv.config = *config;

    /* Attempt to push to driver */
    struct sensor_value sval;
    int rc;
#ifdef SENSOR_ATTR_SAMPLING_FREQUENCY
    sval.val1 = (int)config->sample_rate_hz;
    sval.val2 = 0;
    rc = sensor_attr_set(max30102_priv.dev, SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &sval);
    if (rc) { LOG_WRN("MAX30102: set rate failed (%d)", rc); }
#endif
    
    LOG_INF("MAX30102 configured: rate=%dHz, led_current=%d", 
            config->sample_rate_hz, config->led_current);
    
    return HAL_OK;
}

/**
 * @brief Calibrate sensor
 */
static hal_error_t max30102_hal_calibrate(void)
{
    LOG_INF("Starting MAX30102 calibration");
    
    if (!max30102_priv.dev) {
        return HAL_ERROR_NOT_INITIALIZED;
    }
    
    /* Take several baseline readings */
    uint32_t baseline_sum = 0;
    uint8_t valid_readings = 0;
    const uint8_t calibration_samples = 10;
    
    for (int i = 0; i < calibration_samples; i++) {
        hal_sensor_reading_t reading;
        hal_error_t ret = max30102_hal_read(&reading);
        
        if (ret == HAL_OK && reading.quality > HAL_QUALITY_POOR) {
            baseline_sum += reading.raw_value;
            valid_readings++;
        }
        
        k_sleep(K_MSEC(100));
    }
    
    if (valid_readings < (calibration_samples / 2)) {
        LOG_ERR("Calibration failed: insufficient valid readings");
        return HAL_ERROR;
    }
    
    max30102_priv.baseline_value = baseline_sum / valid_readings;
    max30102_priv.calibrated = true;
    
    LOG_INF("MAX30102 calibration complete: baseline=%d", 
            max30102_priv.baseline_value);
    
    return HAL_OK;
}

/**
 * @brief Get sensor status
 */
static hal_device_status_t max30102_hal_get_status(void)
{
    if (!max30102_priv.dev) {
        return HAL_DEVICE_STATUS_UNKNOWN;
    }
    
    if (!device_is_ready(max30102_priv.dev)) {
        return HAL_DEVICE_STATUS_ERROR;
    }
    
    return HAL_DEVICE_STATUS_READY;
}

/**
 * @brief Get sensor statistics
 */
static hal_error_t max30102_hal_get_stats(hal_sensor_stats_t *stats)
{
    if (!stats) {
        return HAL_ERROR_INVALID_PARAM;
    }
    
    *stats = max30102_priv.stats;
    return HAL_OK;
}

/**
 * @brief Reset sensor statistics
 */
static hal_error_t max30102_hal_reset_stats(void)
{
    memset(&max30102_priv.stats, 0, sizeof(max30102_priv.stats));
    LOG_INF("MAX30102 statistics reset");
    return HAL_OK;
}

static hal_error_t max30102_hal_get_config(hal_sensor_config_t *out)
{
    if (!out) return HAL_ERROR_INVALID_PARAM;
    *out = max30102_priv.config;
    return HAL_OK;
}

/* MAX30102 operations structure */
static const hal_sensor_ops_t max30102_ops = {
    .init = max30102_hal_init,
    .read = max30102_hal_read,
    .configure = max30102_hal_configure,
    .calibrate = max30102_hal_calibrate,
    .get_status = max30102_hal_get_status,
    .get_stats = max30102_hal_get_stats,
    .reset_stats = max30102_hal_reset_stats,
    .get_config = max30102_hal_get_config
};

/* MAX30102 sensor instance */
static hal_sensor_t max30102_sensor = {
    .type = HAL_SENSOR_TYPE_HEART_RATE,
    .name = "MAX30102 Heart Rate Sensor",
    .ops = &max30102_ops,
    .priv_data = &max30102_priv,
    .initialized = false
};

/**
 * @brief Initialize and register MAX30102 sensor
 */
hal_error_t hal_max30102_init(void)
{
    LOG_INF("Registering MAX30102 sensor with HAL");
    return hal_sensor_register(&max30102_sensor);
}
