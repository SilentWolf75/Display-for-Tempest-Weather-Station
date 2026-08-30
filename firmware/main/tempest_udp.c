#include "tempest_udp.h"
#include "wx_state.h"
#include "net.h"
#include "tempest_rest.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tempest_udp";

#define RX_BUF_SIZE   1024
#define TASK_STACK    6144
#define TASK_PRIO     5

static TaskHandle_t s_task;
static int          s_sock = -1;
static volatile bool     s_running;
static volatile uint32_t s_packets;

/* obs_st field indices. Order is the wire format; see docs/tempest-api.md. */
enum {
    OBS_TIME = 0, OBS_WIND_LULL, OBS_WIND_AVG, OBS_WIND_GUST, OBS_WIND_DIR,
    OBS_WIND_INTERVAL, OBS_PRESSURE, OBS_TEMP, OBS_HUMIDITY, OBS_ILLUMINANCE,
    OBS_UV, OBS_SOLAR, OBS_RAIN, OBS_PRECIP_TYPE, OBS_STRIKE_DIST,
    OBS_STRIKE_COUNT, OBS_BATTERY, OBS_REPORT_INTERVAL,
    OBS_FIELD_COUNT     /* 18 */
};

/* The hub does emit nulls for a sensor that has faulted, so every slot needs a
 * type check rather than a blind valuedouble read. */
static double arr_num(const cJSON *arr, int idx, double fallback)
{
    const cJSON *item = cJSON_GetArrayItem(arr, idx);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static void handle_obs_st(const cJSON *root)
{
    const cJSON *obs = cJSON_GetObjectItemCaseSensitive(root, "obs");
    if (!cJSON_IsArray(obs)) {
        return;
    }
    const cJSON *o = cJSON_GetArrayItem(obs, 0);
    if (!cJSON_IsArray(o)) {
        return;
    }

    int n = cJSON_GetArraySize(o);
    if (n < OBS_FIELD_COUNT) {
        /* Not fatal. WeatherFlow has appended trailing fields before, and an
         * older hub firmware may send fewer. Decode what is present. */
        ESP_LOGW(TAG, "obs_st has %d fields, expected %d", n, OBS_FIELD_COUNT);
    }

    wx_state_t p = {0};
    p.obs_epoch               = (int64_t)arr_num(o, OBS_TIME, 0);
    p.wind_lull_ms            = (float)arr_num(o, OBS_WIND_LULL, 0);
    p.wind_avg_ms             = (float)arr_num(o, OBS_WIND_AVG, 0);
    p.wind_gust_ms            = (float)arr_num(o, OBS_WIND_GUST, 0);
    p.wind_dir_deg            = (int)arr_num(o, OBS_WIND_DIR, 0);
    p.wind_sample_interval_s  = (int)arr_num(o, OBS_WIND_INTERVAL, 3);
    p.pressure_mb             = (float)arr_num(o, OBS_PRESSURE, 0);
    p.air_temp_c              = (float)arr_num(o, OBS_TEMP, 0);
    p.humidity_pct            = (float)arr_num(o, OBS_HUMIDITY, 0);
    p.illuminance_lux         = (uint32_t)arr_num(o, OBS_ILLUMINANCE, 0);
    p.uv_index                = (float)arr_num(o, OBS_UV, 0);
    p.solar_radiation_wm2     = (float)arr_num(o, OBS_SOLAR, 0);
    p.rain_last_min_mm        = (float)arr_num(o, OBS_RAIN, 0);
    p.precip_type             = (wx_precip_type_t)(int)arr_num(o, OBS_PRECIP_TYPE, 0);
    p.lightning_avg_dist_km   = (float)arr_num(o, OBS_STRIKE_DIST, 0);
    p.lightning_count         = (int)arr_num(o, OBS_STRIKE_COUNT, 0);
    p.battery_v               = (float)arr_num(o, OBS_BATTERY, 0);
    p.report_interval_min     = (int)arr_num(o, OBS_REPORT_INTERVAL, 1);
    wx_update_obs_st(&p);

    if (p.obs_epoch > 1700000000LL) {
        time_t cur = time(NULL);
        if (cur < 1700000000LL) {
            struct timeval tv = { .tv_sec = (time_t)p.obs_epoch, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            net_mark_time_valid();
            tempest_rest_on_clock_sync();
            ESP_LOGI(TAG, "clock synced from Tempest packet: epoch %lld", (long long)p.obs_epoch);
        }
    }

    ESP_LOGI(TAG, "obs_st  %.1fC  %.0f%%RH  %.1fmb  wind %.1f/%.1f m/s @%d",
             p.air_temp_c, p.humidity_pct, p.pressure_mb,
             p.wind_avg_ms, p.wind_gust_ms, p.wind_dir_deg);
}

static void handle_rapid_wind(const cJSON *root)
{
    const cJSON *ob = cJSON_GetObjectItemCaseSensitive(root, "ob");
    if (!cJSON_IsArray(ob) || cJSON_GetArraySize(ob) < 3) {
        return;
    }
    wx_update_rapid_wind((int64_t)arr_num(ob, 0, 0),
                         (float)arr_num(ob, 1, 0),
                         (int)arr_num(ob, 2, 0));
}

static void handle_strike(const cJSON *root)
{
    const cJSON *evt = cJSON_GetObjectItemCaseSensitive(root, "evt");
    if (!cJSON_IsArray(evt) || cJSON_GetArraySize(evt) < 3) {
        return;
    }
    float dist = (float)arr_num(evt, 1, 0);
    wx_update_strike((int64_t)arr_num(evt, 0, 0), dist,
                     (uint32_t)arr_num(evt, 2, 0));
    ESP_LOGW(TAG, "lightning strike %.1f km", dist);
}

static void handle_precip(const cJSON *root)
{
    const cJSON *evt = cJSON_GetObjectItemCaseSensitive(root, "evt");
    if (!cJSON_IsArray(evt) || cJSON_GetArraySize(evt) < 1) {
        return;
    }
    wx_update_precip_start((int64_t)arr_num(evt, 0, 0));
    ESP_LOGI(TAG, "precipitation started");
}

static int obj_int(const cJSON *root, const char *key, int fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : fallback;
}

static void handle_hub_status(const cJSON *root)
{
    wx_update_hub_status(obj_int(root, "rssi", 0),
                         (uint32_t)obj_int(root, "uptime", 0));
}

static void handle_device_status(const cJSON *root)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "voltage");
    wx_update_device_status(obj_int(root, "rssi", 0),
                            cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f,
                            (uint32_t)obj_int(root, "sensor_status", 0));
}

void tempest_ingest_message(const char *json, int len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGD(TAG, "undecodable datagram (%d bytes)", len);
        return;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || !type->valuestring) {
        cJSON_Delete(root);
        return;
    }

    const char *t = type->valuestring;
    if      (strcmp(t, "rapid_wind")    == 0) handle_rapid_wind(root);
    else if (strcmp(t, "obs_st")        == 0) handle_obs_st(root);
    else if (strcmp(t, "evt_strike")    == 0) handle_strike(root);
    else if (strcmp(t, "evt_precip")    == 0) handle_precip(root);
    else if (strcmp(t, "hub_status")    == 0) handle_hub_status(root);
    else if (strcmp(t, "device_status") == 0) handle_device_status(root);
    else ESP_LOGD(TAG, "ignoring message type %s", t);

    cJSON_Delete(root);
}

static void udp_task(void *arg)
{
    (void)arg;

    char *buf = malloc(RX_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "no memory for rx buffer");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (s_running) {
        struct sockaddr_in addr = {
            .sin_family      = AF_INET,
            .sin_port        = htons(CONFIG_TEMPEST_UDP_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };

        s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s_sock < 0) {
            ESP_LOGE(TAG, "socket() failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        int yes = 1;
        setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        /* Required for the hub's 255.255.255.255 traffic to reach us. If
         * Milestone 2 shows silence, this option -- and whatever the C6 does
         * with broadcast frames -- is the first thing to interrogate. */
        setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

        /* Bounded receive so the task can notice s_running going false. */
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            ESP_LOGE(TAG, "bind(:%d) failed: errno %d",
                     CONFIG_TEMPEST_UDP_PORT, errno);
            close(s_sock);
            s_sock = -1;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        ESP_LOGI(TAG, "listening on UDP %d", CONFIG_TEMPEST_UDP_PORT);

        while (s_running) {
            struct sockaddr_storage src;
            socklen_t srclen = sizeof(src);
            int len = recvfrom(s_sock, buf, RX_BUF_SIZE - 1, 0,
                               (struct sockaddr *)&src, &srclen);
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;       /* just the 5 s timeout, keep waiting */
                }
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;              /* rebuild the socket */
            }
            buf[len] = '\0';
            s_packets++;
            wx_note_udp_packet();
            tempest_ingest_message(buf, len);
        }

        if (s_sock >= 0) {
            shutdown(s_sock, 0);
            close(s_sock);
            s_sock = -1;
        }
    }

    free(buf);
    ESP_LOGI(TAG, "listener stopped");
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t tempest_udp_start(void)
{
    if (s_task) {
        return ESP_ERR_INVALID_STATE;
    }
    s_running = true;
    if (xTaskCreate(udp_task, "tempest_udp", TASK_STACK, NULL,
                    TASK_PRIO, &s_task) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void tempest_udp_stop(void)
{
    s_running = false;
}

uint32_t tempest_udp_packet_count(void)
{
    return s_packets;
}
