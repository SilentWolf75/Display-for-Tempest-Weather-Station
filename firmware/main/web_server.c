#include "web_server.h"
#include "wx_state.h"
#include "config.h"
#include "net.h"
#include "audio.h"
#include "sdcard.h"
#include "ota.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "mdns.h"
#include "cJSON.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;
static bool           s_mdns_started;

#define WEB_PORT 8080

static esp_err_t mdns_start(void)
{
    if (s_mdns_started) {
        return ESP_OK;
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mdns init failed: %s", esp_err_to_name(err));
        return err;
    }

    mdns_hostname_set("tempest");
    mdns_instance_name_set("Tempest Weather Display");

    err = mdns_service_add(NULL, "_http", "_tcp", WEB_PORT, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns service add failed: %s", esp_err_to_name(err));
        return err;
    }

    s_mdns_started = true;
    ESP_LOGI(TAG, "mdns registered: http://tempest.local:%d", WEB_PORT);
    return ESP_OK;
}

static void mdns_stop(void)
{
    if (!s_mdns_started) {
        return;
    }
    mdns_service_remove("_http", "_tcp");
    mdns_free();
    s_mdns_started = false;
}

static const char HTML_PAGE[] = 
"<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Tempest Weather Console</title>"
"<style>"
":root{--bg:#0b0f19;--card:#151d2c;--border:#243048;--text:#f8fafc;--dim:#94a3b8;--accent:#38bdf8;--ok:#10b981;--warn:#f59e0b;--alert:#ef4444;}"
"body{margin:0;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text);padding:16px;}"
".header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;border-bottom:1px solid var(--border);padding-bottom:12px;}"
".title{font-size:22px;font-weight:700;color:var(--accent);}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-bottom:16px;}"
".card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:16px;display:flex;flex-direction:column;}"
".label{font-size:13px;font-weight:600;color:var(--dim);text-transform:uppercase;letter-spacing:0.05em;margin-bottom:6px;}"
".val{font-size:32px;font-weight:700;color:var(--text);}"
".sub{font-size:13px;color:var(--dim);margin-top:6px;}"
".badge{display:inline-block;padding:2px 8px;border-radius:12px;font-size:12px;font-weight:600;margin-top:4px;}"
".badge-ok{background:#064e3b;color:#34d399;}"
".badge-warn{background:#78350f;color:#fbbf24;}"
"</style></head><body>"
"<div class='header'>"
"<div class='title'>⛈️ Tempest Weather Console</div>"
"<div id='station_status' style='font-size:13px;color:var(--ok);'>● Connected</div>"
"</div>"
"<div class='grid'>"
"<div class='card'><div class='label'>Temperature</div><div class='val' id='temp'>--°</div><div class='sub' id='feels'>Feels like --°</div></div>"
"<div class='card'><div class='label'>Humidity / Dew</div><div class='val' id='humidity'>--%</div><div class='sub' id='dew'>Dew point --°</div></div>"
"<div class='card'><div class='label'>Wind Speed</div><div class='val' id='wind'>-- mph</div><div class='sub' id='wind_dir'>Direction --</div></div>"
"<div class='card'><div class='label'>Barometer</div><div class='val' id='pressure'>-- inHg</div><div class='sub' id='trend'>Trend: Steady</div></div>"
"<div class='card'><div class='label'>Rain Today</div><div class='val' id='rain'>-- in</div><div class='sub' id='rain_rate'>Rate: 0.00 in/hr</div></div>"
"<div class='card'><div class='label'>Solar / UV</div><div class='val' id='solar'>-- W/m²</div><div class='sub' id='uv'>UV Index --</div></div>"
"<div class='card'><div class='label'>Air Quality</div><div class='val' id='aqi'>--</div><div class='sub' id='aqi_cat'>EPA Index</div></div>"
"<div class='card'><div class='label'>Station Health</div><div class='val' id='battery'>-- V</div><div class='sub' id='hub_rssi'>Hub RSSI: -- dBm</div></div>"
"<div class='card'><div class='label'>Indoor</div><div class='val' id='indoor_temp'>--°</div><div class='sub' id='indoor_hum'>Humidity --%</div></div>"
"</div>"
"<p style='font-size:13px;color:var(--dim);margin-top:12px'>"
"<a href='/ota' style='color:var(--accent)'>Firmware update</a></p>"
"<script>"
"async function update(){"
"try{"
"let r=await fetch('/api/status');let d=await r.json();"
"document.getElementById('temp').innerText=d.temp_f.toFixed(1)+'°F';"
"document.getElementById('feels').innerText='Feels like '+d.feels_like_f.toFixed(1)+'°F';"
"document.getElementById('humidity').innerText=d.humidity.toFixed(0)+'%';"
"document.getElementById('dew').innerText='Dew point '+d.dew_f.toFixed(1)+'°F';"
"document.getElementById('wind').innerText=d.wind_mph.toFixed(1)+' mph';"
"document.getElementById('wind_dir').innerText=d.wind_dir+' ('+d.wind_deg+'°)';"
"document.getElementById('pressure').innerText=d.pressure_inhg.toFixed(2)+' inHg';"
"document.getElementById('trend').innerText='Trend: '+d.trend_str;"
"document.getElementById('rain').innerText=d.rain_today_in.toFixed(2)+' in';"
"document.getElementById('solar').innerText=d.solar_wm2.toFixed(0)+' W/m²';"
"document.getElementById('uv').innerText='UV Index: '+d.uv.toFixed(1);"
"document.getElementById('aqi').innerText=d.aqi>0?d.aqi:'--';"
"document.getElementById('aqi_cat').innerText=d.aqi_cat||'EPA Index';"
"document.getElementById('battery').innerText=d.battery_v.toFixed(2)+' V';"
"document.getElementById('hub_rssi').innerText='Hub RSSI: '+d.hub_rssi+' dBm';"
"let st=document.getElementById('station_status');"
"if(!d.wifi_connected){st.innerText='● No Wi-Fi';st.style.color='var(--alert)';}"
"else if(d.udp_stale){st.innerText='● No UDP';st.style.color='var(--warn)';}"
"else if(d.obs_stale){st.innerText='● Stale data';st.style.color='var(--warn)';}"
"else{st.innerText='● Live';st.style.color='var(--ok)';}"
"if(d.indoor_valid){document.getElementById('indoor_temp').innerText=d.indoor_temp_f.toFixed(1)+'°F';"
"document.getElementById('indoor_hum').innerText='Humidity '+d.indoor_humidity.toFixed(0)+'%';}"
"else{document.getElementById('indoor_temp').innerText='--°';document.getElementById('indoor_hum').innerText='No Grove sensor';}"
"}catch(e){}}"
"setInterval(update,3000);update();"
"</script></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, HTML_PAGE, sizeof(HTML_PAGE) - 1);
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    wx_state_t s;
    wx_snapshot(&s);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temp_f", (double)wx_c_to_f(s.air_temp_c));
    cJSON_AddNumberToObject(root, "feels_like_f", (double)wx_c_to_f(s.feels_like_c));
    cJSON_AddNumberToObject(root, "dew_f", (double)wx_c_to_f(s.dew_point_c));
    cJSON_AddNumberToObject(root, "humidity", (double)s.humidity_pct);
    cJSON_AddNumberToObject(root, "wind_mph", (double)wx_ms_to_mph(s.wind_avg_ms));
    cJSON_AddNumberToObject(root, "wind_gust_mph", (double)wx_ms_to_mph(s.wind_gust_ms));
    cJSON_AddNumberToObject(root, "wind_deg", s.wind_dir_deg);
    cJSON_AddStringToObject(root, "wind_dir", wx_compass_point(s.wind_dir_deg));
    cJSON_AddNumberToObject(root, "pressure_inhg", (double)wx_mb_to_inhg(s.pressure_mb));
    cJSON_AddStringToObject(root, "trend_str", wx_trend_description(s.pressure_trend));
    cJSON_AddNumberToObject(root, "rain_today_in", (double)wx_mm_to_in(s.rain_today_mm));
    cJSON_AddNumberToObject(root, "solar_wm2", (double)s.solar_radiation_wm2);
    cJSON_AddNumberToObject(root, "uv", (double)s.uv_index);
    cJSON_AddNumberToObject(root, "aqi", s.aqi_valid ? s.aqi_val : 0);
    cJSON_AddStringToObject(root, "aqi_cat", s.aqi_valid ? s.aqi_category : "Unknown");
    cJSON_AddNumberToObject(root, "battery_v", (double)s.battery_v);
    cJSON_AddNumberToObject(root, "hub_rssi", s.hub_rssi);
    cJSON_AddBoolToObject(root, "indoor_valid", s.indoor_valid);
    if (s.indoor_valid) {
        cJSON_AddNumberToObject(root, "indoor_temp_f", (double)wx_c_to_f(s.indoor_temp_c));
        cJSON_AddNumberToObject(root, "indoor_humidity", (double)s.indoor_humidity_pct);
    }
    cJSON_AddBoolToObject(root, "obs_stale", wx_obs_is_stale(&s));
    cJSON_AddBoolToObject(root, "udp_stale", wx_udp_is_stale(&s));
    cJSON_AddBoolToObject(root, "wifi_connected", s.wifi_connected);
    cJSON_AddNumberToObject(root, "udp_packets", s.udp_packets_total);

    char ip[16];
    if (net_get_ip(ip, sizeof(ip))) {
        cJSON_AddStringToObject(root, "ip", ip);
    }
    cJSON_AddNumberToObject(root, "forecast_days", s.forecast_days);
    cJSON_AddBoolToObject(root, "forecast_valid", s.forecast_valid);
    cJSON_AddBoolToObject(root, "forecast_stale", wx_forecast_is_stale(&s));

    sdcard_info_t sdi;
    if (sdcard_get_info(&sdi) == ESP_OK && sdi.mounted) {
        cJSON_AddBoolToObject(root, "sd_mounted", true);
        cJSON_AddStringToObject(root, "sd_name", sdi.name);
        cJSON_AddNumberToObject(root, "sd_total_mb", (double)(sdi.total_bytes / (1024 * 1024)));
        cJSON_AddNumberToObject(root, "sd_free_mb", (double)(sdi.free_bytes / (1024 * 1024)));
        cJSON_AddNumberToObject(root, "sd_log_files", sdi.log_files);
    } else {
        cJSON_AddBoolToObject(root, "sd_mounted", false);
    }

    cJSON *fc_arr = cJSON_AddArrayToObject(root, "daily");
    for (int i = 0; i < s.forecast_days && i < 7; i++) {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddNumberToObject(d, "hi_f", (double)wx_c_to_f(s.forecast[i].air_temp_high_c));
        cJSON_AddNumberToObject(d, "lo_f", (double)wx_c_to_f(s.forecast[i].air_temp_low_c));
        cJSON_AddStringToObject(d, "cond", s.forecast[i].conditions);
        cJSON_AddStringToObject(d, "icon", s.forecast[i].icon);
        cJSON_AddNumberToObject(d, "pop", s.forecast[i].precip_probability);
        cJSON_AddItemToArray(fc_arr, d);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return res;
}

static esp_err_t api_chime_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Triggering test hourly chime via API");
    audio_play_chime();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"status\":\"ok\",\"sound\":\"Hourly Chime\"}", -1);
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_PORT;
    config.ctrl_port = WEB_PORT + 1;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port 8080 (http://tempest.local:8080)...");
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = root_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &root_uri);

    httpd_uri_t status_uri = {
        .uri       = "/api/status",
        .method    = HTTP_GET,
        .handler   = api_status_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &status_uri);

    httpd_uri_t chime_uri = {
        .uri       = "/api/chime",
        .method    = HTTP_GET,
        .handler   = api_chime_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &chime_uri);

    mdns_start();

#if CONFIG_OTA_ENABLED
    ota_register(s_server);
#endif

    char ip[16];
    if (net_get_ip(ip, sizeof(ip))) {
        ESP_LOGI(TAG, "web dashboard: http://%s:%d/ (mdns: http://tempest.local:%d/)",
                 ip, WEB_PORT, WEB_PORT);
    } else {
        ESP_LOGI(TAG, "web dashboard on port %d (mdns: http://tempest.local:%d/)",
                 WEB_PORT, WEB_PORT);
    }
    return ESP_OK;
}

void web_server_stop(void)
{
    mdns_stop();
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
