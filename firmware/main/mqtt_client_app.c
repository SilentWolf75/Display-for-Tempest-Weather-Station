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

#define HA_AVAIL_TOPIC  "tempest/availability"

static void publish_availability(const wx_state_t *s)
{
    if (!s_mqtt_client || !s_connected) {
        return;
    }
    bool online = s->wifi_connected && s->obs_valid && !wx_obs_is_stale(s);
    esp_mqtt_client_publish(s_mqtt_client, HA_AVAIL_TOPIC,
                            online ? "online" : "offline", 0, 1, 1);
}

static void ha_sensor(const char *slug, const char *name, const char *json_key,
                      const char *dev_class, const char *unit)
{
    char payload[420];
    int n = snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"stat_t\":\"tempest/state\","
        "\"avty_t\":\"%s\",\"val_tpl\":\"{{ value_json.%s }}\","
        "\"uniq_id\":\"tempest_%s\"",
        name, HA_AVAIL_TOPIC, json_key, slug);
    if (dev_class && dev_class[0]) {
        n += snprintf(payload + n, sizeof(payload) - (size_t)n,
                      ",\"dev_cla\":\"%s\"", dev_class);
    }
    if (unit && unit[0]) {
        n += snprintf(payload + n, sizeof(payload) - (size_t)n,
                      ",\"unit_of_meas\":\"%s\"", unit);
    }
    snprintf(payload + n, sizeof(payload) - (size_t)n, "}");

    char topic[96];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/tempest/%s/config", slug);
    esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 1);
}

static void publish_ha_discovery(void)
{
    if (!s_mqtt_client || !s_connected) {
        return;
    }

    ESP_LOGI(TAG, "publishing Home Assistant auto-discovery entities");

    char tu[8], wu[8], pu[16], ru[8];
    snprintf(tu, sizeof(tu), "\xC2\xB0%s", cfg_temp_suffix());
    snprintf(wu, sizeof(wu), " %s", cfg_wind_suffix());
    snprintf(pu, sizeof(pu), " %s", cfg_pressure_suffix());
    snprintf(ru, sizeof(ru), " %s", cfg_rain_suffix());

    ha_sensor("temperature", "Tempest Temperature", "temperature", "temperature", tu);
    ha_sensor("humidity", "Tempest Humidity", "humidity", "humidity", "%");
    ha_sensor("pressure", "Tempest Pressure", "pressure", "atmospheric_pressure", pu);
    ha_sensor("wind", "Tempest Wind Speed", "wind_speed", "wind_speed", wu);
    ha_sensor("wind_gust", "Tempest Wind Gust", "wind_gust", "wind_speed", wu);
    ha_sensor("battery", "Tempest Battery Voltage", "battery_v", "voltage", "V");
    ha_sensor("indoor_temperature", "Indoor Temperature", "indoor_temp", "temperature", tu);
    ha_sensor("indoor_humidity", "Indoor Humidity", "indoor_humidity", "humidity", "%");
    ha_sensor("rain_today", "Tempest Rain Today", "rain_today", "precipitation", ru);
    ha_sensor("uv_index", "Tempest UV Index", "uv_index", NULL, NULL);
    ha_sensor("dew_point", "Tempest Dew Point", "dew_point", "temperature", tu);
    ha_sensor("lightning_3h", "Tempest Lightning (3h)", "lightning_3h", NULL, NULL);
    ha_sensor("aqi", "Tempest Air Quality", "aqi", NULL, NULL);
}

static void publish_state(void)
{
    if (!s_mqtt_client || !s_connected) {
        return;
    }

    wx_state_t s;
    wx_snapshot(&s);

    publish_availability(&s);

    if (!s.obs_valid) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperature", (double)cfg_temp(s.air_temp_c));
    cJSON_AddNumberToObject(root, "humidity", (double)s.humidity_pct);
    cJSON_AddNumberToObject(root, "dew_point", (double)cfg_temp(s.dew_point_c));
    cJSON_AddNumberToObject(root, "pressure", (double)cfg_pressure(s.pressure_mb));
    cJSON_AddNumberToObject(root, "wind_speed", (double)cfg_wind(s.wind_avg_ms));
    cJSON_AddNumberToObject(root, "wind_gust", (double)cfg_wind(s.wind_gust_ms));
    cJSON_AddNumberToObject(root, "wind_dir_deg", s.wind_dir_deg);
    cJSON_AddNumberToObject(root, "rain_today", (double)cfg_rain(s.rain_today_mm));
    cJSON_AddNumberToObject(root, "solar_radiation_wm2", (double)s.solar_radiation_wm2);
    cJSON_AddNumberToObject(root, "uv_index", (double)s.uv_index);
    cJSON_AddNumberToObject(root, "lightning_3h", s.strikes_3h);
    cJSON_AddNumberToObject(root, "battery_v", (double)s.battery_v);
    cJSON_AddNumberToObject(root, "hub_rssi", s.hub_rssi);
    cJSON_AddBoolToObject(root, "indoor_valid", s.indoor_valid);
    if (s.indoor_valid) {
        cJSON_AddNumberToObject(root, "indoor_temp", (double)cfg_temp(s.indoor_temp_c));
        cJSON_AddNumberToObject(root, "indoor_humidity", (double)s.indoor_humidity_pct);
    }
    if (s.aqi_valid) {
        cJSON_AddNumberToObject(root, "aqi", s.aqi_val);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return;
    }

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
            wx_state_t snap;
            wx_snapshot(&snap);
            publish_availability(&snap);
            if (snap.obs_valid) {
                publish_state();
            }
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
    if (xTaskCreate(mqtt_loop_task, "mqtt_task", 12 * 1024, NULL, 3,
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

bool mqtt_app_is_connected(void)
{
    return s_connected;
}
