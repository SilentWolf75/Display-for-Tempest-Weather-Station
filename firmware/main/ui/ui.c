#include "weather_icons_data.h"
#include "ui.h"
#include "wx_icons.h"
#include "wx_state.h"
#include "config.h"
#include "settings.h"
#include "graphs.h"
#include "page2.h"
#include "wifi_setup.h"
#include "display.h"
#include "net.h"
#include "history.h"
#include "audio.h"
#include "nws_alerts.h"
#include "ota.h"
#include "tempest_ws.h"

#include "sdkconfig.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "ui";

/* ---- palette: rich commercial console colors ---------------------------- */
#define COL_BG          lv_color_hex(0x06090E)
#define COL_CARD        lv_color_hex(0x0F141C)
#define COL_CARD_BORDER lv_color_hex(0x1B2432)
#define COL_TRACK       lv_color_hex(0x18202C)
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

#define COL_AQI_GOOD    lv_color_hex(0x4CAF50)
#define COL_AQI_MOD     lv_color_hex(0xFDD835)
#define COL_AQI_USG     lv_color_hex(0xFB8C00)
#define COL_AQI_BAD     lv_color_hex(0xE53935)

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

/* Bottom Deck: 7-Day Forecast Strip */
#define FC_Y            440
#define FC_H            152
#define FC_COLS         WX_FORECAST_DAYS

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
static lv_obj_t *hdr_dot, *hdr_station, *hdr_health, *hdr_date, *hdr_clock, *hdr_bat_icon;
static lv_obj_t *s_main_screen;
static ui_page_t s_current_page = UI_PAGE_DASHBOARD;
static bool       s_ui_ready;
static lv_obj_t *hdr_page_lbl;
static lv_obj_t *hdr_aqi_dot, *hdr_aqi_lbl;
static lv_obj_t *s_alert_banner, *s_alert_banner_lbl;

/* Zone 1: Outdoor Temperature */
static lv_obj_t *temp_val, *temp_feels_pill, *temp_dew_pill, *temp_hilo_lbl;
static lv_obj_t *temp_knob, *temp_lo_knob, *temp_hi_knob;

/* Zone 2: Split Wind Radar & Speed */
static lv_obj_t *wind_val, *wind_unit, *wind_dir_deg, *wind_gust_lbl, *wind_avg_lbl;
static lv_obj_t *wind_needle_head, *wind_needle_tail, *wind_hub;
static lv_obj_t *wind_beaufort_bar;
static lv_point_precise_t needle_head_pts[2];
static lv_point_precise_t needle_tail_pts[2];

/* Zone 3: Indoor Climate Dual Dials */
static lv_obj_t *in_temp_arc, *in_temp_knob, *in_temp_val;
static lv_obj_t *in_hum_arc, *in_hum_knob, *in_hum_val;
static lv_obj_t *in_comfort_badge, *in_status_lbl, *in_no_sensor_view;
/* The TEMP/HUMIDITY captions need handles too. Without them they cannot be
 * hidden, so they sat on top of the no-sensor panel. */
static lv_obj_t *in_temp_cap, *in_hum_cap;

/* Zone 4: Current Conditions */
static lv_obj_t *cond_icon, *cond_title, *cond_sub, *cond_badge, *smart_pill;

/* Mid 1: Rain & Precipitation */
static lv_obj_t *rain_val, *rain_rate_lbl, *rain_cylinder_fill, *rain_badge;

/* Mid 2: Lightning Activity Detector */
static lv_obj_t *ltg_badge, *ltg_count_val, *ltg_count_lbl, *ltg_dist_lbl, *ltg_status_lbl;

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
    lv_obj_t *bar_bg;
    lv_obj_t *bar_fill;
    lv_obj_t *bar_dot;
} fc_col_t;

static fc_col_t fc[FC_COLS];
static bool     s_fc_lottie;
static bool     s_fc_rebuild;

static void rebuild_forecast_icons(void);
static void fc_set_icon(int idx, const char *slug);

static void on_gear(lv_event_t *e);
static void on_page_btn(lv_event_t *e);
static void on_toggle_units(lv_event_t *e);
static void on_screen_swipe(lv_event_t *e);
static void ui_sync_page_button_labels(void);

static lv_color_t aqi_color(int aqi)
{
    if (aqi <= 50)  return COL_AQI_GOOD;
    if (aqi <= 100) return COL_AQI_MOD;
    if (aqi <= 150) return COL_AQI_USG;
    return COL_AQI_BAD;
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
    ESP_LOGI(TAG, "units toggled to %s",
             c.units == CFG_UNITS_METRIC ? "metric" : "imperial");
}

static const char *page_button_label(ui_page_t page)
{
    static const char *labels[UI_PAGE_COUNT] = {
        "1/3",
        "2/3",
        "3/3",
    };
    if (page >= UI_PAGE_COUNT) {
        return labels[0];
    }
    return labels[page];
}

void ui_page_goto(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) {
        return;
    }

    switch (page) {
    case UI_PAGE_DASHBOARD:
        if (s_main_screen) {
            lv_screen_load(s_main_screen);
        }
        break;
    case UI_PAGE_INSIGHTS:
        page2_show();
        break;
    case UI_PAGE_GRAPHS:
        graphs_show();
        break;
    default:
        break;
    }

    s_current_page = page;
    ui_sync_page_button_labels();
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
    if (!s_ui_ready) {
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
    lv_obj_set_style_min_width(btn, 52, 0);
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

static void ui_sync_page_button_labels(void)
{
    if (hdr_page_lbl) {
        lv_label_set_text(hdr_page_lbl, page_button_label(s_current_page));
    }
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
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    /* Clean Subtle Glowing Accent Bar at top edge of each panel */
    lv_obj_t *glow = lv_obj_create(c);
    lv_obj_set_pos(glow, 0, 0);
    lv_obj_set_size(glow, w, 2);
    lv_obj_set_style_bg_color(glow, glow_col, 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(glow, 0, 0);
    lv_obj_clear_flag(glow, LV_OBJ_FLAG_SCROLLABLE);

    return c;
}

/* make_card() puts its accent bar in as the FIRST child, so this drops it.
 * Used for the header, where a full-width bar along the very top edge of the
 * screen reads as a stray line rather than as trim on a panel. */
static void card_no_accent(lv_obj_t *c)
{
    lv_obj_t *accent = lv_obj_get_child(c, 0);
    if (accent) {
        lv_obj_delete(accent);
    }
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
    lv_obj_set_size(k, diam, diam);
    lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(k, col, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(k, 0, 0);
    lv_obj_set_style_pad_all(k, 0, 0);
    lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
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

static void make_graded_segments(lv_obj_t *parent, int cx, int cy, int size, int width, const lv_color_t *colors, int n_segs)
{
    float seg_angle = (float)ARC_SWEEP / (float)n_segs;
    for (int i = 0; i < n_segs; i++) {
        lv_obj_t *a = lv_arc_create(parent);
        lv_obj_set_size(a, size, size);
        lv_obj_set_pos(a, cx - size / 2, cy - size / 2);
        int a_start = (int)((float)ARC_START + (float)i * seg_angle);
        int a_end   = (int)((float)ARC_START + (float)(i + 1) * seg_angle);
        lv_arc_set_bg_angles(a, a_start, a_end);
        lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, colors[i], LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(a, (i == 0 || i == n_segs - 1), LV_PART_MAIN);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        lv_obj_remove_style(a, NULL, LV_PART_INDICATOR);
        lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
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

static void build_compass_ticks(lv_obj_t *parent, int cx, int cy, int radius)
{
    for (int deg = 0; deg < 360; deg += 30) {
        float rad = (float)deg * (float)M_PI / 180.0f;
        int is_cardinal = (deg % 90 == 0);
        int tick_len = is_cardinal ? 6 : 4;
        int r_out = radius - 2;
        int r_in  = r_out - tick_len;

        lv_point_precise_t *pts = malloc(sizeof(lv_point_precise_t) * 2);
        pts[0].x = cx + (int)(sinf(rad) * (float)r_in);
        pts[0].y = cy - (int)(cosf(rad) * (float)r_in);
        pts[1].x = cx + (int)(sinf(rad) * (float)r_out);
        pts[1].y = cy - (int)(cosf(rad) * (float)r_out);

        lv_obj_t *line = lv_line_create(parent);
        lv_line_set_points(line, pts, 2);
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

    /* Left: Live Connection & Station Status */
    hdr_dot = lv_obj_create(h);
    lv_obj_set_size(hdr_dot, 8, 8);
    lv_obj_set_style_radius(hdr_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hdr_dot, COL_IDLE, 0);
    lv_obj_set_style_bg_opa(hdr_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr_dot, 0, 0);
    lv_obj_set_style_pad_all(hdr_dot, 0, 0);
    lv_obj_clear_flag(hdr_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hdr_dot, LV_ALIGN_LEFT_MID, 6, 0);

    hdr_station = label(h, &lv_font_montserrat_14, COL_TEXT, "TEMPEST PRO");
    lv_obj_align(hdr_station, LV_ALIGN_LEFT_MID, 22, 0);

    hdr_aqi_dot = lv_obj_create(h);
    lv_obj_set_size(hdr_aqi_dot, 8, 8);
    lv_obj_set_style_radius(hdr_aqi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hdr_aqi_dot, COL_AQI_GOOD, 0);
    lv_obj_set_style_bg_opa(hdr_aqi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr_aqi_dot, 0, 0);
    lv_obj_clear_flag(hdr_aqi_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hdr_aqi_dot, LV_ALIGN_LEFT_MID, 132, 0);
    lv_obj_add_flag(hdr_aqi_dot, LV_OBJ_FLAG_HIDDEN);

    hdr_aqi_lbl = label(h, &lv_font_montserrat_14, COL_DIM, "AQI --");
    lv_obj_align(hdr_aqi_lbl, LV_ALIGN_LEFT_MID, 144, 0);
    lv_obj_add_flag(hdr_aqi_lbl, LV_OBJ_FLAG_HIDDEN);

    hdr_bat_icon = label(h, &lv_font_montserrat_14, COL_OK, LV_SYMBOL_BATTERY_FULL);
    lv_obj_align(hdr_bat_icon, LV_ALIGN_LEFT_MID, 204, 0);

    hdr_health = label(h, &lv_font_montserrat_14, COL_DIM, "BAT 2.75V  RSSI -45 dBm");
    lv_obj_align(hdr_health, LV_ALIGN_LEFT_MID, 226, 0);

    /* Center: Date */
    hdr_date = label(h, &lv_font_montserrat_16, COL_DIM, "");
    lv_obj_align(hdr_date, LV_ALIGN_CENTER, 0, 0);

    /* Right: Clock, page nav, settings */
    hdr_clock = label(h, &lv_font_montserrat_20, COL_TEXT, "--:--");
    lv_obj_align(hdr_clock, LV_ALIGN_RIGHT_MID, -148, 0);

    ui_create_page_button(h, LV_ALIGN_RIGHT_MID, -60, 0, UI_PAGE_DASHBOARD);

    /* Settings Gear Button (Header Bar) */
    lv_obj_t *gear = lv_button_create(h);
    lv_obj_set_size(gear, 48, 30);
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -6, 0);
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

static void build_alert_banner(lv_obj_t *scr)
{
    s_alert_banner = lv_obj_create(scr);
    lv_obj_set_pos(s_alert_banner, PAD, HEAD_Y);
    lv_obj_set_size(s_alert_banner, SCR_W - 2 * PAD, HEAD_H);
    lv_obj_set_style_bg_color(s_alert_banner, COL_ALERT, 0);
    lv_obj_set_style_bg_opa(s_alert_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_alert_banner, 0, 0);
    lv_obj_set_style_radius(s_alert_banner, 8, 0);
    lv_obj_set_style_pad_all(s_alert_banner, 0, 0);
    lv_obj_clear_flag(s_alert_banner, LV_OBJ_FLAG_SCROLLABLE);

    s_alert_banner_lbl = lv_label_create(s_alert_banner);
    lv_label_set_text(s_alert_banner_lbl, LV_SYMBOL_WARNING " Weather Alert");
    lv_obj_set_style_text_font(s_alert_banner_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_alert_banner_lbl, COL_TEXT, 0);
    lv_obj_set_width(s_alert_banner_lbl, SCR_W - 2 * PAD - 16);
    lv_label_set_long_mode(s_alert_banner_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_alert_banner_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_add_flag(s_alert_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_alert_banner);
}

static void build_top_deck(lv_obj_t *scr)
{
    /* --- ZONE 1: OUTDOOR TEMPERATURE --- */
    lv_obj_t *z1 = make_card(scr, PAD, TOP_Y, Z1_W, TOP_H, COL_CARD_BORDER, COL_TEMP_HOT);
    lv_obj_add_flag(z1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z1, on_toggle_units, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t1 = label(z1, &lv_font_montserrat_14, COL_TEMP_HOT, "OUTDOOR TEMP");
    lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 6, 2);

    const int z1_cx = Z1_W / 2;
    const int z1_cy = 92;
    const int z1_box = 144;
    const int z1_r = 65;

    lv_color_t grade[5] = {
        lv_color_hex(0x00B0FF), /* Ice Blue */
        lv_color_hex(0x00E676), /* Emerald  */
        lv_color_hex(0xFFD600), /* Yellow   */
        lv_color_hex(0xFF9800), /* Orange   */
        lv_color_hex(0xFF1744)  /* Crimson  */
    };
    make_graded_segments(z1, z1_cx, z1_cy, z1_box, 12, grade, 5);

    temp_lo_knob = make_knob(z1, COL_TEMP_COLD, 10);
    temp_hi_knob = make_knob(z1, COL_TEMP_HOT, 10);
    place_knob_r(temp_lo_knob, z1_cx, z1_cy, 0.2f, z1_r, 10);
    place_knob_r(temp_hi_knob, z1_cx, z1_cy, 0.8f, z1_r, 10);

    temp_knob = make_knob(z1, COL_TEXT, 18);
    place_knob_r(temp_knob, z1_cx, z1_cy, 0.0f, z1_r, 18);

    /* 38, not 48: this has to hold six glyphs at the extremes -- "104.7" in
     * midsummer and "-22.3" in midwinter -- and at 48 even "89.7" ran into the
     * gauge ring. Width 200 rather than 180 for the same reason. The y offset
     * follows the smaller line height so the number stays centred in the ring. */
    temp_val = clabel(z1, z1_cx, z1_cy - 20, 200, &lv_font_montserrat_38, COL_TEXT, "--");

    /* Styled metric pills */
    temp_feels_pill = make_pill(z1, 10, 160, 106, 24, COL_TEMP_HOT, COL_TEMP_HOT, "Feels --°");
    temp_dew_pill = make_pill(z1, 122, 160, 106, 24, COL_TEMP_COLD, COL_TEMP_COLD, "Dew --°");
    temp_hilo_lbl = clabel(z1, z1_cx, 192, 220, &lv_font_montserrat_14, COL_DIM, "Hi --°   Lo --°   RH --%");

    /* --- ZONE 2: SPLIT WIND COMPASS & SPEED --- */
    lv_obj_t *z2 = make_card(scr, PAD + Z1_W + 6, TOP_Y, Z2_W, TOP_H, COL_CARD_BORDER, COL_WIND_NEON);
    lv_obj_add_flag(z2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z2, on_toggle_units, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t2 = label(z2, &lv_font_montserrat_14, COL_WIND_NEON, "WIND & GUST");
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
    lv_obj_set_size(wind_hub, 10, 10);
    lv_obj_set_style_radius(wind_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(wind_hub, COL_TEXT, 0);
    lv_obj_set_style_border_color(wind_hub, COL_CARD, 0);
    lv_obj_set_style_border_width(wind_hub, 2, 0);
    lv_obj_set_pos(wind_hub, w_cx - 5, w_cy - 5);

    /* Right half: Dedicated Wind Speed & Metrics (cx=184) */
    const int spd_cx = 184;
    wind_val = clabel(z2, spd_cx, 48, 110, &lv_font_montserrat_46, COL_TEXT, "--");
    wind_unit = clabel(z2, spd_cx, 98, 110, &lv_font_montserrat_16, COL_WIND_NEON, "MPH");
    wind_dir_deg = clabel(z2, spd_cx, 120, 110, &lv_font_montserrat_16, COL_TEXT, "0° N");

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

    lv_obj_t *t3 = label(z3, &lv_font_montserrat_14, COL_INDOOR_TEMP, "INDOOR CLIMATE");
    lv_obj_align(t3, LV_ALIGN_TOP_LEFT, 6, 2);

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

    lv_obj_t *t4 = label(z4, &lv_font_montserrat_14, COL_SUN_GOLD, "CURRENT CONDITIONS");
    lv_obj_align(t4, LV_ALIGN_TOP_LEFT, 6, 2);

    cond_badge = label(z4, &lv_font_montserrat_14, COL_OK, "LIVE");
    lv_obj_align(cond_badge, LV_ALIGN_TOP_RIGHT, -6, 2);

    /* Weather Icon (Center) */
    cond_icon = wx_icon_create(z4, 84, true);
    if (cond_icon) {
        lv_obj_align(cond_icon, LV_ALIGN_TOP_MID, 0, 32);
    }

    cond_title = clabel(z4, Z4_W / 2, 134, Z4_W - 12, &lv_font_montserrat_24, COL_TEXT, "Clear");
    cond_sub = clabel(z4, Z4_W / 2, 168, Z4_W - 12, &lv_font_montserrat_14, COL_DIM, "Today: Hi --°  Lo --°");

    smart_pill = make_pill(z4, 8, 196, Z4_W - 16, 26, COL_WIND_NEON, COL_TEXT, "Checking forecast...");
    lv_obj_set_width(smart_pill, Z4_W - 28);
    lv_label_set_long_mode(smart_pill, LV_LABEL_LONG_DOT);
}

static void build_mid_deck(lv_obj_t *scr)
{
    /* --- MID 1: PRECIPITATION & RAIN CYLINDER --- */
    lv_obj_t *m1 = make_card(scr, PAD, MID_Y, M1_W, MID_H, COL_CARD_BORDER, COL_RAIN_NEON);
    lv_obj_add_flag(m1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m1, on_toggle_units, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t1 = label(m1, &lv_font_montserrat_14, COL_RAIN_NEON, "PRECIPITATION");
    lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 8, 6);

    rain_badge = label(m1, &lv_font_montserrat_14, COL_DIM, "Dry");
    lv_obj_align(rain_badge, LV_ALIGN_TOP_RIGHT, -8, 6);

    /* Stylized Beaker / Cylinder (Left side) */
    lv_obj_t *cyl_bg = lv_obj_create(m1);
    lv_obj_set_size(cyl_bg, 26, 76);
    lv_obj_set_pos(cyl_bg, 8, 32);
    lv_obj_set_style_bg_color(cyl_bg, COL_TRACK, 0);
    lv_obj_set_style_border_color(cyl_bg, COL_RAIN_NEON, 0);
    lv_obj_set_style_border_width(cyl_bg, 2, 0);
    lv_obj_set_style_radius(cyl_bg, 6, 0);
    lv_obj_set_style_pad_all(cyl_bg, 2, 0);
    lv_obj_clear_flag(cyl_bg, LV_OBJ_FLAG_SCROLLABLE);

    for (int tk = 1; tk <= 3; tk++) {
        lv_obj_t *h_line = lv_obj_create(cyl_bg);
        lv_obj_set_size(h_line, 8, 1);
        lv_obj_set_pos(h_line, 0, 70 - tk * 18);
        lv_obj_set_style_bg_color(h_line, COL_DIM, 0);
        lv_obj_set_style_border_width(h_line, 0, 0);
        lv_obj_clear_flag(h_line, LV_OBJ_FLAG_SCROLLABLE);
    }

    rain_cylinder_fill = lv_obj_create(cyl_bg);
    lv_obj_set_size(rain_cylinder_fill, 18, 10);
    lv_obj_align(rain_cylinder_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(rain_cylinder_fill, COL_RAIN_NEON, 0);
    lv_obj_set_style_border_width(rain_cylinder_fill, 0, 0);
    lv_obj_set_style_radius(rain_cylinder_fill, 3, 0);
    lv_obj_clear_flag(rain_cylinder_fill, LV_OBJ_FLAG_SCROLLABLE);

    /* Readouts (Right side) */
    rain_val = label(m1, &lv_font_montserrat_34, COL_TEXT, "0.00 in");
    lv_obj_set_pos(rain_val, 42, 32);

    rain_rate_lbl = label(m1, &lv_font_montserrat_14, COL_DIM, "Rate: 0.00 in/hr");
    lv_obj_set_pos(rain_rate_lbl, 42, 74);

    clabel(m1, 140, 104, 180, &lv_font_montserrat_14, COL_FAINT, "Rainfall Today");

    /* --- MID 2: DEDICATED LIGHTNING DETECTOR --- */
    lv_obj_t *m2 = make_card(scr, PAD + M1_W + 6, MID_Y, M2_W, MID_H, COL_CARD_BORDER, COL_UV_MOD);

    lv_obj_t *t2 = label(m2, &lv_font_montserrat_14, COL_UV_MOD, "LIGHTNING DETECTOR");
    lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 8, 6);

    ltg_badge = label(m2, &lv_font_montserrat_14, COL_OK, "Clear");
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

    lv_obj_t *t3 = label(m3, &lv_font_montserrat_14, COL_SUN_GOLD, "SOLAR");
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
        uv_tier_bars[u] = lv_obj_create(m3);
        lv_obj_set_size(uv_tier_bars[u], 8, 4);
        lv_obj_set_pos(uv_tier_bars[u], 8 + u * 10, 94);
        lv_obj_set_style_bg_color(uv_tier_bars[u], uv_cols[u], 0);
        lv_obj_set_style_bg_opa(uv_tier_bars[u], LV_OPA_30, 0);
        lv_obj_set_style_border_width(uv_tier_bars[u], 0, 0);
        lv_obj_set_style_radius(uv_tier_bars[u], 2, 0);
        lv_obj_clear_flag(uv_tier_bars[u], LV_OBJ_FLAG_SCROLLABLE);
    }
    uv_badge = label(m3, &lv_font_montserrat_14, COL_UV_LOW, "UV 0.0 (Low)");
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

    lv_obj_t *t4 = label(m4, &lv_font_montserrat_14, COL_PRESS_NEON, "BAROMETER");
    lv_obj_align(t4, LV_ALIGN_TOP_LEFT, 8, 6);

    baro_badge = label(m4, &lv_font_montserrat_14, COL_OK, "Steady");
    lv_obj_align(baro_badge, LV_ALIGN_TOP_RIGHT, -8, 6);

    baro_val = label(m4, &lv_font_montserrat_34, COL_TEXT, "29.92 inHg");
    lv_obj_set_pos(baro_val, 10, 32);

    baro_trend_lbl = label(m4, &lv_font_montserrat_14, COL_DIM, "+0.0 mb/3h");
    lv_obj_set_pos(baro_trend_lbl, 10, 74);

    /* 10-Bar Pressure Shift Visualization */
    for (int b = 0; b < 10; b++) {
        baro_bars[b] = lv_obj_create(m4);
        lv_obj_set_size(baro_bars[b], 14, 18);
        lv_obj_set_pos(baro_bars[b], 10 + b * 22, 106);
        lv_obj_set_style_bg_color(baro_bars[b], COL_TRACK, 0);
        lv_obj_set_style_bg_opa(baro_bars[b], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(baro_bars[b], 0, 0);
        lv_obj_set_style_radius(baro_bars[b], 3, 0);
        lv_obj_clear_flag(baro_bars[b], LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void fc_set_icon(int idx, const char *slug)
{
    if (!slug || !slug[0] || idx < 0 || idx >= FC_COLS || !fc[idx].icon) {
        return;
    }
    if (s_fc_lottie) {
        wx_icon_set(fc[idx].icon, slug);
    } else {
        const lv_image_dsc_t *dsc = wx_icon_get_image_dsc(slug);
        if (dsc) {
            lv_image_set_src(fc[idx].icon, dsc);
        }
    }
}

static void rebuild_forecast_icons(void)
{
    cfg_t c;
    cfg_get(&c);
    bool want_lottie = c.animate_forecast;
    if (want_lottie == s_fc_lottie && !s_fc_rebuild) {
        return;
    }
    s_fc_lottie = want_lottie;

    for (int i = 0; i < FC_COLS; i++) {
        if (!fc[i].card) {
            continue;
        }
        if (fc[i].icon) {
            lv_obj_delete(fc[i].icon);
            fc[i].icon = NULL;
        }
        const int col_w = (SCR_W - 2 * PAD - 16) / FC_COLS;
        const int cx = col_w / 2;
        if (s_fc_lottie) {
            fc[i].icon = wx_icon_create(fc[i].card, 44, true);
        } else {
            fc[i].icon = lv_image_create(fc[i].card);
            lv_obj_set_size(fc[i].icon, 44, 44);
        }
        lv_obj_set_pos(fc[i].icon, cx - 22, 22);
    }
    s_fc_rebuild = false;
}

void ui_forecast_mode_changed(void)
{
    s_fc_rebuild = true;
}

static void build_forecast_deck(lv_obj_t *scr)
{
    lv_obj_t *p = make_card(scr, PAD, FC_Y, SCR_W - 2 * PAD, FC_H, COL_CARD_BORDER, COL_TEMP_AMBER);
    const int col_w = (SCR_W - 2 * PAD - 16) / FC_COLS;

    for (int i = 0; i < FC_COLS; i++) {
        int x = i * col_w;
        lv_obj_t *col_card = lv_obj_create(p);
        lv_obj_set_pos(col_card, x, 0);
        lv_obj_set_size(col_card, col_w, FC_H - 16);
        lv_obj_set_style_bg_opa(col_card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col_card, 0, 0);
        lv_obj_set_style_pad_all(col_card, 0, 0);
        lv_obj_clear_flag(col_card, LV_OBJ_FLAG_SCROLLABLE);

        fc[i].card = col_card;
        const int cx = col_w / 2;

        fc[i].day = clabel(col_card, cx, 2, col_w, &lv_font_montserrat_14, COL_DIM, (i == 0) ? "TODAY" : "--");

        /* Icon widget created in rebuild_forecast_icons(). */
        fc[i].icon = NULL;

        fc[i].pop = clabel(col_card, cx, 70, col_w, &lv_font_montserrat_14, COL_RAIN_NEON, "");

        /* Temperature Range Bar */
        fc[i].lo = clabel(col_card, cx - 38, 98, 36, &lv_font_montserrat_16, COL_FAINT, "--");

        fc[i].bar_bg = lv_obj_create(col_card);
        lv_obj_set_size(fc[i].bar_bg, 46, 6);
        lv_obj_set_pos(fc[i].bar_bg, cx - 23, 104);
        lv_obj_set_style_bg_color(fc[i].bar_bg, COL_TRACK, 0);
        lv_obj_set_style_radius(fc[i].bar_bg, 3, 0);
        lv_obj_set_style_border_width(fc[i].bar_bg, 0, 0);
        lv_obj_clear_flag(fc[i].bar_bg, LV_OBJ_FLAG_SCROLLABLE);

        fc[i].bar_fill = lv_obj_create(fc[i].bar_bg);
        lv_obj_set_size(fc[i].bar_fill, 20, 6);
        lv_obj_set_pos(fc[i].bar_fill, 0, 0);
        lv_obj_set_style_bg_color(fc[i].bar_fill, COL_TEMP_AMBER, 0);
        lv_obj_set_style_radius(fc[i].bar_fill, 3, 0);
        lv_obj_set_style_border_width(fc[i].bar_fill, 0, 0);
        lv_obj_clear_flag(fc[i].bar_fill, LV_OBJ_FLAG_SCROLLABLE);

        if (i == 0) {
            fc[i].bar_dot = lv_obj_create(fc[i].bar_bg);
            lv_obj_set_size(fc[i].bar_dot, 8, 8);
            lv_obj_set_pos(fc[i].bar_dot, 0, -1);
            lv_obj_set_style_radius(fc[i].bar_dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(fc[i].bar_dot, COL_TEXT, 0);
            lv_obj_set_style_border_width(fc[i].bar_dot, 0, 0);
            lv_obj_clear_flag(fc[i].bar_dot, LV_OBJ_FLAG_SCROLLABLE);
        } else {
            fc[i].bar_dot = NULL;
        }

        fc[i].hi = clabel(col_card, cx + 38, 98, 36, &lv_font_montserrat_16, COL_TEXT, "--");

        if (i < FC_COLS - 1) {
            lv_obj_t *sep = lv_obj_create(p);
            lv_obj_set_pos(sep, x + col_w, 12);
            lv_obj_set_size(sep, 1, FC_H - 40);
            lv_obj_set_style_bg_color(sep, COL_CARD_BORDER, 0);
            lv_obj_set_style_border_width(sep, 0, 0);
            lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    rebuild_forecast_icons();
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
    if (settings_is_visible() || wifi_setup_is_visible() || ota_in_progress()) {
        ui_note_user_activity();
    }

    uint8_t target = cfg_brightness_now();
    if (now >= 1700000000LL || net_time_is_valid()) {
        static int last_hour = -1;
        time_t t = (time_t)now;
        struct tm lt;
        localtime_r(&t, &lt);
        if (lt.tm_hour != last_hour) {
            last_hour = lt.tm_hour;
            target = cfg_brightness_now();
        }
    }
    s_scheduled_brightness = target;

    cfg_t cfg_scr;
    cfg_get(&cfg_scr);
    if (cfg_scr.screensaver_idle_min > 0 &&
        !settings_is_visible() && !wifi_setup_is_visible() && !ota_in_progress()) {
        uint32_t idle_ms = lv_tick_get() - s_last_input_ms;
        uint32_t limit_ms = (uint32_t)cfg_scr.screensaver_idle_min * 60U * 1000U;
        if (idle_ms >= limit_ms) {
            if (!s_screensaver_active) {
                s_screensaver_active = true;
                ESP_LOGI(TAG, "screensaver on (%u min idle)",
                         cfg_scr.screensaver_idle_min);
            }
            target = cfg_scr.screensaver_brightness;
        }
    }

    if (s_screensaver_active) {
        cfg_t c2;
        cfg_get(&c2);
        target = c2.screensaver_brightness;
    }

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

    build_header(scr);
    build_alert_banner(scr);
    build_top_deck(scr);
    build_mid_deck(scr);
    build_forecast_deck(scr);

    settings_init();
    graphs_init();
    page2_init();
    wifi_setup_init();

    ui_attach_swipe_nav(s_main_screen);

    s_current_page = UI_PAGE_DASHBOARD;
    lv_screen_load(s_main_screen);
    lv_obj_invalidate(s_main_screen);
    s_ui_ready = true;
    s_last_input_ms = lv_tick_get();
    s_scheduled_brightness = cfg_brightness_now();

    ESP_LOGI(TAG, "commercial weather console built (%dx%d)", SCR_W, SCR_H);
    return ESP_OK;
}

/* ======================================================================== */
/* ---- UI UPDATE TICKS --------------------------------------------------- */
/* ======================================================================== */

static void set_text(lv_obj_t *l, const char *fmt, ...)
{
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lv_label_set_text(l, buf);
}

static void update_hdr_aqi(const wx_state_t *s)
{
    if (s->aqi_valid && s->aqi_val > 0) {
        lv_color_t ac = aqi_color(s->aqi_val);
        lv_obj_clear_flag(hdr_aqi_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(hdr_aqi_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(hdr_aqi_dot, ac, 0);
        set_text(hdr_aqi_lbl, "AQI %d", s->aqi_val);
        lv_obj_set_style_text_color(hdr_aqi_lbl, ac, 0);
    } else {
        lv_obj_add_flag(hdr_aqi_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hdr_aqi_lbl, LV_OBJ_FLAG_HIDDEN);
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

static void update_alert_banner(const wx_state_t *s, int64_t now)
{
    nws_alert_t alert = {0};
    bool nws = nws_alerts_get_active(&alert);

    bool ltg_near = s->last_strike_epoch > 0 &&
                    s->last_strike_dist_km > 0.0f &&
                    s->last_strike_dist_km <= LIGHTNING_NEAR_KM &&
                    (now - s->last_strike_epoch) < 3 * 3600;

    if (nws) {
        char buf[192];
        snprintf(buf, sizeof(buf), "%s %.32s",
                 LV_SYMBOL_WARNING, alert.event);
        lv_label_set_text(s_alert_banner_lbl, buf);
        lv_obj_set_style_bg_color(s_alert_banner,
            strcmp(alert.severity, "Extreme") == 0 ? COL_ALERT : COL_TEMP_AMBER, 0);
        lv_obj_clear_flag(s_alert_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_alert_banner);
    } else if (ltg_near) {
        float dist = cfg_distance(s->last_strike_dist_km);
        int mins = (int)((now - s->last_strike_epoch) / 60);
        if (mins < 1) {
            mins = 1;
        }
        char buf[128];
        snprintf(buf, sizeof(buf),
                 LV_SYMBOL_WARNING " Lightning %.1f %s away — %d min ago",
                 (double)dist, cfg_distance_suffix(), mins);
        lv_label_set_text(s_alert_banner_lbl, buf);
        lv_obj_set_style_bg_color(s_alert_banner, COL_TEMP_AMBER, 0);
        lv_obj_clear_flag(s_alert_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_alert_banner);
    } else {
        lv_obj_add_flag(s_alert_banner, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_smart_pill(const wx_state_t *s)
{
    char msg[96];
    lv_color_t border = COL_WIND_NEON;
    lv_color_t text = COL_TEXT;

    if (s->obs_valid && wx_obs_is_stale(s) && net_time_is_valid()) {
        int mins = (int)((time(NULL) - s->obs_epoch) / 60);
        if (mins < 1) {
            mins = 1;
        }
        snprintf(msg, sizeof(msg), "Station data %d min old", mins);
        border = COL_TEMP_AMBER;
        text = COL_TEMP_AMBER;
    } else if (s->forecast_valid && wx_forecast_is_stale(s)) {
        snprintf(msg, sizeof(msg), "Forecast outdated — check Wi-Fi");
        border = COL_TEMP_AMBER;
        text = COL_DIM;
    } else if (s->obs_valid && s->rain_rate_mm_hr > 0.05f) {
        char rate[16];
        snprintf(rate, sizeof(rate), cfg_rain_fmt(), (double)U_RAIN(s->rain_rate_mm_hr));
        snprintf(msg, sizeof(msg), "Rain falling now at %s %s/hr", rate, U_RAIN_SUF);
        border = COL_RAIN_NEON;
    } else if (s->hourly_valid && s->hourly_count > 0) {
        int rain_start = -1;
        for (int i = 0; i < s->hourly_count; i++) {
            if (s->hourly[i].precip_probability >= 40) {
                rain_start = i;
                break;
            }
        }
        if (rain_start >= 0) {
            char hr[16];
            format_hour_label(hr, sizeof(hr), s->hourly[rain_start].hour_epoch);
            snprintf(msg, sizeof(msg), "Rain likely around %s (%d%%)",
                     hr, s->hourly[rain_start].precip_probability);
            border = COL_RAIN_NEON;
        } else if (s->comfort_mode == WX_COMFORT_HEAT_INDEX &&
                   (strcmp(s->comfort_risk, "Danger") == 0 ||
                    strcmp(s->comfort_risk, "Extreme Danger") == 0)) {
            snprintf(msg, sizeof(msg), "Heat index %s — take it easy", s->comfort_risk);
            border = COL_TEMP_HOT;
            text = COL_TEMP_AMBER;
        } else if (s->pressure_trend == WX_TREND_FALLING_FAST) {
            snprintf(msg, sizeof(msg), "Pressure falling fast — storms possible");
            border = COL_PRESS_NEON;
        } else if (s->aqi_valid && s->aqi_val > 100) {
            snprintf(msg, sizeof(msg), "AQI %d — %s", s->aqi_val, s->aqi_category);
            border = aqi_color(s->aqi_val);
        } else if (s->current_conditions[0]) {
            snprintf(msg, sizeof(msg), "%s", s->current_conditions);
            border = COL_SUN_GOLD;
        } else {
            snprintf(msg, sizeof(msg), "All clear — no advisories");
            border = COL_OK;
            text = COL_DIM;
        }
    } else if (s->comfort_mode == WX_COMFORT_WIND_CHILL &&
               strcmp(s->comfort_risk, "Danger") == 0) {
        snprintf(msg, sizeof(msg), "Wind chill %s — dress warm", s->comfort_risk);
        border = COL_TEMP_COLD;
    } else if (s->current_conditions[0]) {
        snprintf(msg, sizeof(msg), "%s", s->current_conditions);
        border = COL_SUN_GOLD;
    } else {
        snprintf(msg, sizeof(msg), "Waiting for forecast data...");
        border = COL_DIM;
        text = COL_DIM;
    }

    lv_label_set_text(smart_pill, msg);
    lv_obj_t *pill = lv_obj_get_parent(smart_pill);
    if (pill) {
        lv_obj_set_style_border_color(pill, border, 0);
    }
    lv_obj_set_style_text_color(smart_pill, text, 0);
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
    lv_label_set_text(hdr_clock, time_str);

    /* Date */
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%A, %b %d, %Y", &lt);
    lv_label_set_text(hdr_date, date_str);

    /* Station Status Dot & Diagnostics */
    if (!s->wifi_connected) {
        lv_obj_set_style_bg_color(hdr_dot, COL_ALERT, 0);
        lv_label_set_text(hdr_station, "NO WI-FI");
    } else if (wx_udp_is_stale(s) && tempest_ws_is_active()) {
        lv_obj_set_style_bg_color(hdr_dot, COL_TEMP_AMBER, 0);
        lv_label_set_text(hdr_station, "WS LINK");
    } else if (wx_udp_is_stale(s)) {
        lv_obj_set_style_bg_color(hdr_dot, COL_TEMP_AMBER, 0);
        lv_label_set_text(hdr_station, "NO UDP");
    } else if (wx_obs_is_stale(s)) {
        lv_obj_set_style_bg_color(hdr_dot, COL_TEMP_AMBER, 0);
        lv_label_set_text(hdr_station, "TEMPEST PRO");
    } else if (s->obs_valid) {
        lv_obj_set_style_bg_color(hdr_dot, COL_OK, 0);
        lv_label_set_text(hdr_station, "TEMPEST PRO");
    } else {
        lv_obj_set_style_bg_color(hdr_dot, COL_IDLE, 0);
        lv_label_set_text(hdr_station, "TEMPEST PRO");
    }

    if (s->battery_v > 0.0f) {
        set_text(hdr_health, "BAT: %.2fV  RSSI: %d dBm", (double)s->battery_v, s->hub_rssi);
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

    update_hdr_aqi(s);
}

static void update_top_deck(const wx_state_t *s, int64_t now)
{
    /* Link badge on the conditions card updates even when obs is missing. */
    if (!s->wifi_connected) {
        lv_label_set_text(cond_badge, "OFFLINE");
        lv_obj_set_style_text_color(cond_badge, COL_ALERT, 0);
    } else if (wx_udp_is_stale(s) && tempest_ws_is_active()) {
        lv_label_set_text(cond_badge, "WS LINK");
        lv_obj_set_style_text_color(cond_badge, COL_WIND_NEON, 0);
    } else if (wx_udp_is_stale(s)) {
        lv_label_set_text(cond_badge, "NO LINK");
        lv_obj_set_style_text_color(cond_badge, COL_TEMP_AMBER, 0);
    } else if (wx_obs_is_stale(s)) {
        lv_label_set_text(cond_badge, "STALE");
        lv_obj_set_style_text_color(cond_badge, COL_TEMP_AMBER, 0);
    } else if (s->obs_valid) {
        lv_label_set_text(cond_badge, "LIVE");
        lv_obj_set_style_text_color(cond_badge, COL_OK, 0);
    } else {
        lv_label_set_text(cond_badge, "WAITING");
        lv_obj_set_style_text_color(cond_badge, COL_DIM, 0);
    }

    if (!s->obs_valid) {
        return;
    }

    /* --- Zone 1: Outdoor Temp --- */
    float t_cur = U_TEMP(s->air_temp_c);
    set_text(temp_val, "%.1f°", (double)t_cur);

    float feels = U_TEMP(s->feels_like_c);
    float dew = U_TEMP(s->dew_point_c);

    if (s->comfort_mode == WX_COMFORT_HEAT_INDEX) {
        float hi = U_TEMP(s->heat_index_c);
        set_text(temp_feels_pill, "Heat Idx %.0f°", (double)hi);
        lv_obj_set_style_text_color(temp_feels_pill,
            strcmp(s->comfort_risk, "Danger") == 0 ||
            strcmp(s->comfort_risk, "Extreme Danger") == 0 ? COL_ALERT :
            strcmp(s->comfort_risk, "Caution") == 0 ? COL_TEMP_AMBER : COL_OK, 0);
    } else if (s->comfort_mode == WX_COMFORT_WIND_CHILL) {
        float wc = U_TEMP(s->wind_chill_c);
        set_text(temp_feels_pill, "Wind Chill %.0f°", (double)wc);
        lv_obj_set_style_text_color(temp_feels_pill,
            strcmp(s->comfort_risk, "Extreme Danger") == 0 ? COL_ALERT :
            COL_TEMP_COLD, 0);
    } else {
        set_text(temp_feels_pill, "Feels %.0f°", (double)feels);
        lv_obj_set_style_text_color(temp_feels_pill, COL_DIM, 0);
    }
    set_text(temp_dew_pill, "Dew %.0f°", (double)dew);

    float t_lo = s->daily_valid ? U_TEMP(s->temp_low_today_c) : t_cur;
    float t_hi = s->daily_valid ? U_TEMP(s->temp_high_today_c) : t_cur;
    set_text(temp_hilo_lbl, "Hi %.0f°  Lo %.0f°  •  %.0f%% RH", (double)t_hi, (double)t_lo, (double)s->humidity_pct);

    float t_frac = (s->air_temp_c - TEMP_MIN_C) / (TEMP_MAX_C - TEMP_MIN_C);
    place_knob_r(temp_knob, Z1_W / 2, 92, t_frac, 65, 18);

    if (s->daily_valid) {
        float lo_frac = (s->temp_low_today_c - TEMP_MIN_C) / (TEMP_MAX_C - TEMP_MIN_C);
        float hi_frac = (s->temp_high_today_c - TEMP_MIN_C) / (TEMP_MAX_C - TEMP_MIN_C);
        place_knob_r(temp_lo_knob, Z1_W / 2, 92, lo_frac, 65, 10);
        place_knob_r(temp_hi_knob, Z1_W / 2, 92, hi_frac, 65, 10);
        lv_obj_clear_flag(temp_lo_knob, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(temp_hi_knob, LV_OBJ_FLAG_HIDDEN);
    }

    /* --- Zone 2: Wind Compass & Speed --- */
    /* The big readout and the needle follow rapid_wind, which the hub sends
     * every 3 s. wind_avg_ms comes from obs_st and only lands once a minute,
     * so reading that here made the display look frozen -- and made "Avg"
     * identical to the live number, since both were the same field. Fall back
     * to the average only until the first rapid_wind arrives. */
    float wind_ms = s->rapid_valid ? s->rapid_wind_ms : s->wind_avg_ms;
    int   wdir    = s->rapid_valid ? s->rapid_wind_dir_deg : s->wind_dir_deg;

    set_text(wind_val, "%.1f", (double)U_WIND(wind_ms));
    lv_label_set_text(wind_unit, U_WIND_SUF);

    set_text(wind_dir_deg, "%d° %s", wdir, wx_compass_point(wdir));

    set_text(wind_gust_lbl, "Gust: %.1f %s", (double)U_WIND(s->wind_gust_ms), U_WIND_SUF);
    set_text(wind_avg_lbl, "Avg: %.1f", (double)U_WIND(s->wind_avg_ms));

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

    /* Beaufort Bar */
    int b_val = (int)(wind_ms * 4.0f);
    if (b_val > 100) b_val = 100;
    lv_bar_set_value(wind_beaufort_bar, b_val, LV_ANIM_OFF);

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
        set_text(in_temp_val, "%.1f°", (double)in_t);
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

    /* --- Zone 4: Current Conditions Centerpiece --- */
    if (s->current_conditions[0] != '\0') {
        lv_label_set_text(cond_title, s->current_conditions);
    } else {
        lv_label_set_text(cond_title, "Current Conditions");
    }
    wx_icon_set(cond_icon, s->current_icon);

    if (s->forecast_days > 0) {
        float f_hi = U_TEMP(s->forecast[0].air_temp_high_c);
        float f_lo = U_TEMP(s->forecast[0].air_temp_low_c);
        set_text(cond_sub, "Today: Hi %.0f°  Lo %.0f°", (double)f_hi, (double)f_lo);
    } else if (s->daily_valid) {
        set_text(cond_sub, "Today: Hi %.0f°  Lo %.0f°", (double)t_hi, (double)t_lo);
    }
}

static void update_mid_deck(const wx_state_t *s, int64_t now)
{
    if (!s->obs_valid) {
        return;
    }

    /* --- MID 1: Rain & Precipitation --- */
    char r_buf[32];
    snprintf(r_buf, sizeof(r_buf), "%s %s", U_RAIN_FMT, U_RAIN_SUF);
    set_text(rain_val, r_buf, (double)U_RAIN(s->rain_today_mm));

    float r_frac = s->rain_today_mm / 25.4f;
    if (r_frac < 0.05f) r_frac = 0.05f;
    if (r_frac > 1.0f)  r_frac = 1.0f;
    lv_obj_set_size(rain_cylinder_fill, 18, (int)(r_frac * 68.0f));

    if (s->rain_rate_mm_hr > 0.0f) {
        set_text(rain_rate_lbl, "Rate: %.2f %s/hr", (double)U_RAIN(s->rain_rate_mm_hr), U_RAIN_SUF);
        lv_label_set_text(rain_badge, "Raining");
        lv_obj_set_style_text_color(rain_badge, COL_RAIN_NEON, 0);
    } else {
        set_text(rain_rate_lbl, "Rate: 0.00 %s/hr", U_RAIN_SUF);
        lv_label_set_text(rain_badge, "Dry");
        lv_obj_set_style_text_color(rain_badge, COL_DIM, 0);
    }

    /* --- MID 2: Lightning Activity Detector --- */
    set_text(ltg_count_val, "%d", s->lightning_count);

    if (s->lightning_count > 0 && s->last_strike_dist_km > 0.0f) {
        cfg_t c;
        cfg_get(&c);
        float l_dist = (c.units == CFG_UNITS_METRIC) ? s->last_strike_dist_km : (s->last_strike_dist_km * 0.621371f);
        const char *l_u = (c.units == CFG_UNITS_METRIC) ? "km" : "mi";
        set_text(ltg_dist_lbl, "Strike %.1f %s away", (double)l_dist, l_u);
        set_text(ltg_status_lbl, "Detected %llu min ago", (unsigned long long)((now - s->last_strike_epoch) / 60));
        lv_label_set_text(ltg_badge, "Active");
        lv_obj_set_style_text_color(ltg_badge, COL_ALERT, 0);
    } else {
        lv_label_set_text(ltg_dist_lbl, "No strikes in 3 hrs");
        lv_label_set_text(ltg_status_lbl, "Live detector active");
        lv_label_set_text(ltg_badge, "Clear");
        lv_obj_set_style_text_color(ltg_badge, COL_OK, 0);
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

        /* Daylight duration calculation */
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
    set_text(uv_badge, "UV %.1f (%s)", (double)s->uv_index, wx_uv_description(s->uv_index));
    lv_obj_set_style_text_color(uv_badge, uv_col, 0);

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
        lv_label_set_text(baro_badge, "High Pressure");
        lv_obj_set_style_text_color(baro_badge, COL_OK, 0);
    } else if (s->pressure_mb < 1005.0f) {
        lv_label_set_text(baro_badge, "Low Pressure");
        lv_obj_set_style_text_color(baro_badge, COL_ALERT, 0);
    } else {
        lv_label_set_text(baro_badge, "Fair & Stable");
        lv_obj_set_style_text_color(baro_badge, COL_PRESS_NEON, 0);
    }

    /* Visual 10-bar pressure sparkline */
    for (int b = 0; b < 10; b++) {
        float f = (float)b / 9.0f;
        int bh = 8 + (int)(f * 14.0f + (s->pressure_trend_mb_3h * 2.0f));
        if (bh < 4) bh = 4;
        if (bh > 28) bh = 28;
        lv_obj_set_size(baro_bars[b], 14, bh);
        lv_obj_set_pos(baro_bars[b], 10 + b * 22, 140 - bh);
        lv_obj_set_style_bg_color(baro_bars[b], (b == 9) ? COL_PRESS_NEON : COL_TRACK, 0);
    }
}

static void update_forecast_deck(const wx_state_t *s)
{
    bool has_fcst = s->forecast_valid && s->forecast_days > 0;
    char buf[16];

    float week_min = 100.0f;
    float week_max = -100.0f;

    if (has_fcst) {
        for (int i = 0; i < s->forecast_days && i < FC_COLS; i++) {
            if (s->forecast[i].air_temp_low_c < week_min) {
                week_min = s->forecast[i].air_temp_low_c;
            }
            if (s->forecast[i].air_temp_high_c > week_max) {
                week_max = s->forecast[i].air_temp_high_c;
            }
        }
    } else {
        week_min = s->air_temp_c - 10.0f;
        week_max = s->air_temp_c + 10.0f;
    }

    if (week_max <= week_min) {
        week_max = week_min + 1.0f;
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
                const lv_image_dsc_t *dsc = wx_icon_get_image_dsc("clear-day");
                if (dsc && fc[i].icon && !s_fc_lottie) lv_image_set_src(fc[i].icon, dsc);
                else fc_set_icon(i, "clear-day");
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
                const lv_image_dsc_t *dsc = wx_icon_get_image_dsc("clear-day");
                if (dsc && fc[i].icon && !s_fc_lottie) lv_image_set_src(fc[i].icon, dsc);
                else fc_set_icon(i, "clear-day");
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
            lv_obj_add_flag(fc[i].bar_bg, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const wx_forecast_day_t *d = &s->forecast[i];
        lv_obj_clear_flag(fc[i].bar_bg, LV_OBJ_FLAG_HIDDEN);
        if (fc[i].icon) lv_obj_clear_flag(fc[i].icon, LV_OBJ_FLAG_HIDDEN);

        time_t dt = (time_t)d->day_start_local;
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

        fc_set_icon(i, d->icon[0] ? d->icon : "unknown");

        set_text(fc[i].hi, "%.0f°", (double)U_TEMP(d->air_temp_high_c));
        set_text(fc[i].lo, "%.0f°", (double)U_TEMP(d->air_temp_low_c));

        if (d->precip_probability > 0) {
            set_text(fc[i].pop, "%d%%", d->precip_probability);
            lv_obj_set_style_text_color(fc[i].pop,
                                        wx_icon_is_wet(d->icon) ? COL_RAIN_NEON : COL_DIM, 0);
        } else {
            lv_label_set_text(fc[i].pop, "");
        }

        /* Range Bar */
        const int bar_total_w = 46;
        float f_lo = (d->air_temp_low_c - week_min) / (week_max - week_min);
        float f_hi = (d->air_temp_high_c - week_min) / (week_max - week_min);
        if (f_lo < 0.0f) f_lo = 0.0f;
        if (f_hi > 1.0f) f_hi = 1.0f;
        int bar_x = (int)(f_lo * bar_total_w);
        int bar_w = (int)((f_hi - f_lo) * bar_total_w);
        if (bar_w < 4) bar_w = 4;

        lv_obj_set_pos(fc[i].bar_fill, bar_x, 0);
        lv_obj_set_size(fc[i].bar_fill, bar_w, 6);

        if (i == 0 && fc[i].bar_dot && s->obs_valid) {
            float f_cur = (s->air_temp_c - week_min) / (week_max - week_min);
            if (f_cur < 0.0f) f_cur = 0.0f;
            if (f_cur > 1.0f) f_cur = 1.0f;
            int dot_x = (int)(f_cur * bar_total_w) - 4;
            lv_obj_set_pos(fc[i].bar_dot, dot_x, -1);
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

    if (s_fc_rebuild) {
        rebuild_forecast_icons();
    }

    if (settings_is_visible()) {
        settings_tick();
        return;
    }
    if (graphs_is_visible()) {
        graphs_tick();
        return;
    }
    if (page2_is_visible()) {
        page2_tick();
        return;
    }
    if (wifi_setup_is_visible()) {
        wifi_setup_tick();
        return;
    }

    wx_state_t s;
    wx_snapshot(&s);

    update_header(&s, now);
    update_alert_banner(&s, now);
    check_lightning_sound(&s, now);
    update_top_deck(&s, now);
    update_mid_deck(&s, now);
    update_forecast_deck(&s);
    update_smart_pill(&s);

    audio_scheduler_tick(now, s.current_icon);
}
