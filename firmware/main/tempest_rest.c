#include "config.h"
#include "net.h"
#include "tempest_rest.h"
#include "wx_state.h"
#include "history.h"
#include "graphs.h"
#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include <time.h>
#include <math.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include "credentials_config.h"

static const char *TAG = "tempest_rest";

#define RESP_MAX_BYTES   (192 * 1024)
#define TASK_STACK       12288
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
    bool  overflow;
} resp_accum_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    resp_accum_t *acc = (resp_accum_t *)evt->user_data;

    if (evt->event_id != HTTP_EVENT_ON_DATA || !acc) {
        return ESP_OK;
    }
    /* Chunked responses arrive in pieces; accumulate before parsing. */
    if (acc->len + evt->data_len >= acc->cap) {
        acc->overflow = true;
        ESP_LOGW(TAG, "response exceeds %d bytes; dropping fetch", acc->cap);
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
    return cJSON_IsNumber(v) ? v->valuedouble : NAN;
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

    float station_rain_today = NAN;
    float station_rain_yday = NAN;
    int64_t station_obs_epoch = 0;

    const cJSON *cc = cJSON_GetObjectItemCaseSensitive(root, "current_conditions");
    if (cJSON_IsObject(cc)) {
        copy_str(p.current_conditions, sizeof(p.current_conditions), cc, "conditions");
        copy_str(p.current_icon, sizeof(p.current_icon), cc, "icon");
        /* Official local-day rain -- the number the Tempest app shows.
         * UDP only carries the last minute, so missed packets cannot be
         * reconstructed from the LAN feed. */
        station_rain_today = (float)num_or(cc, "precip_accum_local_day", NAN);
        station_rain_yday = (float)num_or(cc, "precip_accum_local_yesterday", NAN);
        station_obs_epoch = (int64_t)num_or(cc, "time", 0);
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

    if (p.current_conditions[0] == '\0' && p.forecast[0].conditions[0]) {
        snprintf(p.current_conditions, sizeof(p.current_conditions), "%.*s", (int)sizeof(p.current_conditions) - 1, p.forecast[0].conditions);
    }
    if (p.current_icon[0] == '\0' && p.forecast[0].icon[0]) {
        snprintf(p.current_icon, sizeof(p.current_icon), "%.*s", (int)sizeof(p.current_icon) - 1, p.forecast[0].icon);
    }

    wx_apply_station_rain(station_rain_today, station_rain_yday,
                          station_obs_epoch);
    wx_update_forecast(&p);
    ESP_LOGI(TAG, "forecast updated: %d days, now '%s'",
             p.forecast_days, p.current_conditions);
    return ESP_OK;
}


static const char *wmo_to_conditions(int code)
{
    switch (code) {
    case 0: return "Clear";
    case 1: return "Mainly Clear";
    case 2: return "Partly Cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 61: case 63: case 65: return "Rain";
    case 71: case 73: case 75: return "Snow";
    case 77: return "Snow Grains";
    case 80: case 81: case 82: return "Rain Showers";
    case 85: case 86: return "Snow Showers";
    case 95: case 96: case 99: return "Thunderstorm";
    default: return "Partly Cloudy";
    }
}

static const char *wmo_to_icon(int code)
{
    switch (code) {
    case 0: return "clear-day";
    case 1: case 2: return "partly-cloudy-day";
    case 3: return "cloudy";
    case 45: case 48: return "foggy";
    case 51: case 53: case 55:
    case 56: case 57:
    case 61: case 63: case 65:
    case 66: case 67:
    case 80: case 81: case 82: return "rainy";
    case 71: case 73: case 75:
    case 77: case 85: case 86: return "snow";
    case 95: case 96: case 99: return "possibly-thunderstorm-day";
    default: return "partly-cloudy-day";
    }
}

static time_t parse_iso_time(const char *str)
{
    if (!str || strlen(str) < 10) return 0;
    struct tm tm = {0};
    int yr = 0, mon = 0, day = 0, hr = 0, min = 0;
    if (sscanf(str, "%d-%d-%dT%d:%d", &yr, &mon, &day, &hr, &min) >= 3) {
        tm.tm_year = yr - 1900;
        tm.tm_mon  = mon - 1;
        tm.tm_mday = day;
        tm.tm_hour = hr;
        tm.tm_min  = min;
        return mktime(&tm);
    }
    return 0;
}

static esp_err_t geocode_zip(const char *zipcode, float *out_lat, float *out_lon);
static esp_err_t fetch_open_meteo_extras(const char *zipcode, float lat, float lon);

static esp_err_t fetch_open_meteo_forecast(const char *zipcode)
{
    if (!zipcode || strlen(zipcode) < 5) return ESP_FAIL;

    float lat = 0.0f, lon = 0.0f;
    if (geocode_zip(zipcode, &lat, &lon) != ESP_OK) {
        return ESP_FAIL;
    }
    wx_update_station_location(lat, lon);

    char *resp_buf = heap_caps_malloc(RESP_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (!resp_buf) {
        return ESP_ERR_NO_MEM;
    }
    resp_buf[0] = '\0';
    resp_accum_t acc = { .buf = resp_buf, .len = 0, .cap = RESP_MAX_BYTES };

    /* 2. Query 7-Day Forecast from Open-Meteo */
    char fc_url[256];
    snprintf(fc_url, sizeof(fc_url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
             "precipitation_probability_max,sunrise,sunset&timezone=auto",
             lat, lon);

    acc.len = 0;
    resp_buf[0] = '\0';

    esp_http_client_config_t fc_cfg = {
        .url = fc_url,
        .event_handler = http_event,
        .user_data = &acc,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&fc_cfg);
    if (!client) {
        free(resp_buf);
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || acc.len == 0 || acc.overflow) {
        ESP_LOGE(TAG, "Open-Meteo forecast HTTP failed: %d, err %s%s",
                 status, esp_err_to_name(err),
                 acc.overflow ? " (truncated)" : "");
        free(resp_buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        ESP_LOGE(TAG, "failed to parse Open-Meteo JSON");
        return ESP_FAIL;
    }

    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(daily)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *time_arr = cJSON_GetObjectItemCaseSensitive(daily, "time");
    cJSON *tmax_arr = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max");
    cJSON *tmin_arr = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min");
    cJSON *code_arr = cJSON_GetObjectItemCaseSensitive(daily, "weather_code");
    cJSON *pop_arr  = cJSON_GetObjectItemCaseSensitive(daily, "precipitation_probability_max");
    cJSON *sr_arr   = cJSON_GetObjectItemCaseSensitive(daily, "sunrise");
    cJSON *ss_arr   = cJSON_GetObjectItemCaseSensitive(daily, "sunset");

    if (!cJSON_IsArray(time_arr) || !cJSON_IsArray(tmax_arr) || !cJSON_IsArray(tmin_arr)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    wx_state_t p = {0};
    int count = cJSON_GetArraySize(time_arr);
    if (count > WX_FORECAST_DAYS) count = WX_FORECAST_DAYS;

    for (int i = 0; i < count; i++) {
        wx_forecast_day_t *d = &p.forecast[i];
        cJSON *t_item = cJSON_GetArrayItem(time_arr, i);
        cJSON *hi_item = cJSON_GetArrayItem(tmax_arr, i);
        cJSON *lo_item = cJSON_GetArrayItem(tmin_arr, i);
        cJSON *cd_item = code_arr ? cJSON_GetArrayItem(code_arr, i) : NULL;
        cJSON *pop_item = pop_arr ? cJSON_GetArrayItem(pop_arr, i) : NULL;

        if (t_item && cJSON_IsString(t_item)) {
            d->day_start_local = parse_iso_time(t_item->valuestring);
        }
        if (hi_item && cJSON_IsNumber(hi_item)) {
            d->air_temp_high_c = (float)hi_item->valuedouble;
        }
        if (lo_item && cJSON_IsNumber(lo_item)) {
            d->air_temp_low_c = (float)lo_item->valuedouble;
        }
        if (pop_item && cJSON_IsNumber(pop_item)) {
            d->precip_probability = (int)pop_item->valuedouble;
        }
        int code = (cd_item && cJSON_IsNumber(cd_item)) ? cd_item->valueint : 0;
        strncpy(d->conditions, wmo_to_conditions(code), sizeof(d->conditions) - 1);
        strncpy(d->icon, wmo_to_icon(code), sizeof(d->icon) - 1);

        if (i == 0) {
            strncpy(p.current_conditions, d->conditions, sizeof(p.current_conditions) - 1);
            strncpy(p.current_icon, d->icon, sizeof(p.current_icon) - 1);
            cJSON *sr_item = sr_arr ? cJSON_GetArrayItem(sr_arr, 0) : NULL;
            cJSON *ss_item = ss_arr ? cJSON_GetArrayItem(ss_arr, 0) : NULL;
            if (sr_item && cJSON_IsString(sr_item)) p.sunrise_epoch = parse_iso_time(sr_item->valuestring);
            if (ss_item && cJSON_IsString(ss_item)) p.sunset_epoch  = parse_iso_time(ss_item->valuestring);
        }
    }

    p.forecast_days = count;
    cJSON_Delete(root);

    wx_update_forecast(&p);
    ESP_LOGI(TAG, "Open-Meteo forecast updated: %d days (Today: %.1fC / %.1fC, %s)",
             p.forecast_days, p.forecast[0].air_temp_high_c, p.forecast[0].air_temp_low_c, p.current_conditions);
    fetch_open_meteo_extras(zipcode, lat, lon);
    return ESP_OK;
}

static esp_err_t geocode_zip(const char *zipcode, float *out_lat, float *out_lon)
{
    if (!zipcode || strspn(zipcode, "0123456789") < 5 || !out_lat || !out_lon) {
        return ESP_FAIL;
    }

    char zip_url[128];
    snprintf(zip_url, sizeof(zip_url), "http://api.zippopotam.us/us/%.5s", zipcode);

    char *resp_buf = malloc(4096);
    if (!resp_buf) {
        return ESP_ERR_NO_MEM;
    }
    resp_buf[0] = '\0';

    resp_accum_t acc = { .buf = resp_buf, .len = 0, .cap = 4096 };
    esp_http_client_config_t http_cfg = {
        .url = zip_url,
        .event_handler = http_event,
        .user_data = &acc,
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(resp_buf);
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    *out_lat = 0.0f;
    *out_lon = 0.0f;
    if (err == ESP_OK && status == 200 && acc.len > 0) {
        cJSON *root = cJSON_Parse(resp_buf);
        if (root) {
            cJSON *places = cJSON_GetObjectItemCaseSensitive(root, "places");
            if (cJSON_IsArray(places) && cJSON_GetArraySize(places) > 0) {
                cJSON *p0 = cJSON_GetArrayItem(places, 0);
                cJSON *lat_obj = cJSON_GetObjectItemCaseSensitive(p0, "latitude");
                cJSON *lon_obj = cJSON_GetObjectItemCaseSensitive(p0, "longitude");
                if (lat_obj && lon_obj) {
                    *out_lat = (float)atof(lat_obj->valuestring);
                    *out_lon = (float)atof(lon_obj->valuestring);
                }
            }
            cJSON_Delete(root);
        }
    }
    free(resp_buf);
    return (*out_lat != 0.0f || *out_lon != 0.0f) ? ESP_OK : ESP_FAIL;
}

static esp_err_t fetch_open_meteo_extras(const char *zipcode, float lat, float lon)
{
    if (lat == 0.0f && lon == 0.0f) {
        if (geocode_zip(zipcode, &lat, &lon) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    char *url = malloc(512);
    char *resp_buf = heap_caps_malloc(96 * 1024, MALLOC_CAP_SPIRAM);
    if (!url || !resp_buf) {
        free(url);
        free(resp_buf);
        return ESP_ERR_NO_MEM;
    }

    snprintf(url, 512,
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&timezone=auto&forecast_hours=24"
             "&hourly=temperature_2m,precipitation_probability"
             "&daily=moonrise,moonset"
             "&forecast_days=1",
             lat, lon);

    resp_accum_t acc = { .buf = resp_buf, .len = 0, .cap = 96 * 1024 };
    resp_buf[0] = '\0';

    esp_http_client_config_t fc_cfg = {
        .url = url,
        .event_handler = http_event,
        .user_data = &acc,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&fc_cfg);
    if (!client) {
        free(url);
        free(resp_buf);
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(url);

    if (err != ESP_OK || status != 200 || acc.len == 0) {
        free(resp_buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_ParseWithLength(resp_buf, acc.len);
    free(resp_buf);
    if (!root) {
        return ESP_FAIL;
    }

    /* Hourly timeline */
    cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    if (cJSON_IsObject(hourly)) {
        cJSON *times = cJSON_GetObjectItemCaseSensitive(hourly, "time");
        cJSON *temps = cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m");
        cJSON *pops  = cJSON_GetObjectItemCaseSensitive(hourly, "precipitation_probability");
        if (cJSON_IsArray(times) && cJSON_IsArray(temps)) {
            wx_hourly_slot_t slots[WX_HOURLY_SLOTS];
            int n = cJSON_GetArraySize(times);
            if (n > WX_HOURLY_SLOTS) {
                n = WX_HOURLY_SLOTS;
            }
            int64_t now = (int64_t)time(NULL);
            int wrote = 0;
            for (int i = 0; i < n; i++) {
                cJSON *t_item = cJSON_GetArrayItem(times, i);
                if (!t_item || !cJSON_IsString(t_item)) {
                    continue;
                }
                int64_t ts = (int64_t)parse_iso_time(t_item->valuestring);
                if (ts < now - 3600) {
                    continue;
                }
                slots[wrote].hour_epoch = ts;
                cJSON *temp_item = cJSON_GetArrayItem(temps, i);
                slots[wrote].temp_c = (temp_item && cJSON_IsNumber(temp_item))
                                    ? (float)temp_item->valuedouble : 0.0f;
                cJSON *pop_item = pops ? cJSON_GetArrayItem(pops, i) : NULL;
                slots[wrote].precip_probability = (pop_item && cJSON_IsNumber(pop_item))
                                                  ? pop_item->valueint : 0;
                wrote++;
                if (wrote >= WX_HOURLY_SLOTS) {
                    break;
                }
            }
            if (wrote > 0) {
                wx_update_hourly(slots, wrote);
            }
        }
    }

    /* Rain totals + moon schedule from daily block */
    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (cJSON_IsObject(daily)) {
        cJSON *mr_arr = cJSON_GetObjectItemCaseSensitive(daily, "moonrise");
        cJSON *ms_arr = cJSON_GetObjectItemCaseSensitive(daily, "moonset");

        if (cJSON_IsArray(mr_arr) && cJSON_GetArraySize(mr_arr) > 0) {
            /* Open-Meteo daily arrays: index 0 is today, index 1 is tomorrow */
            cJSON *mr0 = cJSON_GetArrayItem(mr_arr, 0);
            cJSON *ms0 = ms_arr ? cJSON_GetArrayItem(ms_arr, 0) : NULL;
            int64_t rise = (mr0 && cJSON_IsString(mr0)) ? (int64_t)parse_iso_time(mr0->valuestring) : 0;
            int64_t set  = (ms0 && cJSON_IsString(ms0)) ? (int64_t)parse_iso_time(ms0->valuestring) : 0;
            wx_update_moon_schedule(rise, set);
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Open-Meteo extras updated (hourly + rain + moon schedule)");
    return ESP_OK;
}

esp_err_t tempest_rest_fetch_now(void)
{
    display_https_begin();
    cfg_t c;
    cfg_get(&c);

    if (TEMPEST_API_TOKEN[0] == '\0') {
        /* No Tempest API key configured -> Use Open-Meteo 7-Day Forecast Engine */
        esp_err_t om = fetch_open_meteo_forecast(c.alert_zipcode);
        display_https_end();
        return om;
    }

    char *url = malloc(512);
    resp_accum_t acc = { .buf = heap_caps_malloc(RESP_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                         .len = 0, .cap = RESP_MAX_BYTES };
    if (!url || !acc.buf) {
        free(url);
        free(acc.buf);
        display_https_end();
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

    if (acc.overflow) {
        ESP_LOGE(TAG, "forecast response truncated; ignoring");
        err = ESP_FAIL;
    } else if (err == ESP_OK && status == 200) {
        err = parse_forecast(acc.buf, acc.len);
    } else {
        ESP_LOGE(TAG, "forecast fetch failed: %s, HTTP %d",
                 esp_err_to_name(err), status);
        err = (err == ESP_OK) ? ESP_FAIL : err;
    }

    free(url);
    free(acc.buf);

    if (err == ESP_OK) {
        fetch_open_meteo_extras(c.alert_zipcode, 0.0f, 0.0f);
    }
    display_https_end();
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
static bool s_history_backfilled;

static bool clock_is_plausible(void)
{
    return time(NULL) > 1700000000LL;
}

static bool wait_for_plausible_clock(int max_wait_s)
{
    for (int i = 0; i < max_wait_s; i++) {
        if (clock_is_plausible()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return clock_is_plausible();
}

static void try_history_backfill(void)
{
    if (s_history_backfilled || !clock_is_plausible()) {
        return;
    }
    if (tempest_rest_backfill_history() == ESP_OK) {
        s_history_backfilled = true;
    }
}

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

    if (err != ESP_OK || status != 200 || acc.overflow) {
        ESP_LOGE(TAG, "GET failed: %s, HTTP %d%s",
                 esp_err_to_name(err), status,
                 acc.overflow ? " (truncated)" : "");
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
        double epoch = num_at(row, 0);
        if (!isfinite(epoch)) continue;
        wx_state_t check = {
            .obs_epoch = (int64_t)epoch, .air_temp_c = num_at(row, 7),
            .humidity_pct = num_at(row, 8), .pressure_mb = num_at(row, 6),
            .wind_avg_ms = num_at(row, 2), .wind_gust_ms = num_at(row, 3),
            .rain_last_min_mm = num_at(row, 12),
        };
        if (!wx_obs_values_valid(&check)) continue;
        history_add_backfill((int64_t)num_at(row, 0),
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
    display_https_begin();
    if (discover_device_id() != ESP_OK) {
        display_https_end();
        return ESP_FAIL;
    }

    char *url = malloc(512);
    char *buf = heap_caps_malloc(BACKFILL_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (!url || !buf) {
        free(url);
        free(buf);
        display_https_end();
        return ESP_ERR_NO_MEM;
    }

    if (history_begin_backfill() != ESP_OK) {
        free(url);
        free(buf);
        display_https_end();
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

    history_end_backfill();

    if (total == 0) {
        ESP_LOGW(TAG, "backfill returned no observations");
        display_https_end();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "backfilled %d observations into the trend graphs", total);
    graphs_request_redraw();
    display_https_end();
    return ESP_OK;
}

static void rest_task(void *arg)
{
    (void)arg;

    while (!net_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; attempt++) {
        err = tempest_rest_fetch_now();
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "forecast fetch failed, retry %d/3 in 15s", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(15000));
    }

    /* History backfill keys off wall-clock windows. SNTP often loses the
     * race to the first UDP obs_st on this board, so keep trying until the
     * clock is trustworthy or we give up for this boot cycle. */
    if (wait_for_plausible_clock(45)) {
        /* Forecast often wins the race against SNTP. Re-apply the cached
         * station rain now that local midnight is trustworthy. */
        wx_apply_station_rain(NAN, NAN, 0);
        try_history_backfill();
    } else {
        ESP_LOGW(TAG, "clock unsynced; history backfill deferred");
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    display_recover_after_sdio("forecast-boot");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_TEMPEST_FORECAST_INTERVAL_S * 1000));
        ESP_LOGI(TAG, "forecast poll begin (uptime %lld s, heap %u/%u)",
                 (long long)(esp_timer_get_time() / 1000000LL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        if (tempest_rest_fetch_now() == ESP_OK) {
            try_history_backfill();
        }
        /* Large HTTPS rides the C6 SDIO link. That is the same bus that
         * blanks the panel at boot; settle, then ask LVGL to wake MIPI. */
        vTaskDelay(pdMS_TO_TICKS(300));
        display_recover_after_sdio("forecast");
        ESP_LOGI(TAG, "forecast poll end (heap %u/%u)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

void tempest_rest_on_clock_sync(void)
{
    /* The REST task waits for a plausible clock and owns backfill. */
}

esp_err_t tempest_rest_start(void)
{
    if (xTaskCreate(rest_task, "tempest_rest", TASK_STACK, NULL,
                    TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

int tempest_rest_device_id(void)
{
    return s_device_id;
}

bool tempest_rest_has_token(void)
{
    return TEMPEST_API_TOKEN[0] != '\0';
}

esp_err_t tempest_rest_ensure_device_id(void)
{
    if (TEMPEST_API_TOKEN[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    return discover_device_id();
}
