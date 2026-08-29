#include "mqtt_client_app.h"
#include "wx_state.h"
#include "config.h"
#include "net.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static TaskHandle_t s_mqtt_task_handle = NULL;
static bool s_connected = false;
static bool s_task_started = false;

static void mqtt_disconnect(void)
{
    if (s_mqtt_client) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }
    s_connected = false;
}

static void publish_ha_discovery(void)
{
    if (!s_mqtt_client || !s_connected) {
        return;
    }

    ESP_LOGI(TAG, "publishing Home Assistant auto-discovery entities");

    const char *temp_disc =
        "{\"name\":\"Tempest Temperature\",\"stat_t\":\"tempest/state\","
        "\"val_tpl\":\"{{ value_json.temperature }}\",\"unit_of_meas\":\"°F\","
        "\"dev_cla\":\"temperature\",\"uniq_id\":\"tempest_temp\"}";
    esp_mqtt_client_publish(s_mqtt_client,
                            "homeassistant/sensor/tempest/temperature/config",
                            temp_disc, 0, 1, 1);

    const char *hum_disc =
        "{\"name\":\"Tempest Humidity\",\"stat_t\":\"tempest/state\","
        "\"val_tpl\":\"{{ value_json.humidity }}\",\"unit_of_meas\":\"%\","
        "\"dev_cla\":\"humidity\",\"uniq_id\":\"tempest_hum\"}";
    esp_mqtt_client_publish(s_mqtt_client,
                            "homeassistant/sensor/tempest/humidity/config",
                            hum_disc, 0, 1, 1);

    const char *press_disc =
        "{\"name\":\"Tempest Pressure\",\"stat_t\":\"tempest/state\","
        "\"val_tpl\":\"{{ value_json.pressure_inhg }}\",\"unit_of_meas\":\"inHg\","
        "\"dev_cla\":\"atmospheric_pressure\",\"uniq_id\":\"tempest_press\"}";
    esp_mqtt_client_publish(s_mqtt_client,
                            "homeassistant/sensor/tempest/pressure/config",
                            press_disc, 0, 1, 1);

    const char *wind_disc =
        "{\"name\":\"Tempest Wind Speed\",\"stat_t\":\"tempest/state\","
        "\"val_tpl\":\"{{ value_json.wind_speed_mph }}\",\"unit_of_meas\":\"mph\","
        "\"dev_cla\":\"wind_speed\",\"uniq_id\":\"tempest_wind\"}";
    esp_mqtt_client_publish(s_mqtt_client,
                            "homeassistant/sensor/tempest/wind/config",
                            wind_disc, 0, 1, 1);

    const char *bat_disc =
        "{\"name\":\"Tempest Battery Voltage\",\"stat_t\":\"tempest/state\","
        "\"val_tpl\":\"{{ value_json.battery_v }}\",\"unit_of_meas\":\"V\","
        "\"dev_cla\":\"voltage\",\"uniq_id\":\"tempest_bat\"}";
    esp_mqtt_client_publish(s_mqtt_client,
                            "homeassistant/sensor/tempest/battery/config",
                            bat_disc, 0, 1, 1);

    const char *in_temp_disc =
        "{\"name\":\"Indoor Temperature\",\"stat_t\":\"tempest/state\","
        "\"val_tpl\":\"{{ value_json.indoor_temp_f }}\",\"unit_of_meas\":\"°F\","
        "\"dev_cla\":\"temperature\",\"uniq_id\":\"tempest_indoor_temp\"}";
    esp_mqtt_client_publish(s_mqtt_client,
                            "homeassistant/sensor/tempest/indoor_temperature/config",
                            in_temp_disc, 0, 1, 1);

    const char *in_hum_disc =
        "{\"name\":\"Indoor Humidity\",\"stat_t\":\"tempest/state\","
        "\"val_tpl\":\"{{ value_json.indoor_humidity }}\",\"unit_of_meas\":\"%\","
        "\"dev_cla\":\"humidity\",\"uniq_id\":\"tempest_indoor_hum\"}";
    esp_mqtt_client_publish(s_mqtt_client,
                            "homeassistant/sensor/tempest/indoor_humidity/config",
                            in_hum_disc, 0, 1, 1);
}

static void publish_state(void)
{
    if (!s_mqtt_client || !s_connected) {
        return;
    }

    wx_state_t s;
    wx_snapshot(&s);
    if (!s.obs_valid) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperature", (double)wx_c_to_f(s.air_temp_c));
    cJSON_AddNumberToObject(root, "humidity", (double)s.humidity_pct);
    cJSON_AddNumberToObject(root, "dew_point", (double)wx_c_to_f(s.dew_point_c));
    cJSON_AddNumberToObject(root, "pressure_inhg", (double)wx_mb_to_inhg(s.pressure_mb));
    cJSON_AddNumberToObject(root, "wind_speed_mph", (double)wx_ms_to_mph(s.wind_avg_ms));
    cJSON_AddNumberToObject(root, "wind_gust_mph", (double)wx_ms_to_mph(s.wind_gust_ms));
    cJSON_AddNumberToObject(root, "wind_dir_deg", s.wind_dir_deg);
    cJSON_AddNumberToObject(root, "rain_today_in", (double)wx_mm_to_in(s.rain_today_mm));
    cJSON_AddNumberToObject(root, "solar_radiation_wm2", (double)s.solar_radiation_wm2);
    cJSON_AddNumberToObject(root, "uv_index", (double)s.uv_index);
    cJSON_AddNumberToObject(root, "battery_v", (double)s.battery_v);
    cJSON_AddNumberToObject(root, "hub_rssi", s.hub_rssi);
    cJSON_AddBoolToObject(root, "indoor_valid", s.indoor_valid);
    if (s.indoor_valid) {
        cJSON_AddNumberToObject(root, "indoor_temp_f", (double)wx_c_to_f(s.indoor_temp_c));
        cJSON_AddNumberToObject(root, "indoor_humidity", (double)s.indoor_humidity_pct);
    }
    if (s.aqi_valid) {
        cJSON_AddNumberToObject(root, "aqi", s.aqi_val);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_mqtt_client_publish(s_mqtt_client, "tempest/state", payload, 0, 0, 0);
    free(payload);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to broker");
        s_connected = true;
        publish_ha_discovery();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from broker");
        s_connected = false;
        break;
    default:
        break;
    }
}

static bool mqtt_try_connect(void)
{
    cfg_t cfg;
    cfg_get(&cfg);

    if (!cfg.mqtt_enabled || cfg.mqtt_broker[0] == '\0' || !net_is_connected()) {
        return false;
    }

    if (s_mqtt_client) {
        return true;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:1883", cfg.mqtt_broker);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "mqtt client init failed");
        return false;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                     mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt start failed: %s", esp_err_to_name(err));
        mqtt_disconnect();
        return false;
    }

    ESP_LOGI(TAG, "connecting to %s", uri);
    return true;
}

static void mqtt_loop_task(void *arg)
{
    (void)arg;

    while (1) {
        cfg_t cfg;
        cfg_get(&cfg);

        if (!cfg.mqtt_enabled || cfg.mqtt_broker[0] == '\0') {
            mqtt_disconnect();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (!net_is_connected()) {
            mqtt_disconnect();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        mqtt_try_connect();

        if (s_connected) {
            publish_state();
        }

        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

esp_err_t mqtt_app_start(void)
{
    if (s_task_started) {
        return ESP_OK;
    }
    s_task_started = true;
    if (xTaskCreate(mqtt_loop_task, "mqtt_task", 6 * 1024, NULL, 3,
                    &s_mqtt_task_handle) != pdPASS) {
        s_task_started = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void mqtt_app_stop(void)
{
    mqtt_disconnect();
}

void mqtt_app_reconnect(void)
{
    mqtt_disconnect();
}
