#include "tempest_ws.h"
#include "tempest_udp.h"
#include "tempest_rest.h"
#include "wx_state.h"
#include "net.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"

#include "credentials_config.h"

static const char *TAG = "tempest_ws";

static esp_websocket_client_handle_t s_client;
static volatile bool           s_active;
static volatile bool           s_running;

static void ws_disconnect(void)
{
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    s_active = false;
}

static void ws_send_listen(int device_id)
{
    if (!s_client || device_id <= 0) {
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"listen_start\",\"device_id\":%d,\"id\":\"tempest-ui\"}",
             device_id);
    esp_websocket_client_send_text(s_client, buf, strlen(buf), pdMS_TO_TICKS(2000));

    snprintf(buf, sizeof(buf),
             "{\"type\":\"listen_rapid_start\",\"device_id\":%d,\"id\":\"tempest-rapid\"}",
             device_id);
    esp_websocket_client_send_text(s_client, buf, strlen(buf), pdMS_TO_TICKS(2000));
}

static void ws_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;

    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "connected to WeatherFlow WebSocket");
        int dev = tempest_rest_device_id();
        if (dev <= 0) {
            tempest_rest_ensure_device_id();
            dev = tempest_rest_device_id();
        }
        ws_send_listen(dev);
        s_active = true;
        break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        s_active = false;
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == WS_TRANSPORT_OPCODES_TEXT && data->data_len > 0) {
            tempest_ingest_message(data->data_ptr, data->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "WebSocket error");
        s_active = false;
        break;
    default:
        break;
    }
}

static esp_err_t ws_connect(void)
{
    if (s_client || TEMPEST_API_TOKEN[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    if (tempest_rest_ensure_device_id() != ESP_OK) {
        ESP_LOGW(TAG, "cannot start WebSocket without device_id");
        return ESP_FAIL;
    }

    char url[160];
    snprintf(url, sizeof(url),
             "wss://ws.weatherflow.com/swd/data?token=%s", TEMPEST_API_TOKEN);

    esp_websocket_client_config_t cfg = {
        .uri                = url,
        .crt_bundle_attach  = esp_crt_bundle_attach,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = 15000,
    };

    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);

    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(err));
        ws_disconnect();
        return err;
    }

    ESP_LOGI(TAG, "WebSocket fallback active (UDP silent)");
    return ESP_OK;
}

void tempest_ws_poll(void)
{
    if (!s_running) {
        return;
    }

    if (!net_is_connected() || TEMPEST_API_TOKEN[0] == '\0') {
        if (s_client) {
            ws_disconnect();
        }
        return;
    }

    wx_state_t snap;
    wx_snapshot(&snap);

    if (!wx_udp_is_stale(&snap)) {
        if (s_client) {
            ESP_LOGI(TAG, "UDP resumed — closing WebSocket fallback");
            ws_disconnect();
        }
        return;
    }

    if (!s_client && snap.wifi_connected) {
        ws_connect();
    }
}

esp_err_t tempest_ws_start(void)
{
    if (TEMPEST_API_TOKEN[0] == '\0') {
        ESP_LOGI(TAG, "no API token — WebSocket fallback disabled");
        return ESP_OK;
    }
    s_running = true;
    return ESP_OK;
}

bool tempest_ws_is_active(void)
{
    return s_active;
}
