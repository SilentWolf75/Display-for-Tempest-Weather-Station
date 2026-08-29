#include "web_server.h"
#include "wx_state.h"
#include "config.h"
#include "net.h"
#include "audio.h"
#include "ota.h"
#include "nws_alerts.h"
#include "history.h"
#include "tempest_ws.h"
#include "sdcard.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

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
".alert-banner{display:none;background:#7f1d1d;color:#fecaca;padding:10px 14px;border-radius:8px;margin-bottom:12px;font-size:14px;font-weight:600;}"
".forecast{display:grid;grid-template-columns:repeat(7,1fr);gap:8px;margin:16px 0;}"
".fc-day{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:10px;text-align:center;}"
".fc-day .day{font-size:12px;color:var(--dim);margin-bottom:4px;}"
".fc-day .hi{font-size:18px;font-weight:700;}"
".fc-day .lo{font-size:13px;color:var(--dim);}"
".fc-day .pop{font-size:11px;color:var(--accent);margin-top:4px;}"
".chart-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px;}"
".chart-wrap{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:12px;}"
"canvas{width:100%;height:100px;display:block;}"
"</style></head><body>"
"<div id='alert_banner' class='alert-banner'></div>"
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
"<div class='forecast' id='forecast_row'></div>"
"<div class='chart-grid'>"
"<div class='chart-wrap'><div class='label' style='margin-bottom:8px'>24h Temperature</div>"
"<canvas id='chart_temp' width='440' height='100'></canvas></div>"
"<div class='chart-wrap'><div class='label' style='margin-bottom:8px'>24h Pressure</div>"
"<canvas id='chart_pres' width='440' height='100'></canvas></div>"
"<div class='chart-wrap'><div class='label' style='margin-bottom:8px'>24h Wind</div>"
"<canvas id='chart_wind' width='440' height='100'></canvas></div>"
"<div class='chart-wrap'><div class='label' style='margin-bottom:8px'>24h Humidity</div>"
"<canvas id='chart_hum' width='440' height='100'></canvas></div>"
"</div>"
"<p style='font-size:13px;color:var(--dim);margin-top:12px'>"
"<a href='/api/logs' style='color:var(--accent)'>Download CSV log</a> &middot; "
"<a href='/ota' style='color:var(--accent)'>Firmware update</a></p>"
"<script>"
"function n(v,f){return(v==null||Number.isNaN(v))?null:v.toFixed(f);}"
"function t(v,s){return(v==null||Number.isNaN(v))?'--':String(v)+s;}"
"async function update(){"
"try{"
"let r=await fetch('/api/status');"
"if(!r.ok)throw new Error('status '+r.status);"
"let d=await r.json();"
"if(!d.obs_valid){document.getElementById('station_status').innerText='● Waiting for data';"
"document.getElementById('station_status').style.color='var(--warn)';return;}"
"let uT=d.u_temp||'°F',uW=' '+(d.u_wind||'mph'),uP=' '+(d.u_pres||'inHg'),uR=' '+(d.u_rain||'in');"
"document.getElementById('temp').innerText=t(n(d.temp,1),uT);"
"document.getElementById('feels').innerText='Feels like '+t(n(d.feels,1),uT);"
"document.getElementById('humidity').innerText=t(n(d.humidity,0),'%');"
"document.getElementById('dew').innerText='Dew point '+t(n(d.dew,1),uT);"
"document.getElementById('wind').innerText=t(n(d.wind,1),uW);"
"document.getElementById('wind_dir').innerText=(d.wind_dir||'--')+' ('+(d.wind_deg!=null?d.wind_deg:'--')+'°)';"
"document.getElementById('pressure').innerText=t(n(d.pressure,2),uP);"
"document.getElementById('trend').innerText='Trend: '+(d.trend_str||'Steady');"
"document.getElementById('rain').innerText=t(n(d.rain_today,2),uR);"
"document.getElementById('rain_rate').innerText='Rate: '+t(n(d.rain_rate,2),uR+'/hr');"
"document.getElementById('solar').innerText=t(n(d.solar_wm2,0),' W/m²');"
"document.getElementById('uv').innerText='UV Index: '+t(n(d.uv,1),'');"
"document.getElementById('aqi').innerText=d.aqi>0?d.aqi:'--';"
"document.getElementById('aqi_cat').innerText=d.aqi_cat||'EPA Index';"
"document.getElementById('battery').innerText=t(n(d.battery_v,2),' V');"
"document.getElementById('hub_rssi').innerText='Hub RSSI: '+(d.hub_rssi!=null?d.hub_rssi:'--')+' dBm';"
"let st=document.getElementById('station_status');"
"if(!d.wifi_connected){st.innerText='● No Wi-Fi';st.style.color='var(--alert)';}"
"else if(d.ws_fallback){st.innerText='● WebSocket fallback';st.style.color='var(--warn)';}"
"else if(d.udp_stale){st.innerText='● No UDP';st.style.color='var(--warn)';}"
"else if(d.obs_stale){st.innerText='● Stale data';st.style.color='var(--warn)';}"
"else{st.innerText='● Live';st.style.color='var(--ok)';}"
"if(d.indoor_valid){"
"document.getElementById('indoor_temp').innerText=t(n(d.indoor_temp,1),uT);"
"document.getElementById('indoor_hum').innerText='Humidity '+t(n(d.indoor_humidity,0),'%');"
"}else{"
"document.getElementById('indoor_temp').innerText='--°';"
"document.getElementById('indoor_hum').innerText='No Grove sensor';"
"}"
"let ab=document.getElementById('alert_banner');"
"if(d.nws_alert){ab.style.display='block';ab.innerText='Warning: '+d.nws_alert;"
"ab.style.background=d.nws_severe?'#7f1d1d':'#78350f';}else{ab.style.display='none';}"
"let fr=document.getElementById('forecast_row');fr.replaceChildren();"
"if(Array.isArray(d.daily)){d.daily.forEach(function(x,i){"
"let el=document.createElement('div');el.className='fc-day';"
"let day=document.createElement('div');day.className='day';day.textContent=i===0?'Today':(x.day||'--');"
"let hi=document.createElement('div');hi.className='hi';hi.textContent=t(n(x.hi,0),uT);"
"let lo=document.createElement('div');lo.className='lo';lo.textContent=t(n(x.lo,0),uT);"
"el.appendChild(day);el.appendChild(hi);el.appendChild(lo);"
"if(x.pop>0){let pop=document.createElement('div');pop.className='pop';pop.textContent=x.pop+'%';el.appendChild(pop);}"
"fr.appendChild(el);});}"
"}catch(e){console.error(e);document.getElementById('station_status').innerText='● API error';"
"document.getElementById('station_status').style.color='var(--alert)';}}"
"function drawChart(id,data,color){"
"let c=document.getElementById(id);if(!c||!Array.isArray(data))return;"
"let ctx=c.getContext('2d');ctx.clearRect(0,0,c.width,c.height);"
"let pts=data.filter(function(v){return v!=null;});if(pts.length<2)return;"
"let mn=Math.min.apply(null,pts),mx=Math.max.apply(null,pts),pad=4;"
"ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();"
"let j=0;for(let i=0;i<data.length;i++){if(data[i]==null)continue;"
"let x=pad+(j/(pts.length-1))*(c.width-2*pad);"
"let y=c.height-pad-((data[i]-mn)/(mx-mn||1))*(c.height-2*pad);"
"if(j===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);j++;}ctx.stroke();}"
"async function updateHistory(){"
"try{let r=await fetch('/api/history');if(!r.ok)return;let h=await r.json();"
"drawChart('chart_temp',h.temp,'#38bdf8');"
"drawChart('chart_pres',h.pressure,'#26c6da');"
"drawChart('chart_wind',h.wind,'#00e5ff');"
"drawChart('chart_hum',h.humidity,'#ba68c8');"
"}catch(e){console.error(e);}}"
"setInterval(update,3000);setInterval(updateHistory,60000);update();updateHistory();"
"</script></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, HTML_PAGE, sizeof(HTML_PAGE) - 1);
}

static void web_add_units(cJSON *root)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "\xC2\xB0%s", cfg_temp_suffix());
    cJSON_AddStringToObject(root, "u_temp", buf);
    cJSON_AddStringToObject(root, "u_wind", cfg_wind_suffix());
    cJSON_AddStringToObject(root, "u_pres", cfg_pressure_suffix());
    cJSON_AddStringToObject(root, "u_rain", cfg_rain_suffix());
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    /* Static snapshot — wx_state_t is ~2 KB; keep it off the httpd stack. */
    static wx_state_t s;
    wx_snapshot(&s);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(root, "obs_valid", s.obs_valid);
    web_add_units(root);
    if (!s.obs_valid) {
        cJSON_AddBoolToObject(root, "wifi_connected", s.wifi_connected);
        cJSON_AddBoolToObject(root, "udp_stale", wx_udp_is_stale(&s));
        char *json_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!json_str) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
            return ESP_FAIL;
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        esp_err_t res = httpd_resp_send(req, json_str, strlen(json_str));
        free(json_str);
        return res;
    }

    cJSON_AddNumberToObject(root, "temp", (double)cfg_temp(s.air_temp_c));
    cJSON_AddNumberToObject(root, "feels", (double)cfg_temp(s.feels_like_c));
    cJSON_AddNumberToObject(root, "dew", (double)cfg_temp(s.dew_point_c));
    cJSON_AddNumberToObject(root, "humidity", (double)s.humidity_pct);
    cJSON_AddNumberToObject(root, "wind", (double)cfg_wind(s.wind_avg_ms));
    cJSON_AddNumberToObject(root, "wind_deg", s.wind_dir_deg);
    cJSON_AddStringToObject(root, "wind_dir", wx_compass_point(s.wind_dir_deg));
    cJSON_AddNumberToObject(root, "pressure", (double)cfg_pressure(s.pressure_mb));
    cJSON_AddStringToObject(root, "trend_str", wx_trend_description(s.pressure_trend));
    cJSON_AddNumberToObject(root, "rain_today", (double)cfg_rain(s.rain_today_mm));
    cJSON_AddNumberToObject(root, "rain_rate", (double)cfg_rain(s.rain_rate_mm_hr));
    cJSON_AddNumberToObject(root, "solar_wm2", (double)s.solar_radiation_wm2);
    cJSON_AddNumberToObject(root, "uv", (double)s.uv_index);
    cJSON_AddNumberToObject(root, "aqi", s.aqi_valid ? s.aqi_val : 0);
    cJSON_AddStringToObject(root, "aqi_cat", s.aqi_valid ? s.aqi_category : "Unknown");
    cJSON_AddNumberToObject(root, "battery_v", (double)s.battery_v);
    cJSON_AddNumberToObject(root, "hub_rssi", s.hub_rssi);
    cJSON_AddBoolToObject(root, "indoor_valid", s.indoor_valid);
    if (s.indoor_valid) {
        cJSON_AddNumberToObject(root, "indoor_temp", (double)cfg_temp(s.indoor_temp_c));
        cJSON_AddNumberToObject(root, "indoor_humidity", (double)s.indoor_humidity_pct);
    }
    cJSON_AddBoolToObject(root, "obs_stale", wx_obs_is_stale(&s));
    cJSON_AddBoolToObject(root, "udp_stale", wx_udp_is_stale(&s));
    cJSON_AddBoolToObject(root, "wifi_connected", s.wifi_connected);
    cJSON_AddBoolToObject(root, "ws_fallback", tempest_ws_is_active());

    nws_alert_t alert = {0};
    if (nws_alerts_get_active(&alert)) {
        cJSON_AddStringToObject(root, "nws_alert", alert.event);
        cJSON_AddBoolToObject(root, "nws_severe",
                              strcmp(alert.severity, "Extreme") == 0 ||
                              strcmp(alert.severity, "Severe") == 0);
    }

    cJSON *fc_arr = cJSON_AddArrayToObject(root, "daily");
    for (int i = 0; i < s.forecast_days && i < 7; i++) {
        cJSON *d = cJSON_CreateObject();
        if (!d) {
            break;
        }
        time_t dt = (time_t)s.forecast[i].day_start_local;
        struct tm tm_day;
        char day_name[8] = "--";
        if (dt > 0) {
            localtime_r(&dt, &tm_day);
            strftime(day_name, sizeof(day_name), "%a", &tm_day);
        }
        cJSON_AddStringToObject(d, "day", day_name);
        cJSON_AddNumberToObject(d, "hi", (double)cfg_temp(s.forecast[i].air_temp_high_c));
        cJSON_AddNumberToObject(d, "lo", (double)cfg_temp(s.forecast[i].air_temp_low_c));
        cJSON_AddNumberToObject(d, "pop", s.forecast[i].precip_probability);
        cJSON_AddItemToArray(fc_arr, d);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return res;
}

static float conv_temp_c(float c)   { return cfg_temp(c); }
static float conv_pres_mb(float mb) { return cfg_pressure(mb); }
static float conv_wind_ms(float ms) { return cfg_wind(ms); }
static float conv_hum_pct(float h)  { return h; }

static void history_add_series(cJSON *root, const char *key,
                               hist_series_t series, float (*conv)(float))
{
    static float buf[HIST_BUCKETS];
    int n = history_get(series, buf, HIST_BUCKETS, NULL, NULL);
    cJSON *arr = cJSON_AddArrayToObject(root, key);
    if (!arr) {
        return;
    }
    for (int i = 0; i < n; i += 3) {
        if (isnan(buf[i])) {
            cJSON_AddItemToArray(arr, cJSON_CreateNull());
        } else {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)conv(buf[i])));
        }
    }
}

static esp_err_t api_history_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    history_add_series(root, "temp", HIST_TEMP, conv_temp_c);
    history_add_series(root, "pressure", HIST_PRESSURE, conv_pres_mb);
    history_add_series(root, "wind", HIST_WIND, conv_wind_ms);
    history_add_series(root, "humidity", HIST_HUMIDITY, conv_hum_pct);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return res;
}

static esp_err_t api_logs_handler(httpd_req_t *req)
{
    char path[128];
    esp_err_t err = sdcard_current_log_path(path, sizeof(path));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "SD card not available");
        return ESP_FAIL;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Log file not found");
        return ESP_FAIL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot open log");
        return ESP_FAIL;
    }

    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;
    char disp[160];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", fname);

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char buf[1024];
    esp_err_t res = ESP_OK;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            res = ESP_FAIL;
            break;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
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
    config.stack_size = 12288;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;

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

    httpd_uri_t history_uri = {
        .uri       = "/api/history",
        .method    = HTTP_GET,
        .handler   = api_history_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &history_uri);

    httpd_uri_t logs_uri = {
        .uri       = "/api/logs",
        .method    = HTTP_GET,
        .handler   = api_logs_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &logs_uri);

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
