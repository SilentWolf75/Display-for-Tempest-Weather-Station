#include "net.h"
#include "wx_state.h"

#if !CONFIG_TEMPEST_NETWORK_ENABLED

#include "esp_log.h"

/* Display-only build. Every esp_wifi_* symbol comes from esp_wifi_remote,
 * which is compiled out here, so this file must not reference any of them
 * -- IDF's own esp_wifi has no implementation on the radio-less P4 and the
 * link would fail. */
esp_err_t net_start(void)
{
    ESP_LOGW("net", "networking is compiled out (CONFIG_TEMPEST_NETWORK_ENABLED=n)");
    wx_set_wifi_connected(false);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t net_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t net_wifi_begin(void) { return ESP_ERR_NOT_SUPPORTED; }
void net_schedule_wifi_begin(void) {}

bool net_is_connected(void) { return false; }
int8_t net_get_rssi(void) { return 0; }
bool net_time_is_valid(void) { return false; }
int net_scan(net_ap_t *out, int max_aps) { (void)out; (void)max_aps; return -1; }
esp_err_t net_apply_credentials(const char *s, const char *p)
{ (void)s; (void)p; return ESP_ERR_NOT_SUPPORTED; }
const char *net_current_ssid(void) { return ""; }
bool net_get_ip(char *buf, size_t len) { (void)buf; (void)len; return false; }
bool net_wifi_scan_busy(void) { return false; }
void net_wifi_ui_active(bool active) { (void)active; }

#else


#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "config.h"
#include "esp_timer.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

/* secrets.h only SEEDS the credentials on a first boot. Once anything has
 * been entered on the settings screen, NVS wins -- otherwise changing networks
 * on the panel would be silently undone by the next reflash. */
#ifndef WIFI_SSID
#define WIFI_SSID      CONFIG_TEMPEST_WIFI_SSID
#define WIFI_PASSWORD  CONFIG_TEMPEST_WIFI_PASSWORD
#endif

#define MAX_SCAN_APS   32

static const char *TAG = "net";

#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1
#define MAX_RETRY            10
#define CONNECT_TIMEOUT_MS   30000

static EventGroupHandle_t s_events;
static SemaphoreHandle_t  s_wifi_lock;
static int  s_retries;
static bool s_time_valid;
static esp_timer_handle_t s_reconnect_timer;
/* Set while a scan is running. The auto-reconnect below must stand down for
 * the duration: esp_wifi_connect() and a scan cannot run at once, and the
 * scan is the one that loses -- it returns zero networks. */
static volatile bool s_scanning;
static volatile bool s_wifi_ui_active;
static bool          s_wifi_begun;
static esp_timer_handle_t s_wifi_begin_timer;

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (s_wifi_ui_active) {
        return;
    }
    s_retries = 0;
    if (cfg_has_wifi() && !s_scanning) {
        ESP_LOGI(TAG, "reconnect timer: attempting Wi-Fi connection...");
        esp_wifi_connect();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        if (cfg_has_wifi() && !s_wifi_ui_active) {
            esp_wifi_connect();
        }
        return;
    }
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wx_set_wifi_connected(false);
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);

        if (s_scanning || s_wifi_ui_active) {
            return;
        }

        if (!cfg_has_wifi()) {
            return;
        }

        if (s_retries < MAX_RETRY) {
            s_retries++;
            ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retries, MAX_RETRY);
            esp_wifi_connect();
        } else {
            /* Non-blocking back-off timer so system event loop is never blocked! */
            ESP_LOGW(TAG, "connect failed %d times, scheduling retry in 15s", MAX_RETRY);
            xEventGroupSetBits(s_events, WIFI_FAIL_BIT);
            if (!s_reconnect_timer) {
                esp_timer_create_args_t t_args = {
                    .callback = reconnect_timer_cb,
                    .name     = "reconnect_tmr"
                };
                esp_timer_create(&t_args, &s_reconnect_timer);
            }
            if (s_reconnect_timer) {
                esp_timer_stop(s_reconnect_timer);
                esp_timer_start_once(s_reconnect_timer, 15000000); /* 15s */
            }
        }
    }
}

static void start_sntp(void);

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&event->ip_info.ip));
    s_retries = 0;
    wx_set_wifi_connected(true);
    xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);

    static bool sntp_started = false;
    if (!sntp_started) {
        start_sntp();
        sntp_started = true;
    }
}

static void on_time_sync(struct timeval *tv)
{
    s_time_valid = true;
    ESP_LOGI(TAG, "clock synchronised");
}


static const char * const TZ_STRS[] = {
    "EST5EDT,M3.2.0,M11.1.0", /* Eastern */
    "CST6CDT,M3.2.0,M11.1.0", /* Central */
    "MST7MDT,M3.2.0,M11.1.0", /* Mountain */
    "MST7",                   /* Arizona */
    "PST8PDT,M3.2.0,M11.1.0", /* Pacific */
    "AKST9AKDT,M3.2.0,M11.1.0",/* Alaska */
    "HST10",                  /* Hawaii */
    "UTC0"                    /* UTC */
};

void net_set_timezone(int tz_idx)
{
    if (tz_idx < 0 || tz_idx >= 8) tz_idx = 1;
    setenv("TZ", TZ_STRS[tz_idx], 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to %s (%s)", TZ_STRS[tz_idx], (tz_idx == 1) ? "Central" : "Configured");
}

static void start_sntp(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_time_sync;
    cfg.start   = true;
    cfg.server_from_dhcp = false;   /* DHCP NTP is off; pool.ntp.org only */
    esp_netif_sntp_init(&cfg);

    cfg_t c;
    cfg_get(&c);
    net_set_timezone(c.timezone_idx);
}

esp_err_t net_init(void)
{
    if (s_events) {
        return ESP_OK;
    }

    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }
    s_wifi_lock = xSemaphoreCreateMutex();
    if (!s_wifi_lock) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "on the ESP32-P4 this almost always means the ESP-Hosted");
        ESP_LOGE(TAG, "link to the ESP32-C6 did not come up. Check that the C6");
        ESP_LOGE(TAG, "slave firmware matches the esp_hosted component version.");
        return err;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL, NULL));

    cfg_t c;
    cfg_get(&c);
    net_set_timezone(c.timezone_idx);
    if (!cfg_wifi_ssid_usable(c.wifi_ssid) && cfg_wifi_ssid_usable(WIFI_SSID)) {
        ESP_LOGI(TAG, "seeding credentials from secrets.h");
        cfg_set_wifi(WIFI_SSID, WIFI_PASSWORD);
        cfg_get(&c);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (cfg_has_wifi()) {
        wifi_config_t wcfg = {0};
        strncpy((char *)wcfg.sta.ssid, c.wifi_ssid, sizeof(wcfg.sta.ssid) - 1);
        strncpy((char *)wcfg.sta.password, c.wifi_password,
                sizeof(wcfg.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    }

    ESP_LOGI(TAG, "network stack ready (radio deferred until UI is drawn)");
    return ESP_OK;
}

esp_err_t net_wifi_begin(void)
{
    if (s_wifi_begun) {
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_start: %s (C6 link not ready; will retry)",
                 esp_err_to_name(err));
        return err;
    }
    s_wifi_begun = true;

    cfg_t c;
    cfg_get(&c);
    if (cfg_has_wifi()) {
        ESP_LOGI(TAG, "connecting to %s", c.wifi_ssid);
    } else {
        ESP_LOGW(TAG, "no wi-fi configured; open settings to connect");
    }
    return ESP_OK;
}

static void wifi_begin_timer_cb(void *arg)
{
    (void)arg;
    if (net_wifi_begin() == ESP_OK && s_wifi_begin_timer) {
        esp_timer_stop(s_wifi_begin_timer);
    }
}

void net_schedule_wifi_begin(void)
{
    if (s_wifi_begun) {
        return;
    }
    if (net_wifi_begin() == ESP_OK) {
        return;
    }
    if (!s_wifi_begin_timer) {
        esp_timer_create_args_t t_args = {
            .callback = wifi_begin_timer_cb,
            .name     = "wifi_begin",
        };
        esp_timer_create(&t_args, &s_wifi_begin_timer);
    }
    if (s_wifi_begin_timer) {
        esp_timer_stop(s_wifi_begin_timer);
        esp_timer_start_periodic(s_wifi_begin_timer, 2000000); /* 2 s */
    }
}

bool net_is_connected(void)
{
    if (!s_events) {
        return false;
    }
    return (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0;
}

bool net_time_is_valid(void)
{
    return s_time_valid;
}

void net_mark_time_valid(void)
{
    s_time_valid = true;
}

const char *net_current_ssid(void)
{
    static char ssid[CFG_SSID_LEN];
    cfg_t c;
    cfg_get(&c);
    strncpy(ssid, c.wifi_ssid, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    return ssid;
}

bool net_wifi_scan_busy(void)
{
    return s_scanning;
}

void net_wifi_ui_active(bool active)
{
    s_wifi_ui_active = active;
    if (active) {
        s_retries = 0;
        if (s_reconnect_timer) {
            esp_timer_stop(s_reconnect_timer);
        }
        return;
    }
    if (cfg_has_wifi() && !net_is_connected() && !s_scanning) {
        s_retries = 0;
        esp_wifi_connect();
    }
}

static void net_scan_finish(void)
{
    s_scanning = false;
    s_retries = 0;
    /* Deliberately do not esp_wifi_connect() here. The Wi-Fi setup screen
     * owns connect after a scan; background retries would fight it. */
}

int net_scan(net_ap_t *out, int max_aps)
{
    if (!out || max_aps <= 0) {
        return -1;
    }
    if (xSemaphoreTake(s_wifi_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "scan skipped: radio busy");
        return -1;
    }

    /* Drop any association or in-flight connect attempt first. While the
     * station is looping on esp_wifi_connect() -- which it does continuously
     * when the configured SSID does not exist -- a scan is pre-empted and
     * comes back with nothing at all. */
    s_scanning = true;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(250));

    wifi_scan_config_t scan = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,               /* all channels */
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = { .min = 120, .max = 300 },
    };
    esp_err_t err = esp_wifi_scan_start(&scan, true);   /* blocking */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        net_scan_finish();
        xSemaphoreGive(s_wifi_lock);
        return -1;
    }

    uint16_t n = MAX_SCAN_APS;
    wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
    if (!recs) {
        net_scan_finish();
        xSemaphoreGive(s_wifi_lock);
        return -1;
    }
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        free(recs);
        net_scan_finish();
        xSemaphoreGive(s_wifi_lock);
        return -1;
    }

    int written = 0;
    for (int i = 0; i < n && written < max_aps; i++) {
        if (recs[i].ssid[0] == '\0') {
            continue;               /* hidden network */
        }
        /* The scan returns one record per BSSID, so a mesh or an extender
         * shows the same name several times. Keep the strongest only. */
        bool dup = false;
        for (int j = 0; j < written; j++) {
            if (strcmp(out[j].ssid, (const char *)recs[i].ssid) == 0) {
                dup = true;
                if (recs[i].rssi > out[j].rssi) {
                    out[j].rssi = recs[i].rssi;
                }
                break;
            }
        }
        if (dup) {
            continue;
        }
        strncpy(out[written].ssid, (const char *)recs[i].ssid,
                sizeof(out[written].ssid) - 1);
        out[written].ssid[sizeof(out[written].ssid) - 1] = '\0';
        out[written].rssi   = recs[i].rssi;
        out[written].secure = (recs[i].authmode != WIFI_AUTH_OPEN);
        written++;
    }
    free(recs);

    net_scan_finish();
    xSemaphoreGive(s_wifi_lock);

    ESP_LOGI(TAG, "scan found %d network(s) (%u raw records)", written,
             (unsigned)n);
    return written;
}

bool net_get_ip(char *buf, size_t len)
{
    if (!buf || len < 8 || !net_is_connected()) {
        return false;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return false;
    }

    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }

    snprintf(buf, len, IPSTR, IP2STR(&info.ip));
    return true;
}

esp_err_t net_apply_credentials(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_wifi_lock, pdMS_TO_TICKS(30000)) != pdTRUE) {
        ESP_LOGW(TAG, "connect skipped: radio busy (scan in progress?)");
        return ESP_ERR_TIMEOUT;
    }

    cfg_set_wifi(ssid, password);

    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wcfg.sta.password, password,
                sizeof(wcfg.sta.password) - 1);
    }

    /* Drop the current association before reconfiguring, and reset the retry
     * counter so a new network gets a full set of attempts. */
    esp_wifi_disconnect();
    s_retries = 0;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    if (err != ESP_OK) {
        xSemaphoreGive(s_wifi_lock);
        return err;
    }
    ESP_LOGI(TAG, "reconnecting to %s", ssid);
    err = esp_wifi_connect();
    xSemaphoreGive(s_wifi_lock);
    return err;
}

int8_t net_get_rssi(void)
{
    if (!net_is_connected()) {
        return 0;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

#endif /* CONFIG_TEMPEST_NETWORK_ENABLED */
