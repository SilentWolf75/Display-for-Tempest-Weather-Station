#include "net.h"
#include "wx_state.h"

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

static const char *TAG = "net";

#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1
#define MAX_RETRY            10
#define CONNECT_TIMEOUT_MS   30000

static EventGroupHandle_t s_events;
static int  s_retries;
static bool s_time_valid;

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wx_set_wifi_connected(false);
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);

        if (s_retries < MAX_RETRY) {
            s_retries++;
            ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retries, MAX_RETRY);
            esp_wifi_connect();
        } else {
            /* Do not give up permanently -- this is a wall display that has to
             * come back on its own after a router reboot. Back off and keep
             * trying, but let net_start() return so the UI can come up and say
             * "no network" instead of hanging at a blank screen. */
            ESP_LOGE(TAG, "connect failed %d times, backing off to 30s", MAX_RETRY);
            xEventGroupSetBits(s_events, WIFI_FAIL_BIT);
            vTaskDelay(pdMS_TO_TICKS(30000));
            s_retries = 0;
            esp_wifi_connect();
        }
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&event->ip_info.ip));
    s_retries = 0;
    wx_set_wifi_connected(true);
    xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
}

static void on_time_sync(struct timeval *tv)
{
    s_time_valid = true;
    ESP_LOGI(TAG, "clock synchronised");
}

static void start_sntp(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_time_sync;
    cfg.start   = true;
    cfg.server_from_dhcp = true;
    esp_netif_sntp_init(&cfg);

    /* TZ is a display concern -- sunrise/sunset from the REST feed arrive as
     * epochs. Set this to your zone; POSIX TZ string, note the inverted sign.
     * TODO: make this a Kconfig option once the units toggle exists. */
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
}

esp_err_t net_start(void)
{
    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        /* The single most likely failure on this board. Say so plainly rather
         * than letting ESP_ERROR_CHECK abort with a bare error code. */
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

    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, CONFIG_TEMPEST_WIFI_SSID,
            sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, CONFIG_TEMPEST_WIFI_PASSWORD,
            sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    /* A wall display has no battery concern and must not miss broadcast
     * frames while dozing. Power save off is deliberate. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to %s", CONFIG_TEMPEST_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        start_sntp();
        return ESP_OK;
    }
    ESP_LOGW(TAG, "not connected yet; continuing, retries run in background");
    return ESP_ERR_TIMEOUT;
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
