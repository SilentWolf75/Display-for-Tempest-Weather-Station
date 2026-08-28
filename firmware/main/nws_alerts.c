#include "esp_heap_caps.h"
#include "nws_alerts.h"
#include "audio.h"
#include "config.h"
#include "wx_state.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "nws_alerts";

#define NWS_POLL_INTERVAL_S   180     /* Check every 3 minutes */
#define TASK_STACK_SIZE       8192
#define TASK_PRIO             3
#define HTTP_BUF_SIZE         (32 * 1024)

static nws_alert_t       s_current_alert = {0};
static SemaphoreHandle_t s_alert_lock = NULL;
static char              s_last_sounded_id[64] = {0};

typedef struct {
    char *buf;
    int   len;
    int   cap;
} http_accum_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_accum_t *acc = (http_accum_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && acc) {
        if (acc->len + evt->data_len < acc->cap) {
            memcpy(acc->buf + acc->len, evt->data, evt->data_len);
            acc->len += evt->data_len;
            acc->buf[acc->len] = '\0';
        }
    }
    return ESP_OK;
}

static esp_err_t fetch_coords_by_zip(const char *zip, float *out_lat, float *out_lon)
{
    if (!zip || strlen(zip) < 5) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://api.zippopotam.us/us/%s", zip);

    char *resp_buf = malloc(2048);
    if (!resp_buf) return ESP_ERR_NO_MEM;
    resp_buf[0] = '\0';

    http_accum_t acc = { .buf = resp_buf, .len = 0, .cap = 2048 };

    esp_http_client_config_t http_cfg = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &acc,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(resp_buf);
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200 && acc.len > 0) {
        cJSON *root = cJSON_Parse(resp_buf);
        if (root) {
            cJSON *places = cJSON_GetObjectItemCaseSensitive(root, "places");
            if (cJSON_IsArray(places) && cJSON_GetArraySize(places) > 0) {
                cJSON *p0 = cJSON_GetArrayItem(places, 0);
                cJSON *lat = cJSON_GetObjectItemCaseSensitive(p0, "latitude");
                cJSON *lon = cJSON_GetObjectItemCaseSensitive(p0, "longitude");
                if (cJSON_IsString(lat) && cJSON_IsString(lon)) {
                    *out_lat = (float)atof(lat->valuestring);
                    *out_lon = (float)atof(lon->valuestring);
                    cJSON_Delete(root);
                    free(resp_buf);
                    ESP_LOGI(TAG, "Zip %s resolved to lat=%.4f, lon=%.4f", zip, *out_lat, *out_lon);
                    return ESP_OK;
                }
            }
            cJSON_Delete(root);
        }
    }

    free(resp_buf);
    return ESP_FAIL;
}

static void poll_nws_alerts(void)
{
    cfg_t cfg;
    cfg_get(&cfg);

    if (strlen(cfg.alert_zipcode) < 5) {
        return;
    }

    float lat = 0.0f, lon = 0.0f;
    if (fetch_coords_by_zip(cfg.alert_zipcode, &lat, &lon) != ESP_OK) {
        ESP_LOGW(TAG, "could not resolve zip %s to coordinates", cfg.alert_zipcode);
        return;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.weather.gov/alerts/active?point=%.4f,%.4f", lat, lon);

    char *resp_buf = heap_caps_malloc(HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!resp_buf) return;
    resp_buf[0] = '\0';

    http_accum_t acc = { .buf = resp_buf, .len = 0, .cap = HTTP_BUF_SIZE };

    esp_http_client_config_t http_cfg = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &acc,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(resp_buf);
        return;
    }
    esp_http_client_set_header(client, "User-Agent", "(tempest-weather-display, contact@weatherflow.com)");
    esp_http_client_set_header(client, "Accept", "application/geo+json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || acc.len == 0) {
        ESP_LOGW(TAG, "NWS alerts query returned HTTP %d, err %s", status, esp_err_to_name(err));
        free(resp_buf);
        return;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        ESP_LOGW(TAG, "failed to parse NWS JSON response");
        return;
    }

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (!cJSON_IsArray(features) || cJSON_GetArraySize(features) == 0) {
        /* No active alerts for this zone */
        xSemaphoreTake(s_alert_lock, portMAX_DELAY);
        s_current_alert.active = false;
        xSemaphoreGive(s_alert_lock);
        cJSON_Delete(root);
        return;
    }

    /* Grab the highest priority alert */
    cJSON *feat0 = cJSON_GetArrayItem(features, 0);
    cJSON *props = cJSON_GetObjectItemCaseSensitive(feat0, "properties");
    if (!props) {
        cJSON_Delete(root);
        return;
    }

    cJSON *id_obj       = cJSON_GetObjectItemCaseSensitive(props, "id");
    cJSON *event_obj    = cJSON_GetObjectItemCaseSensitive(props, "event");
    cJSON *headline_obj = cJSON_GetObjectItemCaseSensitive(props, "headline");
    cJSON *severity_obj = cJSON_GetObjectItemCaseSensitive(props, "severity");

    const char *id_str = cJSON_IsString(id_obj) ? id_obj->valuestring : "";
    const char *event_str = cJSON_IsString(event_obj) ? event_obj->valuestring : "Weather Alert";
    const char *headline_str = cJSON_IsString(headline_obj) ? headline_obj->valuestring : "";
    const char *severity_str = cJSON_IsString(severity_obj) ? severity_obj->valuestring : "Severe";

    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    s_current_alert.active = true;
    strncpy(s_current_alert.id, id_str, sizeof(s_current_alert.id) - 1);
    strncpy(s_current_alert.event, event_str, sizeof(s_current_alert.event) - 1);
    strncpy(s_current_alert.headline, headline_str, sizeof(s_current_alert.headline) - 1);
    strncpy(s_current_alert.severity, severity_str, sizeof(s_current_alert.severity) - 1);

    /* Should we trigger the audible emergency siren & voice broadcast? */
    bool is_new_event = (strcmp(s_last_sounded_id, id_str) != 0);
    if (is_new_event && cfg.alert_siren_enabled) {
        strncpy(s_last_sounded_id, id_str, sizeof(s_last_sounded_id) - 1);

        /* Nighttime DND Filter Check: Only life-threatening alerts sound during night hours */
        bool allow_sound = true;
        if (cfg.night_alert_dnd) {
            time_t now_t = time(NULL);
            struct tm lt;
            localtime_r(&now_t, &lt);
            int hour = lt.tm_hour;
            bool is_sleep_time = (hour >= cfg.night_start_hour || hour < cfg.night_end_hour);
            if (is_sleep_time) {
                bool is_life_threatening = (strstr(event_str, "Tornado") != NULL ||
                                            strstr(event_str, "Flood") != NULL ||
                                            strstr(event_str, "Severe Thunderstorm") != NULL);
                if (!is_life_threatening) {
                    allow_sound = false;
                    ESP_LOGI(TAG, "Night DND active: Silenced non-critical alert '%s'", event_str);
                }
            }
        }

        if (allow_sound) {
            ESP_LOGW(TAG, "EMERGENCY NOAA WEATHER ALERT: %s - Playing 1050 Hz Siren & Voice Broadcast!", event_str);

            char spoken_msg[384];
            if (headline_str[0] != '\0') {
                snprintf(spoken_msg, sizeof(spoken_msg), "The National Weather Service has issued a %s. %s",
                         event_str, headline_str);
            } else {
                snprintf(spoken_msg, sizeof(spoken_msg), "The National Weather Service has issued a %s for your area.",
                         event_str);
            }

            audio_play_full_noaa_broadcast(spoken_msg);
        }
    }
    xSemaphoreGive(s_alert_lock);

    cJSON_Delete(root);
}

static void nws_task(void *arg)
{
    (void)arg;

    /* Wait for Wi-Fi and clock sync */
    while (!net_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        if (net_is_connected()) {
            poll_nws_alerts();
        }
        vTaskDelay(pdMS_TO_TICKS(NWS_POLL_INTERVAL_S * 1000));
    }
}

esp_err_t nws_alerts_start(void)
{
    s_alert_lock = xSemaphoreCreateMutex();
    if (!s_alert_lock) return ESP_ERR_NO_MEM;

    if (xTaskCreate(nws_task, "nws_alerts", TASK_STACK_SIZE, NULL, TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "NWS Weather Alerts background monitor online");
    return ESP_OK;
}

void nws_alerts_refresh(void)
{
    poll_nws_alerts();
}

bool nws_alerts_get_active(nws_alert_t *out_alert)
{
    if (!s_alert_lock || !out_alert) return false;
    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    *out_alert = s_current_alert;
    bool act = s_current_alert.active;
    xSemaphoreGive(s_alert_lock);
    return act;
}
