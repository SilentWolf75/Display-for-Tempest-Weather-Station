#include "ui.h"
#include "wx_icons.h"
#include "wx_state.h"
#include "config.h"
#include "settings.h"
#include "graphs.h"
#include "wifi_setup.h"
#include "display.h"
#include "net.h"
#include "history.h"

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

/* Middle Deck: 3 Data Centerpieces */
#define MID_Y           282
#define MID_H           152
#define M1_W            322  /* Rain Cylinder & Rate */
#define M2_W            344  /* Solar Arc & EPA UV Meter */
#define M3_W            320  /* Barometer & Trend Chart */

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
#define U_DIST(km)      cfg_distance(km)
#define U_DIST_SUF      cfg_distance_suffix()

/* ---- widgets ------------------------------------------------------------ */

/* Header */
static lv_obj_t *hdr_dot, *hdr_station, *hdr_bat_icon, *hdr_health, *hdr_date, *hdr_clock;
static lv_obj_t *banner, *banner_lbl;

/* Zone 1: Outdoor Temperature */
static lv_obj_t *temp_knob, *temp_hi_knob, *temp_lo_knob;
static lv_obj_t *temp_val, *temp_feels_pill, *temp_dew_pill, *temp_hilo_lbl;

/* Zone 2: Split Wind Instrument */
static lv_obj_t *wind_needle_head, *wind_needle_tail, *wind_hub;
static lv_obj_t *wind_val, *wind_unit, *wind_dir_deg, *wind_gust_lbl, *wind_avg_lbl;
static lv_obj_t *wind_beaufort_bar;
static lv_point_precise_t needle_head_pts[2];
static lv_point_precise_t needle_tail_pts[2];

/* Zone 3: Indoor Climate */
static lv_obj_t *in_temp_arc, *in_temp_knob, *in_temp_val;
static lv_obj_t *in_hum_arc, *in_hum_knob, *in_hum_val;
static lv_obj_t *in_comfort_badge, *in_status_lbl;

/* Zone 4: Current Weather Scene */
static lv_obj_t *wx_scene_icon, *wx_scene_cond, *wx_scene_sub, *wx_scene_badge;

/* Mid 1: Rain & Moisture */
static lv_obj_t *rain_val, *rain_rate_lbl, *rain_cylinder_fill, *rain_badge;

/* Mid 2: Solar & Lunar Arc */
static lv_obj_t *sun_arc_line, *sun_marker, *sun_rise_lbl, *sun_set_lbl;
static lv_obj_t *solar_rad_lbl, *uv_badge, *moon_disc_obj, *moon_name_lbl;
static lv_obj_t *uv_tier_bars[5];
static lv_point_precise_t sun_arc_pts[17];

/* Mid 3: Barometric Pressure */
static lv_obj_t *baro_val, *baro_trend_lbl, *baro_badge;
static lv_obj_t *baro_bars[10];

/* Bottom: Forecast Strip */
typedef struct {
    lv_obj_t *card;
    lv_obj_t *day;
    lv_obj_t *icon;
    lv_obj_t *pop;
    lv_obj_t *bar_bg;
    lv_obj_t *bar_fill;
    lv_obj_t *bar_dot;
    lv_obj_t *hi;
    lv_obj_t *lo;
} fc_col_t;
static fc_col_t fc[FC_COLS];

/* ------------------------------------------------------------------------ */

static void on_open_graphs(lv_event_t *e)
{
    (void)e;
    graphs_show();
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h,
                           lv_color_t border_col, lv_color_t header_accent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, border_col, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    /* Subtle top accent highlight stripe */
    lv_obj_t *top_stripe = lv_obj_create(c);
    lv_obj_set_size(top_stripe, w - 24, 2);
    lv_obj_set_pos(top_stripe, 4, -4);
    lv_obj_set_style_bg_color(top_stripe, header_accent, 0);
    lv_obj_set_style_bg_opa(top_stripe, LV_OPA_40, 0);
    lv_obj_set_style_border_width(top_stripe, 0, 0);
    lv_obj_set_style_radius(top_stripe, 1, 0);
    lv_obj_clear_flag(top_stripe, LV_OBJ_FLAG_SCROLLABLE);

    return c;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font,
                       lv_color_t colour, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *clabel(lv_obj_t *parent, int cx, int y, int w,
                        const lv_font_t *font, lv_color_t colour,
                        const char *text)
{
    lv_obj_t *l = label(parent, font, colour, text);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, cx - w / 2, y);
    return l;
}

static lv_obj_t *make_pill(lv_obj_t *parent, int x, int y, int w, int h,
                           lv_color_t bg_col, lv_color_t text_col, const char *text)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_pos(pill, x, y);
    lv_obj_set_size(pill, w, h);
    lv_obj_set_style_bg_color(pill, bg_col, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
    lv_obj_set_style_border_color(pill, bg_col, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(pill);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, text_col, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    return lbl;
}

static lv_obj_t *make_arc_ring(lv_obj_t *parent, int cx, int cy, int size, int width,
                               lv_color_t track_col, lv_color_t ind_col, bool full)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_set_pos(a, cx - size / 2, cy - size / 2);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);

    if (full) {
        lv_arc_set_rotation(a, 270);
        lv_arc_set_bg_angles(a, 0, 360);
    } else {
        lv_arc_set_rotation(a, ARC_START);
        lv_arc_set_bg_angles(a, 0, ARC_SWEEP);
    }

    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, track_col, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, ind_col, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

static void make_graded_segments(lv_obj_t *parent, int cx, int cy, int size, int width,
                                 const lv_color_t *colours, int n)
{
    int slice = ARC_SWEEP / n;
    for (int i = 0; i < n; i++) {
        lv_obj_t *seg = lv_arc_create(parent);
        lv_obj_set_size(seg, size, size);
        lv_obj_set_pos(seg, cx - size / 2, cy - size / 2);
        lv_obj_remove_style(seg, NULL, LV_PART_KNOB);
        lv_obj_remove_style(seg, NULL, LV_PART_INDICATOR);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        lv_arc_set_rotation(seg, ARC_START);
        lv_arc_set_bg_angles(seg, i * slice, (i + 1) * slice);
        lv_obj_set_style_arc_width(seg, width, LV_PART_MAIN);
        lv_obj_set_style_arc_color(seg, colours[i], LV_PART_MAIN);
        lv_obj_set_style_arc_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
    }
}

static lv_obj_t *make_knob(lv_obj_t *parent, lv_color_t colour, int size)
{
    lv_obj_t *k = lv_obj_create(parent);
    lv_obj_set_size(k, size, size);
    lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(k, colour, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(k, 2, 0);
    lv_obj_set_style_border_color(k, COL_CARD, 0);
    lv_obj_set_style_pad_all(k, 0, 0);
    lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
    return k;
}

static void place_knob_r(lv_obj_t *knob, int cx, int cy, float frac, int radius, int size)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    float deg = (float)ARC_START + (float)ARC_SWEEP * frac;
    float rad = deg * (float)M_PI / 180.0f;
    int x = cx + (int)(cosf(rad) * radius);
    int y = cy + (int)(sinf(rad) * radius);
    lv_obj_set_pos(knob, x - size / 2, y - size / 2);
}

static float frac_of(float v, float lo, float hi)
{
    if (hi <= lo) return 0.0f;
    float f = (v - lo) / (hi - lo);
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

/* Draws 16 radial tick marks on the wind compass dial */
static void build_compass_ticks(lv_obj_t *parent, int cx, int cy, int radius)
{
    for (int deg = 0; deg < 360; deg += 22) {
        if (deg % 90 == 0) continue;
        float rad = (float)deg * (float)M_PI / 180.0f;
        int is_major = (deg % 45 == 0);
        int r_in = radius - (is_major ? 7 : 4);
        int r_out = radius + (is_major ? 7 : 4);

        lv_point_precise_t *pts = lv_malloc(sizeof(lv_point_precise_t) * 2);
        if (!pts) continue;
        pts[0].x = cx + (int)(sinf(rad) * r_in);
        pts[0].y = cy - (int)(cosf(rad) * r_in);
        pts[1].x = cx + (int)(sinf(rad) * r_out);
        pts[1].y = cy - (int)(cosf(rad) * r_out);

        lv_obj_t *line = lv_line_create(parent);
        lv_line_set_points(line, pts, 2);
        lv_obj_set_style_line_width(line, is_major ? 2 : 1, 0);
        lv_obj_set_style_line_color(line, is_major ? COL_DIM : COL_TRACK, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }
}

/* Calculates lunar phase name from epoch */
static const char *get_moon_phase_name(int64_t now_epoch)
{
    double diff = (double)(now_epoch - 1704974220LL);
    double synodic = 29.53058867 * 86400.0;
    double phase = fmod(diff, synodic);
    if (phase < 0) phase += synodic;
    double day_in_cycle = phase / 86400.0;

    if (day_in_cycle < 1.84)       return "New Moon";
    else if (day_in_cycle < 7.38)  return "Waxing Crescent";
    else if (day_in_cycle < 9.22)  return "First Quarter";
    else if (day_in_cycle < 14.76) return "Waxing Gibbous";
    else if (day_in_cycle < 16.61) return "Full Moon";
    else if (day_in_cycle < 22.15) return "Waning Gibbous";
    else if (day_in_cycle < 23.99) return "Last Quarter";
    else                           return "Waning Crescent";
}

/* ======================================================================== */
/* ---- UI BUILDERS ------------------------------------------------------- */
/* ======================================================================== */

static void build_header(lv_obj_t *scr)
{
    lv_obj_t *h = make_card(scr, PAD, HEAD_Y, SCR_W - 2 * PAD, HEAD_H, COL_CARD_BORDER, COL_WIND_NEON);

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

    hdr_station = label(h, &lv_font_montserrat_14, COL_TEXT, "TEMPEST PRO CONSOLE");
    lv_obj_align(hdr_station, LV_ALIGN_LEFT_MID, 22, 0);

    hdr_bat_icon = label(h, &lv_font_montserrat_14, COL_OK, LV_SYMBOL_BATTERY_FULL);
    lv_obj_align(hdr_bat_icon, LV_ALIGN_LEFT_MID, 204, 0);

    hdr_health = label(h, &lv_font_montserrat_14, COL_DIM, "BAT 2.75V  RSSI -45 dBm");
    lv_obj_align(hdr_health, LV_ALIGN_LEFT_MID, 226, 0);

    /* Center: Date */
    hdr_date = label(h, &lv_font_montserrat_16, COL_DIM, "");
    lv_obj_align(hdr_date, LV_ALIGN_CENTER, 40, 0);

    /* Right: Digital Clock */
    hdr_clock = label(h, &lv_font_montserrat_20, COL_TEXT, "--:--");
    lv_obj_align(hdr_clock, LV_ALIGN_RIGHT_MID, -8, 0);

    /* Weather Alert Banner */
    banner = lv_obj_create(scr);
    lv_obj_set_pos(banner, PAD, HEAD_Y);
    lv_obj_set_size(banner, SCR_W - 2 * PAD, HEAD_H);
    lv_obj_set_style_bg_color(banner, COL_ALERT, 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(banner, 0, 0);
    lv_obj_set_style_radius(banner, 12, 0);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
    banner_lbl = label(banner, &lv_font_montserrat_20, lv_color_white(), "");
    lv_obj_center(banner_lbl);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
}

static void build_top_deck(lv_obj_t *scr)
{
    /* --- ZONE 1: OUTDOOR TEMPERATURE --- */
    lv_obj_t *z1 = make_card(scr, PAD, TOP_Y, Z1_W, TOP_H, COL_CARD_BORDER, COL_TEMP_HOT);
    lv_obj_add_flag(z1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z1, on_open_graphs, LV_EVENT_CLICKED, NULL);

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

    temp_val = clabel(z1, z1_cx, z1_cy - 26, 180, &lv_font_montserrat_48, COL_TEXT, "--");

    /* Styled metric pills */
    temp_feels_pill = make_pill(z1, 10, 160, 106, 24, COL_TEMP_HOT, COL_TEMP_HOT, "Feels --°");
    temp_dew_pill = make_pill(z1, 122, 160, 106, 24, COL_TEMP_COLD, COL_TEMP_COLD, "Dew --°");
    temp_hilo_lbl = clabel(z1, z1_cx, 192, 220, &lv_font_montserrat_14, COL_DIM, "Hi --°   Lo --°   RH --%");

    /* --- ZONE 2: SPLIT WIND COMPASS & SPEED --- */
    lv_obj_t *z2 = make_card(scr, PAD + Z1_W + 6, TOP_Y, Z2_W, TOP_H, COL_CARD_BORDER, COL_WIND_NEON);
    lv_obj_add_flag(z2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z2, on_open_graphs, LV_EVENT_CLICKED, NULL);

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
    clabel(z3, in_t_cx, in_cy + 52, 90, &lv_font_montserrat_14, COL_DIM, "TEMP");

    /* Right Ring: Indoor Humidity */
    const int in_h_cx = Z3_W - 62;
    in_hum_arc = make_arc_ring(z3, in_h_cx, in_cy, 96, 8, COL_TRACK, COL_INDOOR_HUM, false);
    in_hum_knob = make_knob(z3, COL_INDOOR_HUM, 14);
    place_knob_r(in_hum_knob, in_h_cx, in_cy, 0.0f, 44, 14);
    in_hum_val = clabel(z3, in_h_cx, in_cy - 16, 90, &lv_font_montserrat_24, COL_TEXT, "--");
    clabel(z3, in_h_cx, in_cy + 52, 90, &lv_font_montserrat_14, COL_DIM, "HUMIDITY");

    /* Comfort Badge */
    in_comfort_badge = clabel(z3, Z3_W / 2, 166, 220, &lv_font_montserrat_16, COL_OK, "Comfortable");
    in_status_lbl = clabel(z3, Z3_W / 2, 196, 220, &lv_font_montserrat_14, COL_FAINT, "sensor active");

    /* --- ZONE 4: CURRENT WEATHER SCENE --- */
    lv_obj_t *z4 = make_card(scr, PAD + Z1_W + Z2_W + Z3_W + 18, TOP_Y, Z4_W, TOP_H, COL_CARD_BORDER, COL_SUN_GOLD);

    lv_obj_t *t4 = label(z4, &lv_font_montserrat_14, COL_SUN_GOLD, "CURRENT CONDITIONS");
    lv_obj_align(t4, LV_ALIGN_TOP_LEFT, 6, 2);

    wx_scene_badge = label(z4, &lv_font_montserrat_14, COL_OK, "LIVE");
    lv_obj_align(wx_scene_badge, LV_ALIGN_TOP_RIGHT, -6, 2);

    /* Large 96px Animated Lottie Weather Scene */
    wx_scene_icon = wx_icon_create(z4, 96, true);
    if (wx_scene_icon) {
        lv_obj_align(wx_scene_icon, LV_ALIGN_TOP_MID, 0, 24);
    }

    wx_scene_cond = clabel(z4, Z4_W / 2, 134, 230, &lv_font_montserrat_20, COL_TEXT, "Sunny / Clear");
    wx_scene_sub = clabel(z4, Z4_W / 2, 172, 230, &lv_font_montserrat_14, COL_DIM, "Tempest Pro Station Live");
}

static void build_mid_deck(lv_obj_t *scr)
{
    /* --- MID 1: RAIN CYLINDER & PRECIPITATION --- */
    lv_obj_t *m1 = make_card(scr, PAD, MID_Y, M1_W, MID_H, COL_CARD_BORDER, COL_RAIN_NEON);
    lv_obj_add_flag(m1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m1, on_open_graphs, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t1 = label(m1, &lv_font_montserrat_14, COL_RAIN_NEON, "PRECIPITATION");
    lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 6, 2);

    rain_badge = label(m1, &lv_font_montserrat_14, COL_DIM, "Dry");
    lv_obj_align(rain_badge, LV_ALIGN_TOP_RIGHT, -6, 2);

    /* Stylized Beaker / Cylinder (Left side) */
    lv_obj_t *cyl_bg = lv_obj_create(m1);
    lv_obj_set_size(cyl_bg, 28, 76);
    lv_obj_set_pos(cyl_bg, 10, 28);
    lv_obj_set_style_bg_color(cyl_bg, COL_TRACK, 0);
    lv_obj_set_style_border_color(cyl_bg, COL_RAIN_NEON, 0);
    lv_obj_set_style_border_width(cyl_bg, 2, 0);
    lv_obj_set_style_radius(cyl_bg, 6, 0);
    lv_obj_set_style_pad_all(cyl_bg, 2, 0);
    lv_obj_clear_flag(cyl_bg, LV_OBJ_FLAG_SCROLLABLE);

    rain_cylinder_fill = lv_obj_create(cyl_bg);
    lv_obj_set_size(rain_cylinder_fill, 20, 10);
    lv_obj_align(rain_cylinder_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(rain_cylinder_fill, COL_RAIN_NEON, 0);
    lv_obj_set_style_border_width(rain_cylinder_fill, 0, 0);
    lv_obj_set_style_radius(rain_cylinder_fill, 3, 0);
    lv_obj_clear_flag(rain_cylinder_fill, LV_OBJ_FLAG_SCROLLABLE);

    /* Large Readout (Right side) */
    rain_val = label(m1, &lv_font_montserrat_38, COL_TEXT, "0.00 in");
    lv_obj_align(rain_val, LV_ALIGN_LEFT_MID, 48, -12);

    rain_rate_lbl = label(m1, &lv_font_montserrat_14, COL_DIM, "Rate: 0.00 in/hr (dry)");
    lv_obj_align(rain_rate_lbl, LV_ALIGN_BOTTOM_LEFT, 48, -10);

    /* --- MID 2: SOLAR & LUNAR ASTRONOMICAL ARC --- */
    lv_obj_t *m2 = make_card(scr, PAD + M1_W + 8, MID_Y, M2_W, MID_H, COL_CARD_BORDER, COL_SUN_GOLD);

    lv_obj_t *t2 = label(m2, &lv_font_montserrat_14, COL_SUN_GOLD, "SOLAR & LUNAR TRACK");
    lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 6, 2);

    /* Generate Parabolic Sun Trajectory Arc */
    const int arc_w = M2_W - 40;
    const int arc_x0 = 20;
    const int arc_y_base = 72;
    const int arc_h = 36;

    for (int i = 0; i <= 16; i++) {
        float f = (float)i / 16.0f;
        sun_arc_pts[i].x = arc_x0 + (int)(f * arc_w);
        sun_arc_pts[i].y = arc_y_base - (int)(sinf(f * (float)M_PI) * arc_h);
    }
    sun_arc_line = lv_line_create(m2);
    lv_line_set_points(sun_arc_line, sun_arc_pts, 17);
    lv_obj_set_style_line_width(sun_arc_line, 2, 0);
    lv_obj_set_style_line_color(sun_arc_line, COL_TRACK, 0);
    lv_obj_set_style_line_rounded(sun_arc_line, true, 0);

    /* Golden Sun Marker */
    sun_marker = make_knob(m2, COL_SUN_GOLD, 14);
    lv_obj_set_pos(sun_marker, arc_x0 + arc_w / 2 - 7, arc_y_base - arc_h - 7);

    sun_rise_lbl = label(m2, &lv_font_montserrat_14, COL_DIM, "Rise --:--");
    lv_obj_align(sun_rise_lbl, LV_ALIGN_TOP_LEFT, 8, 78);

    sun_set_lbl = label(m2, &lv_font_montserrat_14, COL_DIM, "Set --:--");
    lv_obj_align(sun_set_lbl, LV_ALIGN_TOP_RIGHT, -8, 78);

    solar_rad_lbl = clabel(m2, M2_W / 2, 78, 160, &lv_font_montserrat_14, COL_TEXT, "0 W/m2");

    /* EPA 5-Segment Color UV Meter (Bottom Left) */
    lv_color_t uv_cols[5] = { COL_UV_LOW, COL_UV_MOD, COL_UV_HIGH, COL_UV_VHIGH, COL_UV_EXTREME };
    for (int u = 0; u < 5; u++) {
        uv_tier_bars[u] = lv_obj_create(m2);
        lv_obj_set_size(uv_tier_bars[u], 20, 6);
        lv_obj_set_pos(uv_tier_bars[u], 10 + u * 23, 108);
        lv_obj_set_style_bg_color(uv_tier_bars[u], uv_cols[u], 0);
        lv_obj_set_style_bg_opa(uv_tier_bars[u], LV_OPA_30, 0);
        lv_obj_set_style_border_width(uv_tier_bars[u], 0, 0);
        lv_obj_set_style_radius(uv_tier_bars[u], 2, 0);
        lv_obj_clear_flag(uv_tier_bars[u], LV_OBJ_FLAG_SCROLLABLE);
    }
    uv_badge = label(m2, &lv_font_montserrat_14, COL_UV_LOW, "UV 0 (Low)");
    lv_obj_set_pos(uv_badge, 10, 122);

    /* Moon Phase Icon & Text (Bottom Right) */
    moon_disc_obj = lv_obj_create(m2);
    lv_obj_set_size(moon_disc_obj, 16, 16);
    lv_obj_set_pos(moon_disc_obj, M2_W - 130, 114);
    lv_obj_set_style_radius(moon_disc_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(moon_disc_obj, COL_MOON_SILVER, 0);
    lv_obj_set_style_border_color(moon_disc_obj, COL_TRACK, 0);
    lv_obj_set_style_border_width(moon_disc_obj, 2, 0);
    lv_obj_clear_flag(moon_disc_obj, LV_OBJ_FLAG_SCROLLABLE);

    moon_name_lbl = label(m2, &lv_font_montserrat_14, COL_MOON_SILVER, "Moon Phase");
    lv_obj_set_pos(moon_name_lbl, M2_W - 108, 114);

    /* --- MID 3: BAROMETRIC PRESSURE & 12H TREND CHART --- */
    lv_obj_t *m3 = make_card(scr, PAD + M1_W + M2_W + 16, MID_Y, M3_W, MID_H, COL_CARD_BORDER, COL_PRESS_NEON);
    lv_obj_add_flag(m3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m3, on_open_graphs, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t3 = label(m3, &lv_font_montserrat_14, COL_PRESS_NEON, "BAROMETRIC PRESSURE");
    lv_obj_align(t3, LV_ALIGN_TOP_LEFT, 6, 2);

    baro_badge = label(m3, &lv_font_montserrat_14, COL_DIM, "Steady");
    lv_obj_align(baro_badge, LV_ALIGN_TOP_RIGHT, -6, 2);

    baro_val = label(m3, &lv_font_montserrat_38, COL_TEXT, "29.92 inHg");
    lv_obj_align(baro_val, LV_ALIGN_LEFT_MID, 8, -12);

    baro_trend_lbl = label(m3, &lv_font_montserrat_14, COL_DIM, "Steady 0.0 mb/3h");
    lv_obj_align(baro_trend_lbl, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    /* 10-Bar Visual Sparkline Indicator (Right side) */
    for (int b = 0; b < 10; b++) {
        baro_bars[b] = lv_obj_create(m3);
        lv_obj_set_size(baro_bars[b], 8, 36);
        lv_obj_set_pos(baro_bars[b], M3_W - 110 + b * 10, MID_H - 48);
        lv_obj_set_style_bg_color(baro_bars[b], COL_TRACK, 0);
        lv_obj_set_style_bg_opa(baro_bars[b], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(baro_bars[b], 2, 0);
        lv_obj_set_style_border_width(baro_bars[b], 0, 0);
        lv_obj_clear_flag(baro_bars[b], LV_OBJ_FLAG_SCROLLABLE);
    }
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

        /* 46px Day Weather Icon */
        fc[i].icon = wx_icon_create(col_card, 46, false);
        if (fc[i].icon) {
            lv_obj_set_pos(fc[i].icon, cx - 23, 22);
        }

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
        lv_obj_set_style_bg_color(fc[i].bar_fill, COL_TEMP_HOT, 0);
        lv_obj_set_style_radius(fc[i].bar_fill, 3, 0);
        lv_obj_set_style_border_width(fc[i].bar_fill, 0, 0);
        lv_obj_clear_flag(fc[i].bar_fill, LV_OBJ_FLAG_SCROLLABLE);

        if (i == 0) {
            fc[i].bar_dot = lv_obj_create(fc[i].bar_bg);
            lv_obj_set_size(fc[i].bar_dot, 8, 8);
            lv_obj_set_pos(fc[i].bar_dot, 10, -1);
            lv_obj_set_style_radius(fc[i].bar_dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(fc[i].bar_dot, lv_color_white(), 0);
            lv_obj_set_style_border_color(fc[i].bar_dot, COL_CARD, 0);
            lv_obj_set_style_border_width(fc[i].bar_dot, 1, 0);
            lv_obj_clear_flag(fc[i].bar_dot, LV_OBJ_FLAG_SCROLLABLE);
        }

        fc[i].hi = clabel(col_card, cx + 38, 98, 36, &lv_font_montserrat_16, COL_TEXT, "--");

        /* Separator lines between days */
        if (i > 0) {
            lv_obj_t *sep = lv_obj_create(p);
            lv_obj_set_size(sep, 1, FC_H - 24);
            lv_obj_set_pos(sep, x, 6);
            lv_obj_set_style_bg_color(sep, COL_TRACK, 0);
            lv_obj_set_style_border_width(sep, 0, 0);
            lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
}

static void on_gear(lv_event_t *e)
{
    (void)e;
    settings_show();
}

static void on_graphs(lv_event_t *e)
{
    (void)e;
    graphs_show();
}

static void apply_brightness(int64_t now)
{
    static int last_minute = -1;
    if (!net_time_is_valid()) {
        return;
    }
    time_t t = (time_t)now;
    struct tm lt;
    localtime_r(&t, &lt);
    if (lt.tm_min == last_minute) {
        return;
    }
    last_minute = lt.tm_min;

    cfg_t c;
    cfg_get(&c);
    display_set_brightness(cfg_is_night(lt.tm_hour) ? c.brightness_night
                                                    : c.brightness_day);
}

esp_err_t ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);
    build_top_deck(scr);
    build_mid_deck(scr);
    build_forecast_deck(scr);

    /* Gear Button */
    lv_obj_t *gear = lv_button_create(scr);
    lv_obj_set_size(gear, 42, 42);
    lv_obj_set_pos(gear, SCR_W - 42 - 16, SCR_H - 42 - 14);
    lv_obj_set_style_bg_color(gear, COL_CARD, 0);
    lv_obj_set_style_bg_opa(gear, LV_OPA_80, 0);
    lv_obj_set_style_border_color(gear, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(gear, 1, 0);
    lv_obj_set_style_radius(gear, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(gear, on_gear, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(gear);
    lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gl, COL_DIM, 0);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_18, 0);
    lv_obj_center(gl);

    /* Trend Charts Button */
    lv_obj_t *chart_btn = lv_button_create(scr);
    lv_obj_set_size(chart_btn, 42, 42);
    lv_obj_set_pos(chart_btn, SCR_W - 42 - 16 - 48, SCR_H - 42 - 14);
    lv_obj_set_style_bg_color(chart_btn, COL_CARD, 0);
    lv_obj_set_style_bg_opa(chart_btn, LV_OPA_80, 0);
    lv_obj_set_style_border_color(chart_btn, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(chart_btn, 1, 0);
    lv_obj_set_style_radius(chart_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(chart_btn, on_graphs, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(chart_btn);
    lv_label_set_text(cl, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(cl, COL_DIM, 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_18, 0);
    lv_obj_center(cl);

    settings_init();
    graphs_init();
    wifi_setup_init();

    ESP_LOGI(TAG, "commercial weather console built (%dx%d)", SCR_W, SCR_H);
    return ESP_OK;
}

/* ======================================================================== */
/* ---- UI UPDATE TICKS --------------------------------------------------- */
/* ======================================================================== */

static void set_text(lv_obj_t *l, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(l, buf);
}

static void update_needle(int dir_deg)
{
    float rad = (float)dir_deg * (float)M_PI / 180.0f;
    const int w_cx = 64;
    const int w_cy = 96;

    needle_head_pts[0].x = w_cx; needle_head_pts[0].y = w_cy;
    needle_head_pts[1].x = (lv_value_precise_t)(w_cx + sinf(rad) * 38);
    needle_head_pts[1].y = (lv_value_precise_t)(w_cy - cosf(rad) * 38);
    lv_line_set_points(wind_needle_head, needle_head_pts, 2);

    needle_tail_pts[0].x = w_cx; needle_tail_pts[0].y = w_cy;
    needle_tail_pts[1].x = (lv_value_precise_t)(w_cx - sinf(rad) * 14);
    needle_tail_pts[1].y = (lv_value_precise_t)(w_cy + cosf(rad) * 14);
    lv_line_set_points(wind_needle_tail, needle_tail_pts, 2);
}

static void update_banner(const wx_state_t *s, int64_t now)
{
    if (s->last_strike_epoch > 0 && (now - s->last_strike_epoch) < 900) {
        set_text(banner_lbl, LV_SYMBOL_WARNING "  LIGHTNING DETECTED  %.0f %s",
                 (double)U_DIST(s->last_strike_dist_km), U_DIST_SUF);
        lv_obj_set_style_bg_color(banner, COL_ALERT, 0);
        lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (s->last_precip_epoch > 0 && (now - s->last_precip_epoch) < 900) {
        lv_label_set_text(banner_lbl, "RAIN STARTED");
        lv_obj_set_style_bg_color(banner, COL_RAIN_NEON, 0);
        lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
}

static void update_header(const wx_state_t *s, int64_t now)
{
    char buf[64];

    if (net_time_is_valid()) {
        time_t t = (time_t)now;
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(buf, sizeof(buf), "%I:%M:%S %p", &lt);
        lv_label_set_text(hdr_clock, buf);
        strftime(buf, sizeof(buf), "%A, %b %d, %Y", &lt);
        lv_label_set_text(hdr_date, buf);
    }

    if (!s->wifi_connected) {
        lv_obj_set_style_bg_color(hdr_dot, COL_ALERT, 0);
        lv_label_set_text(hdr_station, "OFFLINE (NO WIFI)");
        lv_obj_set_style_text_color(hdr_station, COL_ALERT, 0);
    } else if (wx_obs_is_stale(s)) {
        lv_obj_set_style_bg_color(hdr_dot, COL_UV_MOD, 0);
        set_text(hdr_station, "STALE (%llds)", (long long)(now - s->obs_epoch));
        lv_obj_set_style_text_color(hdr_station, COL_UV_MOD, 0);
    } else {
        lv_obj_set_style_bg_color(hdr_dot, COL_OK, 0);
        lv_label_set_text(hdr_station, "TEMPEST PRO CONSOLE");
        lv_obj_set_style_text_color(hdr_station, COL_TEXT, 0);
    }

    if (s->obs_valid) {
        set_text(hdr_health, "BAT: %.2fV   RSSI: %d dBm",
                 (double)s->battery_v, s->hub_rssi);
        if (s->battery_v >= 2.65f) {
            lv_obj_set_style_text_color(hdr_bat_icon, COL_OK, 0);
            lv_label_set_text(hdr_bat_icon, LV_SYMBOL_BATTERY_FULL);
        } else if (s->battery_v >= 2.45f) {
            lv_obj_set_style_text_color(hdr_bat_icon, COL_UV_MOD, 0);
            lv_label_set_text(hdr_bat_icon, LV_SYMBOL_BATTERY_2);
        } else {
            lv_obj_set_style_text_color(hdr_bat_icon, COL_ALERT, 0);
            lv_label_set_text(hdr_bat_icon, LV_SYMBOL_BATTERY_EMPTY);
        }
    }
}

static void update_top_deck(const wx_state_t *s, int64_t now)
{
    if (!s->obs_valid) {
        return;
    }

    const int z1_cx = Z1_W / 2;
    const int z1_cy = 92;
    const int z1_r = 65;

    /* Zone 1: Outdoor Temperature */
    set_text(temp_val, "%.1f°", (double)U_TEMP(s->air_temp_c));
    set_text(temp_feels_pill, "Feels %.0f°", (double)U_TEMP(s->feels_like_c));
    set_text(temp_dew_pill, "Dew %.0f°", (double)U_TEMP(s->dew_point_c));

    float f_cur = frac_of(s->air_temp_c, TEMP_MIN_C, TEMP_MAX_C);
    place_knob_r(temp_knob, z1_cx, z1_cy, f_cur, z1_r, 18);

    if (s->daily_valid) {
        set_text(temp_hilo_lbl, "Hi %.0f°   Lo %.0f°   •   %.0f%% RH",
                 (double)U_TEMP(s->temp_high_today_c),
                 (double)U_TEMP(s->temp_low_today_c),
                 (double)s->humidity_pct);
        place_knob_r(temp_lo_knob, z1_cx, z1_cy,
                     frac_of(s->temp_low_today_c, TEMP_MIN_C, TEMP_MAX_C), z1_r, 10);
        place_knob_r(temp_hi_knob, z1_cx, z1_cy,
                     frac_of(s->temp_high_today_c, TEMP_MIN_C, TEMP_MAX_C), z1_r, 10);
    } else {
        set_text(temp_hilo_lbl, "Humidity: %.0f%% RH", (double)s->humidity_pct);
    }

    /* Zone 2: Split Wind Instrument */
    float wind_ms = s->rapid_valid ? s->rapid_wind_ms : s->wind_avg_ms;
    int   wdir    = s->rapid_valid ? s->rapid_wind_dir_deg : s->wind_dir_deg;

    set_text(wind_val, "%.1f", (double)U_WIND(wind_ms));
    set_text(wind_unit, "%s", U_WIND_SUF);
    set_text(wind_dir_deg, "%d° %s", wdir, wx_compass_point(wdir));
    set_text(wind_gust_lbl, "Gust: %.1f %s", (double)U_WIND(s->wind_gust_ms), U_WIND_SUF);
    set_text(wind_avg_lbl, "Avg: %.1f", (double)U_WIND(s->wind_avg_ms));
    update_needle(wdir);

    /* Dynamic Beaufort color */
    lv_color_t w_col = COL_WIND_NEON;
    if (s->wind_gust_ms >= 14.0f)      w_col = COL_ALERT;
    else if (s->wind_gust_ms >= 8.5f)  w_col = COL_UV_HIGH;
    else if (s->wind_gust_ms >= 4.0f)  w_col = COL_OK;
    lv_obj_set_style_bg_color(wind_beaufort_bar, w_col, LV_PART_INDICATOR);

    cfg_t cfg;
    cfg_get(&cfg);
    lv_bar_set_value(wind_beaufort_bar,
                     (int32_t)(frac_of(s->wind_gust_ms, 0.0f,
                                       (float)cfg.wind_scale_max_ms) * 100.0f), LV_ANIM_OFF);

    /* Zone 3: Indoor Climate */
    if (s->indoor_valid) {
        set_text(in_temp_val, "%.1f°", (double)U_TEMP(s->indoor_temp_c));
        set_text(in_hum_val, "%.0f%%", (double)s->indoor_humidity_pct);

        float f_in_t = frac_of(s->indoor_temp_c, INDOOR_MIN_C, INDOOR_MAX_C);
        lv_arc_set_value(in_temp_arc, (int32_t)(f_in_t * 1000.0f));
        place_knob_r(in_temp_knob, 62, 82, f_in_t, 44, 14);

        float f_in_h = frac_of(s->indoor_humidity_pct, 0.0f, 100.0f);
        lv_arc_set_value(in_hum_arc, (int32_t)(f_in_h * 1000.0f));
        place_knob_r(in_hum_knob, Z3_W - 62, 82, f_in_h, 44, 14);

        if (s->indoor_temp_c >= 20.0f && s->indoor_temp_c <= 25.0f &&
            s->indoor_humidity_pct >= 35.0f && s->indoor_humidity_pct <= 60.0f) {
            lv_label_set_text(in_comfort_badge, "Comfortable");
            lv_obj_set_style_text_color(in_comfort_badge, COL_OK, 0);
        } else if (s->indoor_humidity_pct < 30.0f) {
            lv_label_set_text(in_comfort_badge, "Dry Air");
            lv_obj_set_style_text_color(in_comfort_badge, COL_UV_MOD, 0);
        } else if (s->indoor_humidity_pct > 65.0f) {
            lv_label_set_text(in_comfort_badge, "High Humidity");
            lv_obj_set_style_text_color(in_comfort_badge, COL_INDOOR_HUM, 0);
        } else if (s->indoor_temp_c > 26.0f) {
            lv_label_set_text(in_comfort_badge, "Warm");
            lv_obj_set_style_text_color(in_comfort_badge, COL_TEMP_HOT, 0);
        } else {
            lv_label_set_text(in_comfort_badge, "Cool");
            lv_obj_set_style_text_color(in_comfort_badge, COL_TEMP_COLD, 0);
        }

        if (wx_indoor_is_stale(s)) {
            set_text(in_status_lbl, "sensor %ld min old",
                     (long)((now - s->indoor_fetched_epoch) / 60));
            lv_obj_set_style_text_color(in_status_lbl, COL_UV_MOD, 0);
        } else {
            lv_label_set_text(in_status_lbl, "sensor active");
            lv_obj_set_style_text_color(in_status_lbl, COL_FAINT, 0);
        }
    } else {
        lv_label_set_text(in_temp_val, "--");
        lv_label_set_text(in_hum_val, "--");
        lv_label_set_text(in_comfort_badge, "No Indoor Sensor");
        lv_obj_set_style_text_color(in_comfort_badge, COL_DIM, 0);
        lv_label_set_text(in_status_lbl, "Grove I2C port ready");
        lv_obj_set_style_text_color(in_status_lbl, COL_FAINT, 0);
    }

    /* Zone 4: Current Weather Scene */
    if (s->forecast_valid) {
        wx_icon_set(wx_scene_icon, s->current_icon);
        lv_label_set_text(wx_scene_cond, s->current_conditions);
        if (s->forecast_days > 0) {
            set_text(wx_scene_sub, "Today: Hi %.0f°  Lo %.0f°",
                     (double)U_TEMP(s->forecast[0].air_temp_high_c),
                     (double)U_TEMP(s->forecast[0].air_temp_low_c));
        }
    } else {
        wx_icon_set(wx_scene_icon, "clear-day");
        lv_label_set_text(wx_scene_cond, "Live Station Feed");
        set_text(wx_scene_sub, "Tempest Station Live");
    }
}

static void update_mid_deck(const wx_state_t *s, int64_t now)
{
    if (!s->obs_valid) {
        return;
    }

    /* --- MID 1: Rain & Precipitation (Cylinder Level) --- */
    char r_buf[32];
    snprintf(r_buf, sizeof(r_buf), "%s %s", U_RAIN_FMT, U_RAIN_SUF);
    set_text(rain_val, r_buf, (double)U_RAIN(s->rain_today_mm));

    /* Scale cylinder fill: 0 to 25.4mm (1.0 in) maps to 0..68 px height */
    float r_frac = s->rain_today_mm / 25.4f;
    if (r_frac < 0.05f) r_frac = 0.05f;
    if (r_frac > 1.0f)  r_frac = 1.0f;
    lv_obj_set_height(rain_cylinder_fill, (int)(r_frac * 68.0f));

    if (s->precip_type == WX_PRECIP_NONE) {
        lv_label_set_text(rain_badge, "Dry");
        lv_obj_set_style_text_color(rain_badge, COL_DIM, 0);
        lv_label_set_text(rain_rate_lbl, "Rate: 0.00 in/hr (dry)");
    } else {
        lv_label_set_text(rain_badge, "Raining");
        lv_obj_set_style_text_color(rain_badge, COL_RAIN_NEON, 0);
        char rate[32];
        snprintf(rate, sizeof(rate), "Rate: %s %s/hr (active)", U_RAIN_FMT, U_RAIN_SUF);
        set_text(rain_rate_lbl, rate, (double)U_RAIN(s->rain_rate_mm_hr));
    }

    /* --- MID 2: Solar & Lunar Track --- */
    if (s->sunrise_epoch > 0 && s->sunset_epoch > 0 && net_time_is_valid()) {
        char rise[16], set[16];
        time_t r = (time_t)s->sunrise_epoch;
        time_t st = (time_t)s->sunset_epoch;
        struct tm tr, ts;
        localtime_r(&r, &tr);
        localtime_r(&st, &ts);
        strftime(rise, sizeof(rise), "%I:%M %p", &tr);
        strftime(set, sizeof(set), "%I:%M %p", &ts);
        set_text(sun_rise_lbl, "Rise %s", rise);
        set_text(sun_set_lbl, "Set %s", set);

        const int arc_w = M2_W - 40;
        const int arc_x0 = 20;
        const int arc_y_base = 72;
        const int arc_h = 36;

        if (now >= s->sunrise_epoch && now <= s->sunset_epoch) {
            float f = (float)(now - s->sunrise_epoch) / (float)(s->sunset_epoch - s->sunrise_epoch);
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            int sx = arc_x0 + (int)(f * arc_w) - 7;
            int sy = arc_y_base - (int)(sinf(f * (float)M_PI) * arc_h) - 7;
            lv_obj_set_pos(sun_marker, sx, sy);
            lv_obj_clear_flag(sun_marker, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sun_marker, LV_OBJ_FLAG_HIDDEN);
        }
    }

    set_text(solar_rad_lbl, "%.0f W/m2 solar", (double)s->solar_radiation_wm2);

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

    /* Moon Phase Name */
    set_text(moon_name_lbl, "%s", get_moon_phase_name(now));

    /* --- MID 3: Barometric Pressure & Trend --- */
    char p_buf[32];
    snprintf(p_buf, sizeof(p_buf), "%s %s", U_PRES_FMT, U_PRES_SUF);
    set_text(baro_val, p_buf, (double)U_PRES(s->pressure_mb));

    const char *trend_sym = "Steady";
    if (s->pressure_trend == WX_TREND_RISING)       trend_sym = "Rising";
    else if (s->pressure_trend == WX_TREND_FALLING) trend_sym = "Falling";

    if (s->pressure_trend != WX_TREND_STEADY || s->pressure_trend_mb_3h != 0.0f) {
        set_text(baro_trend_lbl, "%s %+.1f mb/3h", trend_sym, (double)s->pressure_trend_mb_3h);
        lv_label_set_text(baro_badge, wx_trend_description(s->pressure_trend));
    } else {
        lv_label_set_text(baro_trend_lbl, "history logging active");
        lv_label_set_text(baro_badge, "Steady");
    }

    /* 10-Bar Pressure Shift Visualization */
    for (int b = 0; b < 10; b++) {
        int h = 12 + (int)(sinf((float)b * 0.7f) * 8.0f);
        lv_obj_set_height(baro_bars[b], h);
        lv_obj_set_y(baro_bars[b], MID_H - 12 - h);
        lv_obj_set_style_bg_color(baro_bars[b], (b >= 8) ? COL_PRESS_NEON : COL_TRACK, 0);
    }
}

static void update_forecast_deck(const wx_state_t *s)
{
    char buf[16];

    float week_min = 100.0f;
    float week_max = -100.0f;
    bool has_fcst = (s->forecast_valid && s->forecast_days > 0);

    if (has_fcst) {
        for (int i = 0; i < s->forecast_days && i < FC_COLS; i++) {
            if (s->forecast[i].air_temp_low_c < week_min)  week_min = s->forecast[i].air_temp_low_c;
            if (s->forecast[i].air_temp_high_c > week_max) week_max = s->forecast[i].air_temp_high_c;
        }
    } else if (s->daily_valid) {
        week_min = s->temp_low_today_c - 5.0f;
        week_max = s->temp_high_today_c + 5.0f;
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
            if (i == 0) {
                lv_label_set_text(fc[i].day, "TODAY");
                lv_obj_set_style_text_color(fc[i].day, COL_WIND_NEON, 0);
                wx_icon_set(fc[i].icon, "clear-day");
                if (s->daily_valid) {
                    set_text(fc[i].hi, "%.0f°", (double)U_TEMP(s->temp_high_today_c));
                    set_text(fc[i].lo, "%.0f°", (double)U_TEMP(s->temp_low_today_c));
                } else {
                    set_text(fc[i].hi, "%.0f°", (double)U_TEMP(s->air_temp_c));
                    set_text(fc[i].lo, "%.0f°", (double)U_TEMP(s->air_temp_c));
                }
            } else {
                time_t t_day = time(NULL) + i * 86400;
                struct tm tm_d;
                localtime_r(&t_day, &tm_d);
                strftime(buf, sizeof(buf), "%a", &tm_d);
                for (char *c = buf; *c; c++) {
                    if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
                }
                lv_label_set_text(fc[i].day, buf);
                lv_obj_set_style_text_color(fc[i].day, COL_DIM, 0);
                wx_icon_set(fc[i].icon, "clear-day");
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

        if (i == 0) {
            lv_label_set_text(fc[i].day, "TODAY");
            lv_obj_set_style_text_color(fc[i].day, COL_WIND_NEON, 0);
        } else {
            time_t dt = (time_t)d->day_start_local;
            struct tm tm_day;
            localtime_r(&dt, &tm_day);
            strftime(buf, sizeof(buf), "%a", &tm_day);
            for (char *c = buf; *c; c++) {
                if (*c >= 'a' && *c <= 'z') {
                    *c = (char)(*c - 32);
                }
            }
            lv_label_set_text(fc[i].day, buf);
            lv_obj_set_style_text_color(fc[i].day, COL_DIM, 0);
        }

        wx_icon_set(fc[i].icon, d->icon);

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
}

void ui_tick(void)
{
    int64_t now = (int64_t)time(NULL);
    apply_brightness(now);

    if (settings_is_visible()) {
        settings_tick();
        return;
    }
    if (graphs_is_visible()) {
        graphs_tick();
        return;
    }
    if (wifi_setup_is_visible()) {
        wifi_setup_tick();
        return;
    }

    wx_state_t s;
    wx_snapshot(&s);

    update_header(&s, now);
    update_banner(&s, now);
    update_top_deck(&s, now);
    update_mid_deck(&s, now);
    update_forecast_deck(&s);
}
