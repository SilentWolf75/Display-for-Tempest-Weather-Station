#include "esp_heap_caps.h"
#include "aqi_poll.h"
#include "wx_state.h"
#include "config.h"
#include "net.h"
#include "display.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "aqi";

#define AQI_POLL_INTERVAL_S  (30 * 60)  /* 30 minutes */
#define HTTP_BUF_SIZE        (4 * 1024)

static char  s_geo_zip[10];
static float s_geo_lat;
static float s_geo_lon;
static bool  s_geo_ok;

typedef struct {
    char *buf;
    int   len;
    int   cap;
    bool  overflow;
} http_accum_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_accum_t *acc = (http_accum_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && acc) {
        if (acc->len + evt->data_len < acc->cap) {
            memcpy(acc->buf + acc->len, evt->data, evt->data_len);
            acc->len += evt->data_len;
            acc->buf[acc->len] = '\0';
        } else {
            acc->overflow = true;
        }
    }
    return ESP_OK;
}

static const char *aqi_category_for_val(int val)
{
    if (val <= 50)  return "Good";
    if (val <= 100) return "Moderate";
    if (val <= 150) return "USG";
    if (val <= 200) return "Unhealthy";
    if (val <= 300) return "Very Unhealthy";
    return "Hazardous";
}

static bool resolve_coords(const char *zip, float *lat, float *lon)
{
    wx_state_t s;
    wx_snapshot(&s);
    if (s.station_loc_valid) {
        *lat = s.station_lat;
        *lon = s.station_lon;
        return true;
    }

    if (s_geo_ok && zip && strcmp(s_geo_zip, zip) == 0) {
        *lat = s_geo_lat;
        *lon = s_geo_lon;
        return true;
    }

    char zip_url[128];
    snprintf(zip_url, sizeof(zip_url), "http://api.zippopotam.us/us/%s", zip);

    char *resp_buf = heap_caps_malloc(HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!resp_buf) {
        return false;
    }
    resp_buf[0] = '\0';
    http_accum_t acc = { .buf = resp_buf, .len = 0, .cap = HTTP_BUF_SIZE };

    esp_http_client_config_t http_cfg = {
        .url = zip_url,
        .event_handler = http_event_handler,
        .user_data = &acc,
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(resp_buf);
        return false;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || acc.len == 0 || acc.overflow) {
        free(resp_buf);
        return false;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        return false;
    }

    cJSON *places = cJSON_GetObjectItemCaseSensitive(root, "places");
    if (!cJSON_IsArray(places) || cJSON_GetArraySize(places) == 0) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *p0 = cJSON_GetArrayItem(places, 0);
    cJSON *lat_obj = cJSON_GetObjectItemCaseSensitive(p0, "latitude");
    cJSON *lon_obj = cJSON_GetObjectItemCaseSensitive(p0, "longitude");
    *lat = (lat_obj && lat_obj->valuestring) ? (float)atof(lat_obj->valuestring) : 0.0f;
    *lon = (lon_obj && lon_obj->valuestring) ? (float)atof(lon_obj->valuestring) : 0.0f;
    cJSON_Delete(root);

    if (*lat == 0.0f && *lon == 0.0f) {
        return false;
    }
    strncpy(s_geo_zip, zip ? zip : "", sizeof(s_geo_zip) - 1);
    s_geo_zip[sizeof(s_geo_zip) - 1] = '\0';
    s_geo_lat = *lat;
    s_geo_lon = *lon;
    s_geo_ok = true;
    wx_update_station_location(*lat, *lon);
    return true;
}

static void poll_aqi(void)
{
    cfg_t cfg;
    cfg_get(&cfg);

    if (cfg.alert_zipcode[0] == '\0') {
        return;
    }

    display_https_begin();

    float lat = 0.0f, lon = 0.0f;
    if (!resolve_coords(cfg.alert_zipcode, &lat, &lon)) {
        display_https_end();
        return;
    }

    char aqi_url[256];
    snprintf(aqi_url, sizeof(aqi_url),
             "http://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f&current=us_aqi,pm2_5",
             lat, lon);

    char *resp_buf = heap_caps_malloc(HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!resp_buf) {
        display_https_end();
        return;
    }
    resp_buf[0] = '\0';
    http_accum_t acc = { .buf = resp_buf, .len = 0, .cap = HTTP_BUF_SIZE };

    esp_http_client_config_t aqi_http_cfg = {
        .url = aqi_url,
        .event_handler = http_event_handler,
        .user_data = &acc,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&aqi_http_cfg);
    if (!client) {
        free(resp_buf);
        display_https_end();
        return;
    }
    esp_http_client_set_header(client, "User-Agent", "tempest-display/1.0");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || acc.len == 0 || acc.overflow) {
        free(resp_buf);
        display_https_end();
        return;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        display_https_end();
        return;
    }

    cJSON *curr = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (curr) {
        cJSON *us_aqi_obj = cJSON_GetObjectItemCaseSensitive(curr, "us_aqi");
        cJSON *pm25_obj   = cJSON_GetObjectItemCaseSensitive(curr, "pm2_5");

        int us_aqi = (us_aqi_obj && cJSON_IsNumber(us_aqi_obj)) ? us_aqi_obj->valueint : 0;
        float pm25 = (pm25_obj && cJSON_IsNumber(pm25_obj)) ? (float)pm25_obj->valuedouble : 0.0f;

        if (us_aqi > 0) {
            const char *cat = aqi_category_for_val(us_aqi);
            ESP_LOGI(TAG, "Air Quality Update: AQI %d (%s), PM2.5 %.1f ug/m3", us_aqi, cat, pm25);
            wx_update_aqi(us_aqi, cat, pm25);
        }
    }
    cJSON_Delete(root);
    display_https_end();
}

static void aqi_task(void *arg)
{
    (void)arg;
    while (!net_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    /* Wait 15s after connection to stagger TLS handshakes */
    vTaskDelay(pdMS_TO_TICKS(15000));

    while (1) {
        if (net_is_connected()) {
            poll_aqi();
            vTaskDelay(pdMS_TO_TICKS(200));
            display_recover_after_sdio("aqi");
        }
        vTaskDelay(pdMS_TO_TICKS(AQI_POLL_INTERVAL_S * 1000));
    }
}

esp_err_t aqi_poll_start(void)
{
    xTaskCreate(aqi_task, "aqi_task", 10 * 1024, NULL, 3, NULL);
    return ESP_OK;
}

void aqi_poll_refresh(void)
{
    /* Handled on next cycle or manual trigger */
}
