#include "alerts.h"
#include "ui.h"
#include "wx_state.h"
#include "config.h"
#include "nws_alerts.h"
#include "net.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "alerts";

#define COL_BG      lv_color_hex(0x05080D)
#define COL_CARD    lv_color_hex(0x0C1118)
#define COL_BORDER  lv_color_hex(0x243044)
#define COL_TEXT    lv_color_hex(0xF8FAFC)
#define COL_DIM     lv_color_hex(0x94A3B8)
#define COL_FAINT   lv_color_hex(0x475569)
#define COL_AMBER   lv_color_hex(0xFBBF24)
#define COL_RED     lv_color_hex(0xF87171)
#define COL_OK      lv_color_hex(0x00E676)
#define COL_CYAN    lv_color_hex(0x00E5FF)
#define COL_RAIN    lv_color_hex(0x38BDF8)

static lv_obj_t *s_screen;
static lv_obj_t *s_clock;
static lv_obj_t *s_subtitle;
static lv_obj_t *s_event;
static lv_obj_t *s_meta;
static lv_obj_t *s_headline;
static lv_obj_t *s_instruction;
static lv_obj_t *s_ltg;
static lv_obj_t *s_rain;
static lv_obj_t *s_nws_card;

static void set_clock(lv_obj_t *lbl)
{
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    char clk[16];
    strftime(clk, sizeof(clk), "%I:%M %p", &lt);
    if (clk[0] == '0') {
        memmove(clk, clk + 1, strlen(clk));
    }
    ui_label_set(lbl, clk);
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h,
                           lv_color_t glow)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, COL_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(c);
    lv_obj_remove_style_all(accent);
    lv_obj_set_pos(accent, -16, -16);
    lv_obj_set_size(accent, w, 2);
    lv_obj_set_style_bg_color(accent, glow, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static lv_obj_t *kicker(lv_obj_t *parent, lv_color_t col, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_label_set_text(l, text);
    return l;
}

esp_err_t alerts_init(void)
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
    lv_label_set_text(title, "Alerts");
    lv_obj_set_pos(title, 16, 8);

    s_subtitle = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_subtitle, COL_DIM, 0);
    lv_label_set_text(s_subtitle, "National Weather Service");
    lv_obj_set_pos(s_subtitle, 120, 16);

    s_clock = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_clock, COL_TEXT, 0);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -168, 10);

    ui_create_page_dots(s_screen, 16, 40, UI_PAGE_ALERTS);
    ui_create_page_button(s_screen, LV_ALIGN_TOP_RIGHT, -68, 8, UI_PAGE_ALERTS);
    ui_create_gear_button(s_screen, LV_ALIGN_TOP_RIGHT, -14, 8);
    ui_attach_swipe_nav(s_screen);

    s_nws_card = make_card(s_screen, 14, 56, 640, 490, COL_AMBER);
    lv_obj_t *nk = kicker(s_nws_card, COL_AMBER, "NWS");
    lv_obj_set_pos(nk, 0, 0);

    s_event = lv_label_create(s_nws_card);
    lv_obj_set_style_text_font(s_event, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_event, COL_TEXT, 0);
    lv_label_set_text(s_event, "All clear");
    lv_obj_set_pos(s_event, 0, 28);
    lv_obj_set_width(s_event, 600);
    lv_label_set_long_mode(s_event, LV_LABEL_LONG_WRAP);

    s_meta = lv_label_create(s_nws_card);
    lv_obj_set_style_text_font(s_meta, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_meta, COL_DIM, 0);
    lv_label_set_text(s_meta, "");
    lv_obj_set_pos(s_meta, 0, 80);
    lv_obj_set_width(s_meta, 600);

    s_headline = lv_label_create(s_nws_card);
    lv_obj_set_style_text_font(s_headline, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_headline, COL_TEXT, 0);
    lv_label_set_text(s_headline, "No active weather alerts for this zip code.");
    lv_obj_set_pos(s_headline, 0, 116);
    lv_obj_set_width(s_headline, 600);
    lv_label_set_long_mode(s_headline, LV_LABEL_LONG_WRAP);

    s_instruction = lv_label_create(s_nws_card);
    lv_obj_set_style_text_font(s_instruction, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_instruction, COL_DIM, 0);
    lv_label_set_text(s_instruction, "");
    lv_obj_set_pos(s_instruction, 0, 220);
    lv_obj_set_width(s_instruction, 600);
    lv_label_set_long_mode(s_instruction, LV_LABEL_LONG_WRAP);

    lv_obj_t *ltg_card = make_card(s_screen, 668, 56, 342, 230, COL_AMBER);
    lv_obj_t *lk = kicker(ltg_card, COL_AMBER, "LIGHTNING");
    lv_obj_set_pos(lk, 0, 0);
    s_ltg = lv_label_create(ltg_card);
    lv_obj_set_style_text_font(s_ltg, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ltg, COL_DIM, 0);
    lv_label_set_text(s_ltg, "No recent strikes");
    lv_obj_set_pos(s_ltg, 0, 32);
    lv_obj_set_width(s_ltg, 310);
    lv_label_set_long_mode(s_ltg, LV_LABEL_LONG_WRAP);

    lv_obj_t *rain_card = make_card(s_screen, 668, 298, 342, 248, COL_RAIN);
    lv_obj_t *rk = kicker(rain_card, COL_RAIN, "RAIN / STATION");
    lv_obj_set_pos(rk, 0, 0);
    s_rain = lv_label_create(rain_card);
    lv_obj_set_style_text_font(s_rain, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_rain, COL_DIM, 0);
    lv_label_set_text(s_rain, "--");
    lv_obj_set_pos(s_rain, 0, 32);
    lv_obj_set_width(s_rain, 310);
    lv_label_set_long_mode(s_rain, LV_LABEL_LONG_WRAP);

    ESP_LOGI(TAG, "alerts page built");
    return ESP_OK;
}

void alerts_show(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_screen);
    lv_obj_invalidate(s_screen);
    alerts_tick();
}

void alerts_hide(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}

bool alerts_is_visible(void)
{
    return s_screen && !lv_obj_has_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}

void alerts_tick(void)
{
    if (!alerts_is_visible()) {
        return;
    }

    set_clock(s_clock);

    nws_alert_t alert = {0};
    bool have = nws_alerts_get_active(&alert) && alert.event[0];
    if (have) {
        lv_obj_set_style_bg_color(lv_obj_get_child(s_nws_card, 0), COL_RED, 0);
        ui_label_set(s_event, alert.event);
        lv_obj_set_style_text_color(s_event, COL_RED, 0);

        char until[40];
        nws_format_until(alert.expires_epoch, until, sizeof(until));
        char meta[96];
        if (alert.severity[0] && until[0]) {
            snprintf(meta, sizeof(meta), "%s  until %s", alert.severity, until);
        } else if (alert.severity[0]) {
            snprintf(meta, sizeof(meta), "%s", alert.severity);
        } else if (until[0]) {
            snprintf(meta, sizeof(meta), "until %s", until);
        } else {
            snprintf(meta, sizeof(meta), "Active");
        }
        ui_label_set(s_meta, meta);
        lv_obj_set_style_text_color(s_meta, COL_AMBER, 0);

        ui_label_set(s_headline, alert.headline[0] ? alert.headline : alert.event);
        ui_label_set(s_instruction,
                     alert.instruction[0] ? alert.instruction
                                          : "Follow local emergency instructions.");
        lv_obj_set_style_text_color(s_instruction, COL_TEXT, 0);
        ui_label_set(s_subtitle, "Active NWS alert");
        lv_obj_set_style_text_color(s_subtitle, COL_RED, 0);
    } else {
        lv_obj_set_style_bg_color(lv_obj_get_child(s_nws_card, 0), COL_OK, 0);
        nws_forecast_t fc = {0};
        bool have_fc = nws_forecast_get(&fc);
        ui_label_set(s_event, have_fc && fc.period[0] ? fc.period : "All clear");
        lv_obj_set_style_text_color(s_event, COL_OK, 0);
        if (have_fc && (fc.office[0] || fc.radar[0] || fc.city[0])) {
            char meta[96];
            snprintf(meta, sizeof(meta), "%s%s%s  office %s  radar %s",
                     fc.city, fc.city[0] && fc.state[0] ? ", " : "",
                     fc.state, fc.office[0] ? fc.office : "--",
                     fc.radar[0] ? fc.radar : "--");
            ui_label_set(s_meta, meta);
        } else {
            ui_label_set(s_meta, "No watches or warnings");
        }
        lv_obj_set_style_text_color(s_meta, COL_DIM, 0);
        ui_label_set(s_headline, have_fc && fc.short_fc[0]
                     ? fc.short_fc
                     : "No active weather alerts for this zip code.");
        ui_label_set(s_instruction, have_fc && fc.detailed[0]
                     ? fc.detailed
                     : "The ticker at the bottom of every page will crawl "
                       "any new NWS warning or nearby lightning.");
        lv_obj_set_style_text_color(s_instruction, COL_DIM, 0);
        ui_label_set(s_subtitle, "National Weather Service");
        lv_obj_set_style_text_color(s_subtitle, COL_DIM, 0);
    }

    wx_state_t s;
    wx_snapshot(&s);
    int64_t now = (int64_t)time(NULL);

    if (s.strikes_3h > 0 && s.last_strike_epoch > 0) {
        char age[16];
        ui_fmt_age(age, sizeof(age), now - s.last_strike_epoch);
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "%d strike%s in 3 hours\n"
                 "Last %s ago  %.1f %s away",
                 s.strikes_3h, s.strikes_3h == 1 ? "" : "s",
                 age,
                 (double)cfg_distance(s.last_strike_dist_km),
                 cfg_distance_suffix());
        ui_label_set(s_ltg, buf);
        lv_obj_set_style_text_color(s_ltg,
            s.last_strike_dist_km < 9.656f ? COL_RED : COL_AMBER, 0);
    } else {
        ui_label_set(s_ltg, "No strikes in the last 3 hours");
        lv_obj_set_style_text_color(s_ltg, COL_OK, 0);
    }

    char rain_today[16];
    snprintf(rain_today, sizeof(rain_today), cfg_rain_fmt(),
             (double)cfg_rain(s.rain_today_mm));
    char last[32] = "none today";
    if (s.last_precip_epoch > 0) {
        char age[16];
        ui_fmt_age(age, sizeof(age), now - s.last_precip_epoch);
        snprintf(last, sizeof(last), "%s ago", age);
    }

    cfg_t c;
    cfg_get(&c);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "Today %s%s\n"
             "Last rain  %s\n"
             "Zip %s\n"
             "Wi-Fi %s",
             rain_today, cfg_rain_suffix(),
             last,
             c.alert_zipcode[0] ? c.alert_zipcode : "----",
             net_is_connected() ? "up" : "down");
    ui_label_set(s_rain, buf);
}
