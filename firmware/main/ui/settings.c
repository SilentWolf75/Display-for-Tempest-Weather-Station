#include "settings.h"
#include "config.h"
#include "wx_state.h"
#include "net.h"
#include "display.h"
#include "tempest_udp.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"

static const char *TAG = "settings";

#define COL_BG        lv_color_hex(0x0B0E13)
#define COL_CARD      lv_color_hex(0x151A22)
#define COL_TEXT      lv_color_hex(0xE8EDF2)
#define COL_DIM       lv_color_hex(0x7E8B99)
#define COL_ACCENT    lv_color_hex(0x4FC3F7)
#define COL_ALERT     lv_color_hex(0xEF5350)

#define ROW_H         62
#define PANEL_W       486

static lv_obj_t *s_screen;
static lv_obj_t *s_prev_screen;

static lv_obj_t *w_units;
static lv_obj_t *w_day, *w_day_val;
static lv_obj_t *w_night, *w_night_val;
static lv_obj_t *w_night_en;
static lv_obj_t *w_animate;
static lv_obj_t *w_windmax, *w_windmax_val;
static lv_obj_t *w_diag;

/* ------------------------------------------------------------------------ */

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, COL_CARD, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 12, 0);
    lv_obj_set_style_pad_all(p, 14, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *row_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(l, COL_TEXT, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, 0, y + 10);
    return l;
}

static lv_obj_t *value_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, COL_ACCENT, 0);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, 0, y + 12);
    return l;
}

/* ---- change handlers: each writes through immediately ------------------- */

static void on_units(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.units = (cfg_units_t)lv_buttonmatrix_get_selected_button(
        lv_event_get_target(e));
    cfg_set(&c);
}

static void on_day_brightness(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.brightness_day = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    lv_label_set_text_fmt(w_day_val, "%u%%", c.brightness_day);
    /* Immediate feedback unless we are currently in the night window, where
     * changing the day value should not blind anyone. */
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    if (!cfg_is_night(lt.tm_hour)) {
        display_set_brightness(c.brightness_day);
    }
}

static void on_night_brightness(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.brightness_night = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    lv_label_set_text_fmt(w_night_val, "%u%%", c.brightness_night);
}

static void on_night_enable(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.night_dim_enabled =
        lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
}

static void on_animate(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.animate_forecast =
        lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
    ESP_LOGI(TAG, "forecast animation %s (applies after restart)",
             c.animate_forecast ? "on" : "off");
}

static void on_windmax(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.wind_scale_max_ms = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    lv_label_set_text_fmt(w_windmax_val, "%.0f %s",
                          (double)cfg_wind((float)c.wind_scale_max_ms),
                          cfg_wind_suffix());
}

static void on_back(lv_event_t *e)
{
    (void)e;
    settings_hide();
}

static void on_reset(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "settings reset to defaults");
    cfg_reset();

    cfg_t c;
    cfg_get(&c);
    lv_buttonmatrix_set_selected_button(w_units, (uint32_t)c.units);
    lv_slider_set_value(w_day, c.brightness_day, LV_ANIM_ON);
    lv_slider_set_value(w_night, c.brightness_night, LV_ANIM_ON);
    lv_slider_set_value(w_windmax, c.wind_scale_max_ms, LV_ANIM_ON);
    lv_label_set_text_fmt(w_day_val, "%u%%", c.brightness_day);
    lv_label_set_text_fmt(w_night_val, "%u%%", c.brightness_night);
    if (c.night_dim_enabled) lv_obj_add_state(w_night_en, LV_STATE_CHECKED);
    else                     lv_obj_clear_state(w_night_en, LV_STATE_CHECKED);
    if (c.animate_forecast)  lv_obj_add_state(w_animate, LV_STATE_CHECKED);
    else                     lv_obj_clear_state(w_animate, LV_STATE_CHECKED);
    display_set_brightness(c.brightness_day);
}

static void on_reboot(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "reboot requested from settings");
    esp_restart();
}

/* ------------------------------------------------------------------------ */

esp_err_t settings_init(void)
{
    cfg_t c;
    cfg_get(&c);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* --- header --- */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "Settings");
    lv_obj_set_pos(title, 24, 20);

    lv_obj_t *back = lv_button_create(s_screen);
    lv_obj_set_size(back, 150, 52);
    lv_obj_set_pos(back, 1024 - 150 - 24, 14);
    lv_obj_set_style_bg_color(back, COL_CARD, 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Done");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_18, 0);
    lv_obj_center(bl);

    /* --- left panel: display --- */
    lv_obj_t *left = make_panel(s_screen, 24, 84, PANEL_W, 400);
    int y = 0;

    row_label(left, y, "Units");
    static const char *unit_map[] = { "Imperial", "Metric", "" };
    w_units = lv_buttonmatrix_create(left);
    lv_buttonmatrix_set_map(w_units, unit_map);
    lv_obj_set_size(w_units, 230, 44);
    lv_obj_align(w_units, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    lv_buttonmatrix_set_button_ctrl_all(w_units, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(w_units, true);
    lv_buttonmatrix_set_selected_button(w_units, (uint32_t)c.units);
    lv_obj_add_event_cb(w_units, on_units, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 6;

    row_label(left, y, "Day brightness");
    w_day_val = value_label(left, y, "");
    lv_label_set_text_fmt(w_day_val, "%u%%", c.brightness_day);
    w_day = lv_slider_create(left);
    lv_obj_set_size(w_day, PANEL_W - 28, 12);
    lv_obj_set_pos(w_day, 0, y + 40);
    lv_slider_set_range(w_day, 5, 100);
    lv_slider_set_value(w_day, c.brightness_day, LV_ANIM_OFF);
    lv_obj_add_event_cb(w_day, on_day_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 12;

    row_label(left, y, "Night brightness");
    w_night_val = value_label(left, y, "");
    lv_label_set_text_fmt(w_night_val, "%u%%", c.brightness_night);
    w_night = lv_slider_create(left);
    lv_obj_set_size(w_night, PANEL_W - 28, 12);
    lv_obj_set_pos(w_night, 0, y + 40);
    lv_slider_set_range(w_night, 0, 100);
    lv_slider_set_value(w_night, c.brightness_night, LV_ANIM_OFF);
    lv_obj_add_event_cb(w_night, on_night_brightness,
                        LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 12;

    lv_obj_t *nl = row_label(left, y, "Dim at night");
    lv_obj_t *nsub = lv_label_create(left);
    lv_obj_set_style_text_font(nsub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(nsub, COL_DIM, 0);
    lv_label_set_text_fmt(nsub, "%02u:00 to %02u:00",
                          c.night_start_hour, c.night_end_hour);
    lv_obj_set_pos(nsub, 0, y + 34);
    (void)nl;

    w_night_en = lv_switch_create(left);
    lv_obj_set_size(w_night_en, 70, 38);
    lv_obj_align(w_night_en, LV_ALIGN_TOP_RIGHT, 0, y + 6);
    if (c.night_dim_enabled) {
        lv_obj_add_state(w_night_en, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(w_night_en, on_night_enable,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* --- right panel: behaviour --- */
    lv_obj_t *right = make_panel(s_screen, 24 + PANEL_W + 16, 84, PANEL_W, 400);
    y = 0;

    row_label(right, y, "Wind gauge full scale");
    w_windmax_val = value_label(right, y, "");
    lv_label_set_text_fmt(w_windmax_val, "%.0f %s",
                          (double)cfg_wind((float)c.wind_scale_max_ms),
                          cfg_wind_suffix());
    w_windmax = lv_slider_create(right);
    lv_obj_set_size(w_windmax, PANEL_W - 28, 12);
    lv_obj_set_pos(w_windmax, 0, y + 40);
    lv_slider_set_range(w_windmax, 5, 60);
    lv_slider_set_value(w_windmax, c.wind_scale_max_ms, LV_ANIM_OFF);
    lv_obj_add_event_cb(w_windmax, on_windmax, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 12;

    row_label(right, y, "Animate all forecast icons");
    lv_obj_t *asub = lv_label_create(right);
    lv_obj_set_style_text_font(asub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(asub, COL_DIM, 0);
    lv_label_set_text(asub, "costs CPU; restart to apply");
    lv_obj_set_pos(asub, 0, y + 34);

    w_animate = lv_switch_create(right);
    lv_obj_set_size(w_animate, 70, 38);
    lv_obj_align(w_animate, LV_ALIGN_TOP_RIGHT, 0, y + 6);
    if (c.animate_forecast) {
        lv_obj_add_state(w_animate, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(w_animate, on_animate, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 20;

    row_label(right, y, "Diagnostics");
    w_diag = lv_label_create(right);
    lv_obj_set_style_text_font(w_diag, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w_diag, COL_DIM, 0);
    lv_label_set_text(w_diag, "");
    lv_obj_set_pos(w_diag, 0, y + 36);

    /* --- destructive actions, kept away from everything else --- */
    lv_obj_t *reset = lv_button_create(s_screen);
    lv_obj_set_size(reset, 220, 50);
    lv_obj_set_pos(reset, 24, 508);
    lv_obj_set_style_bg_color(reset, COL_CARD, 0);
    lv_obj_set_style_radius(reset, 10, 0);
    lv_obj_add_event_cb(reset, on_reset, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(reset);
    lv_label_set_text(rl, "Reset to defaults");
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_16, 0);
    lv_obj_center(rl);

    lv_obj_t *reboot = lv_button_create(s_screen);
    lv_obj_set_size(reboot, 160, 50);
    lv_obj_set_pos(reboot, 260, 508);
    lv_obj_set_style_bg_color(reboot, COL_CARD, 0);
    lv_obj_set_style_border_color(reboot, COL_ALERT, 0);
    lv_obj_set_style_border_width(reboot, 1, 0);
    lv_obj_set_style_radius(reboot, 10, 0);
    lv_obj_add_event_cb(reboot, on_reboot, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rbl = lv_label_create(reboot);
    lv_label_set_text(rbl, "Restart");
    lv_obj_set_style_text_color(rbl, COL_ALERT, 0);
    lv_obj_set_style_text_font(rbl, &lv_font_montserrat_16, 0);
    lv_obj_center(rbl);

    ESP_LOGI(TAG, "settings screen built");
    return ESP_OK;
}

void settings_show(void)
{
    if (!s_screen) {
        return;
    }
    s_prev_screen = lv_screen_active();
    settings_tick();
    lv_screen_load(s_screen);
}

void settings_hide(void)
{
    if (s_prev_screen) {
        lv_screen_load(s_prev_screen);
    }
}

bool settings_is_visible(void)
{
    return s_screen && lv_screen_active() == s_screen;
}

void settings_tick(void)
{
    if (!settings_is_visible() || !w_diag) {
        return;
    }

    wx_state_t s;
    wx_snapshot(&s);

    const esp_app_desc_t *app = esp_app_get_description();
    uint32_t up = (uint32_t)(esp_log_timestamp() / 1000);

    lv_label_set_text_fmt(w_diag,
        "firmware   %s  (%s)\n"
        "network    %s\n"
        "station    %s   %.2f V   RSSI %d\n"
        "thermostat %s\n"
        "udp        %lu packets\n"
        "memory     %u KB internal   %u KB psram\n"
        "uptime     %luh %02lum",
        app ? app->version : "?",
        app ? app->date : "?",
        net_is_connected() ? "connected" : "down",
        s.obs_valid ? (wx_obs_is_stale(&s) ? "stale" : "live") : "waiting",
        (double)s.battery_v, s.hub_rssi,
        s.indoor_valid ? (wx_indoor_is_stale(&s) ? "stale" : "live")
                       : "not configured",
        (unsigned long)tempest_udp_packet_count(),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
        (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
}
