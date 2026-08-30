#include "page2.h"
#include "ui.h"
#include "wx_state.h"
#include "wx_astronomy.h"
#include "config.h"
#include "nws_alerts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "page2";

#define COL_BG          lv_color_hex(0x05080D)
#define COL_CARD        lv_color_hex(0x0C1118)
#define COL_BORDER      lv_color_hex(0x243044)
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
#define COL_OK          lv_color_hex(0x00E676)

#define SCR_W           1024
#define SCR_H           600
#define PAD             10

#define HEADER_H        50
#define INSIGHT_Y       HEADER_H
#define INSIGHT_H       64
#define MOON_PANEL_Y    (INSIGHT_Y + INSIGHT_H + 6)
#define MOON_PANEL_H    188
#define HOURLY_PANEL_Y  (MOON_PANEL_Y + MOON_PANEL_H + 8)
#define HOURLY_PANEL_H  (UI_CONTENT_H - HOURLY_PANEL_Y - PAD)

#define ARC_X0          16
#define ARC_W           240
#define ARC_Y_BASE      142
#define ARC_H           58
#define MOON_MAX_ALT    65.0f

/* Wide enough that a sun larger than Earth sits fully on-canvas.
 * Height stays 140 so it fits under the ORBIT title. */
#define ORBIT_W         200
#define ORBIT_H         140
#define ORBIT_X         258
#define ORBIT_Y         22
#define SIDEBAR_X       478
#define MOON_ICON_SIZE  88
#define MOON_SYNODIC    29.530588853f

static lv_color32_t s_moon_buf[MOON_ICON_SIZE * MOON_ICON_SIZE];
/* Painted after the panel is up. A shader buffer in .bss during ui_init
 * is the same class of light-blue hang as creating Lotties at boot. */
#define ORBIT_BUF_BYTES (ORBIT_W * ORBIT_H * sizeof(lv_color32_t))
static lv_color32_t *s_orbit_buf;
static float s_moon_drawn_age = -1.0f;
static int   s_orbit_last_min = -1;

static lv_obj_t *s_screen;
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

static lv_obj_t *s_orb_canvas;
static lv_obj_t *s_orb_age;

static lv_obj_t *s_hourly_card;
static lv_obj_t *s_hourly_chart;
static lv_chart_series_t *s_hourly_pop;
static lv_chart_series_t *s_hourly_temp;
static lv_obj_t *s_hourly_note;
static lv_obj_t *s_hourly_hours[8];

static lv_obj_t *s_clock;
static lv_obj_t *s_now_lbl;
static lv_obj_t *s_sun_lbl;
static lv_obj_t *s_moon_next_lbl;
static lv_obj_t *s_ins_aqi, *s_ins_uv, *s_ins_ltg, *s_ins_rain, *s_ins_in;
static int64_t   s_hourly_drawn_epoch = -1;
static int       s_hourly_drawn_hour = -1;
static int       s_hourly_drawn_units = -1;
static bool      s_page2_force;

static void set_text(lv_obj_t *lbl, const char *fmt, ...)
{
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ui_label_set(lbl, buf);
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
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    /* Hairline on the card rim, not in the text box. Theme min-size turns
     * a 2 px bar into a slab that sits on the labels. */
    lv_obj_t *accent = lv_obj_create(c);
    lv_obj_remove_style_all(accent);
    lv_obj_set_style_min_width(accent, 0, 0);
    lv_obj_set_style_min_height(accent, 0, 0);
    lv_obj_set_pos(accent, -10, -10);
    lv_obj_set_size(accent, w, 2);
    lv_obj_set_style_bg_color(accent, glow, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static lv_color_t aqi_col(int val)
{
    if (val <= 50)  return COL_OK;
    if (val <= 100) return COL_AMBER;
    if (val <= 150) return COL_GOLD_SOFT;
    return COL_RED;
}

static lv_color_t uv_col(float uv)
{
    if (uv >= 11.0f) return lv_color_hex(0x8E24AA);
    if (uv >= 8.0f)  return COL_RED;
    if (uv >= 6.0f)  return lv_color_hex(0xFB8C00);
    if (uv >= 3.0f)  return COL_AMBER;
    return COL_OK;
}

static lv_obj_t *section_title(lv_obj_t *parent, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_label_set_text(l, txt);
    return l;
}

static void build_insight_strip(void)
{
    const int gap = 8;
    const int w = (SCR_W - 2 * PAD - 4 * gap) / 5;
    const char *titles[] = { "AIR QUALITY", "UV", "LIGHTNING", "NEXT RAIN", "INDOOR" };
    lv_color_t glows[] = { COL_MOON_ARC, COL_AMBER, COL_AMBER, COL_RAIN, COL_GOLD_SOFT };
    lv_obj_t **vals[] = { &s_ins_aqi, &s_ins_uv, &s_ins_ltg, &s_ins_rain, &s_ins_in };

    for (int i = 0; i < 5; i++) {
        lv_obj_t *c = make_card(s_screen, PAD + i * (w + gap), INSIGHT_Y, w, INSIGHT_H, glows[i]);
        lv_obj_t *t = section_title(c, COL_FAINT, titles[i]);
        lv_obj_set_pos(t, 0, 2);
        *vals[i] = lv_label_create(c);
        lv_obj_set_style_text_font(*vals[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(*vals[i], COL_TEXT, 0);
        lv_label_set_text(*vals[i], "--");
        lv_obj_set_pos(*vals[i], 0, 22);
        lv_obj_set_width(*vals[i], w - 20);
        lv_label_set_long_mode(*vals[i], LV_LABEL_LONG_DOT);
    }
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
    lv_obj_set_size(s_standby_overlay, SCR_W, UI_CONTENT_H);
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
    lv_label_set_text(hint, "Night standby  -  tap page button to exit");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24);
}

/* Top-down Earth-Moon view, painted on a canvas so limbs and the terminator
 * anti-alias. Sun is to the right: new moon sits sunward, full opposite. */
static void orb_blend(int x, int y, float r, float g, float b, float a)
{
    if (!s_orbit_buf ||
        (unsigned)x >= (unsigned)ORBIT_W || (unsigned)y >= (unsigned)ORBIT_H ||
        a <= 0.0f) {
        return;
    }
    if (r < 0.0f) r = 0.0f;
    if (g < 0.0f) g = 0.0f;
    if (b < 0.0f) b = 0.0f;
    if (r > 255.0f) r = 255.0f;
    if (g > 255.0f) g = 255.0f;
    if (b > 255.0f) b = 255.0f;
    if (a > 1.0f) a = 1.0f;
    lv_color32_t *p = &s_orbit_buf[y * ORBIT_W + x];
    float oa = (float)p->alpha * (1.0f / 255.0f);
    float na = a + oa * (1.0f - a);
    if (na < 0.002f) {
        return;
    }
    float keep = oa * (1.0f - a);
    p->red   = (uint8_t)((r * a + (float)p->red   * keep) / na);
    p->green = (uint8_t)((g * a + (float)p->green * keep) / na);
    p->blue  = (uint8_t)((b * a + (float)p->blue  * keep) / na);
    p->alpha = (uint8_t)(na * 255.0f);
}

static void orb_sphere(float cx, float cy, float R,
                       float lit_r, float lit_g, float lit_b,
                       float dark_r, float dark_g, float dark_b,
                       bool earth, float here_ang)
{
    int x0 = (int)(cx - R - 3.0f);
    int y0 = (int)(cy - R - 3.0f);
    int x1 = (int)(cx + R + 3.0f);
    int y1 = (int)(cy + R + 3.0f);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x - cx) / R;
            float dy = ((float)y - cy) / R;
            float rr = sqrtf(dx * dx + dy * dy);

            /* Soft atmosphere / moon halo outside the limb. */
            if (rr > 1.0f) {
                float rim = 1.0f - (rr - 1.0f) / (earth ? 0.18f : 0.28f);
                if (rim > 0.0f) {
                    float day = fmaxf(0.0f, dx);
                    if (earth) {
                        orb_blend(x, y, 80.0f + 40.0f * day, 170.0f, 255.0f,
                                  rim * rim * (0.22f + 0.35f * day));
                    } else {
                        orb_blend(x, y, 180.0f, 190.0f, 255.0f, rim * rim * 0.28f);
                    }
                }
                continue;
            }

            float nz = sqrtf(fmaxf(0.0f, 1.0f - rr * rr));
            float ndotl = dx;   /* sun from +x */
            float lit = ndotl * 0.5f + 0.5f;
            lit = lit * lit * (3.0f - 2.0f * lit);
            float spec = 0.0f;
            if (ndotl > 0.0f) {
                spec = powf(fmaxf(0.0f, dx * 0.4f + nz * 0.9f), 18.0f) * 0.35f;
            }

            float cr = dark_r + (lit_r - dark_r) * lit;
            float cg = dark_g + (lit_g - dark_g) * lit;
            float cb = dark_b + (lit_b - dark_b) * lit;

            if (earth) {
                float land = sinf(dx * 5.2f + dy * 4.1f) * sinf(dx * 9.0f - dy * 6.5f);
                if (land > 0.18f && lit > 0.35f) {
                    float k = (land - 0.18f) * 1.4f;
                    if (k > 1.0f) k = 1.0f;
                    cr = cr * (1.0f - k) + (72.0f + 40.0f * lit) * k;
                    cg = cg * (1.0f - k) + (118.0f + 50.0f * lit) * k;
                    cb = cb * (1.0f - k) + (62.0f + 20.0f * lit) * k;
                }
                if (dy < -0.62f) {
                    float ice = (-0.62f - dy) / 0.38f;
                    if (ice > 1.0f) ice = 1.0f;
                    cr = cr * (1.0f - ice) + (230.0f + 20.0f * lit) * ice;
                    cg = cg * (1.0f - ice) + (240.0f + 12.0f * lit) * ice;
                    cb = cb * (1.0f - ice) + (255.0f) * ice;
                }
            }

            cr += spec * 255.0f;
            cg += spec * 255.0f;
            cb += spec * 255.0f;

            uint8_t alpha = 255;
            if (rr > 0.96f) {
                alpha = (uint8_t)(255.0f * (1.0f - rr) / 0.04f);
            }
            orb_blend(x, y, cr, cg, cb, (float)alpha / 255.0f);
        }
    }

    if (earth) {
        /* Kansas ~39N sits in from the pole; noon faces the sun (+x). */
        float er = R * 0.78f;
        float hx = cx + cosf(here_ang) * er;
        float hy = cy - sinf(here_ang) * er;
        for (int y = (int)hy - 3; y <= (int)hy + 3; y++) {
            for (int x = (int)hx - 3; x <= (int)hx + 3; x++) {
                float d = hypotf((float)x - hx, (float)y - hy);
                if (d < 2.2f) {
                    float a = 1.0f - d / 2.2f;
                    orb_blend(x, y, 255.0f, 210.0f, 90.0f, a);
                }
                if (d < 1.1f) {
                    orb_blend(x, y, 255.0f, 255.0f, 240.0f, 1.0f - d / 1.1f);
                }
            }
        }
    }
}

/* Schematic sun: larger than Earth (light source, not to scale). Bounded
 * box so we do not scan the whole canvas. */
static void orb_sun(float cx, float cy, float R)
{
    float corona_r = R * 2.15f;
    int x0 = (int)(cx - corona_r);
    int y0 = (int)(cy - corona_r);
    int x1 = (int)(cx + corona_r);
    int y1 = (int)(cy + corona_r);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= ORBIT_W) x1 = ORBIT_W - 1;
    if (y1 >= ORBIT_H) y1 = ORBIT_H - 1;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float rr = d / R;

            if (rr <= 1.0f) {
                /* Limb darkening: white-hot core, gold limb. */
                float mu = sqrtf(fmaxf(0.0f, 1.0f - rr * rr));
                float cr = 255.0f;
                float cg = 168.0f + 87.0f * mu;
                float cb = 48.0f + 150.0f * mu;
                float a = 1.0f;
                if (rr > 0.92f) {
                    a = (1.0f - rr) / 0.08f;
                }
                orb_blend(x, y, cr, cg, cb, a);
            } else if (rr < 2.15f) {
                float t = (rr - 1.0f) / 1.15f;
                float a = expf(-t * 3.4f) * 0.62f;
                if (a > 0.012f) {
                    orb_blend(x, y, 255.0f, 170.0f, 55.0f, a);
                }
            }
        }
    }
}

static void orbit_diagram_render(float age_days, float hour)
{
    if (!s_orbit_buf) {
        return;
    }
    memset(s_orbit_buf, 0, ORBIT_BUF_BYTES);

    /* Earth-Moon on the left, sun fully inside on the right. */
    const float cx = 56.0f;
    const float cy = (ORBIT_H - 1) * 0.5f;
    const float orbit_r = 42.0f;
    const float earth_r = 14.0f;
    const float moon_r  = 5.4f;
    const float sun_r   = 24.0f;
    const float scx     = 142.0f;

    orb_sun(scx, cy, sun_r);

    /* Orbit path: walk the ring, not every pixel. */
    const int steps = 180;
    for (int i = 0; i < steps; i++) {
        float ang = (float)i * (2.0f * (float)M_PI / (float)steps);
        float px = cx + cosf(ang) * orbit_r;
        float py = cy - sinf(ang) * orbit_r;
        for (int y = (int)py - 2; y <= (int)py + 2; y++) {
            for (int x = (int)px - 2; x <= (int)px + 2; x++) {
                float band = hypotf((float)x - px, (float)y - py);
                if (band < 1.35f) {
                    float a = 1.0f - band / 1.35f;
                    orb_blend(x, y, 120.0f, 150.0f, 210.0f, a * 0.55f);
                }
            }
        }
    }

    /* Quarter ticks. */
    const float ticks[4] = { 0.0f, (float)M_PI * 0.5f, (float)M_PI, (float)M_PI * 1.5f };
    for (int i = 0; i < 4; i++) {
        float tx = cx + cosf(ticks[i]) * orbit_r;
        float ty = cy - sinf(ticks[i]) * orbit_r;
        for (int y = (int)ty - 2; y <= (int)ty + 2; y++) {
            for (int x = (int)tx - 2; x <= (int)tx + 2; x++) {
                float d = hypotf((float)x - tx, (float)y - ty);
                if (d < 1.6f) {
                    orb_blend(x, y, 180.0f, 200.0f, 230.0f, 1.0f - d / 1.6f);
                }
            }
        }
    }

    float here_ang = (hour - 12.0f) * (float)M_PI / 12.0f;
    orb_sphere(cx, cy, earth_r,
               70.0f, 155.0f, 230.0f,
               10.0f, 22.0f, 48.0f,
               true, here_ang);

    float phi = (age_days / MOON_SYNODIC) * 2.0f * (float)M_PI;
    float mx = cx + cosf(phi) * orbit_r;
    float my = cy - sinf(phi) * orbit_r;
    orb_sphere(mx, my, moon_r,
               235.0f, 238.0f, 245.0f,
               28.0f, 32.0f, 48.0f,
               false, 0.0f);

    if (s_orb_canvas) {
        lv_obj_invalidate(s_orb_canvas);
    }
}

static void build_orbit_diagram(lv_obj_t *card)
{
    lv_obj_t *ot = section_title(card, COL_MOON_TITLE, "ORBIT");
    lv_obj_set_pos(ot, ORBIT_X + 8, 2);

    s_orb_age = lv_label_create(card);
    lv_obj_set_style_text_font(s_orb_age, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_orb_age, COL_DIM, 0);
    lv_label_set_text(s_orb_age, "");
    lv_obj_set_pos(s_orb_age, ORBIT_X + 72, 2);

    s_orbit_buf = heap_caps_malloc(ORBIT_BUF_BYTES,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_orb_canvas = lv_canvas_create(card);
    lv_obj_set_size(s_orb_canvas, ORBIT_W, ORBIT_H);
    lv_obj_set_pos(s_orb_canvas, ORBIT_X, ORBIT_Y);
    lv_obj_set_style_bg_opa(s_orb_canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_orb_canvas, 0, 0);
    if (s_orbit_buf) {
        memset(s_orbit_buf, 0, ORBIT_BUF_BYTES);
        lv_canvas_set_buffer(s_orb_canvas, s_orbit_buf, ORBIT_W, ORBIT_H,
                             LV_COLOR_FORMAT_ARGB8888);
    } else {
        ESP_LOGW(TAG, "orbit canvas buffer alloc failed");
    }
    /* First paint is on page2_tick after ui_mark_panel_visible(). */
}

static void update_orbit_diagram(int64_t now, const struct tm *lt)
{
    if (!s_orbit_buf || !s_orb_canvas) {
        return;
    }
    wx_moon_info_t mi;
    wx_moon_compute(now, &mi);
    int minute = lt->tm_hour * 60 + lt->tm_min;
    if (minute == s_orbit_last_min &&
        s_moon_drawn_age >= 0.0f &&
        fabsf(mi.age_days - s_moon_drawn_age) < 0.02f) {
        set_text(s_orb_age, "%.0f / 29", (double)mi.age_days);
        return;
    }
    s_orbit_last_min = minute;
    float hour = (float)lt->tm_hour + (float)lt->tm_min / 60.0f;
    orbit_diagram_render(mi.age_days, hour);
    set_text(s_orb_age, "%.0f / 29", (double)mi.age_days);
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
    lv_obj_remove_style_all(s_moon_marker_glow);
    lv_obj_set_size(s_moon_marker_glow, 24, 24);
    lv_obj_set_style_radius(s_moon_marker_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_moon_marker_glow, COL_MOON_AURA, 0);
    lv_obj_set_style_bg_opa(s_moon_marker_glow, LV_OPA_40, 0);
    lv_obj_clear_flag(s_moon_marker_glow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_moon_marker = lv_obj_create(card);
    lv_obj_remove_style_all(s_moon_marker);
    lv_obj_set_size(s_moon_marker, 10, 10);
    lv_obj_set_style_radius(s_moon_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_moon_marker, COL_MOON_MARKER, 0);
    lv_obj_set_style_bg_opa(s_moon_marker, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_moon_marker, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_moon_now_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_now_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_now_lbl, COL_MOON_ARC, 0);
    lv_label_set_text(s_moon_now_lbl, "Now");

    build_orbit_diagram(card);

    lv_obj_t *divider = lv_obj_create(card);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 1, MOON_PANEL_H - 36);
    lv_obj_set_pos(divider, SIDEBAR_X - 12, 16);
    lv_obj_set_style_bg_color(divider, COL_BORDER, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

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
    memset(s_moon_buf, 0, sizeof(s_moon_buf));

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

    s_sun_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_sun_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_sun_lbl, COL_GOLD_SOFT, 0);
    lv_label_set_text(s_sun_lbl, "Sun  --:-- / --:--");
    lv_obj_set_pos(s_sun_lbl, SIDEBAR_X + MOON_ICON_SIZE + 18, 128);
    lv_obj_set_width(s_sun_lbl, 280);
    lv_label_set_long_mode(s_sun_lbl, LV_LABEL_LONG_DOT);

    s_moon_next_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(s_moon_next_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_moon_next_lbl, COL_MOON_PCT, 0);
    lv_label_set_text(s_moon_next_lbl, "");
    lv_obj_set_pos(s_moon_next_lbl, SIDEBAR_X + MOON_ICON_SIZE + 18, 152);
    lv_obj_set_width(s_moon_next_lbl, 280);
    lv_label_set_long_mode(s_moon_next_lbl, LV_LABEL_LONG_DOT);
}

static void build_hourly_panel(void)
{
    lv_obj_t *card = make_card(s_screen, PAD, HOURLY_PANEL_Y, SCR_W - 2 * PAD, HOURLY_PANEL_H, COL_RAIN);
    s_hourly_card = card;

    lv_obj_t *ht = section_title(card, COL_RAIN, "24-HOUR HOURLY TIMELINE");
    lv_obj_align(ht, LV_ALIGN_TOP_LEFT, 0, 2);

    const int chart_w = SCR_W - 2 * PAD - 20;
    const int chart_h = HOURLY_PANEL_H - 96;
    s_hourly_chart = lv_chart_create(card);
    lv_obj_set_size(s_hourly_chart, chart_w, chart_h);
    lv_obj_set_pos(s_hourly_chart, 0, 24);
    lv_chart_set_type(s_hourly_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_hourly_chart, WX_HOURLY_SLOTS);
    lv_chart_set_range(s_hourly_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_bg_opa(s_hourly_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_hourly_chart, 0, 0);
    lv_obj_set_style_pad_all(s_hourly_chart, 0, 0);
    lv_obj_set_style_pad_column(s_hourly_chart, 2, 0);
    lv_obj_set_style_pad_bottom(s_hourly_chart, 4, 0);
    lv_obj_clear_flag(s_hourly_chart, LV_OBJ_FLAG_SCROLLABLE);
    s_hourly_pop = lv_chart_add_series(s_hourly_chart, COL_RAIN, LV_CHART_AXIS_PRIMARY_Y);
    s_hourly_temp = lv_chart_add_series(s_hourly_chart, COL_AMBER, LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_range(s_hourly_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);

    const int hour_y = 24 + chart_h + 8;
    const int col_w = chart_w / 8;
    for (int i = 0; i < 8; i++) {
        s_hourly_hours[i] = lv_label_create(card);
        lv_obj_set_style_text_font(s_hourly_hours[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_hourly_hours[i], COL_FAINT, 0);
        lv_obj_set_style_text_align(s_hourly_hours[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_hourly_hours[i], "--");
        lv_obj_set_pos(s_hourly_hours[i], i * col_w, hour_y);
        lv_obj_set_width(s_hourly_hours[i], col_w);
    }

    s_hourly_note = lv_label_create(card);
    lv_obj_set_style_text_font(s_hourly_note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hourly_note, COL_DIM, 0);
    lv_label_set_text(s_hourly_note, "Rain chance by hour  /  amber = temperature");
    lv_obj_set_pos(s_hourly_note, 0, hour_y + 16);
    lv_obj_set_width(s_hourly_note, chart_w);
    lv_label_set_long_mode(s_hourly_note, LV_LABEL_LONG_DOT);
}

esp_err_t page2_init(void)
{
    /* Full-screen overlay on the dashboard — never a second LVGL screen. */
    s_screen = lv_obj_create(ui_main_screen());
    lv_obj_set_size(s_screen, UI_SCR_W, UI_CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "Sky");
    lv_obj_set_pos(title, 16, 6);

    /* Date only -- Montserrat has no em-dash, so "Today -- Sunday" painted a
     * missing-glyph box between the two words. */
    s_subtitle = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_subtitle, COL_DIM, 0);
    lv_label_set_text(s_subtitle, "--");
    lv_obj_set_pos(s_subtitle, 80, 12);

    s_now_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_now_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_now_lbl, COL_MOON_TITLE, 0);
    lv_label_set_text(s_now_lbl, "");
    lv_obj_set_pos(s_now_lbl, 300, 12);
    lv_obj_set_width(s_now_lbl, 240);
    lv_label_set_long_mode(s_now_lbl, LV_LABEL_LONG_DOT);

    s_clock = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_clock, COL_TEXT, 0);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -168, 10);

    ui_create_page_dots(s_screen, 16, 36, UI_PAGE_INSIGHTS);
    ui_create_page_button(s_screen, LV_ALIGN_TOP_RIGHT, -68, 8, UI_PAGE_INSIGHTS);
    ui_create_gear_button(s_screen, LV_ALIGN_TOP_RIGHT, -14, 8);
    ui_attach_swipe_nav(s_screen);

    build_insight_strip();
    build_moon_panel();
    build_hourly_panel();
    build_standby_overlay();

    ESP_LOGI(TAG, "sky page built");
    return ESP_OK;
}

void page2_show(void)
{
    if (!s_screen) {
        return;
    }
    s_page2_force = true;
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_screen);
    lv_obj_invalidate(s_screen);
    page2_tick();
}

void page2_hide(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}

bool page2_is_visible(void)
{
    return s_screen && !lv_obj_has_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
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
        ui_label_set(s_standby_clock, clk);
        lv_obj_clear_flag(s_standby_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_standby_overlay, LV_OBJ_FLAG_HIDDEN);

    char clk[16];
    strftime(clk, sizeof(clk), "%I:%M %p", &lt);
    if (clk[0] == '0') {
        memmove(clk, clk + 1, strlen(clk));
    }
    ui_label_set(s_clock, clk);

    nws_alert_t alert = {0};
    bool have_alert = nws_alerts_get_active(&alert) && alert.event[0];
    if (have_alert) {
        char banner[96];
        nws_format_banner(&alert, banner, sizeof(banner));
        ui_label_set(s_subtitle, banner);
        lv_obj_set_style_text_color(s_subtitle, COL_RED, 0);
    } else {
        char date_buf[32];
        strftime(date_buf, sizeof(date_buf), "%A, %b %d", &lt);
        ui_label_set(s_subtitle, date_buf);
        lv_obj_set_style_text_color(s_subtitle, COL_DIM, 0);
    }

    if (s.obs_valid) {
        nws_forecast_t nws = {0};
        const char *cond = s.current_conditions[0] ? s.current_conditions : "Outdoor";
        if (nws_forecast_get(&nws) && nws.short_fc[0]) {
            set_text(s_now_lbl, "NWS %s  %.0f%s", nws.short_fc,
                     (double)cfg_temp(s.air_temp_c), cfg_temp_suffix());
        } else {
            set_text(s_now_lbl, "%s  %.0f%s", cond,
                     (double)cfg_temp(s.air_temp_c), cfg_temp_suffix());
        }
    } else {
        lv_label_set_text(s_now_lbl, "");
    }

    if (s.aqi_valid && s.aqi_val > 0) {
        if (s.aqi_pm25 > 0.0f) {
            set_text(s_ins_aqi, "%d  %s  PM2.5 %.0f",
                     s.aqi_val, wx_aqi_epa_label(s.aqi_val), (double)s.aqi_pm25);
        } else {
            set_text(s_ins_aqi, "%d  %s", s.aqi_val, wx_aqi_epa_label(s.aqi_val));
        }
        lv_obj_set_style_text_color(s_ins_aqi, aqi_col(s.aqi_val), 0);
    } else {
        lv_label_set_text(s_ins_aqi, "waiting");
        lv_obj_set_style_text_color(s_ins_aqi, COL_DIM, 0);
    }

    if (s.obs_valid) {
        set_text(s_ins_uv, "%.1f  %s", (double)s.uv_index, wx_uv_description(s.uv_index));
        lv_obj_set_style_text_color(s_ins_uv, uv_col(s.uv_index), 0);
    } else {
        lv_label_set_text(s_ins_uv, "waiting");
        lv_obj_set_style_text_color(s_ins_uv, COL_DIM, 0);
    }

    if (s.strikes_3h > 0 && s.last_strike_dist_km > 0.0f) {
        bool near = s.last_strike_dist_km < 9.656f;
        set_text(s_ins_ltg, "%d strikes  %.1f %s%s",
                 s.strikes_3h, (double)cfg_distance(s.last_strike_dist_km),
                 cfg_distance_suffix(), near ? "  NEAR" : "");
        lv_obj_set_style_text_color(s_ins_ltg, near ? COL_RED : COL_AMBER, 0);
    } else {
        lv_label_set_text(s_ins_ltg, "Clear");
        lv_obj_set_style_text_color(s_ins_ltg, COL_OK, 0);
    }

    int nslot = wx_next_precip_slot(&s, 30);
    if (nslot >= 0) {
        char when[16];
        format_time(when, sizeof(when), s.hourly[nslot].hour_epoch);
        set_text(s_ins_rain, "%s  %d%%", when, s.hourly[nslot].precip_probability);
        lv_obj_set_style_text_color(s_ins_rain,
            s.hourly[nslot].precip_probability >= 50 ? COL_AMBER : COL_RAIN, 0);
    } else if (s.hourly_valid) {
        lv_label_set_text(s_ins_rain, "None in 24h");
        lv_obj_set_style_text_color(s_ins_rain, COL_DIM, 0);
    } else {
        lv_label_set_text(s_ins_rain, "waiting");
        lv_obj_set_style_text_color(s_ins_rain, COL_DIM, 0);
    }

    if (s.indoor_valid) {
        if (wx_indoor_is_stale(&s) && now > s.indoor_fetched_epoch) {
            int mins = (int)((now - s.indoor_fetched_epoch) / 60);
            if (mins < 1) {
                mins = 1;
            }
            set_text(s_ins_in, "updated %d min ago", mins);
            lv_obj_set_style_text_color(s_ins_in, COL_AMBER, 0);
        } else {
            float d_c = s.indoor_temp_c - s.air_temp_c;
            const char *vs = "";
            if (s.obs_valid) {
                vs = (d_c > 1.0f) ? "  warmer" : (d_c < -1.0f) ? "  cooler" : "";
            }
            set_text(s_ins_in, "%.1f%s  %.0f%%%s",
                     (double)cfg_temp(s.indoor_temp_c), cfg_temp_suffix(),
                     (double)s.indoor_humidity_pct, vs);
            lv_obj_set_style_text_color(s_ins_in, COL_GOLD_SOFT, 0);
        }
    } else {
        lv_label_set_text(s_ins_in, "no sensor");
        lv_obj_set_style_text_color(s_ins_in, COL_DIM, 0);
    }

    int64_t rise = s.moonrise_epoch;
    int64_t set = s.moonset_epoch;

    float lat = s.station_loc_valid ? s.station_lat : 0.0f;
    float lon = s.station_loc_valid ? s.station_lon : 0.0f;
    float alt = wx_moon_altitude_calc(now, lat, lon);
    float f_now = wx_moon_sky_fraction(now, lat, lon);

    if (alt > 0.0f) {
        set_text(s_moon_alt_lbl, "Currently %.0f° above horizon", (double)alt);
    } else {
        set_text(s_moon_alt_lbl, "Currently below horizon");
    }

    const char *phase = s.moon_phase_name[0] ? s.moon_phase_name : "--";
    set_text(s_moon_phase_lbl, "%s moon", phase);
    set_text(s_moon_pct_lbl, "%.0f%% illuminated", (double)(s.moon_illumination * 100.0f));
    moon_phase_render(now);

    wx_moon_info_t mi;
    wx_moon_compute(now, &mi);
    float d_full = wx_moon_days_until_full(mi.age_days);
    float d_new = wx_moon_days_until_new(mi.age_days);
    if (d_full < 0.6f) {
        ui_label_set(s_moon_next_lbl, "Full moon tonight");
    } else if (d_new < 0.6f) {
        ui_label_set(s_moon_next_lbl, "New moon tonight");
    } else {
        set_text(s_moon_next_lbl, "Full in %.0fd   New in %.0fd",
                 (double)d_full, (double)d_new);
    }

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

    if (s.sunrise_epoch > 0 && s.sunset_epoch > 0) {
        char sr[16], ss[16];
        format_time(sr, sizeof(sr), s.sunrise_epoch);
        format_time(ss, sizeof(ss), s.sunset_epoch);
        set_text(s_sun_lbl, "Sun  %s / %s", sr, ss);
    } else {
        lv_label_set_text(s_sun_lbl, "Sun  --:-- / --:--");
    }

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

    update_orbit_diagram(now, &lt);

    /* Hourly bars only change when REST republishes or the clock hour
     * rolls. Refreshing 24 chart points every second was the SKY hitch. */
    int units_key = (int)cfg.units;
    bool hourly_refresh = s_page2_force ||
                          s.hourly_fetched_epoch != s_hourly_drawn_epoch ||
                          lt.tm_hour != s_hourly_drawn_hour ||
                          units_key != s_hourly_drawn_units;
    s_page2_force = false;
    if (!hourly_refresh) {
        return;
    }
    s_hourly_drawn_epoch = s.hourly_fetched_epoch;
    s_hourly_drawn_hour = lt.tm_hour;
    s_hourly_drawn_units = units_key;

    if (s_hourly_card) {
        lv_obj_set_style_opa(s_hourly_card,
                             (s.hourly_valid && s.hourly_count > 0) ? LV_OPA_COVER : LV_OPA_50, 0);
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

        for (int h = 0; h < 8; h++) {
            int idx = h * 3;
            if (idx >= count || !s_hourly_hours[h]) {
                if (s_hourly_hours[h]) {
                    lv_label_set_text(s_hourly_hours[h], "");
                }
                continue;
            }
            struct tm ht;
            time_t ht_t = (time_t)s.hourly[idx].hour_epoch;
            localtime_r(&ht_t, &ht);
            int hr12 = ht.tm_hour % 12;
            if (hr12 == 0) {
                hr12 = 12;
            }
            char hr[16];
            snprintf(hr, sizeof(hr), "%d%s  %.0f%s",
                     hr12, (ht.tm_hour < 12) ? "a" : "p",
                     (double)cfg_temp(s.hourly[idx].temp_c), cfg_temp_suffix());
            lv_label_set_text(s_hourly_hours[h], hr);
            bool now_col = (now >= s.hourly[idx].hour_epoch &&
                            now < s.hourly[idx].hour_epoch + 3 * 3600);
            lv_obj_set_style_text_color(s_hourly_hours[h],
                                        now_col ? COL_TEXT : COL_FAINT, 0);
        }

        if (rain_start >= 0) {
            char hr[16];
            format_time(hr, sizeof(hr), s.hourly[rain_start].hour_epoch);
            set_text(s_hourly_note, "Rain around %s (%d%%)   24h  %.0f / %.0f%s",
                     hr, s.hourly[rain_start].precip_probability,
                     (double)cfg_temp(tmin), (double)cfg_temp(tmax),
                     cfg_temp_suffix());
        } else {
            set_text(s_hourly_note, "No significant rain in 24h   hi %.0f  lo %.0f%s",
                     (double)cfg_temp(tmax), (double)cfg_temp(tmin),
                     cfg_temp_suffix());
        }
    } else {
        for (int h = 0; h < 8; h++) {
            if (s_hourly_hours[h]) {
                lv_label_set_text(s_hourly_hours[h], "");
            }
        }
        ui_label_set(s_hourly_note, "waiting for hourly");
    }
}
