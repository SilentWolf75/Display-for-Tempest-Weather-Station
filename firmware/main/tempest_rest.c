#include "tempest_rest.h"
#include "wx_state.h"
#include "history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "secrets.h not found -- copy secrets.h.example to secrets.h and add \
your Tempest personal access token. The forecast poll will be skipped."
#define TEMPEST_API_TOKEN ""
#endif

static const char *TAG = "tempest_rest";

#define RESP_MAX_BYTES   (24 * 1024)
#define TASK_STACK       8192
#define TASK_PRIO        4
#define BACKOFF_S        1800

/* Backfill. A 6-hour window is ~360 observations at ~90 bytes of JSON
 * each, so ~35 KB -- four windows cover 24 h without ever holding a whole
 * day in memory at once. */
#define BACKFILL_WINDOWS      4
#define BACKFILL_WINDOW_S     (6 * 3600)
#define BACKFILL_BUF_BYTES    (96 * 1024)
#define STATIONS_BUF_BYTES    (16 * 1024)

#define STATIONS_URL_FMT \
    "https://swd.weatherflow.com/swd/rest/stations/%d?token=%s"

#define OBS_URL_FMT \
    "https://swd.weatherflow.com/swd/rest/observations/" \
    "?device_id=%d&type=obs_st&time_start=%lld&time_end=%lld&token=%s"


/* Metric on the wire so wx_state stays SI; the UI converts for display. */
#define FORECAST_URL_FMT \
    "https://swd.weatherflow.com/swd/rest/better_forecast" \
    "?station_id=%d&units_temp=c&units_wind=mps&units_pressure=mb" \
    "&units_precip=mm&units_distance=km&token=%s"

typedef struct {
    char *buf;
    int   len;
    int   cap;
} resp_accum_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    resp_accum_t *acc = (resp_accum_t *)evt->user_data;

    if (evt->event_id != HTTP_EVENT_ON_DATA || !acc) {
        return ESP_OK;
    }
    /* Chunked responses arrive in pieces; accumulate before parsing. */
    if (acc->len + evt->data_len >= acc->cap) {
        ESP_LOGW(TAG, "response exceeds %d bytes, truncating", acc->cap);
        return ESP_OK;
    }
    memcpy(acc->buf + acc->len, evt->data, evt->data_len);
    acc->len += evt->data_len;
    acc->buf[acc->len] = '\0';
    return ESP_OK;
}

static void copy_str(char *dst, size_t dstlen, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(dst, v->valuestring, dstlen - 1);
        dst[dstlen - 1] = '\0';
    }
}

/* Positional accessor for the obs arrays. Mirrors arr_num() in
 * tempest_udp.c -- both decode the same 18-field obs_st layout. */
static double num_at(const cJSON *arr, int idx)
{
    const cJSON *v = cJSON_GetArrayItem(arr, idx);
    return cJSON_IsNumber(v) ? v->valuedouble : 0.0;
}

static double num_or(const cJSON *obj, const char *key, double fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static esp_err_t parse_forecast(const char *json, int len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGE(TAG, "forecast JSON did not parse");
        return ESP_FAIL;
    }

    wx_state_t p = {0};

    const cJSON *cc = cJSON_GetObjectItemCaseSensitive(root, "current_conditions");
    if (cJSON_IsObject(cc)) {
        copy_str(p.current_conditions, sizeof(p.current_conditions), cc, "conditions");
        copy_str(p.current_icon, sizeof(p.current_icon), cc, "icon");
        /* Deliberately NOT deriving a pressure trend here. sea_level_pressure
         * minus station_pressure is a fixed altitude offset, not a tendency --
         * it was a bug. The real 3-hour trend is accumulated from the local
         * UDP feed in wx_state.c, which also means it survives an internet
         * outage like the rest of the outdoor half. */
    }

    const cJSON *fc = cJSON_GetObjectItemCaseSensitive(root, "forecast");
    const cJSON *daily = cJSON_IsObject(fc)
        ? cJSON_GetObjectItemCaseSensitive(fc, "daily") : NULL;

    if (cJSON_IsArray(daily)) {
        int i = 0;
        const cJSON *day = NULL;
        cJSON_ArrayForEach(day, daily) {
            if (i >= WX_FORECAST_DAYS) {
                break;
            }
            wx_forecast_day_t *d = &p.forecast[i];
            d->day_start_local    = (int64_t)num_or(day, "day_start_local", 0);
            d->air_temp_high_c    = (float)num_or(day, "air_temp_high", 0);
            d->air_temp_low_c     = (float)num_or(day, "air_temp_low", 0);
            d->precip_probability = (int)num_or(day, "precip_probability", 0);
            copy_str(d->conditions, sizeof(d->conditions), day, "conditions");
            copy_str(d->icon, sizeof(d->icon), day, "icon");

            /* Today's entry carries sunrise/sunset. */
            if (i == 0) {
                p.sunrise_epoch = (int64_t)num_or(day, "sunrise", 0);
                p.sunset_epoch  = (int64_t)num_or(day, "sunset", 0);
            }
            i++;
        }
        p.forecast_days = i;
    }

    cJSON_Delete(root);

    if (p.forecast_days == 0) {
        ESP_LOGW(TAG, "forecast response had no daily entries");
        return ESP_FAIL;
    }

    wx_update_forecast(&p);
    ESP_LOGI(TAG, "forecast updated: %d days, now '%s'",
             p.forecast_days, p.current_conditions);
    return ESP_OK;
}

esp_err_t tempest_rest_fetch_now(void)
{
    if (TEMPEST_API_TOKEN[0] == '\0') {
        ESP_LOGW(TAG, "no API token configured, skipping forecast");
        return ESP_ERR_INVALID_STATE;
    }

    char *url = malloc(512);
    resp_accum_t acc = { .buf = malloc(RESP_MAX_BYTES), .len = 0,
                         .cap = RESP_MAX_BYTES };
    if (!url || !acc.buf) {
        free(url);
        free(acc.buf);
        return ESP_ERR_NO_MEM;
    }
    acc.buf[0] = '\0';
    snprintf(url, 512, FORECAST_URL_FMT,
             CONFIG_TEMPEST_STATION_ID, TEMPEST_API_TOKEN);

    esp_http_client_config_t cfg = {
        .url             = url,
        .event_handler   = http_event,
        .user_data       = &acc,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* WeatherFlow rotates
                                                       * certs; do not pin */
        .timeout_ms      = 15000,
        .buffer_size     = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        err = parse_forecast(acc.buf, acc.len);
    } else {
        ESP_LOGE(TAG, "forecast fetch failed: %s, HTTP %d",
                 esp_err_to_name(err), status);
        err = (err == ESP_OK) ? ESP_FAIL : err;
    }

    free(url);
    free(acc.buf);
    return err;
}

/* --------------------------------------------------------------------------
 * History backfill
 *
 * Without this the trend graphs start empty and take a full day to become
 * useful. One shot at boot; after that history.c is maintained purely from the
 * local UDP feed, so the graphs survive an internet outage like everything
 * else on the outdoor half.
 * -------------------------------------------------------------------------- */

static int s_device_id;     /* 0 until discovered */

/* Generic GET into a caller-owned buffer. Returns byte count, or negative. */
static int rest_get(const char *url, char *buf, int cap)
{
    resp_accum_t acc = { .buf = buf, .len = 0, .cap = cap };
    buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url               = url,
        .event_handler     = http_event,
        .user_data         = &acc,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 20000,
        .buffer_size       = 4096,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "GET failed: %s, HTTP %d", esp_err_to_name(err), status);
        return -1;
    }
    return acc.len;
}

/* The observations endpoint is keyed on device_id, not station_id, so the
 * Tempest sensor has to be located first. The device list also contains the
 * hub (device_type "HB"), which is not what we want. */
static esp_err_t discover_device_id(void)
{
    if (s_device_id) {
        return ESP_OK;
    }

    char *url = malloc(384);
    char *buf = heap_caps_malloc(STATIONS_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (!url || !buf) {
        free(url);
        free(buf);
        return ESP_ERR_NO_MEM;
    }
    snprintf(url, 384, STATIONS_URL_FMT,
             CONFIG_TEMPEST_STATION_ID, TEMPEST_API_TOKEN);

    int len = rest_get(url, buf, STATIONS_BUF_BYTES);
    free(url);
    if (len <= 0) {
        free(buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_ParseWithLength(buf, len);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "stations response did not parse");
        return ESP_FAIL;
    }

    esp_err_t rc = ESP_FAIL;
    const cJSON *stations = cJSON_GetObjectItemCaseSensitive(root, "stations");
    const cJSON *station  = cJSON_IsArray(stations)
                          ? cJSON_GetArrayItem(stations, 0) : NULL;
    const cJSON *devices  = station
        ? cJSON_GetObjectItemCaseSensitive(station, "devices") : NULL;

    if (cJSON_IsArray(devices)) {
        const cJSON *dev = NULL;
        cJSON_ArrayForEach(dev, devices) {
            const cJSON *type = cJSON_GetObjectItemCaseSensitive(
                dev, "device_type");
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(
                dev, "device_id");
            if (cJSON_IsString(type) && type->valuestring &&
                strcmp(type->valuestring, "ST") == 0 && cJSON_IsNumber(id)) {
                s_device_id = (int)id->valuedouble;
                ESP_LOGI(TAG, "Tempest device_id %d", s_device_id);
                rc = ESP_OK;
                break;
            }
        }
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "no device of type ST on station %d",
                 CONFIG_TEMPEST_STATION_ID);
    }
    cJSON_Delete(root);
    return rc;
}

/* Feeds one window of observations into history. These rows use the same
 * 18-field layout as the UDP feed, which is why the indices match
 * tempest_udp.c exactly -- if WeatherFlow ever diverges the two, both decoders
 * need changing together. */
static int seed_window(const char *json, int len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return -1;
    }
    const cJSON *obs = cJSON_GetObjectItemCaseSensitive(root, "obs");
    if (!cJSON_IsArray(obs)) {
        /* An empty window is normal: the station may have been offline, or
         * simply not have existed, that long ago. */
        cJSON_Delete(root);
        return 0;
    }

    int n = 0;
    const cJSON *row = NULL;
    cJSON_ArrayForEach(row, obs) {
        if (!cJSON_IsArray(row) || cJSON_GetArraySize(row) < 18) {
            continue;
        }
        history_add((int64_t)num_at(row, 0),
                    (float)num_at(row, 7),     /* air temp  */
                    (float)num_at(row, 8),     /* humidity  */
                    (float)num_at(row, 6),     /* pressure  */
                    (float)num_at(row, 2),     /* wind avg  */
                    (float)num_at(row, 3),     /* wind gust */
                    (float)num_at(row, 12),    /* rain      */
                    (float)num_at(row, 10));   /* uv        */
        n++;
    }
    cJSON_Delete(root);
    return n;
}

esp_err_t tempest_rest_backfill_history(void)
{
    if (TEMPEST_API_TOKEN[0] == '\0') {
        ESP_LOGW(TAG, "no API token; graphs will fill from live data only");
        return ESP_ERR_INVALID_STATE;
    }
    if (discover_device_id() != ESP_OK) {
        return ESP_FAIL;
    }

    char *url = malloc(512);
    char *buf = heap_caps_malloc(BACKFILL_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (!url || !buf) {
        free(url);
        free(buf);
        return ESP_ERR_NO_MEM;
    }

    int64_t now = (int64_t)time(NULL);
    int total = 0;

    /* Oldest window first: history.c only tracks a head bucket and rejects
     * anything older than it, so samples must arrive in ascending time order. */
    for (int w = BACKFILL_WINDOWS; w >= 1; w--) {
        int64_t start = now - (int64_t)w * BACKFILL_WINDOW_S;
        int64_t end   = start + BACKFILL_WINDOW_S;

        snprintf(url, 512, OBS_URL_FMT, s_device_id,
                 (long long)start, (long long)end, TEMPEST_API_TOKEN);

        int len = rest_get(url, buf, BACKFILL_BUF_BYTES);
        if (len <= 0) {
            ESP_LOGW(TAG, "backfill window %d failed; continuing", w);
            continue;
        }
        if (len >= BACKFILL_BUF_BYTES - 1) {
            ESP_LOGW(TAG, "window %d filled the %d KB buffer and was truncated",
                     w, BACKFILL_BUF_BYTES / 1024);
        }
        int n = seed_window(buf, len);
        if (n > 0) {
            total += n;
        }
    }

    free(url);
    free(buf);

    if (total == 0) {
        ESP_LOGW(TAG, "backfill returned no observations");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "backfilled %d observations into the trend graphs", total);
    return ESP_OK;
}

static void rest_task(void *arg)
{
    (void)arg;

    /* Once, before the forecast loop: populate the graphs so they are
     * useful immediately instead of after a day of collecting. Failing is
     * fine -- they just start empty, which was the previous behaviour. */
    tempest_rest_backfill_history();

    while (1) {
        esp_err_t err = tempest_rest_fetch_now();
        int delay_s = (err == ESP_OK)
                    ? CONFIG_TEMPEST_FORECAST_INTERVAL_S
                    : BACKOFF_S;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "backing off %d s before retrying", delay_s);
        }
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
    }
}

esp_err_t tempest_rest_start(void)
{
    if (xTaskCreate(rest_task, "tempest_rest", TASK_STACK, NULL,
                    TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
