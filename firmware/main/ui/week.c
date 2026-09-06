#include "week.h"
#include "ui.h"
#include "wx_state.h"
#include "config.h"
#include "nws_alerts.h"
#include "weather_icons_data.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "week";

#define COL_BG      lv_color_hex(0x05080D)
#define COL_CARD    lv_color_hex(0x0C1118)
#define COL_TODAY   lv_color_hex(0x151C28)
#define COL_BORDER  lv_color_hex(0x243044)
#define COL_TEXT    lv_color_hex(0xF8FAFC)
#define COL_DIM     lv_color_hex(0x94A3B8)
#define COL_FAINT   lv_color_hex(0x475569)
#define COL_CYAN    lv_color_hex(0x00E5FF)
#define COL_HOT     lv_color_hex(0xFF5722)
#define COL_COLD    lv_color_hex(0x00B0FF)
#define COL_RAIN    lv_color_hex(0x38BDF8)
#define COL_AMBER   lv_color_hex(0xFBBF24)
#define COL_ALERT   lv_color_hex(0xF87171)
#define COL_TRACK   lv_color_hex(0x18202C)

#define SCR_W       1024
#define SCR_H       600
#define PAD         10
#define HEADER_H    50
#define ROWS        WX_FORECAST_DAYS
#define ROW_H       64
#define ROW_GAP     4

typedef struct {
    lv_obj_t *card;
    lv_obj_t *rail;
    lv_obj_t *day;
    lv_obj_t *date;
    lv_obj_t *icon;
    lv_obj_t *cond;
    lv_obj_t *now;
    lv_obj_t *hi;
    lv_obj_t *lo;
    lv_obj_t *pop;
    lv_obj_t *bar_track;
    lv_obj_t *bar_fill;
} day_row_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_clock;
static lv_obj_t *s_subtitle;
static day_row_t s_rows[ROWS];
static int       s_week_drawn_min = -1;
static int64_t   s_week_drawn_fc = -1;
static int       s_week_drawn_units = -1;
static int       s_week_drawn_stale = -1;
static bool      s_week_force;

static void set_text(lv_obj_t *lbl, const char *fmt, ...)
{
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ui_label_set(lbl, buf);
}

static lv_obj_t *bare(lv_obj_t *parent, int x, int y, int w, int h,
                     lv_color_t col, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_min_width(o, 0, 0);
    lv_obj_set_style_min_height(o, 0, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, col, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void build_row(int i)
{
    int y = HEADER_H + i * (ROW_H + ROW_GAP);
    int w = SCR_W - 2 * PAD;
    day_row_t *r = &s_rows[i];

    r->card = lv_obj_create(s_screen);
    lv_obj_set_pos(r->card, PAD, y);
    lv_obj_set_size(r->card, w, ROW_H);
    lv_obj_set_style_bg_color(r->card, (i == 0) ? COL_TODAY : COL_CARD, 0);
    lv_obj_set_style_bg_opa(r->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(r->card, COL_BORDER, 0);
    lv_obj_set_style_border_width(r->card, 1, 0);
    lv_obj_set_style_radius(r->card, 10, 0);
    lv_obj_set_style_pad_all(r->card, 8, 0);
    lv_obj_clear_flag(r->card, LV_OBJ_FLAG_SCROLLABLE);

    r->rail = bare(r->card, -8, -8, 3, ROW_H, (i == 0) ? COL_CYAN : COL_FAINT, LV_OPA_COVER);

    r->day = lv_label_create(r->card);
    lv_obj_set_style_text_font(r->day, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(r->day, (i == 0) ? COL_CYAN : COL_TEXT, 0);
    lv_label_set_text(r->day, i == 0 ? "TODAY" : "--");
    lv_obj_set_pos(r->day, 10, 4);

    r->date = lv_label_create(r->card);
    lv_obj_set_style_text_font(r->date, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r->date, COL_DIM, 0);
    lv_label_set_text(r->date, "");
    lv_obj_set_pos(r->date, 10, 30);

    r->icon = lv_image_create(r->card);
    lv_obj_remove_style_all(r->icon);
    lv_obj_set_size(r->icon, 40, 40);
    lv_obj_set_pos(r->icon, 118, 8);
    lv_obj_set_style_bg_opa(r->icon, LV_OPA_TRANSP, 0);

    r->cond = lv_label_create(r->card);
    lv_obj_set_style_text_font(r->cond, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(r->cond, COL_TEXT, 0);
    lv_label_set_text(r->cond, "waiting for forecast");
    lv_obj_set_pos(r->cond, 168, i == 0 ? 2 : 16);
    lv_obj_set_size(r->cond, 300, 24);
    lv_label_set_long_mode(r->cond, LV_LABEL_LONG_DOT);

    r->now = lv_label_create(r->card);
    lv_obj_set_style_text_font(r->now, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(r->now, COL_CYAN, 0);
    lv_label_set_text(r->now, "");
    /* Today uses a second line below conditions, not the low/high columns. */
    lv_obj_set_pos(r->now, 168, 29);
    lv_obj_set_size(r->now, 300, 18);
    lv_label_set_long_mode(r->now, LV_LABEL_LONG_DOT);

    r->lo = lv_label_create(r->card);
    lv_obj_set_style_text_font(r->lo, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(r->lo, COL_COLD, 0);
    lv_label_set_text(r->lo, "--");
    lv_obj_set_pos(r->lo, 530, 16);
    lv_obj_set_size(r->lo, 72, 26);
    lv_label_set_long_mode(r->lo, LV_LABEL_LONG_DOT);

    r->hi = lv_label_create(r->card);
    lv_obj_set_style_text_font(r->hi, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(r->hi, COL_HOT, 0);
    lv_label_set_text(r->hi, "--");
    lv_obj_set_pos(r->hi, 610, 16);
    lv_obj_set_size(r->hi, 86, 26);
    lv_label_set_long_mode(r->hi, LV_LABEL_LONG_DOT);

    r->pop = lv_label_create(r->card);
    lv_obj_set_style_text_font(r->pop, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(r->pop, COL_RAIN, 0);
    lv_label_set_text(r->pop, "");
    lv_obj_set_pos(r->pop, 720, 6);
    lv_obj_set_width(r->pop, 240);

    r->bar_track = bare(r->card, 720, 40, 240, 6, COL_TRACK, LV_OPA_COVER);
    lv_obj_set_style_radius(r->bar_track, 3, 0);
    r->bar_fill = bare(r->bar_track, 0, 0, 4, 6, COL_RAIN, LV_OPA_COVER);
    lv_obj_set_style_radius(r->bar_fill, 3, 0);
}

esp_err_t week_init(void)
{
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
    lv_label_set_text(title, "Week");
    lv_obj_set_pos(title, 16, 8);

    s_subtitle = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_subtitle, COL_DIM, 0);
    lv_label_set_text(s_subtitle, "7-day outlook");
    lv_obj_set_pos(s_subtitle, 100, 16);
    lv_obj_set_size(s_subtitle, 600, 18);
    lv_label_set_long_mode(s_subtitle, LV_LABEL_LONG_SCROLL_CIRCULAR);

    s_clock = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_clock, COL_TEXT, 0);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -168, 10);

    ui_create_page_dots(s_screen, 16, 36, UI_PAGE_WEEK);
    ui_create_page_button(s_screen, LV_ALIGN_TOP_RIGHT, -68, 8, UI_PAGE_WEEK);
    ui_create_gear_button(s_screen, LV_ALIGN_TOP_RIGHT, -14, 8);
    ui_attach_swipe_nav(s_screen);

    for (int i = 0; i < ROWS; i++) {
        build_row(i);
    }

    ESP_LOGI(TAG, "week page built");
    return ESP_OK;
}

void week_show(void)
{
    if (!s_screen) {
        return;
    }
    s_week_force = true;
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_screen);
    lv_obj_invalidate(s_screen);
    week_tick();
}

void week_hide(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}

bool week_is_visible(void)
{
    return s_screen && !lv_obj_has_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}

void week_tick(void)
{
    if (!week_is_visible()) {
        return;
    }

    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
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
        lv_obj_set_style_text_color(s_subtitle, COL_ALERT, 0);
    } else {
        nws_forecast_t nws = {0};
        if (nws_forecast_get(&nws) && nws.short_fc[0]) {
            char sub[128];
            snprintf(sub, sizeof(sub), "NWS %.20s  %.80s",
                     nws.period[0] ? nws.period : "", nws.short_fc);
            ui_label_set(s_subtitle, sub);
            lv_obj_set_style_text_color(s_subtitle, COL_DIM, 0);
        }
    }

    wx_state_t s;
    wx_snapshot(&s);

    int min_key = lt.tm_hour * 60 + lt.tm_min;
    int units_key = (int)(strcmp(cfg_temp_suffix(), "C") == 0);
    int stale_key = wx_forecast_is_stale(&s) ? 1 : 0;
    bool rows = s_week_force ||
                min_key != s_week_drawn_min ||
                s.forecast_fetched_epoch != s_week_drawn_fc ||
                units_key != s_week_drawn_units ||
                stale_key != s_week_drawn_stale;
    if (!rows) {
        return;
    }
    s_week_force = false;
    s_week_drawn_min = min_key;
    s_week_drawn_fc = s.forecast_fetched_epoch;
    s_week_drawn_units = units_key;
    s_week_drawn_stale = stale_key;

    float week_lo = 1e9f, week_hi = -1e9f;
    int wet = 0;
    char day_names[ROWS][8];
    float day_hi[ROWS];
    int day_pop[ROWS];
    int ndays = 0;
    bool stale = wx_forecast_is_stale(&s);

    for (int i = 0; i < ROWS; i++) {
        day_row_t *r = &s_rows[i];
        time_t dt = now + (time_t)i * 86400;
        if (i < s.forecast_days && s.forecast[i].day_start_local > 1600000000LL) {
            dt = (time_t)s.forecast[i].day_start_local;
        }
        struct tm tm_d;
        localtime_r(&dt, &tm_d);

        char day[8];
        strftime(day, sizeof(day), "%a", &tm_d);
        for (char *c = day; *c; c++) {
            if (*c >= 'a' && *c <= 'z') {
                *c = (char)(*c - 32);
            }
        }

        if (i == 0) {
            ui_label_set(r->day, "TODAY");
            lv_obj_set_style_text_color(r->day, COL_CYAN, 0);
            lv_obj_set_style_bg_color(r->rail, COL_CYAN, 0);
        } else {
            ui_label_set(r->day, day);
            lv_obj_set_style_text_color(r->day, COL_TEXT, 0);
            lv_obj_set_style_bg_color(r->rail, COL_FAINT, 0);
        }
        char date_s[12];
        strftime(date_s, sizeof(date_s), "%b %d", &tm_d);
        ui_label_set(r->date, date_s);

        lv_obj_set_style_opa(r->card, stale ? LV_OPA_40 : LV_OPA_COVER, 0);

        if (i >= s.forecast_days) {
            ui_label_set(r->cond, "waiting for forecast");
            ui_label_set(r->hi, "--");
            ui_label_set(r->lo, "--");
            ui_label_set(r->pop, "");
            ui_label_set(r->now, "");
            if (r->icon) {
                lv_obj_add_flag(r->icon, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_set_width(r->bar_fill, 4);
            continue;
        }

        const wx_forecast_day_t *d = &s.forecast[i];
        const char *cond = d->conditions[0] ? d->conditions : "Forecast";
        ui_label_set(r->cond, cond);

        const lv_image_dsc_t *dsc = wx_icon_get_image_dsc(
            d->icon[0] ? d->icon : "clear-day");
        if (r->icon && dsc) {
            lv_image_set_src(r->icon, dsc);
            lv_obj_clear_flag(r->icon, LV_OBJ_FLAG_HIDDEN);
        } else if (r->icon) {
            lv_obj_add_flag(r->icon, LV_OBJ_FLAG_HIDDEN);
        }

        float hi_c = d->air_temp_high_c;
        float lo_c = d->air_temp_low_c;
        if (i == 0 && s.daily_valid) {
            if (s.temp_high_today_c > hi_c) {
                hi_c = s.temp_high_today_c;
            }
            if (s.temp_low_today_c < lo_c) {
                lo_c = s.temp_low_today_c;
            }
        }
        if (lo_c < week_lo) {
            week_lo = lo_c;
        }
        if (hi_c > week_hi) {
            week_hi = hi_c;
        }
        set_text(r->lo, "%.0f%s", (double)cfg_temp(lo_c), cfg_temp_suffix());
        set_text(r->hi, "%.0f%s", (double)cfg_temp(hi_c), cfg_temp_suffix());

        if (i == 0 && s.obs_valid) {
            set_text(r->now, "now %.0f%s",
                     (double)cfg_temp(s.air_temp_c), cfg_temp_suffix());
        } else {
            ui_label_set(r->now, "");
        }

        int pop = d->precip_probability;
        snprintf(day_names[ndays], sizeof(day_names[0]), "%.*s", (int)sizeof(day_names[0]) - 1, (i == 0) ? "TODAY" : day);
        day_names[ndays][sizeof(day_names[0]) - 1] = '\0';
        day_hi[ndays] = hi_c;
        day_pop[ndays] = pop;
        ndays++;
        if (pop > 0) {
            set_text(r->pop, "Rain %d%%", pop);
            lv_obj_set_style_text_color(r->pop, pop >= 50 ? COL_AMBER : COL_RAIN, 0);
        } else {
            ui_label_set(r->pop, "Dry");
            lv_obj_set_style_text_color(r->pop, COL_DIM, 0);
        }
        if (pop >= 40) {
            wet++;
        }
        int bw = (240 * pop) / 100;
        if (bw < 4) {
            bw = 4;
        }
        lv_obj_set_width(r->bar_fill, bw);
        lv_obj_set_style_bg_color(r->bar_fill, pop >= 50 ? COL_AMBER : COL_RAIN, 0);
    }

    if (!have_alert) {
        int wettest = -1, wet_pop = -1;
        int hottest = -1;
        float hottest_hi = -1e9f;
        for (int i = 0; i < ndays; i++) {
            if (day_pop[i] > wet_pop) {
                wet_pop = day_pop[i];
                wettest = i;
            }
            if (day_hi[i] > hottest_hi) {
                hottest_hi = day_hi[i];
                hottest = i;
            }
        }
        int nicest = hottest;
        if (nicest >= 0 && nicest == wettest && ndays > 1) {
            nicest = -1;
            float second = -1e9f;
            for (int i = 0; i < ndays; i++) {
                if (i == wettest) {
                    continue;
                }
                if (day_hi[i] > second) {
                    second = day_hi[i];
                    nicest = i;
                }
            }
        }

        nws_forecast_t nws = {0};
        bool nws_ok = nws_forecast_get(&nws) && nws.short_fc[0];
        if (week_hi > week_lo && week_hi < 1e8f) {
            char sum[128];
            const char *nice = (nicest >= 0) ? day_names[nicest] : NULL;
            if (nws_ok) {
                snprintf(sum, sizeof(sum), "NWS %.48s  /  Hi %.0f  Lo %.0f",
                         nws.short_fc,
                         (double)cfg_temp(week_hi), (double)cfg_temp(week_lo));
            } else if (wet > 0 && nice) {
                snprintf(sum, sizeof(sum),
                         "High %.0f  Low %.0f  /  %d wet  /  nicest %s",
                         (double)cfg_temp(week_hi), (double)cfg_temp(week_lo),
                         wet, nice);
            } else if (wet > 0) {
                snprintf(sum, sizeof(sum), "High %.0f  Low %.0f  /  %d wet",
                         (double)cfg_temp(week_hi), (double)cfg_temp(week_lo),
                         wet);
            } else if (nice) {
                snprintf(sum, sizeof(sum),
                         "High %.0f  Low %.0f  /  dry week  /  nicest %s",
                         (double)cfg_temp(week_hi), (double)cfg_temp(week_lo),
                         nice);
            } else {
                snprintf(sum, sizeof(sum), "High %.0f  Low %.0f  /  dry week",
                         (double)cfg_temp(week_hi), (double)cfg_temp(week_lo));
            }
            ui_label_set(s_subtitle, sum);
        } else if (nws_ok) {
            ui_label_setf(s_subtitle, "NWS %s", nws.short_fc);
        } else {
            ui_label_set(s_subtitle, "7-day outlook");
        }
        lv_obj_set_style_text_color(s_subtitle, COL_DIM, 0);
    }
}
