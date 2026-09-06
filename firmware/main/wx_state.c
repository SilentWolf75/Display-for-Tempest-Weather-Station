#include <stdio.h>
#include "wx_state.h"
#include "history.h"
#include "wx_daily.h"
#include "nvs.h"
#include "esp_timer.h"
#include "wx_astronomy.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "wx_state";

void ui_notify_forecast_updated(void);

static wx_state_t        s_state;
static SemaphoreHandle_t s_lock;
static wx_daily_t s_daily;
static int64_t s_last_checkpoint_us;
static int64_t s_saved_epoch;

#define LOCK()    xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK()  xSemaphoreGive(s_lock)

esp_err_t wx_state_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    memset(&s_state, 0, sizeof(s_state));
    nvs_handle_t h;
    if (nvs_open("wx_daily", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_daily);
        if (nvs_get_blob(h, "totals", &s_daily, &len) != ESP_OK ||
            len != sizeof(s_daily) || s_daily.version != 1) {
            memset(&s_daily, 0, sizeof(s_daily));
        }
        nvs_close(h);
    }
    s_saved_epoch = s_daily.last_epoch;
    return ESP_OK;
}

void wx_snapshot(wx_state_t *out)
{
    LOCK();
    memcpy(out, &s_state, sizeof(*out));
    wx_daily_project(&s_daily, (int64_t)time(NULL), out);
    if (wx_hourly_is_stale(out)) out->hourly_valid = false;
    UNLOCK();
}

/* --------------------------------------------------------------------------
 * History. The UDP feed is instantaneous-only, so anything cumulative or
 * time-windowed has to be accumulated here.
 * -------------------------------------------------------------------------- */

#define PRESSURE_HISTORY   200      /* 3h+ at one obs_st per minute */
#define STRIKE_HISTORY      64
#define TREND_WINDOW_S    (3 * 3600)

typedef struct {
    int64_t epoch;
    float   mb;
} pressure_sample_t;

static pressure_sample_t s_pressure[PRESSURE_HISTORY];
static int  s_pressure_head;
static int  s_pressure_count;

static int64_t s_strikes[STRIKE_HISTORY];
static int  s_strike_head;
static int  s_strike_count;

/* Checkpoint at most every five minutes. A sudden power loss may lose the
 * uncheckpointed tail; totals remain explicitly partial. Called by main only. */
void wx_daily_checkpoint(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_checkpoint_us < 300000000LL) return;
    wx_daily_t saved;
    LOCK();
    saved = s_daily;
    UNLOCK();
    if (!saved.last_epoch || saved.last_epoch == s_saved_epoch) return;
    nvs_handle_t h;
    if (nvs_open("wx_daily", NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t err = nvs_set_blob(h, "totals", &saved, sizeof(saved));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_saved_epoch = saved.last_epoch;
        s_last_checkpoint_us = now;
    }
}

bool wx_obs_values_valid(const wx_state_t *p)
{
    return p && p->obs_epoch >= 1600000000LL &&
        isfinite(p->air_temp_c) && p->air_temp_c >= -100 && p->air_temp_c <= 70 &&
        isfinite(p->humidity_pct) && p->humidity_pct >= 0 && p->humidity_pct <= 100 &&
        isfinite(p->pressure_mb) && p->pressure_mb >= 300 && p->pressure_mb <= 1200 &&
        isfinite(p->wind_avg_ms) && p->wind_avg_ms >= 0 && p->wind_avg_ms <= 150 &&
        isfinite(p->wind_gust_ms) && p->wind_gust_ms >= 0 && p->wind_gust_ms <= 150 &&
        isfinite(p->rain_last_min_mm) && p->rain_last_min_mm >= 0;
}

static void push_pressure(int64_t epoch, float mb)
{
    s_pressure[s_pressure_head].epoch = epoch;
    s_pressure[s_pressure_head].mb    = mb;
    s_pressure_head = (s_pressure_head + 1) % PRESSURE_HISTORY;
    if (s_pressure_count < PRESSURE_HISTORY) {
        s_pressure_count++;
    }
}

/* Compares against the oldest sample at least 3 hours back. Returns false
 * until the buffer actually spans that long -- reporting a "trend" from twenty
 * minutes of data would be worse than reporting none. */
static bool compute_trend(int64_t now, float current_mb, float *delta_out)
{
    int64_t best_age = 0;
    float   best_mb  = 0.0f;
    bool    found    = false;

    for (int i = 0; i < s_pressure_count; i++) {
        const pressure_sample_t *sm = &s_pressure[i];
        int64_t age = now - sm->epoch;
        if (age >= TREND_WINDOW_S && (!found || age < best_age)) {
            best_age = age;
            best_mb  = sm->mb;
            found    = true;
        }
    }
    if (!found) {
        return false;
    }
    *delta_out = current_mb - best_mb;
    return true;
}

static wx_trend_t classify_trend(float delta_mb)
{
    if (delta_mb <= -2.0f) return WX_TREND_FALLING_FAST;
    if (delta_mb <= -0.5f) return WX_TREND_FALLING;
    if (delta_mb >=  2.0f) return WX_TREND_RISING_FAST;
    if (delta_mb >=  0.5f) return WX_TREND_RISING;
    return WX_TREND_STEADY;
}

static int count_recent_strikes(int64_t now)
{
    int n = 0;
    for (int i = 0; i < s_strike_count; i++) {
        if (now >= s_strikes[i] && now - s_strikes[i] <= TREND_WINDOW_S) {
            n++;
        }
    }
    return n;
}

void wx_update_obs_st(const wx_state_t *p)
{
    if (!wx_obs_values_valid(p)) return;
    LOCK();
    if (p->obs_epoch <= s_state.obs_epoch) { UNLOCK(); return; }
    s_state.obs_epoch             = p->obs_epoch;
    s_state.wind_lull_ms          = p->wind_lull_ms;
    s_state.wind_avg_ms           = p->wind_avg_ms;
    s_state.wind_gust_ms          = p->wind_gust_ms;
    s_state.wind_dir_deg          = p->wind_dir_deg;
    s_state.wind_sample_interval_s= p->wind_sample_interval_s;
    s_state.pressure_mb           = p->pressure_mb;
    s_state.air_temp_c            = p->air_temp_c;
    s_state.humidity_pct          = p->humidity_pct;
    s_state.illuminance_lux       = p->illuminance_lux;
    s_state.uv_index              = p->uv_index;
    s_state.solar_radiation_wm2   = p->solar_radiation_wm2;
    s_state.rain_last_min_mm      = p->rain_last_min_mm;
    s_state.precip_type           = p->precip_type;
    s_state.lightning_avg_dist_km = p->lightning_avg_dist_km;
    s_state.lightning_count       = p->lightning_count;
    s_state.battery_v             = p->battery_v;
    s_state.report_interval_min   = p->report_interval_min;
    s_state.obs_valid             = true;

    /* Derive what the UDP feed does not carry. */
    s_state.dew_point_c  = wx_dew_point_c(p->air_temp_c, p->humidity_pct);
    s_state.feels_like_c = wx_feels_like_c(p->air_temp_c, p->humidity_pct,
                                           p->wind_avg_ms);
    s_state.heat_index_c = wx_feels_like_c(p->air_temp_c, p->humidity_pct, 0.0f);
    s_state.wind_chill_c = wx_feels_like_c(p->air_temp_c, 50.0f, p->wind_avg_ms);

    if (p->air_temp_c >= 27.0f) {
        s_state.comfort_mode = WX_COMFORT_HEAT_INDEX;
        strncpy(s_state.comfort_risk,
                wx_heat_index_risk(p->air_temp_c, p->humidity_pct),
                sizeof(s_state.comfort_risk) - 1);
    } else if (p->air_temp_c <= 10.0f && p->wind_avg_ms >= 1.34f) {
        s_state.comfort_mode = WX_COMFORT_WIND_CHILL;
        strncpy(s_state.comfort_risk,
                wx_wind_chill_risk(p->air_temp_c, p->wind_avg_ms),
                sizeof(s_state.comfort_risk) - 1);
    } else {
        s_state.comfort_mode = WX_COMFORT_FEELS;
        strncpy(s_state.comfort_risk, "Comfortable",
                sizeof(s_state.comfort_risk) - 1);
    }

    wx_moon_info_t moon;
    wx_moon_compute(p->obs_epoch, &moon);
    s_state.moon_illumination = moon.illumination;
    snprintf(s_state.moon_phase_name, sizeof(s_state.moon_phase_name), "%.*s", (int)sizeof(s_state.moon_phase_name) - 1, moon.phase_name);
    snprintf(s_state.moon_icon, sizeof(s_state.moon_icon), "%.*s", (int)sizeof(s_state.moon_icon) - 1, moon.icon_slug);

    wx_daily_add(&s_daily, p);
    wx_daily_project(&s_daily, p->obs_epoch, &s_state);
    s_state.rain_rate_mm_hr = p->rain_last_min_mm * 60.0f;

    push_pressure(p->obs_epoch, p->pressure_mb);
    float delta = 0.0f;
    if (compute_trend(p->obs_epoch, p->pressure_mb, &delta)) {
        s_state.pressure_trend_mb_3h = delta;
        s_state.pressure_trend       = classify_trend(delta);
    }

    s_state.strikes_3h = count_recent_strikes(p->obs_epoch);
    UNLOCK();

    /* Outside the lock on purpose: history has its own, and holding both would
     * be a lock-ordering hazard for no benefit. */
    history_add(p->obs_epoch, p->air_temp_c, p->humidity_pct, p->pressure_mb,
                p->wind_avg_ms, p->wind_gust_ms, p->rain_last_min_mm,
                p->uv_index);
    return;
}

void wx_update_rapid_wind(int64_t epoch, float speed_ms, int dir_deg)
{
    if (epoch < 1600000000LL || !isfinite(speed_ms) || speed_ms < 0 || speed_ms > 150) return;
    LOCK();
    if (epoch <= s_state.rapid_epoch) { UNLOCK(); return; }
    s_state.rapid_epoch        = epoch;
    s_state.rapid_wind_ms      = speed_ms;
    s_state.rapid_wind_dir_deg = dir_deg;
    s_state.rapid_valid        = true;
    UNLOCK();
}

void wx_update_strike(int64_t epoch, float dist_km, uint32_t energy)
{
    LOCK();
    if (epoch <= s_state.last_strike_epoch) { UNLOCK(); return; }
    s_state.last_strike_epoch   = epoch;
    s_state.last_strike_dist_km = dist_km;
    s_state.last_strike_energy  = energy;

    s_strikes[s_strike_head] = epoch;
    s_strike_head = (s_strike_head + 1) % STRIKE_HISTORY;
    if (s_strike_count < STRIKE_HISTORY) {
        s_strike_count++;
    }
    s_state.strikes_3h = count_recent_strikes(epoch);
    UNLOCK();
}

void wx_update_precip_start(int64_t epoch)
{
    LOCK();
    s_state.last_precip_epoch = epoch;
    UNLOCK();
}

void wx_update_hub_status(int rssi, uint32_t uptime_s)
{
    LOCK();
    s_state.hub_rssi     = rssi;
    s_state.hub_uptime_s = uptime_s;
    UNLOCK();
}

void wx_update_device_status(int rssi, float voltage, uint32_t sensor_status)
{
    LOCK();
    s_state.device_rssi   = rssi;
    s_state.battery_v     = voltage;
    s_state.sensor_status = sensor_status;
    UNLOCK();
}

void wx_update_forecast(const wx_state_t *p)
{
    LOCK();
    memcpy(s_state.current_conditions, p->current_conditions,
           sizeof(s_state.current_conditions));
    memcpy(s_state.current_icon, p->current_icon, sizeof(s_state.current_icon));
    memcpy(s_state.forecast, p->forecast, sizeof(s_state.forecast));
    s_state.forecast_days           = p->forecast_days;
    s_state.sunrise_epoch           = p->sunrise_epoch;
    s_state.sunset_epoch            = p->sunset_epoch;
    /* pressure_trend_mb_3h is owned by the local accumulator; the forecast
     * poll must not clobber it. */
    s_state.forecast_fetched_epoch  = (int64_t)time(NULL);
    s_state.forecast_valid          = true;
    UNLOCK();

    ui_notify_forecast_updated();
}

void wx_update_hourly(const wx_hourly_slot_t *slots, int count)
{
    if (!slots || count <= 0) {
        return;
    }
    if (count > WX_HOURLY_SLOTS) {
        count = WX_HOURLY_SLOTS;
    }
    LOCK();
    memcpy(s_state.hourly, slots, (size_t)count * sizeof(wx_hourly_slot_t));
    s_state.hourly_count          = count;
    s_state.hourly_fetched_epoch  = (int64_t)time(NULL);
    s_state.hourly_valid          = true;
    UNLOCK();
}

void wx_update_moon_schedule(int64_t rise, int64_t set)
{
    LOCK();
    if (rise > 0) {
        s_state.moonrise_epoch = rise;
    }
    if (set > 0) {
        s_state.moonset_epoch = set;
    }
    UNLOCK();
}

void wx_update_indoor(const wx_state_t *p)
{
    LOCK();
    s_state.indoor_temp_c        = p->indoor_temp_c;
    s_state.indoor_humidity_pct  = p->indoor_humidity_pct;
    s_state.indoor_fetched_epoch = (int64_t)time(NULL);
    s_state.indoor_valid         = true;
    UNLOCK();
}

void wx_set_wifi_connected(bool connected)
{
    LOCK();
    s_state.wifi_connected = connected;
    UNLOCK();
}

void wx_note_udp_packet(void)
{
    LOCK();
    s_state.last_udp_epoch = (int64_t)time(NULL);
    s_state.udp_packets_total++;
    UNLOCK();
}

bool wx_obs_is_stale(const wx_state_t *s)
{
    if (!s->obs_valid) {
        return true;
    }
    int64_t now = (int64_t)time(NULL);
    /* Before SNTP lands, time() is near zero and every comparison is nonsense.
     * Treat an unset clock as "not stale" rather than flapping the UI. */
    if (now < 1600000000LL) {
        return false;
    }
    return (now - s->obs_epoch) > CONFIG_TEMPEST_OBS_STALE_S;
}

bool wx_udp_is_stale(const wx_state_t *s)
{
    if (s->last_udp_epoch <= 0) {
        return true;
    }
    int64_t now = (int64_t)time(NULL);
    if (now < 1600000000LL) {
        return false;
    }
    return (now - s->last_udp_epoch) > CONFIG_TEMPEST_OBS_STALE_S;
}

#define FORECAST_STALE_S  (12 * 3600)

bool wx_forecast_is_stale(const wx_state_t *s)
{
    if (s->forecast_days <= 0) {
        return true;
    }
    if (s->forecast_fetched_epoch <= 0) {
        return true;
    }
    int64_t now = (int64_t)time(NULL);
    if (now < 1600000000LL) {
        return false;
    }
    return (now - s->forecast_fetched_epoch) > FORECAST_STALE_S;
}

bool wx_indoor_is_stale(const wx_state_t *s)
{
    if (!s->indoor_valid) {
        return true;
    }
    int64_t now = (int64_t)time(NULL);
    if (now < 1600000000LL) {
        return false;       /* clock not set yet; see wx_obs_is_stale */
    }
    return (now - s->indoor_fetched_epoch) > CONFIG_INDOOR_STALE_S;
}

/* --------------------------------------------------------------------------
 * Derived values. The UDP feed gives raw sensor readings only.
 * -------------------------------------------------------------------------- */

/* Magnus-Tetens. Accurate to ~0.4 C over -45..60 C. */
float wx_dew_point_c(float temp_c, float humidity_pct)
{
    if (humidity_pct <= 0.0f) {
        return temp_c;
    }
    const float a = 17.62f, b = 243.12f;
    float gamma = (a * temp_c) / (b + temp_c) + logf(humidity_pct / 100.0f);
    return (b * gamma) / (a - gamma);
}

/* NWS heat index above 27 C, NWS wind chill below 10 C, dry-bulb between. */
float wx_feels_like_c(float temp_c, float humidity_pct, float wind_ms)
{
    if (temp_c >= 27.0f) {
        float t = wx_c_to_f(temp_c);
        float r = humidity_pct;
        float hi = -42.379f
                 + 2.04901523f * t
                 + 10.14333127f * r
                 - 0.22475541f * t * r
                 - 0.00683783f * t * t
                 - 0.05481717f * r * r
                 + 0.00122874f * t * t * r
                 + 0.00085282f * t * r * r
                 - 0.00000199f * t * t * r * r;
        return (hi - 32.0f) * 5.0f / 9.0f;
    }
    if (temp_c <= 10.0f) {
        float v_kmh = wind_ms * 3.6f;
        if (v_kmh < 4.8f) {
            return temp_c;      /* formula is not valid in still air */
        }
        float v = powf(v_kmh, 0.16f);
        return 13.12f + 0.6215f * temp_c - 11.37f * v + 0.3965f * temp_c * v;
    }
    return temp_c;
}

const char *wx_uv_description(float uv)
{
    if (uv < 3.0f)  return "Low";
    if (uv < 6.0f)  return "Moderate";
    if (uv < 8.0f)  return "High";
    if (uv < 11.0f) return "Very High";
    return "Extreme";
}

const char *wx_aqi_epa_label(int aqi)
{
    if (aqi <= 50)   return "Good";
    if (aqi <= 100)  return "Moderate";
    if (aqi <= 150)  return "USG";
    if (aqi <= 200)  return "Unhealthy";
    if (aqi <= 300)  return "Very Unhealthy";
    return "Hazardous";
}

const char *wx_aqi_color_name(int aqi)
{
    if (aqi <= 50)   return "green";
    if (aqi <= 100)  return "yellow";
    if (aqi <= 150)  return "orange";
    if (aqi <= 200)  return "red";
    return "purple";
}

const char *wx_heat_index_risk(float temp_c, float humidity_pct)
{
    float hi_f = wx_c_to_f(wx_feels_like_c(temp_c, humidity_pct, 0.0f));
    if (hi_f >= 130.0f) return "Extreme Danger";
    if (hi_f >= 115.0f) return "Danger";
    if (hi_f >= 105.0f) return "Extreme Caution";
    if (hi_f >=  90.0f) return "Caution";
    return "OK";
}

const char *wx_wind_chill_risk(float temp_c, float wind_ms)
{
    float wc_f = wx_c_to_f(wx_feels_like_c(temp_c, 50.0f, wind_ms));
    if (wc_f <= -28.0f) return "Extreme Danger";
    if (wc_f <= -18.0f) return "Danger";
    if (wc_f <= -10.0f) return "Caution";
    return "Cold";
}

const char *wx_trend_description(wx_trend_t trend)
{
    switch (trend) {
    case WX_TREND_FALLING_FAST: return "falling rapidly";
    case WX_TREND_FALLING:      return "falling";
    case WX_TREND_RISING:       return "rising";
    case WX_TREND_RISING_FAST:  return "rising rapidly";
    case WX_TREND_STEADY:
    default:                    return "steady";
    }
}

const char *wx_compass_point(int degrees)
{
    static const char *pts[16] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
    };
    int d = ((degrees % 360) + 360) % 360;
    return pts[(int)((d / 22.5f) + 0.5f) % 16];
}

int wx_beaufort_force(float wind_ms)
{
    /* Mean wind thresholds, WMO scale, in m/s. */
    static const float lim[] = {
        0.3f, 1.6f, 3.4f, 5.5f, 8.0f, 10.8f, 13.9f, 17.2f, 20.8f, 24.5f, 28.5f, 32.7f
    };
    if (wind_ms < 0.0f) {
        wind_ms = 0.0f;
    }
    for (int i = 0; i < 12; i++) {
        if (wind_ms < lim[i]) {
            return i;
        }
    }
    return 12;
}

const char *wx_beaufort_name(int force)
{
    static const char *names[] = {
        "Calm", "Light air", "Light breeze", "Gentle breeze",
        "Moderate breeze", "Fresh breeze", "Strong breeze", "Near gale",
        "Gale", "Strong gale", "Storm", "Violent storm", "Hurricane",
    };
    if (force < 0) {
        force = 0;
    }
    if (force > 12) {
        force = 12;
    }
    return names[force];
}

int wx_next_precip_slot(const wx_state_t *s, int min_pop)
{
    if (!s || !s->hourly_valid || s->hourly_count <= 0) {
        return -1;
    }
    int64_t now = (int64_t)time(NULL);
    int n = s->hourly_count;
    if (n > WX_HOURLY_SLOTS) {
        n = WX_HOURLY_SLOTS;
    }
    for (int i = 0; i < n; i++) {
        if (s->hourly[i].hour_epoch + 3600 < now) {
            continue;
        }
        if (s->hourly[i].precip_probability >= min_pop) {
            return i;
        }
    }
    return -1;
}

void wx_update_aqi(int aqi_val, const char *cat, float pm25)
{
    LOCK();
    s_state.aqi_val = aqi_val;
    s_state.aqi_pm25 = pm25;
    if (cat) {
        strncpy(s_state.aqi_category, cat, sizeof(s_state.aqi_category) - 1);
        s_state.aqi_category[sizeof(s_state.aqi_category) - 1] = '\0';
    }
    s_state.aqi_valid = (aqi_val >= 0);
    s_state.aqi_fetched_epoch = (int64_t)time(NULL);
    UNLOCK();
}

void wx_update_station_location(float lat, float lon)
{
    LOCK();
    s_state.station_lat = lat;
    s_state.station_lon = lon;
    s_state.station_loc_valid = !(lat == 0.0f && lon == 0.0f);
    UNLOCK();
}

bool wx_aqi_is_stale(const wx_state_t *s)
{
    return !s->aqi_valid || (time(NULL) >= 1600000000LL &&
           (int64_t)time(NULL) - s->aqi_fetched_epoch > 7200);
}
bool wx_hourly_is_stale(const wx_state_t *s)
{
    return !s->hourly_valid || (time(NULL) >= 1600000000LL &&
           (int64_t)time(NULL) - s->hourly_fetched_epoch > 43200);
}
