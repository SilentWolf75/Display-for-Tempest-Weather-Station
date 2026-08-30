#include "esp_heap_caps.h"
#include "nws_alerts.h"
#include "audio.h"
#include "config.h"
#include "wx_state.h"
#include "net.h"
#include "display.h"

#include <stdbool.h>
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

static int64_t nws_parse_iso(const char *iso)
{
    if (!iso || !iso[0] || iso[0] == 'n') {
        return 0;
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) {
        return 0;
    }
    struct tm t = {
        .tm_year = y - 1900,
        .tm_mon = mo - 1,
        .tm_mday = d,
        .tm_hour = h,
        .tm_min = mi,
        .tm_sec = sec,
        .tm_isdst = -1,
    };
    time_t loc = mktime(&t);
    if (loc == (time_t)-1) {
        return 0;
    }
    return (int64_t)loc;
}

static void copy_flat(char *dst, size_t dst_n, const char *src, size_t max_copy)
{
    if (!dst || dst_n == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dst_n && j < max_copy; i++) {
        char c = src[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == '*') {
            c = ' ';
        }
        if (c == ' ' && (j == 0 || dst[j - 1] == ' ')) {
            continue;
        }
        dst[j++] = c;
    }
    dst[j] = '\0';
    /* Prefer a sentence boundary if we hit the cap. */
    if (j >= max_copy && j > 40) {
        for (size_t k = j; k > 40; k--) {
            if (dst[k] == '.' || dst[k] == '!') {
                dst[k + 1] = '\0';
                break;
            }
        }
    }
}

static void fmt_clock(const struct tm *lt, char *buf, size_t n)
{
    int hour = lt->tm_hour % 12;
    if (hour == 0) {
        hour = 12;
    }
    const char *ampm = lt->tm_hour >= 12 ? "PM" : "AM";
    if (lt->tm_min == 0) {
        snprintf(buf, n, "%d %s", hour, ampm);
    } else {
        snprintf(buf, n, "%d:%02d %s", hour, lt->tm_min, ampm);
    }
}

void nws_format_until(int64_t ends_epoch, char *buf, size_t n)
{
    if (!buf || n == 0) {
        return;
    }
    buf[0] = '\0';
    if (ends_epoch <= 0) {
        return;
    }
    time_t now = time(NULL);
    time_t e = (time_t)ends_epoch;
    struct tm lt, nt;
    localtime_r(&e, &lt);
    localtime_r(&now, &nt);

    char clock[24];
    fmt_clock(&lt, clock, sizeof(clock));

    int end_day = lt.tm_year * 366 + lt.tm_yday;
    int now_day = nt.tm_year * 366 + nt.tm_yday;
    if (end_day == now_day) {
        snprintf(buf, n, "%s", clock);
    } else if (end_day == now_day + 1) {
        snprintf(buf, n, "%s tomorrow", clock);
    } else {
        char day[8];
        strftime(day, sizeof(day), "%a", &lt);
        snprintf(buf, n, "%s %s", clock, day);
    }
}

void nws_format_banner(const nws_alert_t *alert, char *buf, size_t n)
{
    if (!buf || n == 0) {
        return;
    }
    buf[0] = '\0';
    if (!alert || !alert->event[0]) {
        return;
    }
    char until[40];
    nws_format_until(alert->expires_epoch, until, sizeof(until));
    if (until[0]) {
        snprintf(buf, n, "%s until %s", alert->event, until);
    } else {
        snprintf(buf, n, "%s", alert->event);
    }
}

void nws_format_ticker(const nws_alert_t *alert, char *buf, size_t n)
{
    if (!buf || n == 0) {
        return;
    }
    buf[0] = '\0';
    if (!alert || !alert->event[0]) {
        return;
    }
    char head[96];
    nws_format_banner(alert, head, sizeof(head));
    const char *detail = alert->instruction[0] ? alert->instruction
                        : (alert->headline[0] ? alert->headline : "");
    if (detail[0]) {
        snprintf(buf, n, "%s     %s          %s     %s          ",
                 head, detail, head, detail);
    } else {
        snprintf(buf, n, "%s          %s          ", head, head);
    }
}

#define NWS_POLL_INTERVAL_S   300     /* 5 minutes — HTTPS over SDIO blanks the panel */
#define TASK_STACK_SIZE       12288
#define TASK_PRIO             3
#define HTTP_BUF_SIZE         (48 * 1024)

static nws_alert_t       s_current_alert = {0};
static nws_forecast_t    s_forecast = {0};
static SemaphoreHandle_t s_alert_lock = NULL;
static char              s_last_sounded_id[64] = {0};
static char              s_cached_zip[10];
static float             s_cached_lat;
static float             s_cached_lon;
static bool              s_cached_coords;
static char              s_grid_office[8];
static int               s_grid_x;
static int               s_grid_y;
static bool              s_grid_valid;

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

static esp_err_t nws_http_get(const char *url, char *buf, int cap)
{
    buf[0] = '\0';
    http_accum_t acc = { .buf = buf, .len = 0, .cap = cap };

    esp_http_client_config_t http_cfg = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &acc,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "User-Agent",
                               "(tempest-weather-display, contact@weatherflow.com)");
    esp_http_client_set_header(client, "Accept", "application/geo+json");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || acc.len == 0 || acc.overflow) {
        ESP_LOGW(TAG, "GET %s -> HTTP %d %s%s", url, status,
                 esp_err_to_name(err), acc.overflow ? " truncated" : "");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void fetch_nws_forecast(float lat, float lon)
{
    char *buf = heap_caps_malloc(HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return;
    }

    if (!s_grid_valid) {
        char url[128];
        snprintf(url, sizeof(url),
                 "https://api.weather.gov/points/%.4f,%.4f", lat, lon);
        if (nws_http_get(url, buf, HTTP_BUF_SIZE) != ESP_OK) {
            free(buf);
            return;
        }
        cJSON *root = cJSON_Parse(buf);
        if (!root) {
            free(buf);
            return;
        }
        cJSON *props = cJSON_GetObjectItemCaseSensitive(root, "properties");
        cJSON *gid = props ? cJSON_GetObjectItemCaseSensitive(props, "gridId") : NULL;
        cJSON *gx = props ? cJSON_GetObjectItemCaseSensitive(props, "gridX") : NULL;
        cJSON *gy = props ? cJSON_GetObjectItemCaseSensitive(props, "gridY") : NULL;
        cJSON *rad = props ? cJSON_GetObjectItemCaseSensitive(props, "radarStation") : NULL;
        cJSON *rel = props ? cJSON_GetObjectItemCaseSensitive(props, "relativeLocation") : NULL;
        cJSON *rprops = rel ? cJSON_GetObjectItemCaseSensitive(rel, "properties") : NULL;
        cJSON *city = rprops ? cJSON_GetObjectItemCaseSensitive(rprops, "city") : NULL;
        cJSON *st = rprops ? cJSON_GetObjectItemCaseSensitive(rprops, "state") : NULL;

        if (!cJSON_IsString(gid) || !cJSON_IsNumber(gx) || !cJSON_IsNumber(gy)) {
            cJSON_Delete(root);
            free(buf);
            return;
        }
        strncpy(s_grid_office, gid->valuestring, sizeof(s_grid_office) - 1);
        s_grid_office[sizeof(s_grid_office) - 1] = '\0';
        s_grid_x = gx->valueint;
        s_grid_y = gy->valueint;
        s_grid_valid = true;

        xSemaphoreTake(s_alert_lock, portMAX_DELAY);
        if (cJSON_IsString(rad) && rad->valuestring[0]) {
            strncpy(s_forecast.radar, rad->valuestring, sizeof(s_forecast.radar) - 1);
            s_forecast.radar[sizeof(s_forecast.radar) - 1] = '\0';
        }
        if (cJSON_IsString(city)) {
            strncpy(s_forecast.city, city->valuestring, sizeof(s_forecast.city) - 1);
            s_forecast.city[sizeof(s_forecast.city) - 1] = '\0';
        }
        if (cJSON_IsString(st)) {
            strncpy(s_forecast.state, st->valuestring, sizeof(s_forecast.state) - 1);
            s_forecast.state[sizeof(s_forecast.state) - 1] = '\0';
        }
        strncpy(s_forecast.office, s_grid_office, sizeof(s_forecast.office) - 1);
        xSemaphoreGive(s_alert_lock);
        cJSON_Delete(root);
        ESP_LOGI(TAG, "NWS grid %s/%d,%d radar %s %s %s",
                 s_grid_office, s_grid_x, s_grid_y,
                 s_forecast.radar, s_forecast.city, s_forecast.state);
    }

    char furl[160];
    snprintf(furl, sizeof(furl),
             "https://api.weather.gov/gridpoints/%s/%d,%d/forecast",
             s_grid_office, s_grid_x, s_grid_y);
    if (nws_http_get(furl, buf, HTTP_BUF_SIZE) != ESP_OK) {
        free(buf);
        return;
    }
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return;
    }
    cJSON *props = cJSON_GetObjectItemCaseSensitive(root, "properties");
    cJSON *periods = props ? cJSON_GetObjectItemCaseSensitive(props, "periods") : NULL;
    cJSON *p0 = cJSON_IsArray(periods) ? cJSON_GetArrayItem(periods, 0) : NULL;
    if (!p0) {
        cJSON_Delete(root);
        return;
    }
    cJSON *name = cJSON_GetObjectItemCaseSensitive(p0, "name");
    cJSON *sh = cJSON_GetObjectItemCaseSensitive(p0, "shortForecast");
    cJSON *det = cJSON_GetObjectItemCaseSensitive(p0, "detailedForecast");
    cJSON *tmp = cJSON_GetObjectItemCaseSensitive(p0, "temperature");
    cJSON *popo = cJSON_GetObjectItemCaseSensitive(p0, "probabilityOfPrecipitation");
    cJSON *popv = popo ? cJSON_GetObjectItemCaseSensitive(popo, "value") : NULL;

    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    if (cJSON_IsString(name)) {
        strncpy(s_forecast.period, name->valuestring, sizeof(s_forecast.period) - 1);
        s_forecast.period[sizeof(s_forecast.period) - 1] = '\0';
    }
    if (cJSON_IsString(sh)) {
        copy_flat(s_forecast.short_fc, sizeof(s_forecast.short_fc),
                  sh->valuestring, sizeof(s_forecast.short_fc) - 1);
    }
    if (cJSON_IsString(det)) {
        copy_flat(s_forecast.detailed, sizeof(s_forecast.detailed),
                  det->valuestring, sizeof(s_forecast.detailed) - 1);
    }
    s_forecast.temp_f = cJSON_IsNumber(tmp) ? tmp->valueint : 0;
    s_forecast.pop = cJSON_IsNumber(popv) ? popv->valueint : -1;
    s_forecast.fetched_epoch = (int64_t)time(NULL);
    s_forecast.valid = s_forecast.short_fc[0] != '\0';
    xSemaphoreGive(s_alert_lock);
    cJSON_Delete(root);
    if (s_forecast.valid) {
        ESP_LOGI(TAG, "NWS forecast: %s  %s", s_forecast.period, s_forecast.short_fc);
    }
}

static void poll_nws_alerts(void)
{
    cfg_t cfg;
    cfg_get(&cfg);

    if (strlen(cfg.alert_zipcode) < 5) {
        return;
    }
    display_https_begin();

    float lat = 0.0f, lon = 0.0f;
    if (!s_cached_coords || strncmp(s_cached_zip, cfg.alert_zipcode,
                                    sizeof(s_cached_zip)) != 0) {
        if (fetch_coords_by_zip(cfg.alert_zipcode, &lat, &lon) != ESP_OK) {
            ESP_LOGW(TAG, "could not resolve zip %s to coordinates",
                     cfg.alert_zipcode);
            display_https_end();
            return;
        }
        strncpy(s_cached_zip, cfg.alert_zipcode, sizeof(s_cached_zip) - 1);
        s_cached_zip[sizeof(s_cached_zip) - 1] = '\0';
        s_cached_lat = lat;
        s_cached_lon = lon;
        s_cached_coords = true;
        s_grid_valid = false;
        wx_update_station_location(lat, lon);
    } else {
        lat = s_cached_lat;
        lon = s_cached_lon;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.weather.gov/alerts/active?point=%.4f,%.4f", lat, lon);

    char *resp_buf = heap_caps_malloc(HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!resp_buf) {
        fetch_nws_forecast(lat, lon);
        display_https_end();
        return;
    }
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
        fetch_nws_forecast(lat, lon);
        display_https_end();
        return;
    }
    esp_http_client_set_header(client, "User-Agent", "(tempest-weather-display, contact@weatherflow.com)");
    esp_http_client_set_header(client, "Accept", "application/geo+json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || acc.len == 0 || acc.overflow) {
        ESP_LOGW(TAG, "NWS alerts query returned HTTP %d, err %s%s",
                 status, esp_err_to_name(err),
                 acc.overflow ? " (truncated)" : "");
        free(resp_buf);
        fetch_nws_forecast(lat, lon);
        display_https_end();
        return;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        ESP_LOGW(TAG, "failed to parse NWS JSON response");
        fetch_nws_forecast(lat, lon);
        display_https_end();
        return;
    }

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (!cJSON_IsArray(features) || cJSON_GetArraySize(features) == 0) {
        /* No active alerts for this zone */
        xSemaphoreTake(s_alert_lock, portMAX_DELAY);
        s_current_alert.active = false;
        xSemaphoreGive(s_alert_lock);
        cJSON_Delete(root);
        fetch_nws_forecast(lat, lon);
        display_https_end();
        return;
    }

    /* Grab the highest priority alert */
    cJSON *feat0 = cJSON_GetArrayItem(features, 0);
    cJSON *props = cJSON_GetObjectItemCaseSensitive(feat0, "properties");
    if (!props) {
        cJSON_Delete(root);
        fetch_nws_forecast(lat, lon);
        display_https_end();
        return;
    }

    cJSON *id_obj       = cJSON_GetObjectItemCaseSensitive(props, "id");
    cJSON *event_obj    = cJSON_GetObjectItemCaseSensitive(props, "event");
    cJSON *headline_obj = cJSON_GetObjectItemCaseSensitive(props, "headline");
    cJSON *severity_obj = cJSON_GetObjectItemCaseSensitive(props, "severity");
    cJSON *ends_obj     = cJSON_GetObjectItemCaseSensitive(props, "ends");
    cJSON *expires_obj  = cJSON_GetObjectItemCaseSensitive(props, "expires");
    cJSON *instr_obj    = cJSON_GetObjectItemCaseSensitive(props, "instruction");

    const char *id_str = cJSON_IsString(id_obj) ? id_obj->valuestring : "";
    const char *event_str = cJSON_IsString(event_obj) ? event_obj->valuestring : "Weather Alert";
    const char *headline_str = cJSON_IsString(headline_obj) ? headline_obj->valuestring : "";
    const char *severity_str = cJSON_IsString(severity_obj) ? severity_obj->valuestring : "Severe";
    const char *ends_str = cJSON_IsString(ends_obj) ? ends_obj->valuestring : "";
    const char *expires_str = cJSON_IsString(expires_obj) ? expires_obj->valuestring : "";
    const char *instr_str = cJSON_IsString(instr_obj) ? instr_obj->valuestring : "";

    int64_t ends_epoch = nws_parse_iso(ends_str);
    if (ends_epoch <= 0) {
        ends_epoch = nws_parse_iso(expires_str);
    }

    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    s_current_alert.active = true;
    strncpy(s_current_alert.id, id_str, sizeof(s_current_alert.id) - 1);
    s_current_alert.id[sizeof(s_current_alert.id) - 1] = '\0';
    strncpy(s_current_alert.event, event_str, sizeof(s_current_alert.event) - 1);
    s_current_alert.event[sizeof(s_current_alert.event) - 1] = '\0';
    strncpy(s_current_alert.headline, headline_str, sizeof(s_current_alert.headline) - 1);
    s_current_alert.headline[sizeof(s_current_alert.headline) - 1] = '\0';
    copy_flat(s_current_alert.instruction, sizeof(s_current_alert.instruction),
              instr_str, sizeof(s_current_alert.instruction) - 1);
    strncpy(s_current_alert.severity, severity_str, sizeof(s_current_alert.severity) - 1);
    s_current_alert.severity[sizeof(s_current_alert.severity) - 1] = '\0';
    s_current_alert.expires_epoch = ends_epoch;

    /* Should we trigger the audible emergency siren & voice broadcast? */
    bool is_new_event = (strcmp(s_last_sounded_id, id_str) != 0);
    if (is_new_event && cfg.alert_siren_enabled) {
        strncpy(s_last_sounded_id, id_str, sizeof(s_last_sounded_id) - 1);
        s_last_sounded_id[sizeof(s_last_sounded_id) - 1] = '\0';

        /* Nighttime DND Filter Check: Only life-threatening alerts sound during night hours */
        bool allow_sound = true;
        if (cfg.night_alert_dnd) {
            time_t now_t = time(NULL);
            struct tm lt;
            localtime_r(&now_t, &lt);
            if (cfg_is_night(lt.tm_hour)) {
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
            ESP_LOGW(TAG, "EMERGENCY NOAA WEATHER ALERT: %s - siren and event clip", event_str);
            audio_play_full_noaa_broadcast(event_str);
        }
    }
    xSemaphoreGive(s_alert_lock);

    cJSON_Delete(root);
    fetch_nws_forecast(lat, lon);
    display_https_end();
}

bool nws_forecast_get(nws_forecast_t *out)
{
    if (!s_alert_lock || !out) {
        return false;
    }
    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    *out = s_forecast;
    bool ok = s_forecast.valid;
    xSemaphoreGive(s_alert_lock);
    return ok;
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
            /* NWS HTTPS over SDIO has wedged the shared I2C bus (GT911)
             * on this board; reset it before the next touch poll. */
            vTaskDelay(pdMS_TO_TICKS(200));
            display_recover_after_sdio("nws");
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
