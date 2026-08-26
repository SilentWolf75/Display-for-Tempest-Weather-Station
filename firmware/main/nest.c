#include "nest.h"
#include "wx_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

/* This file still compiles when the Nest is not the selected indoor source,
 * so its Kconfig-gated interval needs a fallback. Keeping it buildable means
 * switching sources is a menuconfig change, not a code change. */
#ifndef CONFIG_NEST_POLL_INTERVAL_S
#define CONFIG_NEST_POLL_INTERVAL_S 300
#endif

#ifndef NEST_CLIENT_ID
#define NEST_CLIENT_ID     ""
#define NEST_CLIENT_SECRET ""
#define NEST_REFRESH_TOKEN ""
#define NEST_PROJECT_ID    ""
#define NEST_DEVICE_ID     ""
#endif

static const char *TAG = "nest";

#define TOKEN_URL       "https://oauth2.googleapis.com/token"
#define SDM_URL_FMT     "https://smartdevicemanagement.googleapis.com/v1/" \
                        "enterprises/%s/devices/%s"

#define TOKEN_BUF_SIZE  4096    /* Google access tokens are long */
#define DEVICE_BUF_SIZE 8192
#define POST_BUF_SIZE   2048
#define TASK_STACK      10240   /* TLS + cJSON; do not shrink casually */
#define TASK_PRIO       4
#define BACKOFF_S       300

/* Refresh the access token this long before it actually expires, so a poll
 * never races the expiry. */
#define TOKEN_MARGIN_US (5 * 60 * 1000000LL)

static char    s_access_token[TOKEN_BUF_SIZE];
static int64_t s_token_expires_at_us;     /* esp_timer clock, not wall clock */
static bool    s_configured;

/* --------------------------------------------------------------------------
 * Small HTTP helper. Deliberately local rather than shared with
 * tempest_rest.c: one is a bare GET, the others are a form POST and a GET with
 * a bearer header, and collapsing three different shapes into one helper costs
 * more than the duplication saves.
 * -------------------------------------------------------------------------- */

typedef struct {
    char *buf;
    int   len;
    int   cap;
} accum_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    accum_t *acc = (accum_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !acc) {
        return ESP_OK;
    }
    if (acc->len + evt->data_len >= acc->cap) {
        ESP_LOGW(TAG, "response exceeds %d bytes, truncating", acc->cap);
        return ESP_OK;
    }
    memcpy(acc->buf + acc->len, evt->data, evt->data_len);
    acc->len += evt->data_len;
    acc->buf[acc->len] = '\0';
    return ESP_OK;
}

/* Google refresh tokens routinely look like "1//0abc..." -- the slashes must
 * be percent-encoded or the form body is parsed wrong and the exchange fails
 * with a confusing invalid_grant. */
static int url_encode(const char *src, char *dst, int dst_cap)
{
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        unsigned char c = *p;
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') ||
                          c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            if (o + 1 >= dst_cap) return -1;
            dst[o++] = (char)c;
        } else {
            if (o + 3 >= dst_cap) return -1;
            dst[o++] = '%';
            dst[o++] = hex[c >> 4];
            dst[o++] = hex[c & 0x0F];
        }
    }
    dst[o] = '\0';
    return o;
}

/* --------------------------------------------------------------------------
 * OAuth
 * -------------------------------------------------------------------------- */

static esp_err_t refresh_access_token(void)
{
    if (NEST_REFRESH_TOKEN[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char *post = malloc(POST_BUF_SIZE);
    char *enc  = malloc(POST_BUF_SIZE);
    accum_t acc = { .buf = malloc(TOKEN_BUF_SIZE), .len = 0,
                    .cap = TOKEN_BUF_SIZE };
    if (!post || !enc || !acc.buf) {
        free(post); free(enc); free(acc.buf);
        return ESP_ERR_NO_MEM;
    }
    acc.buf[0] = '\0';

    int n = snprintf(post, POST_BUF_SIZE, "grant_type=refresh_token");

    if (url_encode(NEST_CLIENT_ID, enc, POST_BUF_SIZE) < 0) goto too_long;
    n += snprintf(post + n, POST_BUF_SIZE - n, "&client_id=%s", enc);

    if (url_encode(NEST_CLIENT_SECRET, enc, POST_BUF_SIZE) < 0) goto too_long;
    n += snprintf(post + n, POST_BUF_SIZE - n, "&client_secret=%s", enc);

    if (url_encode(NEST_REFRESH_TOKEN, enc, POST_BUF_SIZE) < 0) goto too_long;
    n += snprintf(post + n, POST_BUF_SIZE - n, "&refresh_token=%s", enc);

    if (n >= POST_BUF_SIZE) goto too_long;

    esp_http_client_config_t cfg = {
        .url               = TOKEN_URL,
        .method            = HTTP_METHOD_POST,
        .event_handler     = on_http_event,
        .user_data         = &acc,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 15000,
        .buffer_size       = 2048,
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Content-Type",
                               "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(c, post, n);

    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK || status != 200) {
        if (status == 400 || status == 401) {
            /* Almost always a revoked or mistyped refresh token, and it will
             * never fix itself. Say so instead of retrying forever silently. */
            ESP_LOGE(TAG, "token refresh rejected (HTTP %d).", status);
            ESP_LOGE(TAG, "the refresh token is invalid, revoked, or");
            ESP_LOGE(TAG, "expired. If the Google Cloud app is still in");
            ESP_LOGE(TAG, "'Testing' publishing status, Google kills");
            ESP_LOGE(TAG, "refresh tokens after 7 days -- that is the");
            ESP_LOGE(TAG, "most likely cause. See docs/nest-api.md.");
            wx_set_indoor_auth_failed(true);
        } else {
            ESP_LOGE(TAG, "token refresh failed: %s, HTTP %d",
                     esp_err_to_name(err), status);
        }
        free(post); free(enc); free(acc.buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_ParseWithLength(acc.buf, acc.len);
    free(post); free(enc);
    if (!root) {
        ESP_LOGE(TAG, "token response did not parse");
        free(acc.buf);
        return ESP_FAIL;
    }

    const cJSON *tok = cJSON_GetObjectItemCaseSensitive(root, "access_token");
    const cJSON *exp = cJSON_GetObjectItemCaseSensitive(root, "expires_in");

    esp_err_t rc = ESP_FAIL;
    if (cJSON_IsString(tok) && tok->valuestring) {
        strncpy(s_access_token, tok->valuestring, sizeof(s_access_token) - 1);
        s_access_token[sizeof(s_access_token) - 1] = '\0';
        int secs = cJSON_IsNumber(exp) ? (int)exp->valuedouble : 3600;
        /* Monotonic clock deliberately: this must not depend on SNTP. */
        s_token_expires_at_us = esp_timer_get_time() + (int64_t)secs * 1000000LL;
        ESP_LOGI(TAG, "access token refreshed, valid %d s", secs);
        rc = ESP_OK;
    } else {
        ESP_LOGE(TAG, "token response had no access_token");
    }

    cJSON_Delete(root);
    free(acc.buf);
    return rc;

too_long:
    ESP_LOGE(TAG, "OAuth credentials too long for the request buffer");
    free(post); free(enc); free(acc.buf);
    return ESP_ERR_INVALID_SIZE;
}

static bool token_is_fresh(void)
{
    return s_access_token[0] != '\0' &&
           esp_timer_get_time() < (s_token_expires_at_us - TOKEN_MARGIN_US);
}

/* --------------------------------------------------------------------------
 * Device poll
 * -------------------------------------------------------------------------- */

static double trait_num(const cJSON *traits, const char *trait,
                        const char *field, double fallback)
{
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(traits, trait);
    if (!cJSON_IsObject(t)) {
        return fallback;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(t, field);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static void trait_str(const cJSON *traits, const char *trait,
                      const char *field, char *dst, size_t cap)
{
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(traits, trait);
    if (!cJSON_IsObject(t)) {
        return;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(t, field);
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(dst, v->valuestring, cap - 1);
        dst[cap - 1] = '\0';
    }
}

static esp_err_t parse_device(const char *json, int len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGE(TAG, "device response did not parse");
        return ESP_FAIL;
    }
    const cJSON *traits = cJSON_GetObjectItemCaseSensitive(root, "traits");
    if (!cJSON_IsObject(traits)) {
        ESP_LOGE(TAG, "device response had no traits object");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    wx_state_t p = {0};

    /* Sentinel of -1000 distinguishes "trait absent" from a real reading. */
    double temp = trait_num(traits, "sdm.devices.traits.Temperature",
                            "ambientTemperatureCelsius", -1000.0);
    double hum  = trait_num(traits, "sdm.devices.traits.Humidity",
                            "ambientHumidityPercent", -1000.0);

    if (temp < -999.0) {
        ESP_LOGW(TAG, "no Temperature trait on this device");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    p.indoor_temp_c       = (float)temp;
    p.indoor_humidity_pct = (hum < -999.0) ? 0.0f : (float)hum;

    trait_str(traits, "sdm.devices.traits.ThermostatHvac", "status",
              p.hvac_status, sizeof(p.hvac_status));
    trait_str(traits, "sdm.devices.traits.ThermostatMode", "mode",
              p.thermostat_mode, sizeof(p.thermostat_mode));

    /* Only the setpoint matching the active mode is present: HEAT gives
     * heatCelsius, COOL gives coolCelsius, HEATCOOL gives both. */
    p.setpoint_heat_c = (float)trait_num(
        traits, "sdm.devices.traits.ThermostatTemperatureSetpoint",
        "heatCelsius", 0.0);
    p.setpoint_cool_c = (float)trait_num(
        traits, "sdm.devices.traits.ThermostatTemperatureSetpoint",
        "coolCelsius", 0.0);

    char eco[16] = {0};
    trait_str(traits, "sdm.devices.traits.ThermostatEco", "mode",
              eco, sizeof(eco));
    p.eco_mode = (strcmp(eco, "MANUAL_ECO") == 0);

    cJSON_Delete(root);

    wx_update_indoor(&p);
    s_configured = true;

    ESP_LOGI(TAG, "indoor %.1fC  %.0f%%RH  %s  mode %s",
             p.indoor_temp_c, p.indoor_humidity_pct,
             p.hvac_status[0] ? p.hvac_status : "?",
             p.thermostat_mode[0] ? p.thermostat_mode : "?");
    return ESP_OK;
}

esp_err_t nest_fetch_now(void)
{
    if (NEST_REFRESH_TOKEN[0] == '\0' || NEST_DEVICE_ID[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    if (!token_is_fresh()) {
        esp_err_t err = refresh_access_token();
        if (err != ESP_OK) {
            return err;
        }
    }

    char *url  = malloc(512);
    char *auth = malloc(TOKEN_BUF_SIZE + 16);
    accum_t acc = { .buf = malloc(DEVICE_BUF_SIZE), .len = 0,
                    .cap = DEVICE_BUF_SIZE };
    if (!url || !auth || !acc.buf) {
        free(url); free(auth); free(acc.buf);
        return ESP_ERR_NO_MEM;
    }
    acc.buf[0] = '\0';

    snprintf(url, 512, SDM_URL_FMT, NEST_PROJECT_ID, NEST_DEVICE_ID);
    snprintf(auth, TOKEN_BUF_SIZE + 16, "Bearer %s", s_access_token);

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .event_handler     = on_http_event,
        .user_data         = &acc,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 15000,
        .buffer_size       = 2048,
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    free(url);
    free(auth);

    if (err == ESP_OK && status == 200) {
        err = parse_device(acc.buf, acc.len);
    } else if (status == 401) {
        /* Token died early. Drop it so the next poll re-exchanges. */
        ESP_LOGW(TAG, "401 from SDM, discarding access token");
        s_access_token[0] = '\0';
        err = ESP_FAIL;
    } else if (status == 429) {
        ESP_LOGW(TAG, "429 rate limited by SDM -- slow the poll interval");
        err = ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "device fetch failed: %s, HTTP %d",
                 esp_err_to_name(err), status);
        err = (err == ESP_OK) ? ESP_FAIL : err;
    }

    free(acc.buf);
    return err;
}

static void nest_task(void *arg)
{
    (void)arg;

    if (NEST_REFRESH_TOKEN[0] == '\0') {
        ESP_LOGW(TAG, "no Nest credentials in secrets.h; indoor tiles disabled");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        esp_err_t err = nest_fetch_now();
        int delay_s = (err == ESP_OK)
                    ? CONFIG_NEST_POLL_INTERVAL_S
                    : BACKOFF_S;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "backing off %d s", delay_s);
        }
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
    }
}

esp_err_t nest_start(void)
{
    if (xTaskCreate(nest_task, "nest", TASK_STACK, NULL, TASK_PRIO, NULL)
            != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool nest_is_configured(void)
{
    return s_configured;
}
