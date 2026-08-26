#include "indoor.h"
#include "board_pins.h"
#include "display.h"
#include "wx_state.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "indoor";

#define AHT20_ADDR      0x38
#define SHT4X_ADDR      0x44

#define TASK_STACK      3072
#define TASK_PRIO       3
#define PROBE_TIMEOUT   100

static i2c_master_dev_handle_t s_dev;
static indoor_sensor_t         s_type;

/* --------------------------------------------------------------------------
 * AHT20 / DHT20
 * -------------------------------------------------------------------------- */

static esp_err_t aht20_init(void)
{
    /* Datasheet wants 40 ms after power-up before the status byte is valid. */
    vTaskDelay(pdMS_TO_TICKS(40));

    uint8_t status = 0;
    esp_err_t err = i2c_master_receive(s_dev, &status, 1, 200);
    if (err != ESP_OK) {
        return err;
    }
    /* Bit 3 set means calibrated. A fresh part needs the init command. */
    if ((status & 0x08) == 0) {
        const uint8_t init_cmd[3] = { 0xBE, 0x08, 0x00 };
        err = i2c_master_transmit(s_dev, init_cmd, sizeof(init_cmd), 200);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_LOGI(TAG, "AHT20 calibration triggered");
    }
    return ESP_OK;
}

static esp_err_t aht20_read(float *temp_c, float *humidity)
{
    const uint8_t measure[3] = { 0xAC, 0x33, 0x00 };
    esp_err_t err = i2c_master_transmit(s_dev, measure, sizeof(measure), 200);
    if (err != ESP_OK) {
        return err;
    }
    /* Conversion takes ~75 ms; the datasheet says wait 80. */
    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t d[7] = {0};
    err = i2c_master_receive(s_dev, d, sizeof(d), 200);
    if (err != ESP_OK) {
        return err;
    }
    if (d[0] & 0x80) {
        ESP_LOGW(TAG, "AHT20 still busy");
        return ESP_ERR_TIMEOUT;
    }

    /* 20-bit humidity, then 20-bit temperature, sharing the middle byte. */
    uint32_t raw_h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) |
                     ((uint32_t)d[3] >> 4);
    uint32_t raw_t = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) |
                      (uint32_t)d[5];

    *humidity = (float)raw_h * 100.0f / 1048576.0f;
    *temp_c   = (float)raw_t * 200.0f / 1048576.0f - 50.0f;
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * SHT4x
 * -------------------------------------------------------------------------- */

static esp_err_t sht4x_read(float *temp_c, float *humidity)
{
    const uint8_t measure = 0xFD;      /* high repeatability, no heater */
    esp_err_t err = i2c_master_transmit(s_dev, &measure, 1, 200);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t d[6] = {0};
    err = i2c_master_receive(s_dev, d, sizeof(d), 200);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t raw_t = ((uint16_t)d[0] << 8) | d[1];
    uint16_t raw_h = ((uint16_t)d[3] << 8) | d[4];

    *temp_c   = -45.0f + 175.0f * (float)raw_t / 65535.0f;
    float rh  = -6.0f + 125.0f * (float)raw_h / 65535.0f;
    /* The transfer function overshoots slightly at the rails. */
    if (rh < 0.0f)   rh = 0.0f;
    if (rh > 100.0f) rh = 100.0f;
    *humidity = rh;
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Probe and poll
 * -------------------------------------------------------------------------- */

static esp_err_t attach(i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = BOARD_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

static void detach(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

static indoor_sensor_t probe(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, AHT20_ADDR, PROBE_TIMEOUT) == ESP_OK) {
        if (attach(bus, AHT20_ADDR) == ESP_OK && aht20_init() == ESP_OK) {
            ESP_LOGI(TAG, "AHT20/DHT20 found at 0x%02X", AHT20_ADDR);
            return INDOOR_SENSOR_AHT20;
        }
        detach();
    }
    if (i2c_master_probe(bus, SHT4X_ADDR, PROBE_TIMEOUT) == ESP_OK) {
        if (attach(bus, SHT4X_ADDR) == ESP_OK) {
            ESP_LOGI(TAG, "SHT4x found at 0x%02X", SHT4X_ADDR);
            return INDOOR_SENSOR_SHT4X;
        }
        detach();
    }
    return INDOOR_SENSOR_NONE;
}

static void indoor_task(void *arg)
{
    (void)arg;

    while (1) {
        float t = 0.0f, h = 0.0f;
        esp_err_t err = (s_type == INDOOR_SENSOR_AHT20)
                      ? aht20_read(&t, &h)
                      : sht4x_read(&t, &h);

        if (err == ESP_OK) {
            /* Sanity gate. A sensor that has come loose reads garbage rather
             * than failing cleanly, and a wall display showing -50 C indoors
             * is worse than one showing nothing. */
            if (t > -20.0f && t < 70.0f && h >= 0.0f && h <= 100.0f) {
                wx_state_t p = {0};
                p.indoor_temp_c       = t;
                p.indoor_humidity_pct = h;
                /* Temperature and humidity only -- that is the whole
                 * requirement, and the UI shows nothing it cannot measure. */
                wx_update_indoor(&p);
            } else {
                ESP_LOGW(TAG, "implausible reading %.1fC %.0f%%, ignoring",
                         (double)t, (double)h);
            }
        } else {
            ESP_LOGW(TAG, "read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_INDOOR_POLL_INTERVAL_S * 1000));
    }
}

esp_err_t indoor_start(void)
{
    /* Share the bus the touch controller already created, so there is one
     * owner. The GT911 and this sensor sit on the same two pins. */
    i2c_master_bus_handle_t bus = display_get_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "no I2C bus; display_init() must run first");
        return ESP_ERR_INVALID_STATE;
    }

    s_type = probe(bus);
    if (s_type == INDOOR_SENSOR_NONE) {
        ESP_LOGW(TAG, "no indoor sensor at 0x%02X or 0x%02X.",
                 AHT20_ADDR, SHT4X_ADDR);
        ESP_LOGW(TAG, "Plug an AHT20/DHT20 or SHT4x into the Grove header.");
        ESP_LOGW(TAG, "The outdoor half of the display is unaffected.");
        return ESP_ERR_NOT_FOUND;
    }

    if (xTaskCreate(indoor_task, "indoor", TASK_STACK, NULL, TASK_PRIO, NULL)
            != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

indoor_sensor_t indoor_sensor_type(void)
{
    return s_type;
}

const char *indoor_sensor_name(void)
{
    switch (s_type) {
    case INDOOR_SENSOR_AHT20: return "AHT20/DHT20";
    case INDOOR_SENSOR_SHT4X: return "SHT4x";
    default:                  return "none";
    }
}
