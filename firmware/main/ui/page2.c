#include "page2.h"
#include "ui.h"
#include "wx_state.h"
#include "wx_astronomy.h"
#include "config.h"
#include "audio.h"
#include "wx_icons.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "page2";

#define COL_BG          lv_color_hex(0x06090E)
#define COL_CARD        lv_color_hex(0x0F141C)
#define COL_BORDER      lv_color_hex(0x1B2432)
#define COL_TEXT        lv_color_hex(0xF8FAFC)
#define COL_DIM         lv_color_hex(0x94A3B8)
#define COL_MOON        lv_color_hex(0xCFD8DC)
#define COL_AMBER       lv_color_hex(0xFFAB40)
#define COL_RED         lv_color_hex(0xFF5252)
#define COL_ALERT       lv_color_hex(0xFF1744)
#define COL_OK          lv_color_hex(0x00E676)
#define COL_RAIN        lv_color_hex(0x29B6F6)
#define COL_AQI_GOOD    lv_color_hex(0x4CAF50)
#define COL_AQI_MOD     lv_color_hex(0xFDD835)
#define COL_AQI_USG     lv_color_hex(0xFB8C00)
#define COL_AQI_BAD     lv_color_hex(0xE53935)

#define SCR_W           1024
#define SCR_H           600
#define LIGHTNING_NEAR_KM  9.656f   /* 6 miles */

typedef enum {
    RAIN_VIEW_TODAY = 0,
    RAIN_VIEW_7D,
    RAIN_VIEW_MONTH,
    RAIN_VIEW_YTD,
    RAIN_VIEW_COUNT,
} rain_view_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_prev_screen;
static lv_obj_t *s_standby_overlay;
static lv_obj_t *s_standby_clock;

static lv_obj_t *s_moon_title;
static lv_obj_t *s_moon_phase_lbl;
static lv_obj_t *s_moon_pct_lbl;
static lv_obj_t *s_moon_rise_lbl;
static lv_obj_t *s_moon_set_lbl;
static lv_obj_t *s_moon_icon;
static lv_obj_t *s_moon_arc_line;
static lv_point_precise_t s_moon_arc_pts[17];
static lv_obj_t *s_moon_marker;

static lv_obj_t *s_ltg_card;
static lv_obj_t *s_ltg_detail;
static lv_obj_t *s_ltg_status;

static lv_obj_t *s_rain_card;
static lv_obj_t *s_rain_val;
static lv_obj_t *s_rain_cap;

static lv_obj_t *s_aqi_card;
static lv_obj_t *s_aqi_bars[5];
static lv_obj_t *s_aqi_val;
static lv_obj_t *s_aqi_cat;

static lv_obj_t *s_hourly_chart;
static lv_chart_series_t *s_hourly_temp;
static lv_chart_series_t *s_hourly_pop;
static lv_obj_t *s_hourly_note;

static rain_view_t s_rain_view = RAIN_VIEW_TODAY;
static int64_t     s_last_chime_strike;

static lv_color_t aqi_color(int aqi)
{
    if (aqi <= 50)  return COL_AQI_GOOD;
    if (aqi <= 100) return COL_AQI_MOD;
    if (aqi <= 150) return COL_AQI_USG;
    return COL_AQI_BAD;
}

static void set_text(lv_obj_t *lbl, const char *fmt, ...)
{
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lv_label_set_text(lbl, buf);
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h,
                           lv_color_t accent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, accent, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static void on_rain_tap(lv_event_t *e)
{
    (void)e;
    s_rain_view = (rain_view_t)((s_rain_view + 1) % RAIN_VIEW_COUNT);
}

static void build_standby_overlay(void)
{
    s_standby_overlay = lv_obj_create(s_screen);
    lv_obj_set_size(s_standby_overlay, SCR_W, SCR_H);
    lv_obj_set_pos(s_standby_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_standby_overlay, lv_color_hex(0x120800), 0);
    lv_obj_set_style_bg_opa(s_standby_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_standby_overlay, 0, 0);
    lv_obj_add_flag(s_standby_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_standby_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_standby_clock = lv_label_create(s_standby_overlay);
    lv_obj_set_style_text_font(s_standby_clock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_standby_clock, COL_AMBER, 0);
    lv_label_set_text(s_standby_clock, "--:--");
    lv_obj_center(s_standby_clock);

    lv_obj_t *hint = lv_label_create(s_standby_overlay);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, COL_DIM, 0);
    lv_label_set_text(hint, "Night standby  •  tap page button to exit");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24);
}

static void build_moon_panel(void)
{
    lv_obj_t *card = make_card(s_screen, 10, 48, SCR_W - 20, 200, COL_MOON);

    s_moon_title = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_title, COL_MOON, 0);
    lv_label_set_text(s_moon_title, "LUNAR ARC & NIGHT SKY");
    lv_obj_align(s_moon_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_moon_icon = wx_icon_create(card, 72, false);
    if (s_moon_icon) {
        lv_obj_align(s_moon_icon, LV_ALIGN_TOP_LEFT, 8, 28);
    }

    const int arc_w = 520;
    const int arc_x0 = 120;
    const int arc_y_base = 120;
    const int arc_h = 36;
    for (int i = 0; i <= 16; i++) {
        float f = (float)i / 16.0f;
        s_moon_arc_pts[i].x = arc_x0 + (int)(f * arc_w);
        s_moon_arc_pts[i].y = arc_y_base - (int)(sinf(f * (float)M_PI) * arc_h);
    }
    s_moon_arc_line = lv_line_create(card);
    lv_line_set_points(s_moon_arc_line, s_moon_arc_pts, 17);
    lv_obj_set_style_line_width(s_moon_arc_line, 2, 0);
    lv_obj_set_style_line_color(s_moon_arc_line, COL_MOON, 0);
    lv_obj_set_style_line_opa(s_moon_arc_line, LV_OPA_60, 0);

    s_moon_marker = lv_obj_create(card);
    lv_obj_set_size(s_moon_marker, 14, 14);
    lv_obj_set_style_radius(s_moon_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_moon_marker, COL_MOON, 0);
    lv_obj_set_style_border_width(s_moon_marker, 0, 0);

    s_moon_phase_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_phase_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_moon_phase_lbl, COL_TEXT, 0);
    lv_label_set_text(s_moon_phase_lbl, "Waxing Gibbous");
    lv_obj_set_pos(s_moon_phase_lbl, 680, 36);

    s_moon_pct_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_pct_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_moon_pct_lbl, COL_MOON, 0);
    lv_label_set_text(s_moon_pct_lbl, "88% lit");
    lv_obj_set_pos(s_moon_pct_lbl, 680, 72);

    s_moon_rise_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_rise_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_rise_lbl, COL_DIM, 0);
    lv_label_set_text(s_moon_rise_lbl, "Moonrise --:--");
    lv_obj_set_pos(s_moon_rise_lbl, 680, 110);

    s_moon_set_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_set_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_set_lbl, COL_DIM, 0);
    lv_label_set_text(s_moon_set_lbl, "Moonset --:--");
    lv_obj_set_pos(s_moon_set_lbl, 680, 134);
}

static void build_mid_row(void)
{
    const int y = 258;
    const int h = 132;
    const int w = 328;
    const int gap = 10;

    s_ltg_card = make_card(s_screen, 10, y, w, h, COL_AMBER);
    lv_obj_t *lt = lv_label_create(s_ltg_card);
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lt, COL_AMBER, 0);
    lv_label_set_text(lt, "LIGHTNING PROXIMITY");
    s_ltg_detail = lv_label_create(s_ltg_card);
    lv_obj_set_style_text_font(s_ltg_detail, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ltg_detail, COL_TEXT, 0);
    lv_label_set_text(s_ltg_detail, "No nearby strikes");
    lv_obj_set_pos(s_ltg_detail, 0, 36);
    s_ltg_status = lv_label_create(s_ltg_card);
    lv_obj_set_style_text_font(s_ltg_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ltg_status, COL_DIM, 0);
    lv_label_set_text(s_ltg_status, "Monitoring...");
    lv_obj_set_pos(s_ltg_status, 0, 72);

    s_rain_card = make_card(s_screen, 10 + w + gap, y, w, h, COL_RAIN);
    lv_obj_add_flag(s_rain_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_rain_card, on_rain_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rt = lv_label_create(s_rain_card);
    lv_obj_set_style_text_font(rt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rt, COL_RAIN, 0);
    lv_label_set_text(rt, "RAIN TOTALS  (tap to cycle)");
    s_rain_val = lv_label_create(s_rain_card);
    lv_obj_set_style_text_font(s_rain_val, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_rain_val, COL_TEXT, 0);
    lv_label_set_text(s_rain_val, "0.00 in");
    lv_obj_set_pos(s_rain_val, 0, 40);
    s_rain_cap = lv_label_create(s_rain_card);
    lv_obj_set_style_text_font(s_rain_cap, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_rain_cap, COL_DIM, 0);
    lv_label_set_text(s_rain_cap, "Rain Today");
    lv_obj_set_pos(s_rain_cap, 0, 82);

    s_aqi_card = make_card(s_screen, 10 + 2 * (w + gap), y, w, h, COL_AQI_GOOD);
    lv_obj_t *at = lv_label_create(s_aqi_card);
    lv_obj_set_style_text_font(at, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(at, COL_AQI_GOOD, 0);
    lv_label_set_text(at, "AIR QUALITY (EPA)");
    lv_color_t cols[5] = { COL_AQI_GOOD, COL_AQI_MOD, COL_AQI_USG, COL_AQI_BAD, COL_AQI_BAD };
    for (int i = 0; i < 5; i++) {
        s_aqi_bars[i] = lv_obj_create(s_aqi_card);
        lv_obj_set_size(s_aqi_bars[i], 52, 10);
        lv_obj_set_pos(s_aqi_bars[i], i * 58, 36);
        lv_obj_set_style_bg_color(s_aqi_bars[i], cols[i], 0);
        lv_obj_set_style_bg_opa(s_aqi_bars[i], LV_OPA_30, 0);
        lv_obj_set_style_border_width(s_aqi_bars[i], 0, 0);
        lv_obj_set_style_radius(s_aqi_bars[i], 3, 0);
    }
    s_aqi_val = lv_label_create(s_aqi_card);
    lv_obj_set_style_text_font(s_aqi_val, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_aqi_val, COL_TEXT, 0);
    lv_label_set_text(s_aqi_val, "AQI --");
    lv_obj_set_pos(s_aqi_val, 0, 56);
    s_aqi_cat = lv_label_create(s_aqi_card);
    lv_obj_set_style_text_font(s_aqi_cat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_aqi_cat, COL_DIM, 0);
    lv_label_set_text(s_aqi_cat, "Waiting for reading");
    lv_obj_set_pos(s_aqi_cat, 0, 92);
}

static void build_hourly_panel(void)
{
    lv_obj_t *card = make_card(s_screen, 10, 400, SCR_W - 20, 188, COL_RAIN);
    lv_obj_t *ht = lv_label_create(card);
    lv_obj_set_style_text_font(ht, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ht, COL_RAIN, 0);
    lv_label_set_text(ht, "24-HOUR HOURLY TIMELINE");

    s_hourly_chart = lv_chart_create(card);
    lv_obj_set_size(s_hourly_chart, SCR_W - 44, 110);
    lv_obj_set_pos(s_hourly_chart, 0, 28);
    lv_chart_set_type(s_hourly_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_hourly_chart, WX_HOURLY_SLOTS);
    lv_chart_set_range(s_hourly_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_bg_opa(s_hourly_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_hourly_chart, 0, 0);
    s_hourly_pop = lv_chart_add_series(s_hourly_chart, COL_RAIN, LV_CHART_AXIS_PRIMARY_Y);
    s_hourly_temp = lv_chart_add_series(s_hourly_chart, COL_AMBER, LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_range(s_hourly_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);

    s_hourly_note = lv_label_create(card);
    lv_obj_set_style_text_font(s_hourly_note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hourly_note, COL_DIM, 0);
    lv_label_set_text(s_hourly_note, "Rain probability by hour (bars) with temperature trend");
    lv_obj_set_pos(s_hourly_note, 0, 148);
}

esp_err_t page2_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "Night & Insights");
    lv_obj_set_pos(title, 16, 12);

    ui_create_page_button(s_screen, LV_ALIGN_TOP_RIGHT, -14, 8, UI_PAGE_INSIGHTS);
    ui_attach_swipe_nav(s_screen);

    build_moon_panel();
    build_mid_row();
    build_hourly_panel();
    build_standby_overlay();

    ESP_LOGI(TAG, "insights page built");
    return ESP_OK;
}

void page2_show(void)
{
    if (!s_screen) {
        return;
    }
    s_prev_screen = lv_screen_active();
    lv_screen_load(s_screen);
}

void page2_hide(void)
{
    if (s_prev_screen) {
        lv_screen_load(s_prev_screen);
    }
}

bool page2_is_visible(void)
{
    return s_screen && lv_screen_active() == s_screen;
}

bool page2_night_standby_active(void)
{
    return s_standby_overlay &&
           !lv_obj_has_flag(s_standby_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void format_time(char *buf, size_t len, int64_t epoch)
{
    if (epoch <= 0) {
        snprintf(buf, len, "--:--");
        return;
    }
    struct tm lt;
    time_t t = (time_t)epoch;
    localtime_r(&t, &lt);
    strftime(buf, len, "%I:%M %p", &lt);
    if (buf[0] == '0') {
        memmove(buf, buf + 1, strlen(buf));
    }
}

static float rain_for_view(const wx_state_t *s, rain_view_t view)
{
    switch (view) {
    case RAIN_VIEW_7D:    return s->rain_7d_mm + s->rain_today_mm;
    case RAIN_VIEW_MONTH: return s->rain_month_mm + s->rain_today_mm;
    case RAIN_VIEW_YTD:   return s->rain_ytd_mm + s->rain_today_mm;
    case RAIN_VIEW_TODAY:
    default:              return s->rain_today_mm;
    }
}

static const char *rain_caption(rain_view_t view)
{
    switch (view) {
    case RAIN_VIEW_7D:    return "Rain Last 7 Days";
    case RAIN_VIEW_MONTH: return "Rain This Month";
    case RAIN_VIEW_YTD:   return "Rain Year-to-Date";
    case RAIN_VIEW_TODAY:
    default:              return "Rain Today";
    }
}

void page2_tick(void)
{
    if (!page2_is_visible()) {
        return;
    }

    wx_state_t s;
    wx_snapshot(&s);
    int64_t now = (int64_t)time(NULL);

    cfg_t cfg;
    cfg_get(&cfg);

    struct tm lt;
    time_t tnow = (time_t)now;
    localtime_r(&tnow, &lt);
    bool night = cfg_is_night(lt.tm_hour);
    bool standby = cfg.night_standby_enabled && night;

    if (standby) {
        lv_obj_set_style_bg_color(s_standby_overlay,
            cfg.night_standby_red ? lv_color_hex(0x180404) : lv_color_hex(0x120800), 0);
        lv_obj_set_style_text_color(s_standby_clock,
            cfg.night_standby_red ? COL_RED : COL_AMBER, 0);
        char clk[16];
        strftime(clk, sizeof(clk), "%I:%M", &lt);
        if (clk[0] == '0') {
            memmove(clk, clk + 1, strlen(clk));
        }
        lv_label_set_text(s_standby_clock, clk);
        lv_obj_clear_flag(s_standby_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_standby_overlay, LV_OBJ_FLAG_HIDDEN);

    bool lunar_night = wx_is_night_sky(now, s.sunset_epoch,
                                       s.moonrise_epoch, s.moonset_epoch);
    lv_label_set_text(s_moon_title,
                      lunar_night ? "LUNAR ARC & NIGHT SKY" : "SOLAR ARC (DAY)");

    lv_label_set_text(s_moon_phase_lbl, s.moon_phase_name[0] ? s.moon_phase_name : "--");
    set_text(s_moon_pct_lbl, "%.0f%% lit", (double)(s.moon_illumination * 100.0f));
    wx_icon_set(s_moon_icon, s.moon_icon[0] ? s.moon_icon : "moon-full");

    char tr[16], ts[16];
    format_time(tr, sizeof(tr), s.moonrise_epoch);
    format_time(ts, sizeof(ts), s.moonset_epoch);
    set_text(s_moon_rise_lbl, "Moonrise %s", tr);
    set_text(s_moon_set_lbl, "Moonset %s", ts);

    const int arc_w = 520;
    const int arc_x0 = 120;
    const int arc_y_base = 120;
    const int arc_h = 36;
    float f_moon = s.moon_illumination;
    if (lunar_night && s.moonrise_epoch > 0 && s.moonset_epoch > 0 && now >= s.moonrise_epoch) {
        int64_t span = s.moonset_epoch - s.moonrise_epoch;
        if (span > 0) {
            f_moon = (float)(now - s.moonrise_epoch) / (float)span;
        }
    } else if (!lunar_night && s.sunrise_epoch > 0 && s.sunset_epoch > 0 && now >= s.sunrise_epoch) {
        int64_t span = s.sunset_epoch - s.sunrise_epoch;
        if (span > 0 && now <= s.sunset_epoch) {
            f_moon = (float)(now - s.sunrise_epoch) / (float)span;
        }
    }
    if (f_moon < 0.0f) f_moon = 0.0f;
    if (f_moon > 1.0f) f_moon = 1.0f;
    int mx = arc_x0 + (int)(f_moon * arc_w) - 7;
    int my = arc_y_base - (int)(sinf(f_moon * (float)M_PI) * arc_h) - 7;
    lv_obj_set_pos(s_moon_marker, mx, my);
    lv_obj_set_style_line_color(s_moon_arc_line,
                                lunar_night ? COL_MOON : COL_AMBER, 0);

    /* Lightning proximity */
    bool near = false;
    if (s.last_strike_epoch > 0 && s.last_strike_dist_km > 0.0f &&
        s.last_strike_dist_km <= LIGHTNING_NEAR_KM &&
        (now - s.last_strike_epoch) < 3 * 3600) {
        near = true;
        float dist = cfg_distance(s.last_strike_dist_km);
        int mins = (int)((now - s.last_strike_epoch) / 60);
        if (mins < 1) mins = 1;
        set_text(s_ltg_detail, "Strike %.1f %s away", (double)dist, cfg_distance_suffix());
        set_text(s_ltg_status, "%d min ago  •  advisory zone", mins);
        lv_obj_set_style_border_color(s_ltg_card, COL_ALERT, 0);
        lv_obj_set_style_bg_color(s_ltg_card, lv_color_hex(0x241008), 0);
        if (s.last_strike_epoch != s_last_chime_strike) {
            s_last_chime_strike = s.last_strike_epoch;
            audio_play_chime();
        }
    } else {
        lv_label_set_text(s_ltg_detail, "No strikes within 6 mi");
        lv_label_set_text(s_ltg_status, "Live detector active");
        lv_obj_set_style_border_color(s_ltg_card, COL_AMBER, 0);
        lv_obj_set_style_bg_color(s_ltg_card, COL_CARD, 0);
    }
    (void)near;

    /* Rain totals */
    float rain_mm = rain_for_view(&s, s_rain_view);
    char rbuf[32];
    snprintf(rbuf, sizeof(rbuf), "%s %s", cfg_rain_fmt(), cfg_rain_suffix());
    set_text(s_rain_val, rbuf, (double)cfg_rain(rain_mm));
    lv_label_set_text(s_rain_cap, rain_caption(s_rain_view));

    /* AQI */
    if (s.aqi_valid) {
        set_text(s_aqi_val, "AQI %d", s.aqi_val);
        set_text(s_aqi_cat, "%s  •  PM2.5 %.1f", wx_aqi_epa_label(s.aqi_val), (double)s.aqi_pm25);
        lv_color_t ac = aqi_color(s.aqi_val);
        lv_obj_set_style_border_color(s_aqi_card, ac, 0);
        int tier = 0;
        if (s.aqi_val > 50)  tier = 1;
        if (s.aqi_val > 100) tier = 2;
        if (s.aqi_val > 150) tier = 3;
        if (s.aqi_val > 200) tier = 4;
        for (int i = 0; i < 5; i++) {
            lv_obj_set_style_bg_opa(s_aqi_bars[i], i <= tier ? LV_OPA_COVER : LV_OPA_30, 0);
        }
    }

    /* Hourly timeline */
    if (s.hourly_valid && s.hourly_count > 0) {
        lv_chart_set_point_count(s_hourly_chart, s.hourly_count);
        float tmin = 100.0f, tmax = -100.0f;
        for (int i = 0; i < s.hourly_count; i++) {
            if (s.hourly[i].temp_c < tmin) tmin = s.hourly[i].temp_c;
            if (s.hourly[i].temp_c > tmax) tmax = s.hourly[i].temp_c;
        }
        float trange = tmax - tmin;
        if (trange < 4.0f) trange = 4.0f;

        int rain_start = -1;
        for (int i = 0; i < s.hourly_count; i++) {
            lv_chart_set_value_by_id(s_hourly_chart, s_hourly_pop, i,
                                     s.hourly[i].precip_probability);
            int tnorm = (int)(((s.hourly[i].temp_c - tmin) / trange) * 100.0f);
            lv_chart_set_value_by_id(s_hourly_chart, s_hourly_temp, i, tnorm);
            if (rain_start < 0 && s.hourly[i].precip_probability >= 40) {
                rain_start = i;
            }
        }
        lv_chart_refresh(s_hourly_chart);

        if (rain_start >= 0) {
            char note[80];
            struct tm ht;
            time_t ht_t = (time_t)s.hourly[rain_start].hour_epoch;
            localtime_r(&ht_t, &ht);
            char hr[16];
            strftime(hr, sizeof(hr), "%I:%M %p", &ht);
            if (hr[0] == '0') memmove(hr, hr + 1, strlen(hr));
            snprintf(note, sizeof(note), "Rain likely around %s (%d%%)",
                     hr, s.hourly[rain_start].precip_probability);
            lv_label_set_text(s_hourly_note, note);
        } else {
            lv_label_set_text(s_hourly_note, "No significant rain in the next hours");
        }
    }
}
