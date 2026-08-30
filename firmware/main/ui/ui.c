#include "weather_icons_data.h"
#include "ui.h"
#include "wx_icons.h"
#include "wx_state.h"
#include "config.h"
#include "settings.h"
#include "graphs.h"
#include "page2.h"
#include "week.h"
#include "alerts.h"
#include "wifi_setup.h"
#include "display.h"
#include "net.h"
#include "history.h"
#include "audio.h"
#include "nws_alerts.h"
#include "ota.h"
#include "tempest_ws.h"
#include "wx_astronomy.h"

#include "sdkconfig.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "ui";

/* ---- palette: rich commercial console colors ---------------------------- */
#define COL_BG          lv_color_hex(0x05080D)
#define COL_CARD        lv_color_hex(0x0C1118)
#define COL_CARD_BORDER lv_color_hex(0x243044)
#define COL_CARD_TODAY  lv_color_hex(0x151C28)
#define COL_TRACK       lv_color_hex(0x18202C)
#define COL_HAIRLINE    lv_color_hex(0x2A3548)
#define COL_TEXT        lv_color_hex(0xF8FAFC)
#define COL_DIM         lv_color_hex(0x94A3B8)
#define COL_FAINT       lv_color_hex(0x475569)

/* Gauge & Accent Highlights */
#define COL_TEMP_HOT    lv_color_hex(0xFF5722)
#define COL_TEMP_AMBER  lv_color_hex(0xFF9800)
#define COL_TEMP_COLD   lv_color_hex(0x00B0FF)
#define COL_WIND_NEON   lv_color_hex(0x00E5FF)
#define COL_NEEDLE_HEAD lv_color_hex(0xFF1744)
#define COL_NEEDLE_TAIL lv_color_hex(0x546E7A)
#define COL_INDOOR_TEMP lv_color_hex(0xFFA726)
#define COL_INDOOR_HUM  lv_color_hex(0xBA68C8)
#define COL_RAIN_NEON   lv_color_hex(0x29B6F6)
#define COL_PRESS_NEON  lv_color_hex(0x26C6DA)
#define COL_SUN_GOLD    lv_color_hex(0xFFD600)
#define COL_MOON_SILVER lv_color_hex(0xCFD8DC)

/* EPA UV Standard Color Tiers */
#define COL_UV_LOW      lv_color_hex(0x4CAF50) /* 0-2 Green */
#define COL_UV_MOD      lv_color_hex(0xFDD835) /* 3-5 Yellow */
#define COL_UV_HIGH     lv_color_hex(0xFB8C00) /* 6-7 Orange */
#define COL_UV_VHIGH    lv_color_hex(0xE53935) /* 8-10 Red */
#define COL_UV_EXTREME  lv_color_hex(0x8E24AA) /* 11+ Violet */

#define COL_ALERT       lv_color_hex(0xFF1744)
#define COL_OK          lv_color_hex(0x00E676)
#define COL_IDLE        lv_color_hex(0x475569)

#define SIG_BARS        5
#define COL_SIG_DIM     lv_color_hex(0x334155)
#define COL_SIG_1       lv_color_hex(0xE53935) /* Red */
#define COL_SIG_2       lv_color_hex(0xFB8C00) /* Orange */
#define COL_SIG_3       lv_color_hex(0xFDD835) /* Yellow / Amber */
#define COL_SIG_4       lv_color_hex(0x84CC16) /* Lime / Light Green */
#define COL_SIG_5       lv_color_hex(0x22C55E) /* Vivid Green */

#define LIGHTNING_NEAR_KM  9.656f   /* 6 miles */

/* ---- geometry (1024 x 600) ---------------------------------------------- */
#define SCR_W           1024
#define SCR_H           600
#define PAD             10

/* Header Bar */
#define HEAD_Y          6
#define HEAD_H          38

/* Top Deck: 4 Major Instrumentation Zones */
#define TOP_Y           48
#define TOP_H           228
#define Z1_W            242  /* Outdoor Temp */
#define Z2_W            254  /* Split Wind Radar & Speed */
#define Z3_W            244  /* Indoor Climate Dual Gauges */
#define Z4_W            246  /* Animated Weather Centerpiece */

/* Middle Deck: 4 Dedicated Centerpiece Panels */
#define MID_Y           282
#define MID_H           152
#define M1_W            242  /* Precipitation & Rate */
#define M2_W            254  /* Lightning Detector */
#define M3_W            244  /* Solar Arc & Lunar */
#define M4_W            246  /* Barometer & Trend */

/* Bottom Deck: 7-Day Forecast Strip + news-style alert crawl */
#define TICKER_H        UI_TICKER_H
#define TICKER_Y        (SCR_H - TICKER_H)
#define FC_Y            440
#define FC_H            (TICKER_Y - FC_Y - 4)
#define FC_COLS         WX_FORECAST_DAYS
#define FC_INSET        8    /* keep col 0 off the panel's left clip edge */

/* Arc Geometry */
#define ARC_START       135
#define ARC_SWEEP       270

#define TEMP_MIN_C      (-20.0f)
#define TEMP_MAX_C      45.0f
#define INDOOR_MIN_C    10.0f
#define INDOOR_MAX_C    35.0f

#define U_TEMP(c)       cfg_temp(c)
#define U_TEMP_SUF      cfg_temp_suffix()
#define U_WIND(ms)      cfg_wind(ms)
#define U_WIND_SUF      cfg_wind_suffix()
#define U_PRES(mb)      cfg_pressure(mb)
#define U_PRES_SUF      cfg_pressure_suffix()
#define U_PRES_FMT      cfg_pressure_fmt()
#define U_RAIN(mm)      cfg_rain(mm)
#define U_RAIN_SUF      cfg_rain_suffix()
#define U_RAIN_FMT      cfg_rain_fmt()

/* ---- widget handles ----------------------------------------------------- */

/* Header */
static lv_obj_t *hdr_dot, *hdr_health, *hdr_date, *hdr_clock, *hdr_bat_icon, *hdr_aqi;
static lv_obj_t *hdr_units;
static lv_obj_t *s_main_screen;
static lv_obj_t *s_dash;
static ui_page_t s_current_page = UI_PAGE_DASHBOARD;
static bool       s_ui_ready;
static lv_obj_t *hdr_page_lbl;
static lv_obj_t *hdr_signal;
static lv_obj_t *hdr_sig_bars[SIG_BARS];
static lv_obj_t *s_ticker, *s_ticker_tag, *s_ticker_lbl;
static int       s_ticker_mode = -1;

/* Zone 1: Outdoor Temperature */
static lv_obj_t *temp_card, *temp_glow;
static lv_obj_t *temp_arc, *temp_knob;
static lv_obj_t *temp_val, *temp_unit, *temp_feels, *temp_badge;
static lv_obj_t *temp_lo, *temp_hi, *temp_range_track, *temp_range_fill, *temp_now;
static lv_obj_t *temp_dew, *temp_rh, *temp_trend;

#define Z1_CX       (Z1_W / 2)
#define Z1_CY       96
#define Z1_ARC      148
#define Z1_STROKE   11
#define Z1_KNOB     14
#define Z1_KR       (Z1_ARC / 2 - Z1_STROKE / 2)
#define TEMP_TICKS  9

/* Zone 2: Split Wind Radar & Speed */
static lv_obj_t *wind_val, *wind_unit, *wind_dir_deg, *wind_gust_lbl, *wind_avg_lbl;
static lv_obj_t *wind_beaufort_lbl;
static lv_obj_t *wind_needle_head, *wind_needle_tail, *wind_hub;
static lv_obj_t *wind_beaufort_bar;
static lv_point_precise_t needle_head_pts[2];
static lv_point_precise_t needle_tail_pts[2];

/* Zone 3: Indoor Climate Dual Dials */
static lv_obj_t *in_temp_glow, *in_temp_kicker;
static lv_obj_t *in_temp_arc, *in_temp_knob, *in_temp_val;
static lv_obj_t *in_hum_arc, *in_hum_knob, *in_hum_val;
static lv_obj_t *in_comfort_badge, *in_status_lbl, *in_no_sensor_view;
/* The TEMP/HUMIDITY captions need handles too. Without them they cannot be
 * hidden, so they sat on top of the no-sensor panel. */
static lv_obj_t *in_temp_cap, *in_hum_cap;

/* Zone 4: Current Conditions */
static lv_obj_t *cond_icon, *cond_title, *cond_sub, *cond_badge;

/* Mid 1: Rain & Precipitation */
static lv_obj_t *rain_val, *rain_rate_lbl, *rain_totals_lbl, *rain_cylinder_fill, *rain_badge;

/* Mid 2: Lightning Activity Detector */
static lv_obj_t *ltg_card, *ltg_badge, *ltg_count_val, *ltg_count_lbl, *ltg_dist_lbl, *ltg_status_lbl;

/* Mid 3: Solar & Lunar Arc */
static lv_obj_t *sun_arc_line, *sun_marker, *sun_rise_lbl, *sun_set_lbl;
static lv_obj_t *solar_rad_lbl, *uv_badge, *solar_lux_lbl, *solar_daylight_lbl;
static lv_obj_t *uv_tier_bars[5];
static lv_point_precise_t sun_arc_pts[17];

/* Mid 4: Barometric Pressure */
static lv_obj_t *baro_val, *baro_trend_lbl, *baro_badge;
static lv_obj_t *baro_bars[10];

/* Bottom: 7-Day Forecast Columns */
typedef struct {
    lv_obj_t *card;
    lv_obj_t *day;
    lv_obj_t *icon;
    lv_obj_t *hi;
    lv_obj_t *lo;
    lv_obj_t *pop;
    lv_obj_t *range_track;
    lv_obj_t *range_fill;
} fc_col_t;

static fc_col_t fc[FC_COLS];
static lv_obj_t *s_forecast_panel;
static bool     s_forecast_dirty;
static int      s_fc_drawn_min = -1;
static float    s_fc_drawn_hi = -999.0f;
static float    s_fc_drawn_lo = 999.0f;
static char     s_fc_icon_slug[FC_COLS][40];
static bool     s_fc_lottie[FC_COLS];
static int      s_icon_promote_step = -1;
static uint32_t s_last_promote_ms;

static lv_obj_t *s_cond_parent;
static bool      s_cond_lottie;
static char      s_cond_slug[40];

#define COND_ICON_SZ  112
#define FC_ICON_SZ    36
#define FC_ICON_Y     18

static void fc_set_icon(int idx, const char *slug);
static void fc_make_bitmap(int idx);
static void cond_set_icon(const char *slug);
static void cond_make_bitmap(void);
static void set_text(lv_obj_t *l, const char *fmt, ...);

/* REST forecast fields; falls back to day 0 of the 7-day strip. */
static const char *resolve_conditions(const wx_state_t *s)
{
    if (s->current_conditions[0]) {
        return s->current_conditions;
    }
    if (s->forecast_days > 0 && s->forecast[0].conditions[0]) {
        return s->forecast[0].conditions;
    }
    return NULL;
}

static bool conditions_is_night(const wx_state_t *s, int64_t now)
{
    float lat = 0.0f;
    float lon = 0.0f;
    if (s && s->station_loc_valid) {
        lat = s->station_lat;
        lon = s->station_lon;
    }
    int64_t sr = s ? s->sunrise_epoch : 0;
    int64_t ss = s ? s->sunset_epoch : 0;
    return !wx_is_daylight(now, sr, ss, lat, lon);
}

/* Meteocons ships separate day/night assets; Open-Meteo always emits -day. */
static void slug_for_time_of_day(const char *slug, bool night, char *out, size_t out_len)
{
    if (!slug || !slug[0]) {
        strncpy(out, night ? "clear-night" : "clear-day", out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }

    if (night) {
        if (strcmp(slug, "clear-day") == 0) {
            strncpy(out, "clear-night", out_len - 1);
            out[out_len - 1] = '\0';
            return;
        }
        const char *day = strstr(slug, "-day");
        if (day && day[4] == '\0') {
            size_t prefix = (size_t)(day - slug);
            snprintf(out, out_len, "%.*s-night", (int)prefix, slug);
            return;
        }
    } else {
        if (strcmp(slug, "clear-night") == 0 || strcmp(slug, "starry-night") == 0) {
            strncpy(out, "clear-day", out_len - 1);
            out[out_len - 1] = '\0';
            return;
        }
        const char *night_suffix = strstr(slug, "-night");
        if (night_suffix && night_suffix[6] == '\0') {
            size_t prefix = (size_t)(night_suffix - slug);
            snprintf(out, out_len, "%.*s-day", (int)prefix, slug);
            return;
        }
    }

    strncpy(out, slug, out_len - 1);
    out[out_len - 1] = '\0';
}

static const char *icon_slug_for(const wx_state_t *s, int64_t now,
                                 const char *base, bool force_day)
{
    static char buf[WX_COND_STR_LEN];
    bool night = force_day ? false : conditions_is_night(s, now);
    if (!base || !base[0]) {
        base = night ? "clear-night" : "clear-day";
    }
    slug_for_time_of_day(base, night, buf, sizeof(buf));
    return buf;
}

static bool cond_has(const char *hay, const char *needle)
{
    if (!hay || !needle || needle[0] == '\0') {
        return false;
    }
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

/* WeatherFlow tags breezy-but-sunny days as `wind`. That slug has no sky in
 * it, and the file we ship is `windy.json`, so the strip either shows a tiny
 * wind-sock bitmap or a missing-file fallback. Daily columns should show the
 * sky; the wind gauge already covers breeze. Do not infer rain from the
 * conditions text — "Rain Possible" on a wind day is a chance, not the sky. */
static const char *forecast_sky_slug(const wx_forecast_day_t *d)
{
    const char *icon = d->icon;
    if (icon[0] && strcmp(icon, "rain") == 0) {
        return "rainy";
    }
    if (icon[0] && strcmp(icon, "wind") != 0 && strcmp(icon, "windy") != 0) {
        return icon;
    }

    const char *c = d->conditions;
    if (cond_has(c, "thunder")) {
        return "possibly-thunderstorm-day";
    }
    if (cond_has(c, "snow")) {
        return "snow";
    }
    if (cond_has(c, "sleet")) {
        return "sleet";
    }
    if (cond_has(c, "fog")) {
        return "foggy";
    }
    if (cond_has(c, "overcast") ||
        (cond_has(c, "cloud") && !cond_has(c, "partly") && !cond_has(c, "few"))) {
        return "cloudy";
    }
    if (cond_has(c, "clear") || cond_has(c, "sunny")) {
        return "clear-day";
    }
    return "partly-cloudy-day";
}

static const char *resolve_icon_slug(const wx_state_t *s, int64_t now)
{
    const char *base = NULL;

    /* Live sensor overrides beat forecast slugs. */
    if (s->obs_valid) {
        if (s->rain_rate_mm_hr > 0.05f) {
            base = "rainy";
        } else if (s->strikes_3h > 0) {
            base = conditions_is_night(s, now) ? "possibly-thunderstorm-night"
                                               : "possibly-thunderstorm-day";
        }
    }

    if (!base && s->current_icon[0]) {
        base = s->current_icon;
    } else if (!base && s->forecast_days > 0 && s->forecast[0].icon[0]) {
        base = s->forecast[0].icon;
    }

    /* Never force day here — current conditions follow the real sun. */
    return icon_slug_for(s, now, base, false);
}

static void update_conditions_card(const wx_state_t *s, int64_t now)
{
    const char *cond = resolve_conditions(s);
    if (cond) {
        lv_label_set_text(cond_title, cond);
    } else if (s->forecast_days > 0) {
        lv_label_set_text(cond_title, "Forecast ready");
    } else {
        lv_label_set_text(cond_title, "--");
    }

    if (cond_icon) {
        const char *slug = resolve_icon_slug(s, now);
        static char s_last_cond_slug[40];
        if (strcmp(s_last_cond_slug, slug) != 0) {
            float lat = s->station_loc_valid ? s->station_lat : 0.0f;
            float lon = s->station_loc_valid ? s->station_lon : 0.0f;
            float alt = wx_sun_altitude_deg(now, lat, lon);
            struct tm lt;
            time_t t = (time_t)now;
            localtime_r(&t, &lt);
            ESP_LOGI(TAG, "conditions icon '%s' (api '%s', sun %.1f°, local %02d:%02d, %s)",
                     slug, s->current_icon[0] ? s->current_icon : "(none)",
                     (double)alt, lt.tm_hour, lt.tm_min,
                     conditions_is_night(s, now) ? "night" : "day");
            strncpy(s_last_cond_slug, slug, sizeof(s_last_cond_slug) - 1);
            s_last_cond_slug[sizeof(s_last_cond_slug) - 1] = '\0';
        }
        cond_set_icon(slug);
    }

    if (s->forecast_days > 0 && s->daily_valid) {
        set_text(cond_sub, "Obs %.0f°/%.0f°  Fcst %.0f°/%.0f°",
                 (double)U_TEMP(s->temp_high_today_c),
                 (double)U_TEMP(s->temp_low_today_c),
                 (double)U_TEMP(s->forecast[0].air_temp_high_c),
                 (double)U_TEMP(s->forecast[0].air_temp_low_c));
    } else if (s->forecast_days > 0) {
        float f_hi = U_TEMP(s->forecast[0].air_temp_high_c);
        float f_lo = U_TEMP(s->forecast[0].air_temp_low_c);
        set_text(cond_sub, "Today: Hi %.0f°  Lo %.0f°", (double)f_hi, (double)f_lo);
    } else if (s->daily_valid && s->obs_valid) {
        float t_hi = U_TEMP(s->temp_high_today_c);
        float t_lo = U_TEMP(s->temp_low_today_c);
        set_text(cond_sub, "Today: Hi %.0f°  Lo %.0f°", (double)t_hi, (double)t_lo);
    }
}

static void on_gear(lv_event_t *e);
static void on_ticker(lv_event_t *e);
static void on_dot(lv_event_t *e);
static void on_page_btn(lv_event_t *e);
static void on_toggle_units(lv_event_t *e);
static void on_screen_swipe(lv_event_t *e);
static void ui_sync_page_button_labels(void);

static int rssi_to_bars(int rssi)
{
    if (rssi == 0 || rssi < -95) return 0;
    if (rssi >= -55) return 5;  /* Excellent */
    if (rssi >= -65) return 4;  /* Very Good */
    if (rssi >= -75) return 3;  /* Good */
    if (rssi >= -85) return 2;  /* Fair */
    return 1;                   /* Weak */
}

static lv_color_t sig_bar_color(int idx)
{
    switch (idx) {
    case 0: return COL_SIG_1;
    case 1: return COL_SIG_2;
    case 2: return COL_SIG_3;
    case 3: return COL_SIG_4;
    case 4: return COL_SIG_5;
    default: return COL_SIG_DIM;
    }
}

static void format_hour_label(char *buf, size_t len, int64_t epoch)
{
    if (epoch <= 0) {
        snprintf(buf, len, "--:--");
        return;
    }
    struct tm ht;
    time_t ht_t = (time_t)epoch;
    localtime_r(&ht_t, &ht);
    strftime(buf, len, "%I:%M %p", &ht);
    if (buf[0] == '0') {
        memmove(buf, buf + 1, strlen(buf));
    }
}

static lv_point_t s_swipe_start;
static bool       s_swipe_active;
static bool       s_swipe_handled;

static uint32_t   s_last_input_ms;
static bool       s_screensaver_active;
static int64_t    s_ltg_sound_epoch;
static uint8_t    s_scheduled_brightness;
static uint32_t   s_boot_ms;

#define BOOT_BRIGHT_GRACE_MS  90000

void ui_note_user_activity(void)
{
    s_last_input_ms = lv_tick_get();
    if (s_screensaver_active) {
        s_screensaver_active = false;
        display_set_brightness(s_scheduled_brightness);
    }
}

/* ======================================================================== */
/* ---- HELPER BUILDERS --------------------------------------------------- */
/* ======================================================================== */

static void on_toggle_units(lv_event_t *e)
{
    (void)e;
    if (s_swipe_handled) {
        s_swipe_handled = false;
        return;
    }

    cfg_t c;
    cfg_get(&c);
    c.units = (c.units == CFG_UNITS_IMPERIAL) ? CFG_UNITS_METRIC : CFG_UNITS_IMPERIAL;
    cfg_set(&c);
    s_forecast_dirty = true;
    ESP_LOGI(TAG, "units toggled to %s",
             c.units == CFG_UNITS_METRIC ? "metric" : "imperial");
}

const char *ui_page_name(ui_page_t page)
{
    static const char *names[UI_PAGE_COUNT] = {
        "LIVE",
        "SKY",
        "WEEK",
        "24H",
        "ALERTS",
    };
    if (page >= UI_PAGE_COUNT) {
        return names[0];
    }
    return names[page];
}

static const char *page_button_label(ui_page_t page)
{
    /* The button advances the deck, so it names the next page. */
    ui_page_t next = (ui_page_t)((page + 1) % UI_PAGE_COUNT);
    return ui_page_name(next);
}

void ui_create_page_dots(lv_obj_t *parent, int x, int y, ui_page_t current)
{
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(parent);
        lv_obj_remove_style_all(d);
        int w = (i == (int)current) ? 14 : 7;
        lv_obj_set_size(d, w, 8);
        lv_obj_set_pos(d, x + i * 22, y);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_color(d, (i == (int)current) ? COL_WIND_NEON : COL_FAINT, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(d, 10);
        lv_obj_add_event_cb(d, on_dot, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

lv_obj_t *ui_main_screen(void)
{
    return s_main_screen;
}

void ui_page_goto(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) {
        return;
    }

    /* Overlays only — lv_screen_load() during SDIO/Wi-Fi has repeatedly left
     * the MIPI panel lit with no framebuffer. The dashboard screen stays
     * active for the lifetime of the app. */
    page2_hide();
    week_hide();
    graphs_hide();
    alerts_hide();

    if (s_cond_lottie && cond_icon) {
        wx_icon_set_paused(cond_icon, page != UI_PAGE_DASHBOARD);
    }
    if (s_dash) {
        if (page == UI_PAGE_DASHBOARD) {
            lv_obj_clear_flag(s_dash, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_dash, LV_OBJ_FLAG_HIDDEN);
        }
    }

    switch (page) {
    case UI_PAGE_DASHBOARD:
        break;
    case UI_PAGE_INSIGHTS:
        page2_show();
        break;
    case UI_PAGE_WEEK:
        week_show();
        break;
    case UI_PAGE_GRAPHS:
        graphs_show();
        break;
    case UI_PAGE_ALERTS:
        alerts_show();
        break;
    default:
        break;
    }

    s_current_page = page;
    ui_sync_page_button_labels();

    if (s_main_screen) {
        lv_obj_invalidate(s_main_screen);
    }
}

void ui_page_next(void)
{
    ui_page_goto((ui_page_t)((s_current_page + 1) % UI_PAGE_COUNT));
}

void ui_page_prev(void)
{
    ui_page_goto((ui_page_t)((s_current_page + UI_PAGE_COUNT - 1) % UI_PAGE_COUNT));
}

void ui_attach_swipe_nav(lv_obj_t *screen)
{
    lv_obj_add_event_cb(screen, on_screen_swipe, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, on_screen_swipe, LV_EVENT_RELEASED, NULL);
}

static void on_screen_swipe(lv_event_t *e)
{
    if (!s_ui_ready || wifi_setup_is_visible() || settings_is_visible()) {
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        ui_note_user_activity();
        s_swipe_start = p;
        s_swipe_active = true;
        s_swipe_handled = false;
        return;
    }

    if (!s_swipe_active || lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    s_swipe_active = false;

    int dx = p.x - s_swipe_start.x;
    int dy = p.y - s_swipe_start.y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < 100 || adx < ady * 2) {
        return;
    }

    s_swipe_handled = true;
    if (dx < 0) {
        ui_page_next();
    } else {
        ui_page_prev();
    }
}

lv_obj_t *ui_create_page_button(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs,
                                ui_page_t page)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 30);
    lv_obj_set_style_min_width(btn, 64, 0);
    lv_obj_set_style_pad_hor(btn, 10, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    lv_obj_align(btn, align, x_ofs, y_ofs);
    lv_obj_set_style_bg_color(btn, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_ext_click_area(btn, 10);
    lv_obj_add_event_cb(btn, on_page_btn, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, page_button_label(page));
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    lv_obj_update_layout(btn);
    lv_obj_set_width(btn, lv_obj_get_width(lbl) + 20);

    if (page == UI_PAGE_DASHBOARD) {
        hdr_page_lbl = lbl;
    }
    return lbl;
}

void ui_create_gear_button(lv_obj_t *parent, lv_align_t align, int x_ofs, int y_ofs)
{
    lv_obj_t *gear = lv_button_create(parent);
    lv_obj_set_size(gear, 48, 30);
    lv_obj_align(gear, align, x_ofs, y_ofs);
    lv_obj_set_style_bg_color(gear, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(gear, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(gear, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(gear, 1, 0);
    lv_obj_set_style_radius(gear, 6, 0);
    lv_obj_set_style_pad_all(gear, 0, 0);
    lv_obj_set_ext_click_area(gear, 10);
    lv_obj_add_event_cb(gear, on_gear, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(gear);
    lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gl, COL_TEXT, 0);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_18, 0);
    lv_obj_center(gl);
}

void ui_ticker_raise(void)
{
    if (s_ticker) {
        lv_obj_move_foreground(s_ticker);
    }
}

static void on_ticker(lv_event_t *e)
{
    (void)e;
    if (wifi_setup_is_visible() || settings_is_visible()) {
        return;
    }
    ui_note_user_activity();
    ui_page_goto(UI_PAGE_ALERTS);
}

static void on_dot(lv_event_t *e)
{
    if (wifi_setup_is_visible() || settings_is_visible()) {
        return;
    }
    ui_note_user_activity();
    ui_page_goto((ui_page_t)(intptr_t)lv_event_get_user_data(e));
}

static void ui_sync_page_button_labels(void)
{
    if (hdr_page_lbl) {
        lv_label_set_text(hdr_page_lbl, page_button_label(s_current_page));
        lv_obj_t *btn = lv_obj_get_parent(hdr_page_lbl);
        if (btn) {
            lv_obj_update_layout(btn);
            lv_obj_set_width(btn, lv_obj_get_width(hdr_page_lbl) + 20);
        }
    }
}

static lv_obj_t *bare_bar(lv_obj_t *parent, int x, int y, int w, int h,
                          lv_color_t col, lv_opa_t opa)
{
    /* Strip the default theme. Unstyled lv_obj keeps 12 px pad and a min
     * size, so a 1 px hairline becomes a slab that covers the dashboard. */
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, col, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t border_col, lv_color_t glow_col)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, border_col, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    bare_bar(c, 0, 0, w, 2, glow_col, LV_OPA_COVER);
    bare_bar(c, 1, 2, w - 2, 1, COL_HAIRLINE, LV_OPA_40);

    return c;
}

/* make_card() puts its accent bar in as the FIRST child, so this drops it.
 * Used for the header, where a full-width bar along the very top edge of the
 * screen reads as a stray line rather than as trim on a panel. */
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt);

static void card_no_accent(lv_obj_t *c)
{
    /* Glow then hairline are the first two children of make_card(). */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *child = lv_obj_get_child(c, 0);
        if (child) {
            lv_obj_delete(child);
        }
    }
}

static lv_obj_t *kicker(lv_obj_t *parent, lv_color_t color, const char *txt)
{
    lv_obj_t *l = label(parent, &lv_font_montserrat_14, color, txt);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

static void paint_badge(lv_obj_t *lbl, const char *txt, lv_color_t fg)
{
    if (!lbl) {
        return;
    }
    if (txt) {
        ui_label_set(lbl, txt);
    }
    lv_obj_set_style_text_color(lbl, fg, 0);
    lv_obj_set_style_bg_color(lbl, fg, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_20, 0);
    lv_obj_set_style_pad_left(lbl, 7, 0);
    lv_obj_set_style_pad_right(lbl, 7, 0);
    lv_obj_set_style_pad_top(lbl, 1, 0);
    lv_obj_set_style_pad_bottom(lbl, 1, 0);
    lv_obj_set_style_radius(lbl, 8, 0);
}

static lv_color_t aqi_color(int val)
{
    if (val <= 50)  return COL_UV_LOW;
    if (val <= 100) return COL_UV_MOD;
    if (val <= 150) return COL_UV_HIGH;
    if (val <= 200) return COL_UV_VHIGH;
    return COL_UV_EXTREME;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static lv_obj_t *clabel(lv_obj_t *parent, int cx, int y, int w, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, cx - w / 2, y);
    lv_obj_set_width(l, w);
    return l;
}

static lv_obj_t *make_pill(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t border_col, lv_color_t txt_col, const char *txt)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, COL_CARD, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, border_col, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, txt_col, 0);
    lv_obj_center(l);

    /* Return the LABEL, not the container. Callers keep this handle and pass
     * it to set_text(), and lv_label_set_text() on a plain lv_obj reads it as
     * an lv_label_t and dereferences garbage -- a load fault inside
     * lv_label_refr_text, not an obvious null. The container stays as the
     * label's parent and still positions it. */
    return l;
}

static lv_obj_t *make_knob(lv_obj_t *parent, lv_color_t col, int diam)
{
    lv_obj_t *k = lv_obj_create(parent);
    lv_obj_remove_style_all(k);
    lv_obj_set_size(k, diam, diam);
    lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(k, col, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return k;
}

static void place_knob_r(lv_obj_t *k, int cx, int cy, float frac, int radius, int diam)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    float deg = (float)ARC_START + frac * (float)ARC_SWEEP;
    float rad = deg * (float)M_PI / 180.0f;
    int kx = cx + (int)(cosf(rad) * (float)radius) - diam / 2;
    int ky = cy + (int)(sinf(rad) * (float)radius) - diam / 2;
    lv_obj_set_pos(k, kx, ky);
}

static lv_color_t temp_color_at_c(float temp_c);

static lv_point_precise_t s_temp_tick_pts[TEMP_TICKS][2];

static void build_temp_scale_ticks(lv_obj_t *parent, int cx, int cy, int r)
{
    /* Color ticks around the gauge so the ring reads as a thermometer,
     * not a decorative halo. */
    for (int i = 0; i < TEMP_TICKS; i++) {
        float frac = (float)i / (float)(TEMP_TICKS - 1);
        float deg = (float)ARC_START + frac * (float)ARC_SWEEP;
        float rad = deg * (float)M_PI / 180.0f;
        float t_c = TEMP_MIN_C + frac * (TEMP_MAX_C - TEMP_MIN_C);
        int r0 = r - 2;
        int r1 = r + 5;
        s_temp_tick_pts[i][0].x = cx + (int)(cosf(rad) * (float)r0);
        s_temp_tick_pts[i][0].y = cy + (int)(sinf(rad) * (float)r0);
        s_temp_tick_pts[i][1].x = cx + (int)(cosf(rad) * (float)r1);
        s_temp_tick_pts[i][1].y = cy + (int)(sinf(rad) * (float)r1);

        lv_obj_t *ln = lv_line_create(parent);
        lv_line_set_points(ln, s_temp_tick_pts[i], 2);
        lv_obj_set_style_line_width(ln, (i == 0 || i == TEMP_TICKS - 1) ? 3 : 2, 0);
        lv_obj_set_style_line_color(ln, temp_color_at_c(t_c), 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        lv_obj_clear_flag(ln, LV_OBJ_FLAG_SCROLLABLE);
    }
}

typedef struct {
    float    temp_f;
    uint32_t rgb;
} temp_color_stop_t;

/* Thermometer scale people actually read: ice is blue, room is green-gold,
 * heat is orange then red. Channel deltas are signed -- unsigned subtract
 * wrapped mid-range blends into random purples. */
static const temp_color_stop_t TEMP_COLOR_STOPS[] = {
    { -20.f, 0x4A1C96 }, /* deep purple, extreme cold */
    {   0.f, 0x3D4ED4 }, /* indigo */
    {  20.f, 0x1E88E5 }, /* blue */
    {  32.f, 0x29B6F6 }, /* ice cyan -- freezing */
    {  45.f, 0x26C6DA }, /* chilly teal */
    {  55.f, 0x26A69A }, /* cool */
    {  65.f, 0x66BB6A }, /* mild green */
    {  72.f, 0xC0CA33 }, /* comfortable / room */
    {  80.f, 0xFDD835 }, /* warm yellow */
    {  88.f, 0xFB8C00 }, /* hot orange */
    {  95.f, 0xF4511E }, /* very hot */
    { 105.f, 0xD32F2F }, /* extreme red */
    { 115.f, 0x880E4F }, /* dangerous */
};

static lv_color_t temp_color_at_f(float temp_f)
{
    const int n = (int)(sizeof(TEMP_COLOR_STOPS) / sizeof(TEMP_COLOR_STOPS[0]));
    if (temp_f <= TEMP_COLOR_STOPS[0].temp_f) {
        return lv_color_hex(TEMP_COLOR_STOPS[0].rgb);
    }
    if (temp_f >= TEMP_COLOR_STOPS[n - 1].temp_f) {
        return lv_color_hex(TEMP_COLOR_STOPS[n - 1].rgb);
    }
    for (int i = 1; i < n; i++) {
        if (temp_f <= TEMP_COLOR_STOPS[i].temp_f) {
            float t0 = TEMP_COLOR_STOPS[i - 1].temp_f;
            float t1 = TEMP_COLOR_STOPS[i].temp_f;
            float mix = (temp_f - t0) / (t1 - t0);
            uint32_t c0 = TEMP_COLOR_STOPS[i - 1].rgb;
            uint32_t c1 = TEMP_COLOR_STOPS[i].rgb;
            int r0 = (int)((c0 >> 16) & 0xFF), r1 = (int)((c1 >> 16) & 0xFF);
            int g0 = (int)((c0 >>  8) & 0xFF), g1 = (int)((c1 >>  8) & 0xFF);
            int b0 = (int)(c0 & 0xFF),         b1 = (int)(c1 & 0xFF);
            int r = (int)((float)r0 + mix * (float)(r1 - r0));
            int g = (int)((float)g0 + mix * (float)(g1 - g0));
            int b = (int)((float)b0 + mix * (float)(b1 - b0));
            return lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
    }
    return lv_color_hex(TEMP_COLOR_STOPS[n / 2].rgb);
}

static lv_color_t temp_color_at_c(float temp_c)
{
    return temp_color_at_f(temp_c * 9.0f / 5.0f + 32.0f);
}

static void paint_temp_gauge(lv_obj_t *arc, lv_obj_t *knob, lv_obj_t *glow, lv_color_t col)
{
    if (arc) {
        lv_obj_set_style_arc_color(arc, col, LV_PART_INDICATOR);
    }
    if (knob) {
        lv_obj_set_style_bg_color(knob, col, 0);
    }
    if (glow) {
        lv_obj_set_style_bg_color(glow, col, 0);
    }
}

static lv_obj_t *make_arc_ring(lv_obj_t *parent, int cx, int cy, int size, int width, lv_color_t track_col, lv_color_t ind_col, bool is_full)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_set_pos(a, cx - size / 2, cy - size / 2);

    if (is_full) {
        lv_arc_set_bg_angles(a, 0, 360);
        lv_arc_set_rotation(a, 270);
    } else {
        lv_arc_set_bg_angles(a, ARC_START, ARC_START + ARC_SWEEP);
        lv_arc_set_rotation(a, 0);
    }

    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, track_col, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, !is_full, LV_PART_MAIN);

    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, ind_col, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);

    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    return a;
}

static lv_point_precise_t s_compass_pts[12][2];

static void build_compass_ticks(lv_obj_t *parent, int cx, int cy, int radius)
{
    int i = 0;
    for (int deg = 0; deg < 360; deg += 30, i++) {
        float rad = (float)deg * (float)M_PI / 180.0f;
        int is_cardinal = (deg % 90 == 0);
        int tick_len = is_cardinal ? 6 : 4;
        int r_out = radius - 2;
        int r_in  = r_out - tick_len;

        s_compass_pts[i][0].x = cx + (int)(sinf(rad) * (float)r_in);
        s_compass_pts[i][0].y = cy - (int)(cosf(rad) * (float)r_in);
        s_compass_pts[i][1].x = cx + (int)(sinf(rad) * (float)r_out);
        s_compass_pts[i][1].y = cy - (int)(cosf(rad) * (float)r_out);

        lv_obj_t *line = lv_line_create(parent);
        lv_line_set_points(line, s_compass_pts[i], 2);
        lv_obj_set_style_line_width(line, is_cardinal ? 2 : 1, 0);
        lv_obj_set_style_line_color(line, is_cardinal ? COL_WIND_NEON : COL_FAINT, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    }
}

/* ======================================================================== */
/* ---- UI BUILDERS ------------------------------------------------------- */
/* ======================================================================== */

static void build_header(lv_obj_t *scr)
{
    lv_obj_t *h = make_card(scr, PAD, HEAD_Y, SCR_W - 2 * PAD, HEAD_H, COL_CARD_BORDER, COL_WIND_NEON);
    card_no_accent(h);

    /* Left: live link indicator (dot only — no vendor branding) */
    hdr_dot = lv_obj_create(h);
    lv_obj_remove_style_all(hdr_dot);
    lv_obj_set_size(hdr_dot, 10, 10);
    lv_obj_set_style_radius(hdr_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hdr_dot, COL_IDLE, 0);
    lv_obj_set_style_bg_opa(hdr_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(hdr_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(hdr_dot, LV_ALIGN_LEFT_MID, 8, 0);

    /* Hub link strength — ascending bars, color-graded like a phone signal icon */
    hdr_signal = lv_obj_create(h);
    lv_obj_remove_style_all(hdr_signal);
    lv_obj_set_size(hdr_signal, 26, 18);
    lv_obj_clear_flag(hdr_signal, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(hdr_signal, LV_ALIGN_LEFT_MID, 24, 0);

    static const int bar_h[SIG_BARS] = {4, 7, 10, 13, 16};
    for (int i = 0; i < SIG_BARS; i++) {
        hdr_sig_bars[i] = lv_obj_create(hdr_signal);
        lv_obj_remove_style_all(hdr_sig_bars[i]);
        lv_obj_set_size(hdr_sig_bars[i], 3, bar_h[i]);
        lv_obj_set_style_radius(hdr_sig_bars[i], 1, 0);
        lv_obj_set_style_bg_color(hdr_sig_bars[i], COL_SIG_DIM, 0);
        lv_obj_set_style_bg_opa(hdr_sig_bars[i], LV_OPA_40, 0);
        lv_obj_clear_flag(hdr_sig_bars[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(hdr_sig_bars[i], LV_ALIGN_BOTTOM_LEFT, i * 5, 0);
    }

    hdr_bat_icon = label(h, &lv_font_montserrat_14, COL_OK, LV_SYMBOL_BATTERY_FULL);
    lv_obj_align(hdr_bat_icon, LV_ALIGN_LEFT_MID, 54, 0);

    hdr_health = label(h, &lv_font_montserrat_14, COL_DIM, "BAT 2.75V");
    lv_obj_align(hdr_health, LV_ALIGN_LEFT_MID, 76, 0);

    lv_obj_t *ub = lv_button_create(h);
    lv_obj_set_size(ub, 40, 26);
    lv_obj_align(ub, LV_ALIGN_LEFT_MID, 172, 0);
    lv_obj_set_style_bg_color(ub, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(ub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ub, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(ub, 1, 0);
    lv_obj_set_style_radius(ub, 6, 0);
    lv_obj_set_style_pad_all(ub, 0, 0);
    lv_obj_set_ext_click_area(ub, 8);
    lv_obj_add_event_cb(ub, on_toggle_units, LV_EVENT_CLICKED, NULL);
    hdr_units = lv_label_create(ub);
    lv_label_set_text(hdr_units, "°F");
    lv_obj_set_style_text_color(hdr_units, COL_TEXT, 0);
    lv_obj_set_style_text_font(hdr_units, &lv_font_montserrat_14, 0);
    lv_obj_center(hdr_units);

    ui_create_page_dots(h, 222, 16, UI_PAGE_DASHBOARD);

    /* Center: Date */
    hdr_date = label(h, &lv_font_montserrat_16, COL_DIM, "");
    lv_obj_set_style_text_letter_space(hdr_date, 1, 0);
    lv_obj_align(hdr_date, LV_ALIGN_CENTER, 0, 0);

    hdr_aqi = label(h, &lv_font_montserrat_14, COL_DIM, "AQI --");
    paint_badge(hdr_aqi, "AQI --", COL_DIM);
    lv_obj_add_flag(hdr_aqi, LV_OBJ_FLAG_HIDDEN);

    /* Right: Clock, page nav, settings */
    hdr_clock = label(h, &lv_font_montserrat_20, COL_TEXT, "--:--");
    lv_obj_align(hdr_clock, LV_ALIGN_RIGHT_MID, -148, 0);
    lv_obj_align_to(hdr_aqi, hdr_clock, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    ui_create_page_button(h, LV_ALIGN_RIGHT_MID, -60, 0, UI_PAGE_DASHBOARD);
    ui_create_gear_button(h, LV_ALIGN_RIGHT_MID, -6, 0);
}

static void build_ticker(lv_obj_t *scr)
{
    s_ticker = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ticker);
    lv_obj_set_pos(s_ticker, 0, TICKER_Y);
    lv_obj_set_size(s_ticker, SCR_W, TICKER_H);
    lv_obj_set_style_bg_color(s_ticker, lv_color_hex(0x1A1408), 0);
    lv_obj_set_style_bg_opa(s_ticker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ticker, 0, 0);
    lv_obj_clear_flag(s_ticker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ticker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ticker, on_ticker, LV_EVENT_CLICKED, NULL);

    s_ticker_tag = lv_label_create(s_ticker);
    lv_label_set_text(s_ticker_tag, "ALERT");
    lv_obj_set_style_text_font(s_ticker_tag, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(s_ticker_tag, 1, 0);
    lv_obj_set_style_text_color(s_ticker_tag, COL_BG, 0);
    lv_obj_set_style_bg_color(s_ticker_tag, COL_TEMP_AMBER, 0);
    lv_obj_set_style_bg_opa(s_ticker_tag, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(s_ticker_tag, 10, 0);
    lv_obj_set_style_pad_right(s_ticker_tag, 10, 0);
    lv_obj_set_style_pad_top(s_ticker_tag, 4, 0);
    lv_obj_set_style_pad_bottom(s_ticker_tag, 4, 0);
    lv_obj_set_style_radius(s_ticker_tag, 0, 0);
    lv_obj_align(s_ticker_tag, LV_ALIGN_LEFT_MID, 0, 0);

    s_ticker_lbl = lv_label_create(s_ticker);
    lv_label_set_text(s_ticker_lbl, "No active weather alerts");
    lv_obj_set_style_text_font(s_ticker_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ticker_lbl, COL_DIM, 0);
    lv_obj_set_width(s_ticker_lbl, SCR_W - 92);
    lv_label_set_long_mode(s_ticker_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(s_ticker_lbl, 36000, 0);
    lv_obj_align(s_ticker_lbl, LV_ALIGN_LEFT_MID, 88, 0);
}

static void build_top_deck(lv_obj_t *scr)
{
    /* --- ZONE 1: OUTDOOR TEMPERATURE --- */
    temp_card = make_card(scr, PAD, TOP_Y, Z1_W, TOP_H, COL_CARD_BORDER, COL_TEMP_HOT);
    lv_obj_t *z1 = temp_card;
    lv_obj_add_flag(z1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z1, on_toggle_units, LV_EVENT_CLICKED, NULL);
    temp_glow = lv_obj_get_child(z1, 0);

    lv_obj_t *t1 = kicker(z1, COL_TEMP_HOT, "OUTDOOR");
    lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 6, 2);

    temp_badge = label(z1, &lv_font_montserrat_14, COL_DIM, "--");
    paint_badge(temp_badge, "--", COL_DIM);
    lv_obj_align(temp_badge, LV_ALIGN_TOP_RIGHT, -6, 2);

    temp_arc = make_arc_ring(z1, Z1_CX, Z1_CY, Z1_ARC, Z1_STROKE, COL_TRACK, COL_TEMP_HOT, false);
    build_temp_scale_ticks(z1, Z1_CX, Z1_CY, Z1_KR + 4);
    temp_knob = make_knob(z1, COL_TEMP_HOT, Z1_KNOB);
    place_knob_r(temp_knob, Z1_CX, Z1_CY, 0.5f, Z1_KR, Z1_KNOB);

    temp_val = clabel(z1, Z1_CX, Z1_CY - 28, 160, &lv_font_montserrat_46, COL_TEXT, "--");
    temp_unit = clabel(z1, Z1_CX, Z1_CY + 16, 80, &lv_font_montserrat_14, COL_DIM, "°F");
    temp_feels = clabel(z1, Z1_CX, Z1_CY + 36, 200, &lv_font_montserrat_14, COL_DIM, "feels --°");

    temp_lo = label(z1, &lv_font_montserrat_14, COL_TEMP_COLD, "--");
    lv_obj_set_pos(temp_lo, 6, 174);
    temp_hi = label(z1, &lv_font_montserrat_14, COL_TEMP_HOT, "--");
    lv_obj_align(temp_hi, LV_ALIGN_TOP_RIGHT, -6, 174);

    temp_range_track = bare_bar(z1, 40, 182, Z1_W - 80, 4, COL_TRACK, LV_OPA_COVER);
    lv_obj_set_style_radius(temp_range_track, 2, 0);
    lv_obj_add_flag(temp_range_track, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    temp_range_fill = bare_bar(temp_range_track, 0, 0, 8, 4, COL_TEMP_AMBER, LV_OPA_COVER);
    lv_obj_set_style_radius(temp_range_fill, 2, 0);
    temp_now = bare_bar(temp_range_track, 0, -3, 3, 10, COL_TEXT, LV_OPA_COVER);
    lv_obj_set_style_radius(temp_now, 1, 0);

    temp_dew = label(z1, &lv_font_montserrat_14, COL_DIM, "Dew --°");
    lv_obj_set_pos(temp_dew, 6, 202);
    temp_rh = clabel(z1, Z1_CX, 202, 80, &lv_font_montserrat_14, COL_DIM, "RH --%");
    temp_trend = label(z1, &lv_font_montserrat_14, COL_FAINT, "--");
    lv_obj_align(temp_trend, LV_ALIGN_TOP_RIGHT, -6, 202);

    /* --- ZONE 2: SPLIT WIND COMPASS & SPEED --- */
    lv_obj_t *z2 = make_card(scr, PAD + Z1_W + 6, TOP_Y, Z2_W, TOP_H, COL_CARD_BORDER, COL_WIND_NEON);
    lv_obj_add_flag(z2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z2, on_toggle_units, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t2 = kicker(z2, COL_WIND_NEON, "WIND & GUST");
    lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 6, 2);

    /* Left half: Clean Radar Compass (cx=64, cy=96, size=108) */
    const int w_cx = 64;
    const int w_cy = 96;
    const int w_box = 108;
    const int w_r = 48;

    make_arc_ring(z2, w_cx, w_cy, w_box, 6, COL_TRACK, COL_WIND_NEON, true);
    build_compass_ticks(z2, w_cx, w_cy, w_r);

    clabel(z2, w_cx, w_cy - 56, 18, &lv_font_montserrat_14, COL_DIM, "N");
    clabel(z2, w_cx, w_cy + 42, 18, &lv_font_montserrat_14, COL_DIM, "S");
    clabel(z2, w_cx + 50, w_cy - 8, 18, &lv_font_montserrat_14, COL_DIM, "E");
    clabel(z2, w_cx - 50, w_cy - 8, 18, &lv_font_montserrat_14, COL_DIM, "W");

    /* Rotating Direction Pointer */
    needle_tail_pts[0].x = w_cx; needle_tail_pts[0].y = w_cy;
    needle_tail_pts[1].x = w_cx; needle_tail_pts[1].y = w_cy + 14;
    wind_needle_tail = lv_line_create(z2);
    lv_line_set_points(wind_needle_tail, needle_tail_pts, 2);
    lv_obj_set_style_line_width(wind_needle_tail, 3, 0);
    lv_obj_set_style_line_color(wind_needle_tail, COL_NEEDLE_TAIL, 0);
    lv_obj_set_style_line_rounded(wind_needle_tail, true, 0);

    needle_head_pts[0].x = w_cx; needle_head_pts[0].y = w_cy;
    needle_head_pts[1].x = w_cx; needle_head_pts[1].y = w_cy - 38;
    wind_needle_head = lv_line_create(z2);
    lv_line_set_points(wind_needle_head, needle_head_pts, 2);
    lv_obj_set_style_line_width(wind_needle_head, 5, 0);
    lv_obj_set_style_line_color(wind_needle_head, COL_NEEDLE_HEAD, 0);
    lv_obj_set_style_line_rounded(wind_needle_head, true, 0);

    wind_hub = lv_obj_create(z2);
    lv_obj_remove_style_all(wind_hub);
    lv_obj_set_size(wind_hub, 10, 10);
    lv_obj_set_style_radius(wind_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(wind_hub, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(wind_hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(wind_hub, COL_CARD, 0);
    lv_obj_set_style_border_width(wind_hub, 2, 0);
    lv_obj_clear_flag(wind_hub, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(wind_hub, w_cx - 5, w_cy - 5);

    /* Right half: Dedicated Wind Speed & Metrics (cx=184) */
    const int spd_cx = 184;
    wind_val = clabel(z2, spd_cx, 42, 110, &lv_font_montserrat_46, COL_TEXT, "--");
    wind_unit = clabel(z2, spd_cx, 90, 110, &lv_font_montserrat_16, COL_WIND_NEON, "MPH");
    wind_dir_deg = clabel(z2, spd_cx, 112, 110, &lv_font_montserrat_16, COL_TEXT, "0° N");
    wind_beaufort_lbl = clabel(z2, spd_cx, 136, 120, &lv_font_montserrat_14, COL_DIM, "Calm");

    /* Bottom: Beaufort Scale Bar + Gust/Avg readouts */
    wind_beaufort_bar = lv_bar_create(z2);
    lv_obj_set_size(wind_beaufort_bar, Z2_W - 24, 6);
    lv_obj_align(wind_beaufort_bar, LV_ALIGN_BOTTOM_LEFT, 6, -32);
    lv_obj_set_style_bg_color(wind_beaufort_bar, COL_TRACK, 0);
    lv_obj_set_style_bg_color(wind_beaufort_bar, COL_WIND_NEON, LV_PART_INDICATOR);
    lv_obj_set_style_radius(wind_beaufort_bar, 3, 0);
    lv_bar_set_range(wind_beaufort_bar, 0, 100);
    lv_bar_set_value(wind_beaufort_bar, 0, LV_ANIM_OFF);

    wind_gust_lbl = label(z2, &lv_font_montserrat_14, COL_DIM, "Gust: 0.0 mph");
    lv_obj_align(wind_gust_lbl, LV_ALIGN_BOTTOM_LEFT, 6, -8);

    wind_avg_lbl = label(z2, &lv_font_montserrat_14, COL_DIM, "Avg: 0.0");
    lv_obj_align(wind_avg_lbl, LV_ALIGN_BOTTOM_RIGHT, -6, -8);

    /* --- ZONE 3: INDOOR CLIMATE & DUAL GAUGES --- */
    lv_obj_t *z3 = make_card(scr, PAD + Z1_W + Z2_W + 12, TOP_Y, Z3_W, TOP_H, COL_CARD_BORDER, COL_INDOOR_TEMP);
    in_temp_glow = lv_obj_get_child(z3, 0);

    in_temp_kicker = kicker(z3, COL_INDOOR_TEMP, "INDOOR CLIMATE");
    lv_obj_align(in_temp_kicker, LV_ALIGN_TOP_LEFT, 6, 2);

    /* Left Ring: Indoor Temp */
    const int in_t_cx = 62;
    const int in_cy = 82;
    in_temp_arc = make_arc_ring(z3, in_t_cx, in_cy, 96, 8, COL_TRACK, COL_INDOOR_TEMP, false);
    in_temp_knob = make_knob(z3, COL_INDOOR_TEMP, 14);
    place_knob_r(in_temp_knob, in_t_cx, in_cy, 0.0f, 44, 14);
    in_temp_val = clabel(z3, in_t_cx, in_cy - 16, 90, &lv_font_montserrat_24, COL_TEXT, "--");
    in_temp_cap = clabel(z3, in_t_cx, in_cy + 52, 90, &lv_font_montserrat_14, COL_DIM, "TEMP");

    /* Right Ring: Indoor Humidity */
    const int in_h_cx = Z3_W - 62;
    in_hum_arc = make_arc_ring(z3, in_h_cx, in_cy, 96, 8, COL_TRACK, COL_INDOOR_HUM, false);
    in_hum_knob = make_knob(z3, COL_INDOOR_HUM, 14);
    place_knob_r(in_hum_knob, in_h_cx, in_cy, 0.0f, 44, 14);
    in_hum_val = clabel(z3, in_h_cx, in_cy - 16, 90, &lv_font_montserrat_24, COL_TEXT, "--");
    in_hum_cap = clabel(z3, in_h_cx, in_cy + 52, 90, &lv_font_montserrat_14, COL_DIM, "HUMIDITY");

    /* Comfort Badge */
    in_comfort_badge = clabel(z3, Z3_W / 2, 166, 220, &lv_font_montserrat_16, COL_OK, "Comfortable");
    in_status_lbl = clabel(z3, Z3_W / 2, 196, 220, &lv_font_montserrat_14, COL_FAINT, "sensor active");

    /* Optional sensor placeholder container */
    in_no_sensor_view = lv_obj_create(z3);
    lv_obj_set_pos(in_no_sensor_view, 10, 36);
    lv_obj_set_size(in_no_sensor_view, Z3_W - 20, 176);
    lv_obj_set_style_bg_opa(in_no_sensor_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(in_no_sensor_view, 0, 0);
    lv_obj_clear_flag(in_no_sensor_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *in_opt_icon = label(in_no_sensor_view, &lv_font_montserrat_34, COL_INDOOR_TEMP, LV_SYMBOL_HOME);
    lv_obj_align(in_opt_icon, LV_ALIGN_TOP_MID, 0, 10);
    clabel(in_no_sensor_view, (Z3_W - 20) / 2, 60, Z3_W - 24, &lv_font_montserrat_16, COL_TEXT, "INDOOR MODULE");
    clabel(in_no_sensor_view, (Z3_W - 20) / 2, 88, Z3_W - 24, &lv_font_montserrat_14, COL_DIM, "Grove I2C Sensor Port");
    clabel(in_no_sensor_view, (Z3_W - 20) / 2, 114, Z3_W - 24, &lv_font_montserrat_14, COL_FAINT, "SHTC3 / SHT31 / BME280");

    /* --- ZONE 4: ANIMATED WEATHER CENTERPIECE --- */
    lv_obj_t *z4 = make_card(scr, PAD + Z1_W + Z2_W + Z3_W + 18, TOP_Y, Z4_W, TOP_H, COL_CARD_BORDER, COL_SUN_GOLD);

    lv_obj_t *t4 = kicker(z4, COL_SUN_GOLD, "CURRENT CONDITIONS");
    lv_obj_align(t4, LV_ALIGN_TOP_LEFT, 6, 2);

    cond_badge = label(z4, &lv_font_montserrat_14, COL_OK, "LIVE");
    paint_badge(cond_badge, "LIVE", COL_OK);
    lv_obj_align(cond_badge, LV_ALIGN_TOP_RIGHT, -6, 2);

    /* Static bitmap at boot. ThorVG is promoted after the dashboard is on
     * glass — creating a Lottie here is what painted the light-blue crash. */
    s_cond_parent = z4;
    s_cond_lottie = false;
    s_cond_slug[0] = '\0';
    cond_make_bitmap();
    cond_set_icon("clear-day");

    cond_title = clabel(z4, Z4_W / 2, 148, Z4_W - 12, &lv_font_montserrat_24, COL_TEXT, "--");
    cond_sub = clabel(z4, Z4_W / 2, 182, Z4_W - 12, &lv_font_montserrat_14, COL_DIM, "Today: Hi --°  Lo --°");
}

static void build_mid_deck(lv_obj_t *scr)
{
    /* --- MID 1: PRECIPITATION & RAIN CYLINDER --- */
    lv_obj_t *m1 = make_card(scr, PAD, MID_Y, M1_W, MID_H, COL_CARD_BORDER, COL_RAIN_NEON);
    lv_obj_add_flag(m1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m1, on_toggle_units, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t1 = kicker(m1, COL_RAIN_NEON, "PRECIPITATION");
    lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 8, 6);

    rain_badge = label(m1, &lv_font_montserrat_14, COL_DIM, "Dry");
    paint_badge(rain_badge, "Dry", COL_DIM);
    lv_obj_align(rain_badge, LV_ALIGN_TOP_RIGHT, -8, 6);

    /* Stylized Beaker / Cylinder (Left side) */
    lv_obj_t *cyl_bg = lv_obj_create(m1);
    lv_obj_remove_style_all(cyl_bg);
    lv_obj_set_size(cyl_bg, 26, 76);
    lv_obj_set_pos(cyl_bg, 8, 32);
    lv_obj_set_style_bg_color(cyl_bg, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(cyl_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cyl_bg, COL_RAIN_NEON, 0);
    lv_obj_set_style_border_width(cyl_bg, 2, 0);
    lv_obj_set_style_radius(cyl_bg, 6, 0);
    lv_obj_set_style_pad_all(cyl_bg, 2, 0);
    lv_obj_clear_flag(cyl_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int tk = 1; tk <= 3; tk++) {
        bare_bar(cyl_bg, 0, 70 - tk * 18, 8, 1, COL_DIM, LV_OPA_COVER);
    }

    rain_cylinder_fill = lv_obj_create(cyl_bg);
    lv_obj_remove_style_all(rain_cylinder_fill);
    lv_obj_set_size(rain_cylinder_fill, 18, 10);
    lv_obj_align(rain_cylinder_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(rain_cylinder_fill, COL_RAIN_NEON, 0);
    lv_obj_set_style_bg_opa(rain_cylinder_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rain_cylinder_fill, 3, 0);
    lv_obj_clear_flag(rain_cylinder_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Readouts (Right side) */
    rain_val = label(m1, &lv_font_montserrat_34, COL_TEXT, "0.00 in");
    lv_obj_set_pos(rain_val, 42, 32);

    rain_rate_lbl = label(m1, &lv_font_montserrat_14, COL_DIM, "Rate: 0.00 in/hr");
    lv_obj_set_pos(rain_rate_lbl, 42, 74);

    rain_totals_lbl = label(m1, &lv_font_montserrat_14, COL_FAINT, "7d --  /  Mo --  /  YTD --");
    lv_obj_set_pos(rain_totals_lbl, 8, 108);
    lv_obj_set_width(rain_totals_lbl, M1_W - 16);
    lv_label_set_long_mode(rain_totals_lbl, LV_LABEL_LONG_CLIP);

    /* --- MID 2: DEDICATED LIGHTNING DETECTOR --- */
    ltg_card = make_card(scr, PAD + M1_W + 6, MID_Y, M2_W, MID_H, COL_CARD_BORDER, COL_UV_MOD);
    lv_obj_t *m2 = ltg_card;

    lv_obj_t *t2 = kicker(m2, COL_UV_MOD, "LIGHTNING");
    lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 8, 6);

    ltg_badge = label(m2, &lv_font_montserrat_14, COL_OK, "Clear");
    paint_badge(ltg_badge, "Clear", COL_OK);
    lv_obj_align(ltg_badge, LV_ALIGN_TOP_RIGHT, -8, 6);

    /* FontAwesome Lightning Bolt Symbol */
    lv_obj_t *ltg_icon = label(m2, &lv_font_montserrat_34, COL_UV_MOD, LV_SYMBOL_CHARGE);
    lv_obj_set_pos(ltg_icon, 12, 34);

    ltg_count_val = label(m2, &lv_font_montserrat_34, COL_TEXT, "0");
    lv_obj_set_pos(ltg_count_val, 50, 34);

    ltg_count_lbl = label(m2, &lv_font_montserrat_14, COL_DIM, "strikes (last 3h)");
    lv_obj_set_pos(ltg_count_lbl, 82, 48);

    ltg_dist_lbl = label(m2, &lv_font_montserrat_14, COL_TEXT, "No strikes in 3 hrs");
    lv_obj_set_pos(ltg_dist_lbl, 10, 80);

    ltg_status_lbl = label(m2, &lv_font_montserrat_14, COL_FAINT, "Live detector active");
    lv_obj_set_pos(ltg_status_lbl, 10, 106);

    /* --- MID 3: SOLAR RADIATION & UV --- */
    lv_obj_t *m3 = make_card(scr, PAD + M1_W + M2_W + 12, MID_Y, M3_W, MID_H, COL_CARD_BORDER, COL_SUN_GOLD);

    lv_obj_t *t3 = kicker(m3, COL_SUN_GOLD, "SOLAR");
    lv_obj_align(t3, LV_ALIGN_TOP_LEFT, 8, 6);

    /* Solar Irradiance placed cleanly in header row */
    solar_rad_lbl = label(m3, &lv_font_montserrat_14, COL_SUN_GOLD, "0 W/m2");
    lv_obj_align(solar_rad_lbl, LV_ALIGN_TOP_RIGHT, -8, 6);

    /* Parabolic Solar Trajectory Arc */
    const int arc_w = M3_W - 36;
    const int arc_x0 = 18;
    const int arc_y_base = 56;
    const int arc_h = 24;

    for (int i = 0; i <= 16; i++) {
        float f = (float)i / 16.0f;
        sun_arc_pts[i].x = arc_x0 + (int)(f * arc_w);
        sun_arc_pts[i].y = arc_y_base - (int)(sinf(f * (float)M_PI) * arc_h);
    }
    sun_arc_line = lv_line_create(m3);
    lv_line_set_points(sun_arc_line, sun_arc_pts, 17);
    lv_obj_set_style_line_width(sun_arc_line, 2, 0);
    lv_obj_set_style_line_color(sun_arc_line, COL_TRACK, 0);
    lv_obj_set_style_line_rounded(sun_arc_line, true, 0);

    /* Golden Sun Marker on the arch */
    sun_marker = make_knob(m3, COL_SUN_GOLD, 14);
    lv_obj_set_pos(sun_marker, arc_x0 + 40 - 7, arc_y_base - 18 - 7);

    /* Sunrise & Sunset - cleanly spaced under the arc */
    sun_rise_lbl = label(m3, &lv_font_montserrat_14, COL_DIM, "Rise --:--");
    lv_obj_align(sun_rise_lbl, LV_ALIGN_TOP_LEFT, 6, 64);

    sun_set_lbl = label(m3, &lv_font_montserrat_14, COL_DIM, "Set --:--");
    lv_obj_align(sun_set_lbl, LV_ALIGN_TOP_RIGHT, -6, 64);

    /* Row 1: EPA 5-Segment Color UV Meter & UV Badge (y = 88) */
    lv_color_t uv_cols[5] = { COL_UV_LOW, COL_UV_MOD, COL_UV_HIGH, COL_UV_VHIGH, COL_UV_EXTREME };
    for (int u = 0; u < 5; u++) {
        uv_tier_bars[u] = bare_bar(m3, 8 + u * 10, 94, 8, 4, uv_cols[u], LV_OPA_30);
        lv_obj_set_style_radius(uv_tier_bars[u], 2, 0);
    }
    uv_badge = label(m3, &lv_font_montserrat_14, COL_UV_LOW, "UV 0.0 (Low)");
    paint_badge(uv_badge, "UV 0.0 (Low)", COL_UV_LOW);
    lv_obj_set_pos(uv_badge, 64, 88);

    /* Row 2: Optical Illuminance Lux (y = 114) */
    solar_lux_lbl = label(m3, &lv_font_montserrat_14, COL_TEXT, "Lux: --");
    lv_obj_align(solar_lux_lbl, LV_ALIGN_TOP_LEFT, 8, 114);

    solar_daylight_lbl = label(m3, &lv_font_montserrat_14, COL_DIM, "Daylight: --");
    lv_obj_align(solar_daylight_lbl, LV_ALIGN_TOP_RIGHT, -8, 114);

    /* --- MID 4: BAROMETRIC PRESSURE & 12H TREND CHART --- */
    lv_obj_t *m4 = make_card(scr, PAD + M1_W + M2_W + M3_W + 18, MID_Y, M4_W, MID_H, COL_CARD_BORDER, COL_PRESS_NEON);
    lv_obj_add_flag(m4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m4, on_toggle_units, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t4 = kicker(m4, COL_PRESS_NEON, "BAROMETER");
    lv_obj_align(t4, LV_ALIGN_TOP_LEFT, 8, 6);

    baro_badge = label(m4, &lv_font_montserrat_14, COL_OK, "Steady");
    paint_badge(baro_badge, "Steady", COL_PRESS_NEON);
    lv_obj_align(baro_badge, LV_ALIGN_TOP_RIGHT, -8, 6);

    baro_val = label(m4, &lv_font_montserrat_34, COL_TEXT, "29.92 inHg");
    lv_obj_set_pos(baro_val, 10, 32);

    baro_trend_lbl = label(m4, &lv_font_montserrat_14, COL_DIM, "+0.0 mb/3h");
    lv_obj_set_pos(baro_trend_lbl, 10, 74);

    /* 10-Bar Pressure Shift Visualization */
    for (int b = 0; b < 10; b++) {
        baro_bars[b] = bare_bar(m4, 10 + b * 22, 106, 14, 18, COL_TRACK, LV_OPA_COVER);
        lv_obj_set_style_radius(baro_bars[b], 3, 0);
    }
}

static void fc_place_icon(int idx)
{
    if (!fc[idx].icon || !fc[idx].card) {
        return;
    }
    /* TOP_MID recenters when the card gets its real width. set_pos with
     * lv_obj_get_width() during ui_init lands at x = -22 because the
     * parent is still 0-wide; columns that stay on the init slug (clear-day)
     * then never get a second placement. */
    lv_obj_align(fc[idx].icon, LV_ALIGN_TOP_MID, 0, FC_ICON_Y);
}

static void fc_set_icon(int idx, const char *slug)
{
    if (!slug || !slug[0] || idx < 0 || idx >= FC_COLS || !fc[idx].icon) {
        return;
    }
    if (strncmp(s_fc_icon_slug[idx], slug, sizeof(s_fc_icon_slug[idx])) == 0) {
        fc_place_icon(idx);
        return;
    }
    strncpy(s_fc_icon_slug[idx], slug, sizeof(s_fc_icon_slug[idx]) - 1);
    s_fc_icon_slug[idx][sizeof(s_fc_icon_slug[idx]) - 1] = '\0';

    if (s_fc_lottie[idx]) {
        if (!wx_icon_set(fc[idx].icon, slug)) {
            fc_make_bitmap(idx);
            const lv_image_dsc_t *dsc = wx_icon_get_image_dsc(slug);
            if (dsc) {
                lv_image_set_src(fc[idx].icon, dsc);
            }
        }
    } else {
        const lv_image_dsc_t *dsc = wx_icon_get_image_dsc(slug);
        if (dsc) {
            lv_image_set_src(fc[idx].icon, dsc);
        }
    }
    fc_place_icon(idx);
}

static void fc_make_bitmap(int idx)
{
    if (!fc[idx].card) {
        return;
    }
    if (fc[idx].icon) {
        lv_obj_delete(fc[idx].icon);
        fc[idx].icon = NULL;
    }
    s_fc_lottie[idx] = false;
    fc[idx].icon = lv_image_create(fc[idx].card);
    lv_obj_remove_style_all(fc[idx].icon);
    lv_obj_set_size(fc[idx].icon, FC_ICON_SZ, FC_ICON_SZ);
    lv_obj_set_style_bg_opa(fc[idx].icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fc[idx].icon, 0, 0);
    lv_obj_set_style_pad_all(fc[idx].icon, 0, 0);
    fc_place_icon(idx);
}

static void cond_place(void)
{
    if (cond_icon) {
        lv_obj_align(cond_icon, LV_ALIGN_TOP_MID, 0, 26);
    }
}

static void cond_make_bitmap(void)
{
    if (!s_cond_parent) {
        return;
    }
    if (cond_icon) {
        lv_obj_delete(cond_icon);
        cond_icon = NULL;
    }
    s_cond_lottie = false;
    cond_icon = lv_image_create(s_cond_parent);
    lv_obj_set_size(cond_icon, COND_ICON_SZ, COND_ICON_SZ);
    lv_obj_remove_style_all(cond_icon);
    lv_obj_set_style_bg_opa(cond_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cond_icon, 0, 0);
    lv_obj_set_style_pad_all(cond_icon, 0, 0);
    cond_place();
}

static void cond_set_icon(const char *slug)
{
    if (!slug || !slug[0] || !cond_icon) {
        return;
    }
    if (strncmp(s_cond_slug, slug, sizeof(s_cond_slug)) == 0) {
        return;
    }
    strncpy(s_cond_slug, slug, sizeof(s_cond_slug) - 1);
    s_cond_slug[sizeof(s_cond_slug) - 1] = '\0';

    if (s_cond_lottie) {
        wx_icon_set(cond_icon, slug);
    } else {
        const lv_image_dsc_t *dsc = wx_icon_get_image_dsc(slug);
        if (dsc) {
            lv_image_set_src(cond_icon, dsc);
        }
    }
    cond_place();
}

static void cond_promote(void)
{
    if (s_cond_lottie || !s_cond_parent) {
        return;
    }
    char slug[40];
    strncpy(slug, s_cond_slug[0] ? s_cond_slug : "clear-day", sizeof(slug) - 1);
    slug[sizeof(slug) - 1] = '\0';

    if (cond_icon) {
        lv_obj_delete(cond_icon);
        cond_icon = NULL;
    }

    cond_icon = wx_icon_create(s_cond_parent, COND_ICON_SZ, true);
    if (!cond_icon) {
        ESP_LOGW(TAG, "hero lottie failed; keeping bitmap");
        cond_make_bitmap();
    } else {
        s_cond_lottie = true;
        cond_place();
        ESP_LOGI(TAG, "hero icon promoted to lottie");
    }
    s_cond_slug[0] = '\0';
    cond_set_icon(slug);
}

/* step 0 = hero, then done — forecast strip stays bitmaps */
static void icon_tick_promote(void)
{
    if (s_boot_ms == 0 || s_icon_promote_step < 0 || s_icon_promote_step > FC_COLS) {
        return;
    }
    /* ThorVG + HTTPS on the C6 at the same time blanks this panel.
     * Restart the settle clock while the radio is busy, otherwise the 5 s
     * wait expires during the fetch and the first Lottie starts on the
     * very next tick — which is when MIPI drops and the backlight stays on. */
    if (display_https_busy()) {
        s_last_promote_ms = lv_tick_get();
        return;
    }
    if (!net_is_connected() && (lv_tick_get() - s_boot_ms) < 15000) {
        return;
    }
    uint32_t now_ms = lv_tick_get();
    if (now_ms - s_last_promote_ms < 8000) {
        return;
    }

    if (s_icon_promote_step == 0) {
        cond_promote();
        s_last_promote_ms = now_ms;
        /* Forecast strip stays on bitmaps. At 44px ThorVG drops the
         * clear-day sun core (a ~13px gradient) and leaves the rotating
         * rays, which look like a tiny yellow wedge — Tue/Wed on this
         * station. The 112px hero is large enough for the same file. */
        s_icon_promote_step = FC_COLS + 1;
    }
}

void ui_mark_panel_visible(void)
{
    s_boot_ms = lv_tick_get();
    s_last_promote_ms = s_boot_ms;
    if (s_icon_promote_step < 0) {
        s_icon_promote_step = 0;
    }
}

void ui_forecast_mode_changed(void)
{
    /* Strip is bitmaps only. Demote anything an older build promoted. */
    for (int i = 0; i < FC_COLS; i++) {
        if (!s_fc_lottie[i]) {
            continue;
        }
        char slug[40];
        strncpy(slug, s_fc_icon_slug[i][0] ? s_fc_icon_slug[i] : "clear-day",
                sizeof(slug) - 1);
        slug[sizeof(slug) - 1] = '\0';
        fc_make_bitmap(i);
        s_fc_icon_slug[i][0] = '\0';
        fc_set_icon(i, slug);
    }
    s_icon_promote_step = FC_COLS + 1;
}

void ui_notify_forecast_updated(void)
{
    /* Called from tempest_rest — never touch LVGL here; ui_tick() redraws. */
    s_forecast_dirty = true;
}

static void build_forecast_deck(lv_obj_t *scr)
{
    s_forecast_panel = make_card(scr, PAD, FC_Y, SCR_W - 2 * PAD, FC_H, COL_CARD_BORDER, COL_TEMP_AMBER);
    lv_obj_t *p = s_forecast_panel;
    const int inner_w = (SCR_W - 2 * PAD) - 2 * FC_INSET;
    const int col_w = inner_w / FC_COLS;

    lv_obj_t *fc_title = kicker(p, COL_TEMP_AMBER, "7-DAY FORECAST");
    lv_obj_align(fc_title, LV_ALIGN_TOP_MID, 0, 3);

    memset(s_fc_icon_slug, 0, sizeof(s_fc_icon_slug));
    memset(s_fc_lottie, 0, sizeof(s_fc_lottie));
    s_icon_promote_step = -1;

    for (int i = 0; i < FC_COLS; i++) {
        int x = FC_INSET + i * col_w;
        lv_obj_t *col_card = lv_obj_create(p);
        lv_obj_remove_style_all(col_card);
        lv_obj_set_pos(col_card, x, 16);
        lv_obj_set_size(col_card, col_w, FC_H - 24);
        lv_obj_set_style_bg_color(col_card, (i == 0) ? COL_CARD_TODAY : COL_CARD, 0);
        lv_obj_set_style_bg_opa(col_card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(col_card, 8, 0);
        lv_obj_set_style_border_width(col_card, 0, 0);
        lv_obj_set_style_pad_all(col_card, 0, 0);
        lv_obj_clear_flag(col_card, LV_OBJ_FLAG_SCROLLABLE);

        fc[i].card = col_card;
        const int cx = col_w / 2;

        fc[i].day = clabel(col_card, cx, 2, col_w, &lv_font_montserrat_14,
                           (i == 0) ? COL_WIND_NEON : COL_DIM, (i == 0) ? "TODAY" : "--");
        lv_obj_set_style_text_letter_space(fc[i].day, 1, 0);

        fc_make_bitmap(i);
        fc_set_icon(i, "clear-day");

        fc[i].pop = clabel(col_card, cx, 56, col_w, &lv_font_montserrat_14, COL_RAIN_NEON, "");

        fc[i].range_track = bare_bar(col_card, 10, 68, col_w - 20, 3, COL_TRACK, LV_OPA_COVER);
        lv_obj_set_style_radius(fc[i].range_track, 2, 0);
        fc[i].range_fill = bare_bar(fc[i].range_track, 0, 0, 4, 3, COL_TEMP_AMBER, LV_OPA_COVER);
        lv_obj_set_style_radius(fc[i].range_fill, 2, 0);

        fc[i].lo = clabel(col_card, cx - 38, 74, 36, &lv_font_montserrat_16, COL_TEMP_COLD, "--");
        fc[i].hi = clabel(col_card, cx + 38, 74, 36, &lv_font_montserrat_16, COL_TEMP_HOT, "--");

        if (i < FC_COLS - 1) {
            lv_obj_t *sep = lv_obj_create(p);
            lv_obj_remove_style_all(sep);
            lv_obj_set_pos(sep, x + col_w, 12);
            lv_obj_set_size(sep, 1, FC_H - 28);
            lv_obj_set_style_bg_color(sep, COL_CARD_BORDER, 0);
            lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(sep, 0, 0);
            lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    lv_obj_update_layout(p);
    for (int i = 0; i < FC_COLS; i++) {
        fc_place_icon(i);
    }
}

static void on_gear(lv_event_t *e)
{
    (void)e;
    settings_show();
}

static void on_page_btn(lv_event_t *e)
{
    (void)e;
    ui_page_next();
}

static void apply_brightness(int64_t now)
{
    (void)now;
    if (settings_is_visible() || wifi_setup_is_visible() || ota_in_progress()) {
        ui_note_user_activity();
    }

    /* First 90 s stay at full brightness. Night dim at 25% on this panel
     * looks unpowered — that is what "crashed" after the last flash. */
    uint32_t uptime_ms = s_boot_ms ? (lv_tick_get() - s_boot_ms) : 0;
    uint8_t target = 100;
    if (uptime_ms >= BOOT_BRIGHT_GRACE_MS) {
        target = cfg_brightness_now();
        if (target < 50) {
            target = 50;
        }

        cfg_t c;
        cfg_get(&c);
        if (c.screensaver_idle_min > 0) {
            uint32_t idle_ms = (uint32_t)c.screensaver_idle_min * 60u * 1000u;
            uint32_t idle = lv_tick_get() - s_last_input_ms;
            if (idle >= idle_ms) {
                s_screensaver_active = true;
                target = c.screensaver_brightness;
                if (target < 20) {
                    target = 20;
                }
            }
        }
    }

    s_scheduled_brightness = target;

    static uint8_t last_applied;
    if (target != last_applied) {
        last_applied = target;
        display_set_brightness(target);
    }
}

esp_err_t ui_init(void)
{
    s_main_screen = lv_screen_active();
    lv_obj_t *scr = s_main_screen;
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_dash = lv_obj_create(scr);
    lv_obj_set_size(s_dash, SCR_W, SCR_H);
    lv_obj_set_pos(s_dash, 0, 0);
    lv_obj_set_style_bg_opa(s_dash, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dash, 0, 0);
    lv_obj_set_style_pad_all(s_dash, 0, 0);
    lv_obj_clear_flag(s_dash, LV_OBJ_FLAG_SCROLLABLE);

    build_header(s_dash);
    build_top_deck(s_dash);
    build_mid_deck(s_dash);
    build_forecast_deck(s_dash);

    settings_init();
    graphs_init();
    page2_init();
    week_init();
    alerts_init();
    wifi_setup_init();
    /* Last child of layer_top so it stays above page overlays and sits
     * under Settings/Wi-Fi only until those call ui_ticker_raise(). */
    build_ticker(lv_layer_top());

    ui_attach_swipe_nav(s_main_screen);

    s_current_page = UI_PAGE_DASHBOARD;
    lv_screen_load(s_main_screen);
    lv_obj_invalidate(s_main_screen);
    s_ui_ready = true;

    cfg_t boot_cfg;
    cfg_get(&boot_cfg);
    if (boot_cfg.start_page > 0 && boot_cfg.start_page < UI_PAGE_COUNT) {
        ui_page_goto((ui_page_t)boot_cfg.start_page);
    }
    s_last_input_ms = lv_tick_get();
    /* s_boot_ms stays 0 until ui_mark_panel_visible() after SDIO. */
    s_scheduled_brightness = cfg_brightness_now();

    ESP_LOGI(TAG, "commercial weather console built (%dx%d)", SCR_W, SCR_H);
    return ESP_OK;
}

/* ======================================================================== */
/* ---- UI UPDATE TICKS --------------------------------------------------- */
/* ======================================================================== */

void ui_label_set(lv_obj_t *lbl, const char *text)
{
    if (!lbl || !text) {
        return;
    }
    const char *cur = lv_label_get_text(lbl);
    if (cur && strcmp(cur, text) == 0) {
        return;
    }
    lv_label_set_text(lbl, text);
}

void ui_label_setf(lv_obj_t *lbl, const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ui_label_set(lbl, buf);
}

void ui_fmt_clock(char *buf, size_t n, int64_t epoch, bool seconds)
{
    if (!buf || n == 0) {
        return;
    }
    time_t t = (time_t)epoch;
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(buf, n, seconds ? "%I:%M:%S %p" : "%I:%M %p", &lt);
    if (buf[0] == '0') {
        memmove(buf, buf + 1, strlen(buf));
    }
}

void ui_fmt_age(char *buf, size_t n, int64_t age_s)
{
    if (!buf || n == 0) {
        return;
    }
    if (age_s < 0) {
        snprintf(buf, n, "--");
    } else if (age_s < 90) {
        snprintf(buf, n, "%llds", (long long)age_s);
    } else if (age_s < 3600) {
        snprintf(buf, n, "%lldm", (long long)(age_s / 60));
    } else {
        snprintf(buf, n, "%lldh", (long long)(age_s / 3600));
    }
}

static void set_text(lv_obj_t *l, const char *fmt, ...)
{
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ui_label_set(l, buf);
}

static void update_hdr_signal(int hub_rssi, int dev_rssi)
{
    int rssi = 0;
    int8_t wifi_rssi = net_get_rssi();
    if (wifi_rssi != 0) {
        rssi = wifi_rssi;
    } else if (hub_rssi != 0) {
        rssi = hub_rssi;
    } else if (dev_rssi != 0) {
        rssi = dev_rssi;
    }

    int active = (rssi != 0) ? rssi_to_bars(rssi) : 0;
    for (int i = 0; i < SIG_BARS; i++) {
        bool on = (i < active);
        lv_obj_set_style_bg_color(hdr_sig_bars[i],
                                  on ? sig_bar_color(i) : COL_SIG_DIM, 0);
        lv_obj_set_style_bg_opa(hdr_sig_bars[i],
                                on ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

static void check_lightning_sound(const wx_state_t *s, int64_t now)
{
    bool ltg_near = s->last_strike_epoch > 0 &&
                    s->last_strike_dist_km > 0.0f &&
                    s->last_strike_dist_km <= LIGHTNING_NEAR_KM &&
                    (now - s->last_strike_epoch) < 3 * 3600;

    if (!ltg_near || s->last_strike_epoch == s_ltg_sound_epoch) {
        return;
    }

    cfg_t c;
    cfg_get(&c);
    if (!c.lightning_alert_sound && !c.lightning_alert_voice) {
        return;
    }
    if (audio_is_playing()) {
        return;
    }

    s_ltg_sound_epoch = s->last_strike_epoch;
    ESP_LOGW(TAG, "lightning %.1f km — proximity alert",
             (double)s->last_strike_dist_km);

    if (c.lightning_alert_sound) {
        audio_play_eas_siren(2);
    }
    if (c.lightning_alert_voice) {
        char msg[96];
        float dist = cfg_distance(s->last_strike_dist_km);
        snprintf(msg, sizeof(msg), "Lightning detected %.0f %s away.",
                 (double)dist, cfg_distance_suffix());
        audio_play_tts(msg);
    }
}

static void header_show_date(int64_t now)
{
    if (!hdr_date) {
        return;
    }
    time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%A, %b %d, %Y", &lt);
    ui_label_set(hdr_date, date_str);
}

static void ticker_paint(int mode, const char *tag, const char *crawl,
                         lv_color_t bar, lv_color_t tag_fg, lv_color_t tag_bg,
                         lv_color_t crawl_col)
{
    if (s_ticker_mode != mode) {
        s_ticker_mode = mode;
        lv_obj_set_style_bg_color(s_ticker, bar, 0);
        lv_obj_set_style_text_color(s_ticker_tag, tag_fg, 0);
        lv_obj_set_style_bg_color(s_ticker_tag, tag_bg, 0);
        lv_obj_set_style_text_color(s_ticker_lbl, crawl_col, 0);
        lv_label_set_text(s_ticker_tag, tag);
    }
    ui_label_set(s_ticker_lbl, crawl);
}

static void ticker_set_idle(const wx_state_t *s, int64_t now)
{
    char body[192];
    body[0] = '\0';
    size_t n = 0;
    if (s->obs_valid) {
        n += (size_t)snprintf(body + n, sizeof(body) - n, "Out %.0f%s",
                              (double)U_TEMP(s->air_temp_c), U_TEMP_SUF);
        if (s->indoor_valid) {
            n += (size_t)snprintf(body + n, sizeof(body) - n, "  In %.0f%s",
                                  (double)U_TEMP(s->indoor_temp_c), U_TEMP_SUF);
        }
        n += (size_t)snprintf(body + n, sizeof(body) - n, "  UV %.0f",
                              (double)s->uv_index);
    }
    nws_forecast_t nws = {0};
    if (nws_forecast_get(&nws) && nws.short_fc[0]) {
        n += (size_t)snprintf(body + n, sizeof(body) - n, "%sNWS %s",
                              n > 0 ? "  " : "", nws.short_fc);
    }
    int slot = wx_next_precip_slot(s, 30);
    if (slot >= 0) {
        char when[16];
        ui_fmt_clock(when, sizeof(when), s->hourly[slot].hour_epoch, false);
        n += (size_t)snprintf(body + n, sizeof(body) - n, "  Rain %s %d%%",
                              when, s->hourly[slot].precip_probability);
    } else if (s->hourly_valid) {
        n += (size_t)snprintf(body + n, sizeof(body) - n, "  Dry 24h");
    }
    if (n == 0) {
        snprintf(body, sizeof(body), "No active weather alerts");
    }

    char crawl[420];
    snprintf(crawl, sizeof(crawl), "%.180s          %.180s          ", body, body);
    ticker_paint(0, "LIVE", crawl,
                 lv_color_hex(0x0C1118), COL_DIM, COL_TRACK, COL_FAINT);
}

static void ticker_set_alert(int mode, const char *tag, const char *crawl, lv_color_t col)
{
    ticker_paint(mode, tag, crawl, lv_color_hex(0x1A1408), COL_BG, col, COL_TEXT);
}

static void update_alert_banner(const wx_state_t *s, int64_t now)
{
    if (s_current_page == UI_PAGE_DASHBOARD) {
        header_show_date(now);
    }
    if (!s_ticker || !s_ticker_lbl || !s_ticker_tag) {
        return;
    }

    nws_alert_t alert = {0};
    bool nws = nws_alerts_get_active(&alert);

    bool ltg_near = s->last_strike_epoch > 0 &&
                    s->last_strike_dist_km > 0.0f &&
                    s->last_strike_dist_km <= LIGHTNING_NEAR_KM &&
                    (now - s->last_strike_epoch) < 3 * 3600;

    if (nws && alert.event[0]) {
        char crawl[512];
        nws_format_ticker(&alert, crawl, sizeof(crawl));
        lv_color_t col = (strcmp(alert.severity, "Extreme") == 0)
            ? COL_ALERT : COL_TEMP_AMBER;
        ticker_set_alert(1, "ALERT", crawl, col);
    } else if (ltg_near) {
        float dist = cfg_distance(s->last_strike_dist_km);
        int mins = (int)((now - s->last_strike_epoch) / 60);
        if (mins < 1) {
            mins = 1;
        }
        char crawl[160];
        snprintf(crawl, sizeof(crawl),
                 "Lightning %.0f %s away, %d min ago          "
                 "Lightning %.0f %s away, %d min ago          ",
                 (double)dist, cfg_distance_suffix(), mins,
                 (double)dist, cfg_distance_suffix(), mins);
        ticker_set_alert(2, "STORM", crawl, COL_TEMP_AMBER);
    } else {
        ticker_set_idle(s, now);
    }
}

static void update_header(const wx_state_t *s, int64_t now)
{
    /* Digital Clock */
    time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%I:%M:%S %p", &lt);
    if (time_str[0] == '0') {
        memmove(time_str, time_str + 1, strlen(time_str));
    }
    ui_label_set(hdr_clock, time_str);
    lv_obj_align(hdr_clock, LV_ALIGN_RIGHT_MID, -148, 0);

    /* Date stays in the header. Alert copy crawls the bottom ticker. */

    /* Link indicator: green = live UDP, amber = degraded, red = offline */
    bool live = s->wifi_connected && s->obs_valid &&
                !wx_udp_is_stale(s) && !wx_obs_is_stale(s);
    if (!s->wifi_connected) {
        lv_obj_set_style_bg_color(hdr_dot, COL_ALERT, 0);
        lv_obj_set_style_bg_opa(hdr_dot, LV_OPA_COVER, 0);
    } else if (wx_udp_is_stale(s) || wx_obs_is_stale(s)) {
        lv_obj_set_style_bg_color(hdr_dot, COL_TEMP_AMBER, 0);
        lv_obj_set_style_bg_opa(hdr_dot, LV_OPA_COVER, 0);
    } else if (live) {
        lv_obj_set_style_bg_color(hdr_dot, COL_OK, 0);
        lv_obj_set_style_bg_opa(hdr_dot,
            ((lv_tick_get() / 500) & 1) ? LV_OPA_COVER : LV_OPA_40, 0);
    } else {
        lv_obj_set_style_bg_color(hdr_dot, COL_IDLE, 0);
        lv_obj_set_style_bg_opa(hdr_dot, LV_OPA_COVER, 0);
    }

    if (hdr_aqi) {
        if (s->aqi_valid && s->aqi_val > 0) {
            char aqi_buf[32];
            snprintf(aqi_buf, sizeof(aqi_buf), "AQI %d %s",
                     s->aqi_val, wx_aqi_epa_label(s->aqi_val));
            paint_badge(hdr_aqi, aqi_buf, aqi_color(s->aqi_val));
            lv_obj_align_to(hdr_aqi, hdr_clock, LV_ALIGN_OUT_LEFT_MID, -10, 0);
            lv_obj_clear_flag(hdr_aqi, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(hdr_aqi, LV_OBJ_FLAG_HIDDEN);
        }
    }

    update_hdr_signal(s->hub_rssi, s->device_rssi);

    if (hdr_units) {
        ui_label_set(hdr_units,
                     (strcmp(U_TEMP_SUF, "C") == 0) ? "°C" : "°F");
    }

    if (s->battery_v > 0.0f) {
        int64_t age = (s->last_udp_epoch > 0) ? (now - s->last_udp_epoch) : -1;
        char age_s[12];
        ui_fmt_age(age_s, sizeof(age_s), age);
        if (wx_udp_is_stale(s) || wx_obs_is_stale(s)) {
            set_text(hdr_health, "BAT %.2fV  stale %s",
                     (double)s->battery_v, age_s);
        } else {
            set_text(hdr_health, "BAT %.2fV", (double)s->battery_v);
        }
        if (s->battery_v > 2.6f) {
            lv_label_set_text(hdr_bat_icon, LV_SYMBOL_BATTERY_FULL);
            lv_obj_set_style_text_color(hdr_bat_icon, COL_OK, 0);
        } else if (s->battery_v > 2.4f) {
            lv_label_set_text(hdr_bat_icon, LV_SYMBOL_BATTERY_2);
            lv_obj_set_style_text_color(hdr_bat_icon, COL_TEMP_AMBER, 0);
        } else {
            lv_label_set_text(hdr_bat_icon, LV_SYMBOL_BATTERY_EMPTY);
            lv_obj_set_style_text_color(hdr_bat_icon, COL_ALERT, 0);
        }
    }
}

static float temp_delta_1h_c(void)
{
    static float last_d;
    static uint32_t last_ms;
    uint32_t now_ms = lv_tick_get();
    if (last_ms != 0 && (now_ms - last_ms) < 5000) {
        return last_d;
    }
    last_ms = now_ms;

    float buf[HIST_BUCKETS];
    int n = history_get(HIST_TEMP, buf, HIST_BUCKETS, NULL, NULL);
    if (n < 2) {
        last_d = NAN;
        return last_d;
    }

    int newest = -1;
    for (int i = HIST_BUCKETS - 1; i >= 0; i--) {
        if (!isnan(buf[i])) {
            newest = i;
            break;
        }
    }
    if (newest < 0) {
        last_d = NAN;
        return last_d;
    }

    int ago = newest - 12;      /* 12 x 5 min = 1 hour */
    if (ago < 0) {
        ago = 0;
    }
    int older = -1;
    for (int i = ago; i >= 0; i--) {
        if (!isnan(buf[i])) {
            older = i;
            break;
        }
    }
    if (older < 0 || older == newest) {
        last_d = NAN;
        return last_d;
    }

    last_d = buf[newest] - buf[older];
    return last_d;
}

static void update_outdoor_temp(const wx_state_t *s, int64_t now)
{
    (void)now;
    lv_label_set_text(temp_unit, (strcmp(U_TEMP_SUF, "C") == 0) ? "°C" : "°F");

    float lo_c = NAN, hi_c = NAN;
    if (s->daily_valid) {
        lo_c = s->temp_low_today_c;
        hi_c = s->temp_high_today_c;
    } else if (s->forecast_days > 0) {
        lo_c = s->forecast[0].air_temp_low_c;
        hi_c = s->forecast[0].air_temp_high_c;
    }

    if (!isnan(lo_c) && !isnan(hi_c)) {
        set_text(temp_lo, "%.0f°", (double)U_TEMP(lo_c));
        set_text(temp_hi, "%.0f°", (double)U_TEMP(hi_c));
        lv_obj_set_style_text_color(temp_lo, temp_color_at_c(lo_c), 0);
        lv_obj_set_style_text_color(temp_hi, temp_color_at_c(hi_c), 0);
        paint_badge(temp_badge,
                    s->daily_valid ? "TODAY" : "FCST",
                    s->daily_valid ? COL_WIND_NEON : COL_DIM);
    }

    if (!s->obs_valid) {
        return;
    }

    float t_c = s->air_temp_c;
    float t_cur = U_TEMP(t_c);
    lv_color_t tcol = temp_color_at_c(t_c);

    set_text(temp_val, "%.1f", (double)t_cur);
    lv_obj_set_style_text_color(temp_val, tcol, 0);

    float frac = (t_c - TEMP_MIN_C) / (TEMP_MAX_C - TEMP_MIN_C);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    lv_arc_set_value(temp_arc, (int)(frac * 1000.0f));
    paint_temp_gauge(temp_arc, temp_knob, temp_glow, tcol);
    place_knob_r(temp_knob, Z1_CX, Z1_CY, frac, Z1_KR, Z1_KNOB);

    float feels_c = s->feels_like_c;
    lv_color_t feels_col = temp_color_at_c(s->feels_like_c);
    const char *feels_tag = "feels";
    if (s->comfort_mode == WX_COMFORT_HEAT_INDEX) {
        feels_c = s->heat_index_c;
        feels_tag = "heat";
        feels_col = (strcmp(s->comfort_risk, "Danger") == 0 ||
                     strcmp(s->comfort_risk, "Extreme Danger") == 0) ? COL_ALERT :
                    (strcmp(s->comfort_risk, "Caution") == 0 ||
                     strcmp(s->comfort_risk, "Extreme Caution") == 0) ? COL_TEMP_AMBER : COL_TEMP_HOT;
        const char *risk = "HEAT";
        if (strcmp(s->comfort_risk, "Extreme Danger") == 0 ||
            strcmp(s->comfort_risk, "Danger") == 0) {
            risk = "DANGER";
        } else if (strstr(s->comfort_risk, "Caution")) {
            risk = "CAUTION";
        }
        paint_badge(temp_badge, risk, feels_col);
    } else if (s->comfort_mode == WX_COMFORT_WIND_CHILL) {
        feels_c = s->wind_chill_c;
        feels_tag = "chill";
        feels_col = (strcmp(s->comfort_risk, "Extreme Danger") == 0) ? COL_ALERT : COL_TEMP_COLD;
        paint_badge(temp_badge, "CHILL", feels_col);
    }
    set_text(temp_feels, "%s %.1f°", feels_tag, (double)U_TEMP(feels_c));
    lv_obj_set_style_text_color(temp_feels, feels_col, 0);

    set_text(temp_dew, "Dew %.0f°", (double)U_TEMP(s->dew_point_c));
    set_text(temp_rh, "RH %.0f%%", (double)s->humidity_pct);
    if (s->humidity_pct < 30.0f) {
        lv_obj_set_style_text_color(temp_rh, COL_TEMP_AMBER, 0);
    } else if (s->humidity_pct > 70.0f) {
        lv_obj_set_style_text_color(temp_rh, COL_RAIN_NEON, 0);
    } else {
        lv_obj_set_style_text_color(temp_rh, COL_DIM, 0);
    }

    /* Today range bar: observed (or forecast) envelope, with a now marker. */
    if (!isnan(lo_c) && !isnan(hi_c) && temp_range_track) {
        float pad = (hi_c - lo_c) * 0.18f;
        if (pad < 1.5f) {
            pad = 1.5f;
        }
        float scale_lo = lo_c - pad;
        float scale_hi = hi_c + pad;
        float span = scale_hi - scale_lo;
        int tw = (int)lv_obj_get_width(temp_range_track);
        if (tw < 8) {
            tw = Z1_W - 80;
        }
        float x0 = (lo_c - scale_lo) / span;
        float x1 = (hi_c - scale_lo) / span;
        float xn = (t_c - scale_lo) / span;
        if (xn < 0.0f) xn = 0.0f;
        if (xn > 1.0f) xn = 1.0f;
        int px = (int)(x0 * (float)tw);
        int pw = (int)((x1 - x0) * (float)tw);
        if (pw < 4) pw = 4;
        lv_obj_set_pos(temp_range_fill, px, 0);
        lv_obj_set_size(temp_range_fill, pw, 4);
        lv_obj_set_style_bg_color(temp_range_fill, tcol, 0);
        int nx = (int)(xn * (float)(tw - 3));
        lv_obj_set_pos(temp_now, nx, -3);
        lv_obj_set_style_bg_color(temp_now, COL_TEXT, 0);
    }

    float d1h = temp_delta_1h_c();
    if (isnan(d1h)) {
        lv_label_set_text(temp_trend, "--");
        lv_obj_set_style_text_color(temp_trend, COL_FAINT, 0);
    } else {
        float d_disp = U_TEMP(t_c) - U_TEMP(t_c - d1h);
        if (d_disp > 0.15f) {
            set_text(temp_trend, LV_SYMBOL_UP " %.1f°", (double)d_disp);
            lv_obj_set_style_text_color(temp_trend, COL_TEMP_HOT, 0);
        } else if (d_disp < -0.15f) {
            set_text(temp_trend, LV_SYMBOL_DOWN " %.1f°", (double)(-d_disp));
            lv_obj_set_style_text_color(temp_trend, COL_TEMP_COLD, 0);
        } else {
            lv_label_set_text(temp_trend, "steady");
            lv_obj_set_style_text_color(temp_trend, COL_FAINT, 0);
        }
    }
}

static void update_top_deck(const wx_state_t *s, int64_t now)
{
    /* Link badge on the conditions card updates even when obs is missing. */
    if (!s->wifi_connected) {
        paint_badge(cond_badge, "OFFLINE", COL_ALERT);
    } else if (wx_udp_is_stale(s) && tempest_ws_is_active()) {
        paint_badge(cond_badge, "WS LINK", COL_WIND_NEON);
    } else if (wx_udp_is_stale(s)) {
        paint_badge(cond_badge, "NO LINK", COL_TEMP_AMBER);
    } else if (wx_obs_is_stale(s)) {
        paint_badge(cond_badge, "STALE", COL_TEMP_AMBER);
    } else if (s->obs_valid) {
        paint_badge(cond_badge, "LIVE", COL_OK);
    } else {
        paint_badge(cond_badge, "WAITING", COL_DIM);
    }

    /* Forecast-driven card content updates even when UDP obs is still booting. */
    update_conditions_card(s, now);
    update_outdoor_temp(s, now);

    if (!s->obs_valid) {
        return;
    }

    /* --- Zone 2: Wind Compass & Speed --- */
    /* The big readout and the needle follow rapid_wind, which the hub sends
     * every 3 s. Fall back to obs_st average if none has arrived, or if the
     * last rapid sample is older than 30 s (UDP drop). */
    bool rapid_fresh = s->rapid_valid &&
        (now < 1600000000LL || (now - s->rapid_epoch) <= 30);
    float wind_ms = rapid_fresh ? s->rapid_wind_ms : s->wind_avg_ms;
    int   wdir    = rapid_fresh ? s->rapid_wind_dir_deg : s->wind_dir_deg;

    set_text(wind_val, "%.1f", (double)U_WIND(wind_ms));
    lv_label_set_text(wind_unit, U_WIND_SUF);

    set_text(wind_dir_deg, "%d° %s", wdir, wx_compass_point(wdir));

    set_text(wind_gust_lbl, "Gust: %.1f %s", (double)U_WIND(s->wind_gust_ms), U_WIND_SUF);
    set_text(wind_avg_lbl, "Avg: %.1f  Lull: %.1f",
             (double)U_WIND(s->wind_avg_ms), (double)U_WIND(s->wind_lull_ms));

    int bft = wx_beaufort_force(wind_ms);
    if (wind_beaufort_lbl) {
        lv_label_set_text(wind_beaufort_lbl, wx_beaufort_name(bft));
        lv_obj_set_style_text_color(wind_beaufort_lbl,
            (bft >= 8) ? COL_ALERT : (bft >= 6) ? COL_TEMP_AMBER : COL_DIM, 0);
    }

    /* Rotate Wind Compass Pointer */
    const int w_cx = 64;
    const int w_cy = 96;
    float rad = (float)wdir * (float)M_PI / 180.0f;
    needle_head_pts[0].x = w_cx; needle_head_pts[0].y = w_cy;
    needle_head_pts[1].x = w_cx + (int)(sinf(rad) * 38.0f);
    needle_head_pts[1].y = w_cy - (int)(cosf(rad) * 38.0f);
    lv_line_set_points(wind_needle_head, needle_head_pts, 2);

    needle_tail_pts[0].x = w_cx; needle_tail_pts[0].y = w_cy;
    needle_tail_pts[1].x = w_cx - (int)(sinf(rad) * 14.0f);
    needle_tail_pts[1].y = w_cy + (int)(cosf(rad) * 14.0f);
    lv_line_set_points(wind_needle_tail, needle_tail_pts, 2);

    /* Beaufort Bar — 0..12 mapped onto 0..100 */
    lv_bar_set_value(wind_beaufort_bar, bft * 100 / 12, LV_ANIM_OFF);

    /* --- Zone 3: Indoor Climate --- */
    if (s->indoor_valid) {
        lv_obj_add_flag(in_no_sensor_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_temp_arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_hum_arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_temp_val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_hum_val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_temp_knob, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_hum_knob, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_comfort_badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_status_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_temp_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(in_hum_cap, LV_OBJ_FLAG_HIDDEN);

        float in_t = U_TEMP(s->indoor_temp_c);
        lv_color_t icol = temp_color_at_c(s->indoor_temp_c);
        set_text(in_temp_val, "%.1f°", (double)in_t);
        lv_obj_set_style_text_color(in_temp_val, icol, 0);
        if (in_temp_kicker) {
            lv_obj_set_style_text_color(in_temp_kicker, icol, 0);
        }
        paint_temp_gauge(in_temp_arc, in_temp_knob, in_temp_glow, icol);
        set_text(in_hum_val, "%.0f%%", (double)s->indoor_humidity_pct);

        float in_t_frac = (s->indoor_temp_c - INDOOR_MIN_C) / (INDOOR_MAX_C - INDOOR_MIN_C);
        if (in_t_frac < 0.0f) in_t_frac = 0.0f;
        if (in_t_frac > 1.0f) in_t_frac = 1.0f;
        lv_arc_set_value(in_temp_arc, (int)(in_t_frac * 1000.0f));
        place_knob_r(in_temp_knob, 62, 82, in_t_frac, 44, 14);

        float in_h_frac = s->indoor_humidity_pct / 100.0f;
        if (in_h_frac < 0.0f) in_h_frac = 0.0f;
        if (in_h_frac > 1.0f) in_h_frac = 1.0f;
        lv_arc_set_value(in_hum_arc, (int)(in_h_frac * 1000.0f));
        place_knob_r(in_hum_knob, Z3_W - 62, 82, in_h_frac, 44, 14);

        if (s->indoor_humidity_pct < 30.0f) {
            lv_label_set_text(in_comfort_badge, "Dry Air");
            lv_obj_set_style_text_color(in_comfort_badge, COL_TEMP_AMBER, 0);
        } else if (s->indoor_humidity_pct > 60.0f) {
            lv_label_set_text(in_comfort_badge, "High Humidity");
            lv_obj_set_style_text_color(in_comfort_badge, COL_ALERT, 0);
        } else {
            lv_label_set_text(in_comfort_badge, "Comfortable");
            lv_obj_set_style_text_color(in_comfort_badge, COL_OK, 0);
        }

        if (wx_indoor_is_stale(s) && net_time_is_valid()) {
            int mins = (int)((now - s->indoor_fetched_epoch) / 60);
            if (mins < 1) {
                mins = 1;
            }
            set_text(in_status_lbl, "updated %d min ago", mins);
            lv_obj_set_style_text_color(in_status_lbl, COL_TEMP_AMBER, 0);
            lv_obj_set_style_text_opa(in_temp_val, LV_OPA_40, 0);
            lv_obj_set_style_text_opa(in_hum_val, LV_OPA_40, 0);
        } else if (s->obs_valid) {
            float dlt = U_TEMP(s->indoor_temp_c) - U_TEMP(s->air_temp_c);
            if (dlt > 0.4f) {
                set_text(in_status_lbl, "%.0f° warmer than outside", (double)dlt);
            } else if (dlt < -0.4f) {
                set_text(in_status_lbl, "%.0f° cooler than outside", (double)(-dlt));
            } else {
                lv_label_set_text(in_status_lbl, "matches outdoor");
            }
            lv_obj_set_style_text_color(in_status_lbl, COL_FAINT, 0);
            lv_obj_set_style_text_opa(in_temp_val, LV_OPA_COVER, 0);
            lv_obj_set_style_text_opa(in_hum_val, LV_OPA_COVER, 0);
        } else {
            lv_label_set_text(in_status_lbl, "sensor active");
            lv_obj_set_style_text_color(in_status_lbl, COL_FAINT, 0);
            lv_obj_set_style_text_opa(in_temp_val, LV_OPA_COVER, 0);
            lv_obj_set_style_text_opa(in_hum_val, LV_OPA_COVER, 0);
        }
    } else {
        lv_obj_clear_flag(in_no_sensor_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_temp_arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_hum_arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_temp_val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_hum_val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_temp_knob, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_hum_knob, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_comfort_badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_status_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_temp_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(in_hum_cap, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_mid_deck(const wx_state_t *s, int64_t now)
{
    /* Sunrise/sunset come from the forecast and should paint even before
     * the first UDP obs_st. Live rain/lightning/UV still need the station. */

    /* --- MID 1: Rain & Precipitation --- */
    char r_buf[32];
    snprintf(r_buf, sizeof(r_buf), "%s %s", U_RAIN_FMT, U_RAIN_SUF);
    set_text(rain_val, r_buf, (double)U_RAIN(s->rain_today_mm));

    if (rain_totals_lbl) {
        char t7[16], tm[16], ty[16];
        snprintf(t7, sizeof(t7), U_RAIN_FMT, (double)U_RAIN(s->rain_7d_mm));
        snprintf(tm, sizeof(tm), U_RAIN_FMT, (double)U_RAIN(s->rain_month_mm));
        snprintf(ty, sizeof(ty), U_RAIN_FMT, (double)U_RAIN(s->rain_ytd_mm));
        set_text(rain_totals_lbl, "7d %s  /  Mo %s  /  YTD %s",
                 t7, tm, ty);
    }

    float r_frac = s->rain_today_mm / 25.4f;
    if (r_frac < 0.05f) r_frac = 0.05f;
    if (r_frac > 1.0f)  r_frac = 1.0f;
    lv_obj_set_size(rain_cylinder_fill, 18, (int)(r_frac * 68.0f));

    if (s->precip_type == WX_PRECIP_HAIL || s->precip_type == WX_PRECIP_RAIN_HAIL) {
        paint_badge(rain_badge, (s->precip_type == WX_PRECIP_HAIL) ? "Hail" : "Rain+Hail",
                    COL_ALERT);
    } else if (s->rain_rate_mm_hr > 0.0f) {
        paint_badge(rain_badge, "Raining", COL_RAIN_NEON);
    } else {
        paint_badge(rain_badge, "Dry", COL_DIM);
    }

    if (s->rain_rate_mm_hr > 0.0f) {
        set_text(rain_rate_lbl, "Rate: %.2f %s/hr", (double)U_RAIN(s->rain_rate_mm_hr), U_RAIN_SUF);
    } else {
        int slot = wx_next_precip_slot(s, 30);
        if (slot >= 0) {
            char when[16];
            format_hour_label(when, sizeof(when), s->hourly[slot].hour_epoch);
            set_text(rain_rate_lbl, "Next: %s  %d%%",
                     when, s->hourly[slot].precip_probability);
        } else if (s->last_precip_epoch > 0 && now > s->last_precip_epoch) {
            char age_s[12];
            ui_fmt_age(age_s, sizeof(age_s), now - s->last_precip_epoch);
            set_text(rain_rate_lbl, "Last rain %s ago", age_s);
        } else if (s->hourly_valid) {
            set_text(rain_rate_lbl, "No rain in 24h");
        } else {
            set_text(rain_rate_lbl, "Rate: 0.00 %s/hr", U_RAIN_SUF);
        }
    }

    /* --- MID 2: Lightning Activity Detector --- */
    set_text(ltg_count_val, "%d", s->strikes_3h);

    bool ltg_near = s->last_strike_epoch > 0 &&
                    s->last_strike_dist_km > 0.0f &&
                    s->last_strike_dist_km <= LIGHTNING_NEAR_KM &&
                    (now - s->last_strike_epoch) < 3 * 3600;

    if (s->strikes_3h > 0 && s->last_strike_dist_km > 0.0f) {
        float l_dist = cfg_distance(s->last_strike_dist_km);
        int mins = (int)((now - s->last_strike_epoch) / 60);
        if (mins < 1) {
            mins = 1;
        }
        set_text(ltg_dist_lbl, "Strike %.1f %s away", (double)l_dist, cfg_distance_suffix());
        set_text(ltg_status_lbl, "Detected %d min ago", mins);
        paint_badge(ltg_badge, ltg_near ? "NEAR" : "Active",
                    ltg_near ? COL_ALERT : COL_TEMP_AMBER);
        if (ltg_card) {
            lv_obj_set_style_border_color(ltg_card,
                ltg_near ? COL_ALERT : COL_TEMP_AMBER, 0);
        }
    } else {
        lv_label_set_text(ltg_dist_lbl, "No strikes in 3 hrs");
        lv_label_set_text(ltg_status_lbl, "Live detector active");
        paint_badge(ltg_badge, "Clear", COL_OK);
        if (ltg_card) {
            lv_obj_set_style_border_color(ltg_card, COL_CARD_BORDER, 0);
        }
    }

    /* --- MID 3: Solar Radiation & UV Meter --- */
    if (s->sunrise_epoch > 0 && s->sunset_epoch > 0) {
        time_t sr = (time_t)s->sunrise_epoch;
        time_t ss = (time_t)s->sunset_epoch;
        struct tm tm_sr, tm_ss;
        localtime_r(&sr, &tm_sr);
        localtime_r(&ss, &tm_ss);

        char sr_buf[16], ss_buf[16];
        strftime(sr_buf, sizeof(sr_buf), "%I:%M %p", &tm_sr);
        strftime(ss_buf, sizeof(ss_buf), "%I:%M %p", &tm_ss);
        if (sr_buf[0] == '0') memmove(sr_buf, sr_buf + 1, strlen(sr_buf));
        if (ss_buf[0] == '0') memmove(ss_buf, ss_buf + 1, strlen(ss_buf));

        set_text(sun_rise_lbl, "Rise %s", sr_buf);
        set_text(sun_set_lbl, "Set %s", ss_buf);

        /* Golden Sun on Arch */
        const int arc_w = M3_W - 36;
        const int arc_x0 = 18;
        const int arc_y_base = 56;
        const int arc_h = 24;

        float f_sun = 0.0f;
        if (now >= sr && now <= ss) {
            f_sun = (float)(now - sr) / (float)(ss - sr);
        } else if (now > ss) {
            f_sun = 1.0f;
        } else {
            f_sun = 0.0f;
        }
        if (f_sun < 0.0f) f_sun = 0.0f;
        if (f_sun > 1.0f) f_sun = 1.0f;
        int sx = arc_x0 + (int)(f_sun * arc_w) - 7;
        int sy = arc_y_base - (int)(sinf(f_sun * (float)M_PI) * arc_h) - 7;
        lv_obj_set_pos(sun_marker, sx, sy);
        lv_obj_clear_flag(sun_marker, LV_OBJ_FLAG_HIDDEN);

        /* Daylight duration — moon lives on SKY, not here. */
        int64_t day_secs = ss - sr;
        if (day_secs > 0) {
            int dh = (int)(day_secs / 3600);
            int dm = (int)((day_secs % 3600) / 60);
            set_text(solar_daylight_lbl, "Day: %dh %dm", dh, dm);
        }
    }

    set_text(solar_rad_lbl, "%.0f W/m2", (double)s->solar_radiation_wm2);

    /* EPA UV Color Tier Bars */
    int active_tier = 0;
    lv_color_t uv_col = COL_UV_LOW;
    if (s->uv_index >= 11.0f)      { active_tier = 4; uv_col = COL_UV_EXTREME; }
    else if (s->uv_index >= 8.0f)  { active_tier = 3; uv_col = COL_UV_VHIGH; }
    else if (s->uv_index >= 6.0f)  { active_tier = 2; uv_col = COL_UV_HIGH; }
    else if (s->uv_index >= 3.0f)  { active_tier = 1; uv_col = COL_UV_MOD; }

    for (int u = 0; u < 5; u++) {
        lv_obj_set_style_bg_opa(uv_tier_bars[u], (u <= active_tier) ? LV_OPA_COVER : LV_OPA_30, 0);
    }
    char uv_buf[32];
    snprintf(uv_buf, sizeof(uv_buf), "UV %.1f (%s)", (double)s->uv_index, wx_uv_description(s->uv_index));
    paint_badge(uv_badge, uv_buf, uv_col);

    /* Optical Illuminance (Lux / kLux) */
    if (s->illuminance_lux >= 10000) {
        set_text(solar_lux_lbl, "Lux: %.1f kLux", (double)s->illuminance_lux / 1000.0);
    } else {
        set_text(solar_lux_lbl, "Lux: %u", (unsigned int)s->illuminance_lux);
    }

    /* --- MID 4: Barometric Pressure --- */
    char p_buf[32];
    snprintf(p_buf, sizeof(p_buf), "%s %s", U_PRES_FMT, U_PRES_SUF);
    set_text(baro_val, p_buf, (double)U_PRES(s->pressure_mb));
    set_text(baro_trend_lbl, "Trend: %s (%+.1f mb/3h)", wx_trend_description(s->pressure_trend), (double)s->pressure_trend_mb_3h);

    if (s->pressure_mb > 1022.0f) {
        paint_badge(baro_badge, "High", COL_OK);
    } else if (s->pressure_mb < 1005.0f) {
        paint_badge(baro_badge, "Low", COL_ALERT);
    } else {
        paint_badge(baro_badge, "Steady", COL_PRESS_NEON);
    }

    /* Last 10 pressure buckets, oldest on the left. */
    float pbuf[HIST_BUCKETS];
    float pmin = 0.0f, pmax = 0.0f;
    history_get(HIST_PRESSURE, pbuf, HIST_BUCKETS, &pmin, &pmax);
    float plast[10];
    int pgot = 0;
    for (int i = HIST_BUCKETS - 1; i >= 0 && pgot < 10; i--) {
        if (!isnan(pbuf[i])) {
            plast[pgot++] = pbuf[i];
        }
    }
    float pspan = pmax - pmin;
    if (pspan < 0.4f) {
        pspan = 0.4f;
    }
    for (int b = 0; b < 10; b++) {
        int idx = (pgot - 1) - b;
        int bh = 8;
        lv_color_t bcol = COL_TRACK;
        if (idx >= 0) {
            float f = (plast[idx] - pmin) / pspan;
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            bh = 8 + (int)(f * 20.0f);
            if (idx == 0) {
                bcol = COL_PRESS_NEON;
            }
        }
        if (bh < 4) bh = 4;
        if (bh > 28) bh = 28;
        lv_obj_set_size(baro_bars[b], 14, bh);
        lv_obj_set_pos(baro_bars[b], 10 + b * 22, 140 - bh);
        lv_obj_set_style_bg_color(baro_bars[b], bcol, 0);
    }
}

static void update_forecast_deck(const wx_state_t *s, int64_t now)
{
    bool has_fcst = s->forecast_days > 0;
    char buf[16];
    float week_lo = 1e9f, week_hi = -1e9f;
    if (has_fcst) {
        for (int i = 0; i < s->forecast_days && i < FC_COLS; i++) {
            if (s->forecast[i].air_temp_low_c < week_lo) {
                week_lo = s->forecast[i].air_temp_low_c;
            }
            if (s->forecast[i].air_temp_high_c > week_hi) {
                week_hi = s->forecast[i].air_temp_high_c;
            }
        }
        if (s->daily_valid) {
            if (s->temp_low_today_c < week_lo) {
                week_lo = s->temp_low_today_c;
            }
            if (s->temp_high_today_c > week_hi) {
                week_hi = s->temp_high_today_c;
            }
        }
    }

    for (int i = 0; i < FC_COLS; i++) {
        if (!has_fcst) {
            /* Placeholder day view while forecast initializes */
            time_t t_day = time(NULL) + i * 86400;
            struct tm tm_d;
            localtime_r(&t_day, &tm_d);
            char day_name[8];
            strftime(day_name, sizeof(day_name), "%a", &tm_d);
            for (char *c = day_name; *c; c++) {
                if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
            }
            if (i == 0) {
                snprintf(buf, sizeof(buf), "TODAY %d", tm_d.tm_mday);
                lv_label_set_text(fc[i].day, buf);
                lv_obj_set_style_text_color(fc[i].day, COL_WIND_NEON, 0);
                fc_set_icon(i, "clear-day");
                if (s->daily_valid) {
                    set_text(fc[i].hi, "%.0f°", (double)U_TEMP(s->temp_high_today_c));
                    set_text(fc[i].lo, "%.0f°", (double)U_TEMP(s->temp_low_today_c));
                } else {
                    set_text(fc[i].hi, "%.0f°", (double)U_TEMP(s->air_temp_c));
                    set_text(fc[i].lo, "%.0f°", (double)U_TEMP(s->air_temp_c));
                }
            } else {
                snprintf(buf, sizeof(buf), "%s %d", day_name, tm_d.tm_mday);
                lv_label_set_text(fc[i].day, buf);
                lv_obj_set_style_text_color(fc[i].day, COL_DIM, 0);
                fc_set_icon(i, "clear-day");
                set_text(fc[i].hi, "--°");
                set_text(fc[i].lo, "--°");
            }
            lv_label_set_text(fc[i].pop, "");
            continue;
        }

        if (i >= s->forecast_days) {
            lv_label_set_text(fc[i].day, "");
            if (fc[i].icon) lv_obj_add_flag(fc[i].icon, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(fc[i].hi, "");
            lv_label_set_text(fc[i].lo, "");
            lv_label_set_text(fc[i].pop, "");
            continue;
        }

        const wx_forecast_day_t *d = &s->forecast[i];
        if (fc[i].icon) lv_obj_clear_flag(fc[i].icon, LV_OBJ_FLAG_HIDDEN);

        time_t dt;
        if (d->day_start_local > 1600000000LL) {
            dt = (time_t)d->day_start_local;
        } else {
            dt = time(NULL) + (time_t)i * 86400;
        }
        struct tm tm_day;
        localtime_r(&dt, &tm_day);
        char day_name[8];
        strftime(day_name, sizeof(day_name), "%a", &tm_day);
        for (char *c = day_name; *c; c++) {
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        }

        if (i == 0) {
            snprintf(buf, sizeof(buf), "TODAY %d", tm_day.tm_mday);
            lv_label_set_text(fc[i].day, buf);
            lv_obj_set_style_text_color(fc[i].day, COL_WIND_NEON, 0);
        } else {
            snprintf(buf, sizeof(buf), "%s %d", day_name, tm_day.tm_mday);
            lv_label_set_text(fc[i].day, buf);
            lv_obj_set_style_text_color(fc[i].day, COL_DIM, 0);
        }

        const char *icon_base = forecast_sky_slug(d);
        /* Daily strip is a day-summary; always the -day artwork. */
        fc_set_icon(i, icon_slug_for(s, now, icon_base, true));

        float hi_c = d->air_temp_high_c;
        float lo_c = d->air_temp_low_c;
        /* Today's column tracks what has already happened, so observed
         * extremes can stretch the forecast envelope. */
        if (i == 0 && s->daily_valid) {
            if (s->temp_high_today_c > hi_c) {
                hi_c = s->temp_high_today_c;
            }
            if (s->temp_low_today_c < lo_c) {
                lo_c = s->temp_low_today_c;
            }
        }
        set_text(fc[i].hi, "%.0f°", (double)U_TEMP(hi_c));
        set_text(fc[i].lo, "%.0f°", (double)U_TEMP(lo_c));
        lv_obj_set_style_text_color(fc[i].hi, COL_TEMP_HOT, 0);
        lv_obj_set_style_text_color(fc[i].lo, COL_TEMP_COLD, 0);

        if (fc[i].range_track && fc[i].range_fill && week_hi > week_lo) {
            int tw = (int)lv_obj_get_width(fc[i].range_track);
            float span = week_hi - week_lo;
            float x0 = (lo_c - week_lo) / span;
            float x1 = (hi_c - week_lo) / span;
            int px = (int)(x0 * (float)tw);
            int pw = (int)((x1 - x0) * (float)tw);
            if (pw < 4) {
                pw = 4;
            }
            if (px + pw > tw) {
                px = tw - pw;
            }
            if (px < 0) {
                px = 0;
            }
            lv_obj_set_pos(fc[i].range_fill, px, 0);
            lv_obj_set_size(fc[i].range_fill, pw, 3);
            lv_obj_set_style_bg_color(fc[i].range_fill,
                (i == 0) ? COL_WIND_NEON : COL_TEMP_AMBER, 0);
        }

        if (d->precip_probability > 0) {
            set_text(fc[i].pop, "%d%%", d->precip_probability);
            lv_obj_set_style_text_color(fc[i].pop,
                                        wx_icon_is_wet(d->icon) ? COL_RAIN_NEON : COL_DIM, 0);
        } else {
            lv_label_set_text(fc[i].pop, "");
        }
    }

    lv_opa_t f_opa = (has_fcst && wx_forecast_is_stale(s)) ? LV_OPA_40 : LV_OPA_COVER;
    for (int i = 0; i < FC_COLS; i++) {
        if (fc[i].card) {
            lv_obj_set_style_opa(fc[i].card, f_opa, 0);
        }
    }
}

void ui_tick(void)
{
    int64_t now = (int64_t)time(NULL);
    apply_brightness(now);

    /* After a forecast HTTPS burst the MIPI PHY can drop. Wake the panel
     * and force a few full invalidates on whatever page is showing. */
    static int s_recover_repaints;
    if (display_apply_recover_request()) {
        s_recover_repaints = 4;
    }
    if (s_recover_repaints > 0) {
        s_recover_repaints--;
        lv_obj_t *scr = lv_screen_active();
        if (!scr) {
            scr = s_main_screen;
        }
        if (scr) {
            lv_obj_invalidate(scr);
        }
    }

    wx_state_t snap;
    wx_snapshot(&snap);
    update_alert_banner(&snap, now);
    check_lightning_sound(&snap, now);
    audio_scheduler_tick(now, snap.current_icon);

    if (wifi_setup_is_visible()) {
        wifi_setup_tick();
        return;
    }
    if (settings_is_visible()) {
        settings_tick();
        return;
    }
    if (graphs_is_visible()) {
        graphs_tick();
        return;
    }
    if (week_is_visible()) {
        week_tick();
        return;
    }
    if (page2_is_visible()) {
        page2_tick();
        return;
    }
    if (alerts_is_visible()) {
        alerts_tick();
        return;
    }

    icon_tick_promote();

    bool fc_dirty = s_forecast_dirty;
    if (s_forecast_dirty) {
        s_forecast_dirty = false;
        if (s_forecast_panel) {
            lv_obj_invalidate(s_forecast_panel);
        }
    }

    update_header(&snap, now);

    static int64_t s_deck_obs, s_deck_rapid, s_deck_indoor;
    static int s_deck_aqi = -1, s_deck_units = -1;
    int units_now = (int)(strcmp(cfg_temp_suffix(), "C") == 0);
    bool decks = fc_dirty ||
                 snap.obs_epoch != s_deck_obs ||
                 snap.rapid_epoch != s_deck_rapid ||
                 snap.indoor_fetched_epoch != s_deck_indoor ||
                 snap.aqi_val != s_deck_aqi ||
                 units_now != s_deck_units;
    if (decks) {
        s_deck_obs = snap.obs_epoch;
        s_deck_rapid = snap.rapid_epoch;
        s_deck_indoor = snap.indoor_fetched_epoch;
        s_deck_aqi = snap.aqi_val;
        s_deck_units = units_now;
        update_top_deck(&snap, now);
        update_mid_deck(&snap, now);
    }

    /* Forecast columns change on a new REST payload, a new local day, or
     * when today's observed hi/lo moves. Rebuilding the strip every second
     * was rewriting seven icons and range bars for nothing. */
    int min_key = (int)(now / 60);
    bool extremes = snap.daily_valid &&
                    (snap.temp_high_today_c != s_fc_drawn_hi ||
                     snap.temp_low_today_c != s_fc_drawn_lo);
    if (fc_dirty || min_key != s_fc_drawn_min || extremes) {
        s_fc_drawn_min = min_key;
        if (snap.daily_valid) {
            s_fc_drawn_hi = snap.temp_high_today_c;
            s_fc_drawn_lo = snap.temp_low_today_c;
        }
        update_forecast_deck(&snap, now);
    }
}
