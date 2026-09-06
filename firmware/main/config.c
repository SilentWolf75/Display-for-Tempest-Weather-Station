#include "config.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "config";

#define NVS_NAMESPACE  "wxcfg"
#define NVS_BLOB_KEY   "cfg"

/* Bump when the struct layout changes so a stale blob is discarded rather than
 * reinterpreted as garbage settings. */
#define CFG_VERSION    9

static cfg_t             s_cfg;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_save_lock;
static TaskHandle_t s_save_task;

static const cfg_t DEFAULTS = {
    .wifi_ssid         = "",
    .wifi_password     = "",
    .units             = CFG_UNITS_IMPERIAL,
    .timezone_idx      = 1,
    .brightness_day    = 100,
    .brightness_night  = 70,
    .night_start_hour  = 22,
    .night_end_hour    = 7,
    .night_dim_enabled   = true,
    .animate_forecast    = true,
    .wind_scale_max_ms   = 20,
    .alert_zipcode       = "66030",
    .alert_volume        = 80,
    .notification_volume = 70,
    .alert_siren_enabled = true,
    .hourly_chime_enabled = true,
    .morning_briefing_enabled = true,
    .night_alert_dnd     = true,
    .night_standby_enabled = false,
    .night_standby_red     = false,
    .web_server_enabled  = true,
    .mqtt_enabled        = false,
    .mqtt_broker         = "192.168.1.100",
    .screensaver_idle_min      = 0,
    .screensaver_brightness    = 50,
    .lightning_alert_sound     = true,
    .lightning_alert_voice     = false,
    .indoor_temp_offset_c      = 0.0f,   /* user trim; board heat is automatic */
    .start_page                = 0,
};

typedef struct {
    uint8_t version;
    cfg_t   cfg;
} stored_t;

#define LOCK()    xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK()  xSemaphoreGive(s_lock)

static void sanitize_str(char *s, size_t max_len)
{
    if (!s || max_len == 0) return;
    s[max_len - 1] = '\0';
}

static void clamp(cfg_t *c)
{
    sanitize_str(c->wifi_ssid, sizeof(c->wifi_ssid));
    sanitize_str(c->wifi_password, sizeof(c->wifi_password));
    if (!cfg_wifi_ssid_usable(c->wifi_ssid)) {
        c->wifi_ssid[0] = '\0';
        c->wifi_password[0] = '\0';
    }
    if (c->start_page > 4)             c->start_page = 0;
    if (c->units > CFG_UNITS_METRIC)   c->units = CFG_UNITS_IMPERIAL;
    if (c->timezone_idx > 7)           c->timezone_idx = 1;
    if (c->brightness_day < 5)         c->brightness_day = 5;
    if (c->brightness_day > 100)       c->brightness_day = 100;
    if (c->brightness_night < 50)      c->brightness_night = 50;
    if (c->brightness_night > 100)     c->brightness_night = 100;
    if (c->screensaver_idle_min > 60)  c->screensaver_idle_min = 60;
    if (c->screensaver_brightness < 5) c->screensaver_brightness = 5;
    if (c->screensaver_brightness > 50) c->screensaver_brightness = 50;
    if (c->indoor_temp_offset_c < -10.0f || c->indoor_temp_offset_c > 10.0f) {
        c->indoor_temp_offset_c = 0.0f;
    }
    /* Old firmware stored the full board correction here; indoor.c handles
     * that automatically now. */
    if (c->indoor_temp_offset_c < -6.0f && c->indoor_temp_offset_c > -4.5f) {
        c->indoor_temp_offset_c = 0.0f;
    }

    char zip[sizeof(c->alert_zipcode)];
    size_t z = 0;
    for (size_t i = 0; c->alert_zipcode[i] != '\0' && z < 5; i++) {
        if (isdigit((unsigned char)c->alert_zipcode[i])) {
            zip[z++] = c->alert_zipcode[i];
        }
    }
    zip[z] = '\0';
    strncpy(c->alert_zipcode, zip, sizeof(c->alert_zipcode) - 1);
    c->alert_zipcode[sizeof(c->alert_zipcode) - 1] = '\0';
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

esp_err_t cfg_flush(void)
{
    xSemaphoreTake(s_save_lock, portMAX_DELAY);
    cfg_t c;
    cfg_get(&c);
    esp_err_t err = persist(&c);
    xSemaphoreGive(s_save_lock);
    return err;
}

static void save_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* Restart the debounce window whenever another value changes. */
        while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(750))) {}
        if (cfg_flush() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            xTaskNotifyGive(s_save_task);
        }
    }
}

esp_err_t cfg_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    s_cfg = DEFAULTS;
    s_save_lock = xSemaphoreCreateMutex();
    if (!s_save_lock || xTaskCreate(save_task, "cfg_save", 4096, NULL, 2,
                                    &s_save_task) != pdPASS) return ESP_ERR_NO_MEM;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings; using defaults");
        return ESP_OK;
    }

    stored_t blob = {0};
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(h, NVS_BLOB_KEY, &blob, &len);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings blob");
        return ESP_OK;
    }

    if (len < sizeof(uint8_t) + offsetof(cfg_t, wifi_ssid)) {
        ESP_LOGW(TAG, "settings blob too small (%u bytes)", (unsigned)len);
        return ESP_OK;
    }

    if (blob.version == 7) {
        /* Keep every setting; turn on forecast animation that v7 defaulted off. */
        s_cfg = blob.cfg;
        clamp(&s_cfg);
        s_cfg.animate_forecast = true;
        s_cfg.start_page = 0;
        persist(&s_cfg);
        ESP_LOGI(TAG, "settings v7→v9: forecast animation enabled");
        return ESP_OK;
    }

    if (blob.version == 8) {
        s_cfg = blob.cfg;
        s_cfg.start_page = 0;
        clamp(&s_cfg);
        persist(&s_cfg);
        ESP_LOGI(TAG, "settings v8→v9: start page LIVE");
        return ESP_OK;
    }

    if (blob.version != CFG_VERSION) {
        ESP_LOGW(TAG, "settings version %u (expected %u); keeping Wi-Fi only",
                 blob.version, CFG_VERSION);
        if (cfg_wifi_ssid_usable(blob.cfg.wifi_ssid)) {
            strncpy(s_cfg.wifi_ssid, blob.cfg.wifi_ssid, CFG_SSID_LEN - 1);
            s_cfg.wifi_ssid[CFG_SSID_LEN - 1] = '\0';
            strncpy(s_cfg.wifi_password, blob.cfg.wifi_password,
                    CFG_PASSWORD_LEN - 1);
            s_cfg.wifi_password[CFG_PASSWORD_LEN - 1] = '\0';
            persist(&s_cfg);
        }
        return ESP_OK;
    }

    if (len < sizeof(stored_t)) {
        ESP_LOGW(TAG, "settings blob truncated (%u bytes)", (unsigned)len);
        return ESP_OK;
    }

    cfg_t before = blob.cfg;
    s_cfg = blob.cfg;
    clamp(&s_cfg);
    ESP_LOGI(TAG, "settings loaded: %s, wifi '%s', day %u%%",
             (s_cfg.units == CFG_UNITS_METRIC) ? "metric" : "imperial",
             s_cfg.wifi_ssid[0] ? s_cfg.wifi_ssid : "(none)",
             s_cfg.brightness_day);
    if (memcmp(&before, &s_cfg, sizeof(cfg_t)) != 0) {
        persist(&s_cfg);
    }
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
    xTaskNotifyGive(s_save_task);
    return ESP_OK;
}

esp_err_t cfg_set_wifi(const char *ssid, const char *password)
{
    cfg_t c;
    cfg_get(&c);
    strncpy(c.wifi_ssid, ssid ? ssid : "", CFG_SSID_LEN - 1);
    c.wifi_ssid[CFG_SSID_LEN - 1] = '\0';
    strncpy(c.wifi_password, password ? password : "",
            CFG_PASSWORD_LEN - 1);
    c.wifi_password[CFG_PASSWORD_LEN - 1] = '\0';
    /* Deliberately never logs the password. */
    ESP_LOGI(TAG, "wi-fi credentials stored for '%s'", c.wifi_ssid);
    esp_err_t err = cfg_set(&c);
    return err == ESP_OK ? cfg_flush() : err;
}

bool cfg_has_wifi(void)
{
    cfg_t c;
    cfg_get(&c);
    return cfg_wifi_ssid_usable(c.wifi_ssid);
}

bool cfg_wifi_ssid_usable(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') {
        return false;
    }
    if (strcmp(ssid, "changeme") == 0) {
        return false;
    }
    for (int i = 0; ssid[i] != '\0'; i++) {
        if ((unsigned char)ssid[i] < 32 || (unsigned char)ssid[i] > 126) {
            return false;
        }
    }
    return true;
}

uint8_t cfg_brightness_now(void)
{
    cfg_t c;
    cfg_get(&c);

    time_t now = time(NULL);
    uint8_t b = c.brightness_day;
    if (now >= 1700000000LL) {
        struct tm lt;
        localtime_r(&now, &lt);
        if (cfg_is_night(lt.tm_hour)) {
            b = c.brightness_night;
        }
    }
    if (b < 5) {
        b = 5;
    }
    return b;
}

esp_err_t cfg_reset(void)
{
    /* Keep the network. Resetting display preferences should not strand a
     * wall-mounted panel off the network with no way back on. */
    cfg_t c = DEFAULTS;
    cfg_t cur;
    cfg_get(&cur);
    memcpy(c.wifi_ssid, cur.wifi_ssid, sizeof(c.wifi_ssid));
    memcpy(c.wifi_password, cur.wifi_password, sizeof(c.wifi_password));
    esp_err_t err = cfg_set(&c);
    return err == ESP_OK ? cfg_flush() : err;
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
