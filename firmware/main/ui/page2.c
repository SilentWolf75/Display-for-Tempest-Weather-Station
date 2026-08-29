#include "page2.h"
#include "ui.h"
#include "wx_state.h"
#include "wx_astronomy.h"
#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "page2";

#define COL_BG          lv_color_hex(0x06090E)
#define COL_CARD        lv_color_hex(0x0A0F1D)
#define COL_BORDER      lv_color_hex(0x1B243B)
#define COL_TEXT        lv_color_hex(0xF8FAFC)
#define COL_DIM         lv_color_hex(0x94A3B8)
#define COL_FAINT       lv_color_hex(0x475569)

/* Night Theme / Celestial Palette */
#define COL_MOON_ACCENT lv_color_hex(0x818CF8) /* Twilight Indigo Top Glow */
#define COL_MOON_TITLE  lv_color_hex(0xC7D2FE) /* Ethereal Moonlight Silver */
#define COL_MOON_ARC    lv_color_hex(0x93C5FD) /* Soft Moonlight Ice Blue */
#define COL_MOON_MARKER lv_color_hex(0xF8FAFC) /* Bright White Moon Marker */
#define COL_MOON_AURA   lv_color_hex(0x6366F1) /* Soft Indigo Celestial Glow */
#define COL_MOON_PCT    lv_color_hex(0xA5B4FC) /* Soft Moonlight text */

#define COL_RAIN        lv_color_hex(0x38BDF8)
#define COL_AMBER       lv_color_hex(0xFBBF24)
#define COL_RED         lv_color_hex(0xF87171)
#define COL_GOLD_SOFT   lv_color_hex(0xFFAB40)

#define SCR_W           1024
#define SCR_H           600
#define PAD             10

#define HEADER_H        50
#define MOON_PANEL_Y    HEADER_H
#define MOON_PANEL_H    228
#define HOURLY_PANEL_Y  (MOON_PANEL_Y + MOON_PANEL_H + 8)
#define HOURLY_PANEL_H  (SCR_H - HOURLY_PANEL_Y - PAD)

#define ARC_X0          28
#define ARC_W           500
#define ARC_Y_BASE      142
#define ARC_H           58
#define MOON_MAX_ALT    65.0f

#define SIDEBAR_X       560
#define MOON_ICON_SIZE  88
#define MOON_SYNODIC    29.530588853f

static lv_color32_t s_moon_buf[MOON_ICON_SIZE * MOON_ICON_SIZE];
static float s_moon_drawn_age = -1.0f;

static lv_obj_t *s_screen;
static lv_obj_t *s_prev_screen;
static lv_obj_t *s_standby_overlay;
static lv_obj_t *s_standby_clock;
static lv_obj_t *s_subtitle;

static lv_obj_t *s_moon_title;
static lv_obj_t *s_moon_alt_lbl;
static lv_obj_t *s_moon_phase_lbl;
static lv_obj_t *s_moon_pct_lbl;
static lv_obj_t *s_moon_rise_lbl;
static lv_obj_t *s_moon_set_lbl;
static lv_obj_t *s_moon_canvas;
static lv_obj_t *s_moon_arc_line;
static lv_point_precise_t s_moon_arc_pts[25];
static lv_obj_t *s_peak_line;
static lv_point_precise_t s_peak_pts[2];
static lv_obj_t *s_arc_rise_lbl;
static lv_obj_t *s_arc_peak_lbl;
static lv_obj_t *s_arc_set_lbl;
static lv_obj_t *s_moon_marker_glow;
static lv_obj_t *s_moon_marker;
static lv_obj_t *s_moon_now_lbl;

static lv_obj_t *s_hourly_chart;
static lv_chart_series_t *s_hourly_pop;
static lv_chart_series_t *s_hourly_temp;
static lv_obj_t *s_hourly_note;

static void set_text(lv_obj_t *lbl, const char *fmt, ...)
{
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lv_label_set_text(lbl, buf);
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t glow)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, COL_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(c);
    lv_obj_set_pos(accent, 0, 0);
    lv_obj_set_size(accent, w, 2);
    lv_obj_set_style_bg_color(accent, glow, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *section_title(lv_obj_t *parent, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    return l;
}

/* Photorealistic 3D spherical lunar shader with Lambertian + Lommel-Seeliger scattering,
 * authentic lunar maria, Tycho crater rays, and soft terminator penumbra. */
static void moon_phase_render(int64_t epoch)
{
    wx_moon_info_t mi;
    wx_moon_compute(epoch, &mi);

    if (s_moon_drawn_age >= 0.0f &&
        fabsf(mi.age_days - s_moon_drawn_age) < 0.05f) {
        return;
    }
    s_moon_drawn_age = mi.age_days;

    const int size = MOON_ICON_SIZE;
    const float cx = (size - 1) * 0.5f;
    const float cy = cx;
    const float R  = size * 0.44f;
    const float phi = (mi.age_days / MOON_SYNODIC) * 2.0f * (float)M_PI;

    /* Sun light vector: Waxing lit from right (+x), Full lit from front (+z), Waning from left (-x) */
    const float Lx = sinf(phi);
    const float Ly = 0.0f;
    const float Lz = -cosf(phi);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = ((float)x - cx) / R;
            float dy = ((float)y - cy) / R;
            float r = sqrtf(dx * dx + dy * dy);

            if (r > 1.04f) {
                s_moon_buf[y * size + x] = (lv_color32_t){ .red = 0, .green = 0, .blue = 0, .alpha = 0 };
                continue;
            }

            uint8_t alpha = 255;
            if (r > 0.98f) {
                alpha = (uint8_t)(255.0f * (1.04f - r) / 0.06f);
                if (alpha == 0) {
                    s_moon_buf[y * size + x] = (lv_color32_t){ .red = 0, .green = 0, .blue = 0, .alpha = 0 };
                    continue;
                }
            }

            float nz = sqrtf(fmaxf(0.0f, 1.0f - fminf(1.0f, r * r)));
            float nx = dx;
            float ny = dy;

            float lon = atan2f(nx, nz);
            float lat = asinf(fmaxf(-1.0f, fminf(1.0f, -ny)));

            /* Realistic Lunar Maria */
            float d_imb   = (lon - (-0.30f))*(lon - (-0.30f)) + (lat - 0.45f)*(lat - 0.45f);
            float d_proc  = (lon - (-0.55f))*(lon - (-0.55f)) + (lat - 0.15f)*(lat - 0.15f);
            float d_ser   = (lon - 0.28f)*(lon - 0.28f) + (lat - 0.42f)*(lat - 0.42f);
            float d_tranq = (lon - 0.42f)*(lon - 0.42f) + (lat - 0.12f)*(lat - 0.12f);
            float d_cris  = (lon - 0.78f)*(lon - 0.78f) + (lat - 0.28f)*(lat - 0.28f);
            float d_nub   = (lon - (-0.28f))*(lon - (-0.28f)) + (lat - (-0.38f))*(lat - (-0.38f));
            float d_hum   = (lon - (-0.58f))*(lon - (-0.58f)) + (lat - (-0.42f))*(lat - (-0.42f));
            float d_fec   = (lon - 0.58f)*(lon - 0.58f) + (lat - (-0.08f))*(lat - (-0.08f));

            float mare = expf(-d_imb / 0.08f) * 0.38f +
                         expf(-d_proc / 0.12f) * 0.42f +
                         expf(-d_ser / 0.05f) * 0.36f +
                         expf(-d_tranq / 0.07f) * 0.38f +
                         expf(-d_cris / 0.015f) * 0.42f +
                         expf(-d_nub / 0.06f) * 0.32f +
                         expf(-d_hum / 0.035f) * 0.30f +
                         expf(-d_fec / 0.05f) * 0.32f;
            if (mare > 0.55f) mare = 0.55f;

            /* Tycho & Copernicus craters */
            float d_tycho = (lon - (-0.12f))*(lon - (-0.12f)) + (lat - (-0.65f))*(lat - (-0.65f));
            float r_tycho = sqrtf(d_tycho);
            float crater_tycho = expf(-(r_tycho - 0.045f)*(r_tycho - 0.045f) / 0.0008f) * 0.25f;
            float angle_tycho = atan2f(lat - (-0.65f), lon - (-0.12f));
            float s_ray = sinf(angle_tycho * 7.0f) * 0.5f + 0.5f;
            float rays_tycho = (s_ray * s_ray * s_ray) * expf(-r_tycho / 0.9f) * 0.28f;

            float d_cop = (lon - (-0.30f))*(lon - (-0.30f)) + (lat - 0.15f)*(lat - 0.15f);
            float crater_cop = expf(-(sqrtf(d_cop) - 0.035f)*(sqrtf(d_cop) - 0.035f) / 0.0008f) * 0.20f;

            float tex = sinf(lon * 7.0f + lat * 5.0f) * 0.035f +
                        sinf(lon * 13.0f - lat * 11.0f) * 0.022f +
                        sinf(lon * 21.0f + lat * 17.0f) * 0.015f;

            float albedo = 0.96f - mare + crater_tycho + rays_tycho + crater_cop + tex;
            if (albedo < 0.45f) albedo = 0.45f;
            if (albedo > 1.22f) albedo = 1.22f;

            /* Shading */
            float ndotl = nx * Lx + ny * Ly + nz * Lz;
            float t_val = ndotl + (albedo - 0.8f) * 0.04f;

            /* Lommel-Seeliger scattering */
            float ls_lit = 0.0f;
            if (t_val > 0.0f) {
                ls_lit = (t_val / (t_val + nz + 0.001f)) * 1.5f + t_val * 0.3f;
                if (ls_lit > 1.0f) ls_lit = 1.0f;
            }

            /* Penumbra smoothstep */
            float lit_factor = 0.0f;
            if (t_val > 0.04f) {
                lit_factor = 1.0f;
            } else if (t_val > -0.04f) {
                lit_factor = (t_val - (-0.04f)) / 0.08f;
                lit_factor = lit_factor * lit_factor * (3.0f - 2.0f * lit_factor);
            }

            /* Lit colors (Silvery moonlight rock) */
            float lit_r = fminf(255.0f, 240.0f * albedo * ls_lit);
            float lit_g = fminf(255.0f, 244.0f * albedo * ls_lit);
            float lit_b = fminf(255.0f, 252.0f * albedo * ls_lit);

            /* Earthshine / Dark side */
            float dark_base = 0.12f + 0.03f * albedo;
            float dark_r = 22.0f * dark_base * 4.0f;
            float dark_g = 28.0f * dark_base * 4.0f;
            float dark_b = 45.0f * dark_base * 4.0f;

            s_moon_buf[y * size + x] = (lv_color32_t){
                .red   = (uint8_t)(lit_r * lit_factor + dark_r * (1.0f - lit_factor)),
                .green = (uint8_t)(lit_g * lit_factor + dark_g * (1.0f - lit_factor)),
                .blue  = (uint8_t)(lit_b * lit_factor + dark_b * (1.0f - lit_factor)),
                .alpha = alpha
            };
        }
    }

    if (s_moon_canvas) {
        lv_obj_invalidate(s_moon_canvas);
    }
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

static void get_active_moon_transit(int64_t now, int64_t rise, int64_t set,
                                   int64_t *out_rise, int64_t *out_set)
{
    if (rise <= 0 || set <= 0) {
        *out_rise = rise;
        *out_set = set;
        return;
    }

    if (set < rise) {
        /* Moonset is in early morning, Moonrise is in the evening */
        if (now <= set) {
            /* Early morning: transit rose yesterday evening and sets this morning */
            *out_rise = set - 45000; /* ~12.5 h transit duration */
            *out_set  = set;
        } else {
            /* Daytime / Night: transit rises tonight and sets tomorrow morning */
            *out_rise = rise;
            *out_set  = rise + 45000;
        }
    } else {
        /* Standard same-day rise < set */
        *out_rise = rise;
        *out_set  = set;
    }
}

static float moon_path_fraction(int64_t epoch, int64_t rise, int64_t set)
{
    int64_t ar = 0, as = 0;
    get_active_moon_transit(epoch, rise, set, &ar, &as);
    if (ar <= 0 || as <= 0) {
        return 0.5f;
    }
    int64_t span = as - ar;
    if (span <= 0) {
        return 0.5f;
    }
    if (epoch < ar) {
        return -1.0f;
    }
    if (epoch > as) {
        return 2.0f;
    }
    return (float)(epoch - ar) / (float)span;
}

static float moon_altitude_deg(int64_t epoch, int64_t rise, int64_t set)
{
    float f = moon_path_fraction(epoch, rise, set);
    if (f < 0.0f || f > 1.0f) {
        return 0.0f;
    }
    return sinf(f * (float)M_PI) * MOON_MAX_ALT;
}

static void arc_point_at(float f, int *x, int *y)
{
    if (f < 0.0f) {
        f = 0.0f;
    }
    if (f > 1.0f) {
        f = 1.0f;
    }
    *x = ARC_X0 + (int)(f * ARC_W);
    *y = ARC_Y_BASE - (int)(sinf(f * (float)M_PI) * ARC_H);
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
    lv_obj_set_style_text_color(s_standby_clock, COL_GOLD_SOFT, 0);
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
    lv_obj_t *card = make_card(s_screen, PAD, MOON_PANEL_Y, SCR_W - 2 * PAD, MOON_PANEL_H, COL_MOON_ACCENT);

    s_moon_title = section_title(card, COL_MOON_TITLE, "MOON PATH TONIGHT");
    lv_obj_align(s_moon_title, LV_ALIGN_TOP_LEFT, 0, 2);

    for (int i = 0; i <= 24; i++) {
        float f = (float)i / 24.0f;
        int x, y;
        arc_point_at(f, &x, &y);
        s_moon_arc_pts[i].x = x;
        s_moon_arc_pts[i].y = y;
    }
    s_moon_arc_line = lv_line_create(card);
    lv_line_set_points(s_moon_arc_line, s_moon_arc_pts, 25);
    lv_obj_set_style_line_width(s_moon_arc_line, 3, 0);
    lv_obj_set_style_line_color(s_moon_arc_line, COL_MOON_ARC, 0);
    lv_obj_set_style_line_opa(s_moon_arc_line, LV_OPA_80, 0);
    lv_obj_set_style_line_rounded(s_moon_arc_line, true, 0);

    int peak_x, peak_y0;
    arc_point_at(0.5f, &peak_x, &peak_y0);
    s_peak_pts[0].x = peak_x;
    s_peak_pts[0].y = peak_y0;
    s_peak_pts[1].x = peak_x;
    s_peak_pts[1].y = ARC_Y_BASE + 14;
    s_peak_line = lv_line_create(card);
    lv_line_set_points(s_peak_line, s_peak_pts, 2);
    lv_obj_set_style_line_width(s_peak_line, 1, 0);
    lv_obj_set_style_line_color(s_peak_line, COL_FAINT, 0);
    lv_obj_set_style_line_opa(s_peak_line, LV_OPA_50, 0);
    lv_obj_set_style_line_dash_width(s_peak_line, 4, 0);
    lv_obj_set_style_line_dash_gap(s_peak_line, 4, 0);

    s_arc_rise_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_arc_rise_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_arc_rise_lbl, COL_DIM, 0);
    lv_label_set_text(s_arc_rise_lbl, "--:--");
    lv_obj_set_pos(s_arc_rise_lbl, ARC_X0 - 4, ARC_Y_BASE + 4);

    s_arc_peak_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_arc_peak_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_arc_peak_lbl, COL_DIM, 0);
    lv_label_set_text(s_arc_peak_lbl, "--:--");
    lv_obj_set_pos(s_arc_peak_lbl, peak_x - 28, ARC_Y_BASE + 4);

    s_arc_set_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_arc_set_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_arc_set_lbl, COL_DIM, 0);
    lv_label_set_text(s_arc_set_lbl, "--:--");
    lv_obj_set_pos(s_arc_set_lbl, ARC_X0 + ARC_W - 48, ARC_Y_BASE + 4);

    s_moon_marker_glow = lv_obj_create(card);
    lv_obj_set_size(s_moon_marker_glow, 24, 24);
    lv_obj_set_style_radius(s_moon_marker_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_moon_marker_glow, COL_MOON_AURA, 0);
    lv_obj_set_style_bg_opa(s_moon_marker_glow, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_moon_marker_glow, 0, 0);

    s_moon_marker = lv_obj_create(card);
    lv_obj_set_size(s_moon_marker, 10, 10);
    lv_obj_set_style_radius(s_moon_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_moon_marker, COL_MOON_MARKER, 0);
    lv_obj_set_style_border_width(s_moon_marker, 0, 0);
    lv_obj_set_style_shadow_color(s_moon_marker, COL_MOON_ARC, 0);
    lv_obj_set_style_shadow_width(s_moon_marker, 10, 0);
    lv_obj_set_style_shadow_opa(s_moon_marker, LV_OPA_60, 0);

    s_moon_now_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_now_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_now_lbl, COL_MOON_ARC, 0);
    lv_label_set_text(s_moon_now_lbl, "Now");

    lv_obj_t *divider = lv_obj_create(card);
    lv_obj_set_size(divider, 1, MOON_PANEL_H - 36);
    lv_obj_set_pos(divider, SIDEBAR_X - 12, 16);
    lv_obj_set_style_bg_color(divider, COL_BORDER, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    s_moon_alt_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_alt_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_alt_lbl, COL_DIM, 0);
    lv_label_set_text(s_moon_alt_lbl, "Currently --° above horizon");
    lv_obj_set_pos(s_moon_alt_lbl, SIDEBAR_X, 0);
    lv_obj_set_width(s_moon_alt_lbl, 420);
    lv_label_set_long_mode(s_moon_alt_lbl, LV_LABEL_LONG_WRAP);

    s_moon_canvas = lv_canvas_create(card);
    lv_obj_set_size(s_moon_canvas, MOON_ICON_SIZE, MOON_ICON_SIZE);
    lv_obj_set_pos(s_moon_canvas, SIDEBAR_X, 24);
    lv_obj_set_style_bg_opa(s_moon_canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_moon_canvas, 0, 0);
    lv_canvas_set_buffer(s_moon_canvas, s_moon_buf, MOON_ICON_SIZE, MOON_ICON_SIZE,
                         LV_COLOR_FORMAT_ARGB8888);
    moon_phase_render((int64_t)time(NULL));

    s_moon_phase_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_phase_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_moon_phase_lbl, COL_TEXT, 0);
    lv_label_set_text(s_moon_phase_lbl, "Full moon");
    lv_obj_set_pos(s_moon_phase_lbl, SIDEBAR_X + MOON_ICON_SIZE + 18, 26);

    s_moon_pct_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_pct_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_pct_lbl, COL_MOON_PCT, 0);
    lv_label_set_text(s_moon_pct_lbl, "99% illuminated");
    lv_obj_set_pos(s_moon_pct_lbl, SIDEBAR_X + MOON_ICON_SIZE + 18, 54);

    s_moon_rise_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_rise_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_rise_lbl, COL_DIM, 0);
    lv_label_set_text(s_moon_rise_lbl, "Moonrise  --:--");
    lv_obj_set_pos(s_moon_rise_lbl, SIDEBAR_X + MOON_ICON_SIZE + 18, 80);

    s_moon_set_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_set_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_set_lbl, COL_DIM, 0);
    lv_label_set_text(s_moon_set_lbl, "Moonset  --:--");
    lv_obj_set_pos(s_moon_set_lbl, SIDEBAR_X + MOON_ICON_SIZE + 18, 104);
}

static void build_hourly_panel(void)
{
    lv_obj_t *card = make_card(s_screen, PAD, HOURLY_PANEL_Y, SCR_W - 2 * PAD, HOURLY_PANEL_H, COL_RAIN);

    lv_obj_t *ht = section_title(card, COL_RAIN, "24-HOUR HOURLY TIMELINE");
    lv_obj_align(ht, LV_ALIGN_TOP_LEFT, 0, 2);

    const int chart_h = HOURLY_PANEL_H - 62;
    s_hourly_chart = lv_chart_create(card);
    lv_obj_set_size(s_hourly_chart, SCR_W - 2 * PAD - 24, chart_h);
    lv_obj_set_pos(s_hourly_chart, 0, 26);
    lv_chart_set_type(s_hourly_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_hourly_chart, WX_HOURLY_SLOTS);
    lv_chart_set_range(s_hourly_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_bg_opa(s_hourly_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_hourly_chart, 0, 0);
    lv_obj_set_style_pad_column(s_hourly_chart, 2, 0);
    s_hourly_pop = lv_chart_add_series(s_hourly_chart, COL_RAIN, LV_CHART_AXIS_PRIMARY_Y);
    s_hourly_temp = lv_chart_add_series(s_hourly_chart, COL_AMBER, LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_range(s_hourly_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);

    s_hourly_note = lv_label_create(card);
    lv_obj_set_style_text_font(s_hourly_note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hourly_note, COL_DIM, 0);
    lv_label_set_text(s_hourly_note, "Rain probability by hour with temperature trend");
    lv_obj_set_pos(s_hourly_note, 0, 26 + chart_h + 4);
    lv_obj_set_width(s_hourly_note, SCR_W - 2 * PAD - 24);
    lv_label_set_long_mode(s_hourly_note, LV_LABEL_LONG_DOT);
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
    lv_label_set_text(title, "Moon & hourly");
    lv_obj_set_pos(title, 16, 8);

    s_subtitle = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_subtitle, COL_DIM, 0);
    lv_label_set_text(s_subtitle, "Tonight - --");
    lv_obj_set_pos(s_subtitle, 18, 34);

    ui_create_page_button(s_screen, LV_ALIGN_TOP_RIGHT, -14, 8, UI_PAGE_INSIGHTS);
    ui_attach_swipe_nav(s_screen);

    build_moon_panel();
    build_hourly_panel();
    build_standby_overlay();

    ESP_LOGI(TAG, "moon & hourly page built");
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
            cfg.night_standby_red ? COL_RED : COL_GOLD_SOFT, 0);
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

    char date_buf[32];
    strftime(date_buf, sizeof(date_buf), "%A, %b %d", &lt);
    if (lt.tm_hour >= 18 || lt.tm_hour < 6) {
        set_text(s_subtitle, "Tonight - %s", date_buf);
    } else {
        set_text(s_subtitle, "Today - %s", date_buf);
    }

    int64_t rise = s.moonrise_epoch;
    int64_t set = s.moonset_epoch;

    float alt = wx_moon_altitude_calc(now, 38.8075f, -94.9157f);
    float f_now = wx_moon_sky_fraction(now, 38.8075f, -94.9157f);

    if (alt > 0.0f) {
        set_text(s_moon_alt_lbl, "Currently %.0f° above horizon", (double)alt);
    } else {
        set_text(s_moon_alt_lbl, "Currently below horizon");
    }

    const char *phase = s.moon_phase_name[0] ? s.moon_phase_name : "--";
    set_text(s_moon_phase_lbl, "%s moon", phase);
    set_text(s_moon_pct_lbl, "%.0f%% illuminated", (double)(s.moon_illumination * 100.0f));
    moon_phase_render(now);

    char tr[16], ts[16], tp[16];
    format_time(tr, sizeof(tr), rise);
    format_time(ts, sizeof(ts), set);
    if (rise > 0 && set > 0) {
        int64_t mid = (rise < set) ? (rise + (set - rise) / 2) : (set + 45000 / 2);
        format_time(tp, sizeof(tp), mid);
    } else {
        snprintf(tp, sizeof(tp), "--:--");
    }
    lv_label_set_text(s_arc_rise_lbl, tr);
    lv_label_set_text(s_arc_peak_lbl, tp);
    lv_label_set_text(s_arc_set_lbl, ts);
    set_text(s_moon_rise_lbl, "Moonrise  %s", tr);
    set_text(s_moon_set_lbl, "Moonset  %s", ts);

    int mx, my;
    arc_point_at(f_now, &mx, &my);
    if (alt > 0.0f) {
        lv_obj_clear_flag(s_moon_marker_glow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_moon_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_moon_now_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_moon_marker_glow, mx - 12, my - 12);
        lv_obj_set_pos(s_moon_marker, mx - 5, my - 5);
        lv_obj_set_pos(s_moon_now_lbl, mx - 14, my + 10);
    } else {
        lv_obj_add_flag(s_moon_marker_glow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_moon_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_moon_now_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    if (s.hourly_valid && s.hourly_count > 0) {
        int count = s.hourly_count;
        if (count > WX_HOURLY_SLOTS) {
            count = WX_HOURLY_SLOTS;
        }
        lv_chart_set_point_count(s_hourly_chart, count);

        float tmin = 100.0f, tmax = -100.0f;
        for (int i = 0; i < count; i++) {
            if (s.hourly[i].temp_c < tmin) {
                tmin = s.hourly[i].temp_c;
            }
            if (s.hourly[i].temp_c > tmax) {
                tmax = s.hourly[i].temp_c;
            }
        }
        float trange = tmax - tmin;
        if (trange < 4.0f) {
            trange = 4.0f;
        }

        int rain_start = -1;
        for (int i = 0; i < count; i++) {
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
            char hr[16];
            format_time(hr, sizeof(hr), s.hourly[rain_start].hour_epoch);
            snprintf(note, sizeof(note), "Rain likely around %s (%d%%)",
                     hr, s.hourly[rain_start].precip_probability);
            lv_label_set_text(s_hourly_note, note);
        } else {
            lv_label_set_text(s_hourly_note, "No significant rain in the next 24 hours");
        }
    }
}
