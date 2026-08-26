#include "ui.h"
#include "wx_icons.h"
#include "wx_state.h"
#include "config.h"
#include "settings.h"
#include "graphs.h"
#include "display.h"
#include "net.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "ui";

/* ---- palette ------------------------------------------------------------ */
#define COL_BG        lv_color_hex(0x0B0E13)
#define COL_CARD      lv_color_hex(0x151A22)
#define COL_TRACK     lv_color_hex(0x222A35)
#define COL_TEXT      lv_color_hex(0xE8EDF2)
#define COL_DIM       lv_color_hex(0x7E8B99)
#define COL_FAINT     lv_color_hex(0x6C7A89)

#define COL_TEMP      lv_color_hex(0xFF7043)
#define COL_WIND      lv_color_hex(0x4FC3F7)
#define COL_NEEDLE    lv_color_hex(0xFFB300)
#define COL_HUMID     lv_color_hex(0x9C8CFF)
#define COL_RAIN      lv_color_hex(0x2196F3)
#define COL_PRESS     lv_color_hex(0x26C6DA)
#define COL_UV        lv_color_hex(0xFFC107)
#define COL_ALERT     lv_color_hex(0xEF5350)
#define COL_OK        lv_color_hex(0x66BB6A)

/* HVAC state colours -- the whole point of the indoor ring. */
#define COL_HEATING   lv_color_hex(0xFF8A3D)
#define COL_COOLING   lv_color_hex(0x4FC3F7)
#define COL_IDLE      lv_color_hex(0x5C6B7A)

/* ---- geometry ----------------------------------------------------------- */
#define SCR_W         1024
#define SCR_H         600
#define PAD           12

#define HEAD_Y        12
#define HEAD_H        72

#define GAUGE_Y       92
#define GAUGE_H       252
#define RING_BOX      210        /* lv_arc widget size */
#define RING_W        15         /* arc stroke width */
#define RING_R        97         /* stroke centreline radius, for knob maths */

/* Gauge centres, in the gauge panel's CONTENT coordinates (10px padding). */
#define G_CY          108
#define G1_CX         163
#define G2_CX         490
#define G3_CX         817
#define IN_CX         130        /* indoor centre within its own container */

#define CARD_Y        352
#define CARD_H        100
#define CARD_W        241

#define FC_Y          462
#define FC_H          126
#define FC_COLS       WX_FORECAST_DAYS

/* 270-degree gauges: start at 135 deg (down-left), sweep clockwise. */
#define ARC_START     135
#define ARC_SWEEP     270

/* Ranges the rings map onto. Deliberately wide -- a gauge that pins is worse
 * than one that reads low. */
#define TEMP_MIN_C    (-20.0f)
#define TEMP_MAX_C    45.0f
#define INDOOR_MIN_C  10.0f
#define INDOOR_MAX_C  32.0f
#define WIND_MAX_MS   20.0f

/* Units are a runtime setting now (see config.h). These macros keep the old
 * call-site spelling; only what they expand to changed. Note the *_SUF macros
 * are function calls returning strings, so they can no longer be
 * string-concatenated into a format literal -- use %s. */
#define U_TEMP(c)    cfg_temp(c)
#define U_TEMP_SUF   cfg_temp_suffix()
#define U_WIND(ms)   cfg_wind(ms)
#define U_WIND_SUF   cfg_wind_suffix()
#define U_PRES(mb)   cfg_pressure(mb)
#define U_PRES_SUF   cfg_pressure_suffix()
#define U_PRES_FMT   cfg_pressure_fmt()
#define U_RAIN(mm)   cfg_rain(mm)
#define U_RAIN_SUF   cfg_rain_suffix()
#define U_RAIN_FMT   cfg_rain_fmt()
#define U_DIST(km)   cfg_distance(km)
#define U_DIST_SUF   cfg_distance_suffix()

/* ---- widgets ------------------------------------------------------------ */
static lv_obj_t *hdr_icon, *hdr_cond, *hdr_sun, *hdr_clock, *hdr_date;
static lv_obj_t *hdr_dot, *hdr_link, *hdr_health;
static lv_obj_t *banner, *banner_lbl;

static lv_obj_t *temp_knob, *temp_val, *temp_feels, *temp_hilo;
static lv_obj_t *wind_arc, *wind_needle, *wind_val, *wind_dir, *wind_gust;
static lv_point_precise_t needle_pts[2];

static lv_obj_t *in_cont, *in_arc, *in_knob, *in_val, *in_hum;
static lv_obj_t *in_state, *in_setpoint, *in_age;

typedef struct { lv_obj_t *value; lv_obj_t *sub; lv_obj_t *unit; } card_t;
static card_t cards[4];
enum { C_HUMID = 0, C_RAIN, C_PRESS, C_UV };

typedef struct { lv_obj_t *day, *icon, *hi, *lo, *pop; } fc_col_t;
static fc_col_t fc[FC_COLS];

/* ------------------------------------------------------------------------ */

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h,
                           lv_color_t accent, bool with_bar)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    if (with_bar) {
        lv_obj_t *bar = lv_obj_create(c);
        lv_obj_set_size(bar, 4, h - 20);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, -6, 0);
        lv_obj_set_style_bg_color(bar, accent, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }
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

/* Centred on cx with the top edge at y. Fixed width so centring survives a
 * text change without waiting for a layout pass. */
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

static lv_obj_t *make_ring(lv_obj_t *parent, int cx, int cy,
                           lv_color_t indicator, bool full_circle)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, RING_BOX, RING_BOX);
    lv_obj_set_pos(a, cx - RING_BOX / 2, cy - RING_BOX / 2);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);

    if (full_circle) {
        lv_arc_set_rotation(a, 270);
        lv_arc_set_bg_angles(a, 0, 360);
    } else {
        lv_arc_set_rotation(a, ARC_START);
        lv_arc_set_bg_angles(a, 0, ARC_SWEEP);
    }

    lv_obj_set_style_arc_width(a, RING_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, RING_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, indicator, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

/* LVGL has no gradient-along-an-arc, so a graded scale is N solid arcs sharing
 * a centre, each covering one slice. This is exactly how the commercial
 * consoles do it -- look closely and their "gradient" rings are segments too. */
static void make_graded_scale(lv_obj_t *parent, int cx, int cy,
                              const lv_color_t *colours, int n)
{
    int slice = ARC_SWEEP / n;
    for (int i = 0; i < n; i++) {
        lv_obj_t *seg = lv_arc_create(parent);
        lv_obj_set_size(seg, RING_BOX, RING_BOX);
        lv_obj_set_pos(seg, cx - RING_BOX / 2, cy - RING_BOX / 2);
        lv_obj_remove_style(seg, NULL, LV_PART_KNOB);
        lv_obj_remove_style(seg, NULL, LV_PART_INDICATOR);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        lv_arc_set_rotation(seg, ARC_START);
        lv_arc_set_bg_angles(seg, i * slice, (i + 1) * slice);
        lv_obj_set_style_arc_width(seg, RING_W, LV_PART_MAIN);
        lv_obj_set_style_arc_color(seg, colours[i], LV_PART_MAIN);
        lv_obj_set_style_arc_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
    }
}

static lv_obj_t *make_knob(lv_obj_t *parent, lv_color_t colour)
{
    lv_obj_t *k = lv_obj_create(parent);
    lv_obj_set_size(k, 20, 20);
    lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(k, colour, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(k, 3, 0);
    lv_obj_set_style_border_color(k, COL_CARD, 0);
    lv_obj_set_style_pad_all(k, 0, 0);
    lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
    return k;
}

static void place_knob(lv_obj_t *knob, int cx, int cy, float frac)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    float deg = (float)ARC_START + (float)ARC_SWEEP * frac;
    float rad = deg * (float)M_PI / 180.0f;
    int x = cx + (int)(cosf(rad) * RING_R);
    int y = cy + (int)(sinf(rad) * RING_R);
    lv_obj_set_pos(knob, x - 10, y - 10);
}

static float frac_of(float v, float lo, float hi)
{
    if (hi <= lo) return 0.0f;
    float f = (v - lo) / (hi - lo);
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

/* ---- header ------------------------------------------------------------- */

static void build_header(lv_obj_t *scr)
{
    lv_obj_t *p = make_card(scr, PAD, HEAD_Y, SCR_W - 2 * PAD, HEAD_H,
                            COL_TEXT, false);

    /* The one animated icon on the panel. Everything else is static, so the
     * vector renderer only ever drives a single 96px surface. */
    hdr_icon = wx_icon_create(p, 52, true);
    if (hdr_icon) {
        lv_obj_align(hdr_icon, LV_ALIGN_LEFT_MID, 6, 0);
    }

    hdr_cond = label(p, &lv_font_montserrat_24, COL_TEXT, "");
    lv_obj_align(hdr_cond, LV_ALIGN_LEFT_MID, 78, -11);

    hdr_sun = label(p, &lv_font_montserrat_14, COL_DIM, "");
    lv_obj_align(hdr_sun, LV_ALIGN_LEFT_MID, 80, 13);

    hdr_clock = label(p, &lv_font_montserrat_34, COL_TEXT, "--:--");
    lv_obj_align(hdr_clock, LV_ALIGN_CENTER, 0, -11);

    hdr_date = label(p, &lv_font_montserrat_14, COL_DIM, "");
    lv_obj_align(hdr_date, LV_ALIGN_CENTER, 0, 15);

    hdr_dot = lv_obj_create(p);
    lv_obj_set_size(hdr_dot, 8, 8);
    lv_obj_set_style_radius(hdr_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hdr_dot, COL_IDLE, 0);
    lv_obj_set_style_bg_opa(hdr_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr_dot, 0, 0);
    lv_obj_set_style_pad_all(hdr_dot, 0, 0);
    lv_obj_clear_flag(hdr_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hdr_dot, LV_ALIGN_RIGHT_MID, -168, -11);

    hdr_link = label(p, &lv_font_montserrat_14, COL_DIM, "starting");
    lv_obj_align(hdr_link, LV_ALIGN_RIGHT_MID, -8, -11);

    hdr_health = label(p, &lv_font_montserrat_14, COL_DIM, "");
    lv_obj_align(hdr_health, LV_ALIGN_RIGHT_MID, -8, 15);

    banner = lv_obj_create(scr);
    lv_obj_set_pos(banner, PAD, HEAD_Y);
    lv_obj_set_size(banner, SCR_W - 2 * PAD, HEAD_H);
    lv_obj_set_style_bg_color(banner, COL_ALERT, 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(banner, 0, 0);
    lv_obj_set_style_radius(banner, 12, 0);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
    banner_lbl = label(banner, &lv_font_montserrat_28, lv_color_white(), "");
    lv_obj_center(banner_lbl);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
}

/* ---- gauges ------------------------------------------------------------- */

static void build_gauges(lv_obj_t *scr)
{
    lv_obj_t *p = make_card(scr, PAD, GAUGE_Y, SCR_W - 2 * PAD, GAUGE_H,
                            COL_TEXT, false);

    /* --- outdoor temperature: graded scale + position knob --- */
    lv_color_t grade[5];
    grade[0] = lv_color_hex(0x3E6FA8);   /* cold */
    grade[1] = lv_color_hex(0x2E9E8F);
    grade[2] = lv_color_hex(0x7FA83E);
    grade[3] = lv_color_hex(0xC98B2E);
    grade[4] = lv_color_hex(0xE2603C);   /* hot  */
    make_graded_scale(p, G1_CX, G_CY, grade, 5);

    temp_knob  = make_knob(p, COL_TEMP);
    place_knob(temp_knob, G1_CX, G_CY, 0.0f);
    temp_val   = clabel(p, G1_CX, G_CY - 34, 210, &lv_font_montserrat_48,
                        COL_TEXT, "--");
    temp_feels = clabel(p, G1_CX, G_CY + 24, 210, &lv_font_montserrat_18,
                        COL_TEMP, "");
    temp_hilo  = clabel(p, G1_CX, G_CY + 48, 210, &lv_font_montserrat_14,
                        COL_DIM, "");
    clabel(p, G1_CX, G_CY + 110, 220, &lv_font_montserrat_14, COL_DIM,
           "OUTDOOR");

    /* --- wind: full compass ring, gust arc, direction needle --- */
    wind_arc = make_ring(p, G2_CX, G_CY, COL_WIND, true);

    clabel(p, G2_CX, G_CY - 110, 30, &lv_font_montserrat_14, COL_DIM, "N");
    clabel(p, G2_CX, G_CY + 94, 30, &lv_font_montserrat_14, COL_DIM, "S");
    clabel(p, G2_CX + 110, G_CY - 8, 30, &lv_font_montserrat_14, COL_DIM, "E");
    clabel(p, G2_CX - 110, G_CY - 8, 30, &lv_font_montserrat_14, COL_DIM, "W");

    needle_pts[0].x = G2_CX;
    needle_pts[0].y = G_CY;
    needle_pts[1].x = G2_CX;
    needle_pts[1].y = G_CY - 70;
    wind_needle = lv_line_create(p);
    lv_line_set_points(wind_needle, needle_pts, 2);
    lv_obj_set_style_line_width(wind_needle, 6, 0);
    lv_obj_set_style_line_color(wind_needle, COL_NEEDLE, 0);
    lv_obj_set_style_line_rounded(wind_needle, true, 0);

    lv_obj_t *hub = lv_obj_create(p);
    lv_obj_set_size(hub, 14, 14);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, COL_NEEDLE, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hub, 0, 0);
    lv_obj_set_style_pad_all(hub, 0, 0);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(hub, G2_CX - 7, G_CY - 7);

    wind_val  = clabel(p, G2_CX, G_CY - 42, 210, &lv_font_montserrat_46,
                       COL_TEXT, "--");
    wind_dir  = clabel(p, G2_CX, G_CY + 14, 210, &lv_font_montserrat_16,
                       COL_DIM, "");
    wind_gust = clabel(p, G2_CX, G_CY + 40, 210, &lv_font_montserrat_14,
                       COL_WIND, "");
    clabel(p, G2_CX, G_CY + 110, 220, &lv_font_montserrat_14, COL_DIM, "WIND");

    /* --- indoor: the Nest. Wrapped in its own container so staleness can dim
     * the whole group in one call without touching the outdoor half. --- */
    in_cont = lv_obj_create(p);
    lv_obj_set_pos(in_cont, G3_CX - IN_CX, 0);
    lv_obj_set_size(in_cont, 260, GAUGE_H - 20);
    lv_obj_set_style_bg_opa(in_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(in_cont, 0, 0);
    lv_obj_set_style_pad_all(in_cont, 0, 0);
    lv_obj_clear_flag(in_cont, LV_OBJ_FLAG_SCROLLABLE);

    in_arc  = make_ring(in_cont, IN_CX, G_CY, COL_IDLE, false);
    in_knob = make_knob(in_cont, COL_IDLE);
    place_knob(in_knob, IN_CX, G_CY, 0.0f);

    in_val      = clabel(in_cont, IN_CX, G_CY - 34, 250,
                         &lv_font_montserrat_48, COL_TEXT, "--");
    in_hum      = clabel(in_cont, IN_CX, G_CY + 24, 250,
                         &lv_font_montserrat_18, COL_HUMID, "");
    in_setpoint = clabel(in_cont, IN_CX, G_CY + 48, 250,
                         &lv_font_montserrat_14, COL_DIM, "");
    in_state    = clabel(in_cont, IN_CX, G_CY + 86, 250,
                         &lv_font_montserrat_16, COL_IDLE, "");
    in_age      = clabel(in_cont, IN_CX, G_CY + 110, 250,
                         &lv_font_montserrat_14, COL_DIM, "INDOOR");
}

/* ---- metric cards ------------------------------------------------------- */

static const char *CARD_CAPTION[4] = {
    "HUMIDITY", "RAIN TODAY", "PRESSURE", "UV  /  SOLAR",
};

static void build_cards(lv_obj_t *scr)
{
    const lv_color_t accents[4] = { COL_HUMID, COL_RAIN, COL_PRESS, COL_UV };
    const char *units[4] = { "%", U_RAIN_SUF, U_PRES_SUF, "" };  /* runtime */

    for (int i = 0; i < 4; i++) {
        int x = PAD + i * (CARD_W + 13);
        lv_obj_t *c = make_card(scr, x, CARD_Y, CARD_W, CARD_H,
                                accents[i], true);

        lv_obj_t *cap = label(c, &lv_font_montserrat_14, accents[i],
                              CARD_CAPTION[i]);
        lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 8, 0);

        cards[i].value = label(c, &lv_font_montserrat_38, COL_TEXT, "--");
        lv_obj_align(cards[i].value, LV_ALIGN_LEFT_MID, 8, 6);

        cards[i].unit = label(c, &lv_font_montserrat_16, COL_DIM, units[i]);
        lv_obj_align(cards[i].unit, LV_ALIGN_RIGHT_MID, -8, 6);

        cards[i].sub = label(c, &lv_font_montserrat_14, COL_DIM, "");
        lv_obj_align(cards[i].sub, LV_ALIGN_BOTTOM_LEFT, 8, 0);
    }
}

/* ---- forecast ----------------------------------------------------------- */

static void build_forecast(lv_obj_t *scr)
{
    lv_obj_t *p = make_card(scr, PAD, FC_Y, SCR_W - 2 * PAD, FC_H,
                            COL_TEXT, false);
    const int w = (SCR_W - 2 * PAD - 20) / FC_COLS;

    for (int i = 0; i < FC_COLS; i++) {
        int cx = i * w + w / 2;

        fc[i].day  = clabel(p, cx, 2, w, &lv_font_montserrat_14, COL_DIM, "");
        fc[i].icon = wx_icon_create(p, 46, false);
        if (fc[i].icon) {
            lv_obj_set_pos(fc[i].icon, cx - 23, 22);
        }
        fc[i].pop  = clabel(p, cx, 52, w, &lv_font_montserrat_14, COL_RAIN, "");
        fc[i].hi   = clabel(p, cx - 22, 72, 44, &lv_font_montserrat_20,
                            COL_TEXT, "");
        fc[i].lo   = clabel(p, cx + 22, 72, 44, &lv_font_montserrat_20,
                            COL_FAINT, "");

        if (i > 0) {
            lv_obj_t *sep = lv_obj_create(p);
            lv_obj_set_size(sep, 1, FC_H - 44);
            lv_obj_set_pos(sep, i * w, 4);
            lv_obj_set_style_bg_color(sep, COL_TRACK, 0);
            lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(sep, 0, 0);
            lv_obj_set_style_pad_all(sep, 0, 0);
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

/* Applies the day/night brightness rule. Called once a minute rather than
 * every tick -- writing the same LEDC duty 60 times a minute is pointless. */
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
    build_gauges(scr);
    build_cards(scr);
    build_forecast(scr);

    /* Gear, bottom-right over the forecast panel. Deliberately small and dim:
     * it is a wall display, not a tablet, and the control should be findable
     * without competing with the weather. */
    lv_obj_t *gear = lv_button_create(scr);
    lv_obj_set_size(gear, 44, 44);
    lv_obj_set_pos(gear, SCR_W - 44 - 20, SCR_H - 44 - 18);
    lv_obj_set_style_bg_color(gear, COL_CARD, 0);
    lv_obj_set_style_bg_opa(gear, LV_OPA_70, 0);
    lv_obj_set_style_border_width(gear, 0, 0);
    lv_obj_set_style_radius(gear, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_add_event_cb(gear, on_gear, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(gear);
    lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gl, COL_DIM, 0);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_20, 0);
    lv_obj_center(gl);

    lv_obj_t *chart_btn = lv_button_create(scr);
    lv_obj_set_size(chart_btn, 44, 44);
    lv_obj_set_pos(chart_btn, SCR_W - 44 - 20 - 52, SCR_H - 44 - 18);
    lv_obj_set_style_bg_color(chart_btn, COL_CARD, 0);
    lv_obj_set_style_bg_opa(chart_btn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(chart_btn, 0, 0);
    lv_obj_set_style_radius(chart_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(chart_btn, 0, 0);
    lv_obj_add_event_cb(chart_btn, on_graphs, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(chart_btn);
    lv_label_set_text(cl, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(cl, COL_DIM, 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_20, 0);
    lv_obj_center(cl);

    settings_init();
    graphs_init();

    ESP_LOGI(TAG, "screen built (%dx%d)", SCR_W, SCR_H);
    return ESP_OK;
}

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
    /* Meteorological convention: the reported direction is where the wind is
     * coming FROM, and 0 degrees is north = straight up on the dial. */
    float rad = (float)dir_deg * (float)M_PI / 180.0f;
    const int len = 70;
    needle_pts[0].x = G2_CX;
    needle_pts[0].y = G_CY;
    needle_pts[1].x = (lv_value_precise_t)(G2_CX + sinf(rad) * len);
    needle_pts[1].y = (lv_value_precise_t)(G_CY - cosf(rad) * len);
    lv_line_set_points(wind_needle, needle_pts, 2);
}

static void update_banner(const wx_state_t *s, int64_t now)
{
    if (s->last_strike_epoch > 0 && (now - s->last_strike_epoch) < 900) {
        set_text(banner_lbl, LV_SYMBOL_WARNING "  LIGHTNING  %.0f %s",
                 (double)U_DIST(s->last_strike_dist_km), U_DIST_SUF);
        lv_obj_set_style_bg_color(banner, COL_ALERT, 0);
        lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (s->last_precip_epoch > 0 && (now - s->last_precip_epoch) < 900) {
        lv_label_set_text(banner_lbl, "RAIN STARTED");
        lv_obj_set_style_bg_color(banner, COL_RAIN, 0);
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
        strftime(buf, sizeof(buf), "%I:%M %p", &lt);
        lv_label_set_text(hdr_clock, buf);
        strftime(buf, sizeof(buf), "%A, %B %d", &lt);
        lv_label_set_text(hdr_date, buf);
    }

    if (s->forecast_valid) {
        lv_label_set_text(hdr_cond, s->current_conditions);
        wx_icon_set(hdr_icon, s->current_icon);

        if (s->sunrise_epoch > 0 && net_time_is_valid()) {
            char rise[16], set[16];
            time_t r = (time_t)s->sunrise_epoch;
            time_t st = (time_t)s->sunset_epoch;
            struct tm tr, ts;
            localtime_r(&r, &tr);
            localtime_r(&st, &ts);
            strftime(rise, sizeof(rise), "%I:%M", &tr);
            strftime(set, sizeof(set), "%I:%M", &ts);
            set_text(hdr_sun, "sunrise %s   sunset %s", rise, set);
        }
    }

    /* Link health reports the LOCAL feed, because that is the one that keeps
     * the display honest when the internet goes away. */
    if (!s->wifi_connected) {
        lv_obj_set_style_bg_color(hdr_dot, COL_ALERT, 0);
        lv_label_set_text(hdr_link, "no network");
        lv_obj_set_style_text_color(hdr_link, COL_ALERT, 0);
    } else if (wx_obs_is_stale(s)) {
        lv_obj_set_style_bg_color(hdr_dot, COL_UV, 0);
        set_text(hdr_link, "station stale %llds",
                 (long long)(now - s->obs_epoch));
        lv_obj_set_style_text_color(hdr_link, COL_UV, 0);
    } else {
        lv_obj_set_style_bg_color(hdr_dot, COL_OK, 0);
        lv_label_set_text(hdr_link, "station online");
        lv_obj_set_style_text_color(hdr_link, COL_DIM, 0);
    }

    if (s->obs_valid) {
        set_text(hdr_health, "%.2f V   RSSI %d",
                 (double)s->battery_v, s->hub_rssi);
    }
}

static void update_outdoor(const wx_state_t *s)
{
    if (!s->obs_valid) {
        return;
    }

    set_text(temp_val, "%.1f", (double)U_TEMP(s->air_temp_c));
    set_text(temp_feels, "feels %.0f%s",
             (double)U_TEMP(s->feels_like_c), U_TEMP_SUF);
    place_knob(temp_knob, G1_CX, G_CY,
               frac_of(s->air_temp_c, TEMP_MIN_C, TEMP_MAX_C));

    /* Observed extremes since local midnight -- what actually happened today,
     * which is more interesting on a wall display than what was forecast.
     * Falls back to the forecast until enough of the day has been observed. */
    if (s->daily_valid) {
        set_text(temp_hilo, "hi %.0f    lo %.0f",
                 (double)U_TEMP(s->temp_high_today_c),
                 (double)U_TEMP(s->temp_low_today_c));
    } else if (s->forecast_valid && s->forecast_days > 0) {
        set_text(temp_hilo, "hi %.0f    lo %.0f  (fcst)",
                 (double)U_TEMP(s->forecast[0].air_temp_high_c),
                 (double)U_TEMP(s->forecast[0].air_temp_low_c));
    }

    float wind_ms = s->rapid_valid ? s->rapid_wind_ms : s->wind_avg_ms;
    int   wdir    = s->rapid_valid ? s->rapid_wind_dir_deg : s->wind_dir_deg;

    set_text(wind_val, "%.1f", (double)U_WIND(wind_ms));
    set_text(wind_dir, "%s   %s", U_WIND_SUF, wx_compass_point(wdir));
    set_text(wind_gust, "gust %.1f", (double)U_WIND(s->wind_gust_ms));
    update_needle(wdir);
    cfg_t cfg;
    cfg_get(&cfg);
    lv_arc_set_value(wind_arc,
                     (int32_t)(frac_of(s->wind_gust_ms, 0.0f,
                                       (float)cfg.wind_scale_max_ms)
                               * 1000.0f));

    set_text(cards[C_HUMID].value, "%.0f", (double)s->humidity_pct);
    set_text(cards[C_HUMID].sub, "dew point %.0f%s",
             (double)U_TEMP(s->dew_point_c), U_TEMP_SUF);

    set_text(cards[C_RAIN].value, U_RAIN_FMT,
             (double)U_RAIN(s->rain_today_mm));
    if (s->precip_type == WX_PRECIP_NONE) {
        lv_label_set_text(cards[C_RAIN].sub, "currently dry");
    } else {
        char rate[32];
        snprintf(rate, sizeof(rate), "%s %s/hr", U_RAIN_FMT, U_RAIN_SUF);
        set_text(cards[C_RAIN].sub, rate,
                 (double)U_RAIN(s->rain_rate_mm_hr));
    }

    set_text(cards[C_PRESS].value, U_PRES_FMT, (double)U_PRES(s->pressure_mb));
    if (s->pressure_trend != WX_TREND_STEADY ||
        s->pressure_trend_mb_3h != 0.0f) {
        set_text(cards[C_PRESS].sub, "%s  %+.1f mb/3h",
                 wx_trend_description(s->pressure_trend),
                 (double)s->pressure_trend_mb_3h);
    } else {
        /* Says nothing rather than claiming "steady" from a cold boot. */
        lv_label_set_text(cards[C_PRESS].sub, "building history...");
    }

    set_text(cards[C_UV].value, "%.1f", (double)s->uv_index);
    set_text(cards[C_UV].sub, "%s   %.0f W/m2",
             wx_uv_description(s->uv_index),
             (double)s->solar_radiation_wm2);
}

/* HVAC state is the whole reason the indoor ring exists: colour carries the
 * information so it reads from across the room without being parsed. */
static lv_color_t hvac_colour(const wx_state_t *s)
{
    if (strcmp(s->hvac_status, "HEATING") == 0) return COL_HEATING;
    if (strcmp(s->hvac_status, "COOLING") == 0) return COL_COOLING;
    return COL_IDLE;
}

static void update_indoor(const wx_state_t *s, int64_t now)
{
    if (!s->indoor_valid) {
        lv_label_set_text(in_val, "--");
        lv_label_set_text(in_state, s->indoor_auth_failed ? "REAUTHORIZE"
                                                          : "no thermostat");
        lv_obj_set_style_text_color(in_state,
                                    s->indoor_auth_failed ? COL_ALERT
                                                          : COL_IDLE, 0);
        lv_label_set_text(in_age, "INDOOR");
        return;
    }

    lv_color_t col = hvac_colour(s);
    lv_obj_set_style_arc_color(in_arc, col, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(in_knob, col, 0);
    lv_obj_set_style_text_color(in_state, col, 0);

    set_text(in_val, "%.1f", (double)U_TEMP(s->indoor_temp_c));
    set_text(in_hum, "%.0f%% humidity", (double)s->indoor_humidity_pct);

    float f = frac_of(s->indoor_temp_c, INDOOR_MIN_C, INDOOR_MAX_C);
    lv_arc_set_value(in_arc, (int32_t)(f * 1000.0f));
    place_knob(in_knob, IN_CX, G_CY, f);

    /* Only the setpoint matching the active mode is populated, so branch on
     * the mode rather than testing a setpoint for non-zero -- an Eco or OFF
     * thermostat would otherwise read as 0 degrees. */
    if (strcmp(s->thermostat_mode, "HEAT") == 0) {
        set_text(in_setpoint, "set to %.0f%s",
                 (double)U_TEMP(s->setpoint_heat_c), U_TEMP_SUF);
    } else if (strcmp(s->thermostat_mode, "COOL") == 0) {
        set_text(in_setpoint, "set to %.0f%s",
                 (double)U_TEMP(s->setpoint_cool_c), U_TEMP_SUF);
    } else if (strcmp(s->thermostat_mode, "HEATCOOL") == 0) {
        set_text(in_setpoint, "%.0f - %.0f%s",
                 (double)U_TEMP(s->setpoint_heat_c),
                 (double)U_TEMP(s->setpoint_cool_c), U_TEMP_SUF);
    } else {
        lv_label_set_text(in_setpoint, "thermostat off");
    }

    if (s->eco_mode) {
        lv_label_set_text(in_state, "ECO");
    } else if (strcmp(s->hvac_status, "HEATING") == 0) {
        lv_label_set_text(in_state, "HEATING");
    } else if (strcmp(s->hvac_status, "COOLING") == 0) {
        lv_label_set_text(in_state, "COOLING");
    } else {
        lv_label_set_text(in_state, "IDLE");
    }

    /* Stale handling: dim the group AND say how old it is. Dimming alone hides
     * whether the reading is six minutes or six hours out of date, and this
     * feed is cloud-dependent so it fails on its own while outdoor keeps
     * running from the local UDP broadcast. */
    /* An expired or revoked token never recovers by waiting, so showing a
     * climbing age would be misleading -- say what has to happen instead. */
    if (s->indoor_auth_failed) {
        lv_obj_set_style_opa(in_cont, LV_OPA_40, 0);
        lv_label_set_text(in_age, "INDOOR   REAUTHORIZE");
        lv_obj_set_style_text_color(in_age, COL_ALERT, 0);
        return;
    }

    if (wx_indoor_is_stale(s)) {
        lv_obj_set_style_opa(in_cont, LV_OPA_40, 0);
        long mins = (long)((now - s->indoor_fetched_epoch) / 60);
        if (mins < 120) {
            set_text(in_age, "INDOOR   %ld min old", mins);
        } else {
            set_text(in_age, "INDOOR   %ld h old", mins / 60);
        }
        lv_obj_set_style_text_color(in_age, COL_UV, 0);
    } else {
        lv_obj_set_style_opa(in_cont, LV_OPA_COVER, 0);
        lv_label_set_text(in_age, "INDOOR");
        lv_obj_set_style_text_color(in_age, COL_DIM, 0);
    }
}

static void update_forecast(const wx_state_t *s)
{
    if (!s->forecast_valid) {
        return;
    }
    char buf[16];

    for (int i = 0; i < FC_COLS; i++) {
        if (i >= s->forecast_days) {
            lv_label_set_text(fc[i].day, "");
            if (fc[i].icon) lv_obj_add_flag(fc[i].icon, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(fc[i].hi, "");
            lv_label_set_text(fc[i].lo, "");
            lv_label_set_text(fc[i].pop, "");
            continue;
        }
        const wx_forecast_day_t *d = &s->forecast[i];

        if (i == 0) {
            lv_label_set_text(fc[i].day, "TODAY");
            lv_obj_set_style_text_color(fc[i].day, COL_WIND, 0);
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
        }

        wx_icon_set(fc[i].icon, d->icon);

        set_text(fc[i].hi, "%.0f", (double)U_TEMP(d->air_temp_high_c));
        set_text(fc[i].lo, "%.0f", (double)U_TEMP(d->air_temp_low_c));

        if (d->precip_probability > 0) {
            lv_obj_set_style_text_color(fc[i].pop,
                                        wx_icon_is_wet(d->icon) ? COL_RAIN
                                                                : COL_DIM, 0);
            set_text(fc[i].pop, "%d%%", d->precip_probability);
        } else {
            lv_label_set_text(fc[i].pop, "");
        }
    }
}

void ui_tick(void)
{
    int64_t now = (int64_t)time(NULL);
    apply_brightness(now);

    /* Repainting the hidden main screen every second while the user is in
     * settings is pure waste. */
    if (settings_is_visible()) {
        settings_tick();
        return;
    }
    if (graphs_is_visible()) {
        graphs_tick();
        return;
    }

    wx_state_t s;
    wx_snapshot(&s);

    update_header(&s, now);
    update_banner(&s, now);
    update_outdoor(&s);
    update_indoor(&s, now);
    update_forecast(&s);
}
