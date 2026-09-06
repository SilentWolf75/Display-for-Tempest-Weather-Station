#include "esp_heap_caps.h"
#include "nws_alerts.h"
#include "alert_policy.h"
#include "esp_timer.h"
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
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "nws_alerts";
static char s_sounded_ids[16][64];
static unsigned s_sounded_next;
static TaskHandle_t s_task;
static int64_t s_last_success;
static bool s_last_poll_ok;

#define NVS_NWS_NS       "nws"
#define NVS_SIREN_ID_KEY "siren_id"

/* NWS ids are URLs (~90+ chars). The unique oid is at the end; the shared
 * prefix does not fit in 64 bytes, so strcmp(truncated, full) was always
 * unequal and the siren retriggered on every 5-minute poll. */
static void copy_alert_key(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }
    size_t len = strlen(src);
    if (len < n) {
        memcpy(dst, src, len + 1);
        return;
    }
    memcpy(dst, src + len - (n - 1), n - 1);
    dst[n - 1] = '\0';
}

static bool alert_key_match(const char *stored, const char *id_str)
{
    char cur[64];
    copy_alert_key(cur, sizeof(cur), id_str);
    return stored[0] && cur[0] && strcmp(stored, cur) == 0;
}

static void siren_id_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NWS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(s_sounded_ids);
    if (nvs_get_blob(h, "sounded_ids", s_sounded_ids, &n) != ESP_OK ||
        n != sizeof(s_sounded_ids)) {
        memset(s_sounded_ids, 0, sizeof(s_sounded_ids));
        n = sizeof(s_sounded_ids[0]);
        nvs_get_str(h, NVS_SIREN_ID_KEY, s_sounded_ids[0], &n);
    }
    nvs_close(h);
    for (int i = 0; i < 16; i++) s_sounded_ids[i][63] = '\0';
    s_sounded_next = 1;
}

static void siren_id_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NWS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "sounded_ids", s_sounded_ids, sizeof(s_sounded_ids)) == ESP_OK)
        nvs_commit(h);
    nvs_close(h);
}

static bool already_sounded(const char *id)
{
    for (int i = 0; i < 16; i++)
        if (alert_key_match(s_sounded_ids[i], id)) return true;
    return false;
}

/* Settings label is "siren on active warning". Advisories stay on the
 * ticker; they must not blast the NOAA tone every poll. */
static bool alert_warrants_siren(const char *event, const char *severity)
{
    (void)severity;
    if (!event || strstr(event, "Advisory") || strstr(event, "Statement") ||
        strstr(event, "Outlook")) {
        return false;
    }
    return strstr(event, "Warning") || strstr(event, "Watch") ||
           strstr(event, "Emergency") || strstr(event, "Tornado");
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
#define TASK_STACK_SIZE       8192
#define TASK_PRIO             3
#define HTTP_BUF_SIZE         (48 * 1024)

static nws_alert_t *s_alerts;
static int s_alert_count;
static nws_forecast_t    s_forecast = {0};
static SemaphoreHandle_t s_alert_lock = NULL;

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
        snprintf(s_forecast.office, sizeof(s_forecast.office), "%.*s", (int)sizeof(s_forecast.office) - 1, s_grid_office);
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

/* Own HTTPS window, and only when AES still has internal RAM. Stacking this
 * on the alerts GET plus the NOAA siren is what froze the panel (esp-aes
 * alloc failed while LVGL was locked). */
static void maybe_nws_forecast(float lat, float lon)
{
    int64_t now = (int64_t)time(NULL);
    if (s_forecast.valid && s_forecast.fetched_epoch > 0 &&
        (now - s_forecast.fetched_epoch) < 900) {
        return;
    }
    size_t heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (heap < 48 * 1024) {
        ESP_LOGW(TAG, "skip NWS forecast, internal heap %u B", (unsigned)heap);
        return;
    }
    if (audio_is_playing()) {
        ESP_LOGI(TAG, "skip NWS forecast while alert audio plays");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    display_https_begin();
    fetch_nws_forecast(lat, lon);
    display_https_end();
}

static void poll_nws_alerts(void)
{
    cfg_t cfg;
    cfg_get(&cfg);

    if (strlen(cfg.alert_zipcode) < 5) {
        return;
    }
    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    s_last_poll_ok = false;
    if (strcmp(s_cached_zip, cfg.alert_zipcode) != 0) {
        s_alert_count = 0;
        s_last_success = 0;
        s_forecast.valid = false;
    }
    xSemaphoreGive(s_alert_lock);
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
        display_https_end();
        return;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        ESP_LOGW(TAG, "failed to parse NWS JSON response");
        display_https_end();
        return;
    }

    cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (!cJSON_IsArray(features)) {
        cJSON_Delete(root);
        display_https_end();
        return;
    }
    int64_t now = (int64_t)time(NULL);
    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    s_alert_count = 0;
    memset(s_alerts, 0, 16 * sizeof(*s_alerts));
    const cJSON *feature;
    cJSON_ArrayForEach(feature, features) {
        const cJSON *props = cJSON_GetObjectItemCaseSensitive(feature, "properties");
        if (!cJSON_IsObject(props)) continue;
        nws_alert_t alert = { .active = true };
        const char *keys[] = {"id", "event", "headline", "severity", "instruction"};
        char *dest[] = {alert.id, alert.event, alert.headline, alert.severity, alert.instruction};
        size_t sizes[] = {sizeof(alert.id), sizeof(alert.event), sizeof(alert.headline),
                          sizeof(alert.severity), sizeof(alert.instruction)};
        for (int i = 0; i < 5; i++) {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(props, keys[i]);
            if (!cJSON_IsString(v)) continue;
            if (i == 0) copy_alert_key(dest[i], sizes[i], v->valuestring);
            else copy_flat(dest[i], sizes[i], v->valuestring, sizes[i] - 1);
        }
        const cJSON *ends = cJSON_GetObjectItemCaseSensitive(props, "ends");
        const cJSON *expires = cJSON_GetObjectItemCaseSensitive(props, "expires");
        alert.expires_epoch = nws_parse_iso(cJSON_IsString(ends) ? ends->valuestring : NULL);
        if (!alert.expires_epoch)
            alert.expires_epoch = nws_parse_iso(cJSON_IsString(expires) ? expires->valuestring : NULL);
        if (!alert.event[0] || !nws_alert_live(&alert, now)) continue;
        /* Keep the highest-ranked entries even when the response exceeds capacity. */
        bool full = s_alert_count == 16;
        int pos = full ? 15 : s_alert_count++;
        if (full &&
            nws_alert_rank(&alert) <= nws_alert_rank(&s_alerts[pos])) continue;
        while (pos > 0 && nws_alert_rank(&alert) > nws_alert_rank(&s_alerts[pos-1])) {
            s_alerts[pos] = s_alerts[pos-1];
            pos--;
        }
        s_alerts[pos] = alert;
    }
    s_last_success = now;
    s_last_poll_ok = true;
    xSemaphoreGive(s_alert_lock);
    cJSON_Delete(root);
    display_https_end();
}

/* Called only by the NWS task, outside HTTP and outside the display lock. */
static void service_siren(void)
{
    if (audio_is_playing()) return;
    cfg_t cfg;
    cfg_get(&cfg);
    if (!cfg.alert_siren_enabled) return;
    nws_alert_t pending = {0};
    int64_t now = (int64_t)time(NULL);
    time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    if (now - s_last_success <= 2 * NWS_POLL_INTERVAL_S) {
        for (int i = 0; i < s_alert_count; i++) {
            const nws_alert_t *a = &s_alerts[i];
            if (!a->id[0] || !nws_alert_live(a, now) || already_sounded(a->id) ||
                !alert_warrants_siren(a->event, a->severity)) continue;
            bool critical = strstr(a->event, "Tornado") || strstr(a->event, "Flood") ||
                            strstr(a->event, "Severe Thunderstorm");
            if (cfg.night_alert_dnd && cfg_is_night(lt.tm_hour) && !critical) continue;
            pending = *a;
            break;
        }
    }
    xSemaphoreGive(s_alert_lock);
    if (pending.active && audio_play_full_noaa_broadcast(pending.event)) {
        copy_alert_key(s_sounded_ids[s_sounded_next++ % 16], 64, pending.id);
        siren_id_save();
    }
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
    int64_t next_poll = 0;
    while (1) {
        bool refresh = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) != 0;
        int64_t now = esp_timer_get_time();
        if (refresh) next_poll = 0;
        if (net_is_connected() && now >= next_poll) {
            poll_nws_alerts();
            if (s_cached_coords && !audio_is_playing())
                maybe_nws_forecast(s_cached_lat, s_cached_lon);
            display_recover_after_sdio("nws");
            next_poll = esp_timer_get_time() + NWS_POLL_INTERVAL_S * 1000000LL;
        }
        service_siren();
    }
}

esp_err_t nws_alerts_start(void)
{
    s_alert_lock = xSemaphoreCreateMutex();
    s_alerts = heap_caps_calloc(16, sizeof(nws_alert_t), MALLOC_CAP_SPIRAM);
    if (!s_alert_lock || !s_alerts) return ESP_ERR_NO_MEM;
    siren_id_load();
    if (xTaskCreate(nws_task, "nws_alerts", TASK_STACK_SIZE, NULL, TASK_PRIO,
                    &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void nws_alerts_refresh(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

bool nws_alerts_is_current(void)
{
    if (!s_alert_lock) return false;
    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    int64_t now = (int64_t)time(NULL);
    bool ok = s_last_poll_ok && s_last_success > 1600000000LL &&
              now - s_last_success <= 2 * NWS_POLL_INTERVAL_S;
    xSemaphoreGive(s_alert_lock);
    return ok;
}

bool nws_alerts_get_active(nws_alert_t *out_alert)
{
    if (!s_alert_lock || !out_alert) return false;
    memset(out_alert, 0, sizeof(*out_alert));
    int64_t now = (int64_t)time(NULL);
    xSemaphoreTake(s_alert_lock, portMAX_DELAY);
    for (int i = 0; i < s_alert_count; i++) {
        if (nws_alert_live(&s_alerts[i], now)) {
            *out_alert = s_alerts[i];
            break;
        }
    }
    xSemaphoreGive(s_alert_lock);
    return out_alert->active;
}
