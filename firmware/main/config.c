#include "config.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "config";

#define NVS_NAMESPACE  "wxcfg"
#define NVS_BLOB_KEY   "cfg"

/* Bump when the struct layout changes so a stale blob is discarded rather than
 * reinterpreted as garbage settings. */
#define CFG_VERSION    1

static cfg_t             s_cfg;
static SemaphoreHandle_t s_lock;

static const cfg_t DEFAULTS = {
    .units             = CFG_UNITS_IMPERIAL,
    .brightness_day    = 100,
    .brightness_night  = 25,
    .night_start_hour  = 22,
    .night_end_hour    = 7,
    .night_dim_enabled = true,
    .animate_forecast  = false,
    .wind_scale_max_ms = 20,
};

typedef struct {
    uint8_t version;
    cfg_t   cfg;
} stored_t;

#define LOCK()    xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK()  xSemaphoreGive(s_lock)

static void clamp(cfg_t *c)
{
    if (c->units > CFG_UNITS_METRIC)   c->units = CFG_UNITS_IMPERIAL;
    if (c->brightness_day < 5)         c->brightness_day = 5;
    if (c->brightness_day > 100)       c->brightness_day = 100;
    if (c->brightness_night > 100)     c->brightness_night = 100;
    if (c->night_start_hour > 23)      c->night_start_hour = 22;
    if (c->night_end_hour > 23)        c->night_end_hour = 7;
    if (c->wind_scale_max_ms < 5)      c->wind_scale_max_ms = 5;
    if (c->wind_scale_max_ms > 60)     c->wind_scale_max_ms = 60;
}

static esp_err_t persist(const cfg_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    stored_t blob = { .version = CFG_VERSION, .cfg = *c };
    err = nvs_set_blob(h, NVS_BLOB_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings not saved: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t cfg_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_cfg = DEFAULTS;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings; using defaults");
        return ESP_OK;
    }

    stored_t blob = {0};
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(h, NVS_BLOB_KEY, &blob, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof(blob)) {
        ESP_LOGI(TAG, "no usable settings blob; using defaults");
        return ESP_OK;
    }
    if (blob.version != CFG_VERSION) {
        ESP_LOGW(TAG, "settings from version %u discarded (expected %u)",
                 blob.version, CFG_VERSION);
        return ESP_OK;
    }

    s_cfg = blob.cfg;
    clamp(&s_cfg);
    ESP_LOGI(TAG, "settings loaded: %s, day %u%%, night %u%%",
             s_cfg.units == CFG_UNITS_METRIC ? "metric" : "imperial",
             s_cfg.brightness_day, s_cfg.brightness_night);
    return ESP_OK;
}

void cfg_get(cfg_t *out)
{
    LOCK();
    *out = s_cfg;
    UNLOCK();
}

esp_err_t cfg_set(const cfg_t *in)
{
    cfg_t next = *in;
    clamp(&next);

    LOCK();
    bool changed = memcmp(&next, &s_cfg, sizeof(cfg_t)) != 0;
    if (changed) {
        s_cfg = next;
    }
    UNLOCK();

    /* Writing NVS on every slider pixel would wear the flash for no reason. */
    if (!changed) {
        return ESP_OK;
    }
    return persist(&next);
}

esp_err_t cfg_reset(void)
{
    return cfg_set(&DEFAULTS);
}

bool cfg_is_night(int local_hour)
{
    cfg_t c;
    cfg_get(&c);
    if (!c.night_dim_enabled) {
        return false;
    }
    if (c.night_start_hour == c.night_end_hour) {
        return false;
    }
    /* The normal case wraps past midnight (22 -> 7), so the comparison is not
     * a simple range test. */
    if (c.night_start_hour < c.night_end_hour) {
        return local_hour >= c.night_start_hour &&
               local_hour <  c.night_end_hour;
    }
    return local_hour >= c.night_start_hour ||
           local_hour <  c.night_end_hour;
}

/* ---- conversions ---------------------------------------------------------
 * Read the live setting on every call. These run a few dozen times per second
 * at most, against a mutex take -- immaterial next to a screen repaint. */

static bool imperial(void)
{
    cfg_t c;
    cfg_get(&c);
    return c.units == CFG_UNITS_IMPERIAL;
}

float cfg_temp(float c)          { return imperial() ? c * 9.0f / 5.0f + 32.0f : c; }
const char *cfg_temp_suffix(void){ return imperial() ? "F" : "C"; }

float cfg_wind(float ms)         { return imperial() ? ms * 2.236936f : ms; }
const char *cfg_wind_suffix(void){ return imperial() ? "mph" : "m/s"; }

float cfg_pressure(float mb)     { return imperial() ? mb * 0.0295299830714f : mb; }
const char *cfg_pressure_suffix(void) { return imperial() ? "inHg" : "mb"; }
const char *cfg_pressure_fmt(void)    { return imperial() ? "%.2f" : "%.1f"; }

float cfg_rain(float mm)         { return imperial() ? mm / 25.4f : mm; }
const char *cfg_rain_suffix(void){ return imperial() ? "in" : "mm"; }
const char *cfg_rain_fmt(void)   { return imperial() ? "%.2f" : "%.1f"; }

float cfg_distance(float km)     { return imperial() ? km * 0.621371f : km; }
const char *cfg_distance_suffix(void) { return imperial() ? "mi" : "km"; }
