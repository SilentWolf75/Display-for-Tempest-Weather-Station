#include "settings.h"
#include "config.h"
#include "wx_state.h"
#include "ui.h"
#include "net.h"
#include "display.h"
#include "tempest_udp.h"
#include "wifi_setup.h"
#include "audio.h"
#include "nws_alerts.h"
#include "sdcard.h"
#include "web_server.h"
#include "mqtt_client_app.h"
#include "ota.h"
#include "tempest_rest.h"
#include "sdkconfig.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"

static const char *TAG = "settings";

#define COL_BG        lv_color_hex(0x05080D)
#define COL_CARD      lv_color_hex(0x0C1118)
#define COL_BORDER    lv_color_hex(0x243044)
#define COL_TRACK     lv_color_hex(0x18202C)
#define COL_TEXT      lv_color_hex(0xF8FAFC)
#define COL_DIM       lv_color_hex(0x94A3B8)
#define COL_ACCENT    lv_color_hex(0x00E5FF)
#define COL_ALERT     lv_color_hex(0xEF5350)

#define ROW_H         58
#define PANEL_W       476

static lv_obj_t *s_screen;

static lv_obj_t *w_units;
static lv_obj_t *w_tz;
static lv_obj_t *w_day, *w_day_val;
static lv_obj_t *w_night, *w_night_val;
static lv_obj_t *w_night_en;
static lv_obj_t *w_night_start, *w_night_start_val;
static lv_obj_t *w_night_end, *w_night_end_val;
static lv_obj_t *w_animate;
static lv_obj_t *w_ss_idle, *w_ss_idle_val;
static lv_obj_t *w_ss_dim, *w_ss_dim_val;
static lv_obj_t *w_ltg_sound_sw;
static lv_obj_t *w_ltg_voice_sw;
static lv_obj_t *w_windmax, *w_windmax_val;
static lv_obj_t *w_temp_offset, *w_temp_offset_val;
static lv_obj_t *w_diag;
static lv_obj_t *w_station;
static lv_obj_t *w_sd, *w_sd_btn, *w_sd_btn_lbl;
/* Two-step format: the first press arms, the second commits.
 * Formatting wipes somebody's weather history, so it does not
 * happen on a single stray tap. Disarms itself after 5 s. */
static bool     s_fmt_armed;
static uint32_t s_fmt_armed_at;
static lv_obj_t *w_wifi_sub;
static lv_obj_t *w_zip_ta, *w_kb;
static lv_obj_t *w_vol_slider, *w_vol_val;
static lv_obj_t *w_notif_vol_slider, *w_notif_vol_val;
static lv_obj_t *w_alert_siren_sw;
static lv_obj_t *w_hourly_chime_sw;
static lv_obj_t *w_morning_brief_sw;
static lv_obj_t *w_night_dnd_sw;
static lv_obj_t *w_night_standby_sw;
static lv_obj_t *w_night_standby_red_sw;
static lv_obj_t *w_web_server_sw;
static lv_obj_t *w_mqtt_sw;
static lv_obj_t *w_mqtt_broker_ta;
static lv_obj_t *w_net_status;
static lv_obj_t *w_clock;
static lv_obj_t *w_alert_event;
static lv_obj_t *w_alert_body;
static lv_obj_t *w_start;

#define TAB_COUNT 4
static lv_obj_t *s_tabs[TAB_COUNT];
static lv_obj_t *s_tab_btns[TAB_COUNT];
static int       s_tab;

/* ------------------------------------------------------------------------ */

static const char *format_hour_12(int h, char *buf, size_t sz)
{
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, sz, "%d:00 %s", h12, (h >= 12) ? "PM" : "AM");
    return buf;
}

/* set_selected_button() alone does not apply LV_STATE_CHECKED on boot — the
 * saved value loads from NVS but nothing looks selected until you tap again. */
static void buttonmatrix_apply_selection(lv_obj_t *bm, uint32_t sel)
{
    if (!bm || sel == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }

    const char *const *map = (const char *const *)lv_buttonmatrix_get_map(bm);
    if (!map) {
        return;
    }

    uint32_t btn = 0;
    for (uint32_t i = 0; map[i] && map[i][0] != '\0'; i++) {
        if (map[i][0] == '\n') {
            continue;
        }
        if (btn == sel) {
            lv_buttonmatrix_set_button_ctrl(bm, btn, LV_BUTTONMATRIX_CTRL_CHECKED);
            lv_buttonmatrix_set_selected_button(bm, btn);
        } else {
            lv_buttonmatrix_clear_button_ctrl(bm, btn, LV_BUTTONMATRIX_CTRL_CHECKED);
        }
        btn++;
    }
}

static void sync_settings_toggles(void)
{
    cfg_t c;
    cfg_get(&c);
    buttonmatrix_apply_selection(w_units, (uint32_t)c.units);
    buttonmatrix_apply_selection(w_tz, (uint32_t)c.timezone_idx);
    if (w_start) {
        buttonmatrix_apply_selection(w_start, (uint32_t)c.start_page);
    }
}

static void style_buttonmatrix(lv_obj_t *bm)
{
    /* Container (Main background) */
    lv_obj_set_style_bg_color(bm, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bm, COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_width(bm, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bm, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bm, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(bm, 2, LV_PART_MAIN);

    /* Unselected buttons */
    lv_obj_set_style_bg_color(bm, COL_BG, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bm, COL_DIM, LV_PART_ITEMS);
    lv_obj_set_style_text_font(bm, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_border_width(bm, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(bm, 6, LV_PART_ITEMS);

    /* Selected button (Bright Electric Cyan with Bold Dark Text) */
    lv_obj_set_style_bg_color(bm, COL_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(bm, lv_color_hex(0x06090E), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(bm, &lv_font_montserrat_14, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_radius(bm, 6, LV_PART_ITEMS | LV_STATE_CHECKED);
}

static void style_slider(lv_obj_t *s)
{
    lv_obj_set_style_bg_color(s, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, COL_TEXT, LV_PART_KNOB);
}

static void show_tab(int idx)
{
    s_tab = idx;
    for (int i = 0; i < TAB_COUNT; i++) {
        if (s_tabs[i]) {
            if (i == idx) {
                lv_obj_clear_flag(s_tabs[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_tabs[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (s_tab_btns[i]) {
            bool on = (i == idx);
            lv_obj_set_style_bg_color(s_tab_btns[i], on ? COL_ACCENT : COL_TRACK, 0);
            lv_obj_t *lbl = lv_obj_get_child(s_tab_btns[i], 0);
            if (lbl) {
                lv_obj_set_style_text_color(lbl,
                    on ? lv_color_hex(0x06090E) : COL_TEXT, 0);
            }
        }
    }
}

static void on_tab(lv_event_t *e)
{
    show_tab((int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *make_tab_page(lv_obj_t *host)
{
    lv_obj_t *p = lv_obj_create(host);
    lv_obj_set_size(p, 1024, UI_CONTENT_H - 58);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_pad_hor(p, 24, 0);
    lv_obj_set_style_pad_bottom(p, 48, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, COL_CARD, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, COL_BORDER, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 12, 0);
    lv_obj_set_style_pad_all(p, 14, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(p);
    lv_obj_remove_style_all(accent);
    lv_obj_set_pos(accent, -14, -14);
    lv_obj_set_size(accent, w, 2);
    lv_obj_set_style_bg_color(accent, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return p;
}

static lv_obj_t *row_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(l, COL_TEXT, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, 0, y + 8);
    return l;
}

static lv_obj_t *value_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, COL_ACCENT, 0);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, 0, y + 10);
    return l;
}

/* ---- change handlers ---------------------------------------------------- */

static void on_sd_format(lv_event_t *e)
{
    (void)e;
    if (sdcard_format_state() == SDCARD_FMT_BUSY) {
        return;
    }
    if (!s_fmt_armed) {
        s_fmt_armed    = true;
        s_fmt_armed_at = lv_tick_get();
        return;
    }
    s_fmt_armed = false;
    sdcard_format_async();
}

static void on_sd_remount(lv_event_t *e)
{
    (void)e;
    sdcard_remount();
}

static void on_tz(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target(e);
    uint32_t btn_id = lv_buttonmatrix_get_selected_button(bm);
    if (btn_id != LV_BUTTONMATRIX_BUTTON_NONE) {
        cfg_t c;
        cfg_get(&c);
        c.timezone_idx = (uint8_t)btn_id;
        cfg_set(&c);
        net_set_timezone(c.timezone_idx);
    }
}

static void on_start_page(lv_event_t *e)
{
    uint32_t btn = lv_buttonmatrix_get_selected_button(lv_event_get_target(e));
    if (btn == LV_BUTTONMATRIX_BUTTON_NONE || btn >= UI_PAGE_COUNT) {
        return;
    }
    cfg_t c;
    cfg_get(&c);
    c.start_page = (uint8_t)btn;
    cfg_set(&c);
}

static void on_units(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.units = (cfg_units_t)lv_buttonmatrix_get_selected_button(
        lv_event_get_target(e));
    cfg_set(&c);
    ui_notify_forecast_updated();
}

static void on_day_brightness(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.brightness_day = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    lv_label_set_text_fmt(w_day_val, "%u%%", c.brightness_day);
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
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    if (cfg_is_night(lt.tm_hour)) {
        display_set_brightness(c.brightness_night);
    }
}

static void on_night_enable(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.night_dim_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
}

static void on_night_start(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.night_start_hour = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    char buf[16];
    lv_label_set_text(w_night_start_val, format_hour_12(c.night_start_hour, buf, sizeof(buf)));
}

static void on_night_end(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.night_end_hour = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    char buf[16];
    lv_label_set_text(w_night_end_val, format_hour_12(c.night_end_hour, buf, sizeof(buf)));
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

static void on_temp_offset(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int32_t val_tenth_f = lv_slider_get_value(s);
    float offset_f = (float)val_tenth_f / 10.0f;
    float offset_c = offset_f / 1.8f;

    cfg_t c;
    cfg_get(&c);
    c.indoor_temp_offset_c = offset_c;
    cfg_set(&c);

    if (w_temp_offset_val) {
        if (c.units == CFG_UNITS_METRIC) {
            lv_label_set_text_fmt(w_temp_offset_val, "%+.1f °C", (double)offset_c);
        } else {
            lv_label_set_text_fmt(w_temp_offset_val, "%+.1f °F", (double)offset_f);
        }
    }
}

static void on_animate_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.animate_forecast = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
    ui_forecast_mode_changed();
}

static void on_ss_idle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.screensaver_idle_min = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    if (w_ss_idle_val) {
        if (c.screensaver_idle_min == 0) {
            lv_label_set_text(w_ss_idle_val, "Off");
        } else {
            lv_label_set_text_fmt(w_ss_idle_val, "%u min", c.screensaver_idle_min);
        }
    }
}

static void on_ss_dim(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.screensaver_brightness = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    if (w_ss_dim_val) {
        lv_label_set_text_fmt(w_ss_dim_val, "%u%%", c.screensaver_brightness);
    }
}

static void on_ltg_sound_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.lightning_alert_sound = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
}

static void on_ltg_voice_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.lightning_alert_voice = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
}

static void on_alert_volume(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.alert_volume = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    audio_set_alert_volume(c.alert_volume);
    if (w_vol_val) {
        lv_label_set_text_fmt(w_vol_val, "%u%%", c.alert_volume);
    }
}

static void on_notification_volume(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.notification_volume = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    cfg_set(&c);
    audio_set_notification_volume(c.notification_volume);
    if (w_notif_vol_val) {
        lv_label_set_text_fmt(w_notif_vol_val, "%u%%", c.notification_volume);
    }
    audio_play_notify();
}

static void on_alert_siren_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.alert_siren_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
}

static void on_test_siren(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Testing 1050 Hz NOAA Weather Alert Siren");
    audio_play_noaa_tone(2);
}

static void on_test_voice_alert(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Testing NOAA Weather Alert Voice Broadcast");
    audio_play_full_noaa_broadcast("Severe Weather Alert test");
}

static void on_hourly_chime_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.hourly_chime_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
    if (c.hourly_chime_enabled) {
        audio_play_chime();
    }
}

static void on_morning_brief_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.morning_briefing_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
    if (c.morning_briefing_enabled) {
        wx_state_t s;
        wx_snapshot(&s);
        const char *slug = s.current_icon[0] ? s.current_icon : "clear-day";
        audio_play_morning_briefing(slug);
    }
}

static void on_test_morning_briefing(lv_event_t *e)
{
    (void)e;
    wx_state_t s;
    wx_snapshot(&s);
    const char *slug = s.current_icon[0] ? s.current_icon : "clear-day";
    audio_play_morning_briefing(slug);
}

static void on_night_dnd_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.night_alert_dnd = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
}

static void on_night_standby_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.night_standby_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
}

static void on_night_standby_red_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.night_standby_red = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
}

static void on_web_server_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.web_server_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
    if (c.web_server_enabled) {
        web_server_start();
    } else {
        web_server_stop();
#if CONFIG_OTA_ENABLED
        ota_start();
#endif
    }
}

static void on_mqtt_toggle(lv_event_t *e)
{
    cfg_t c;
    cfg_get(&c);
    c.mqtt_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    cfg_set(&c);
    mqtt_app_reconnect();
}

static void on_mqtt_broker_changed(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
        const char *txt = lv_textarea_get_text(w_mqtt_broker_ta);
        if (txt && txt[0]) {
            cfg_t c;
            cfg_get(&c);
            strncpy(c.mqtt_broker, txt, sizeof(c.mqtt_broker) - 1);
            c.mqtt_broker[sizeof(c.mqtt_broker) - 1] = '\0';
            cfg_set(&c);
            ESP_LOGI(TAG, "mqtt broker set to %s", c.mqtt_broker);
            mqtt_app_reconnect();
        }
    }
}

static void zip_keep_digits(void)
{
    const char *txt = lv_textarea_get_text(w_zip_ta);
    if (!txt) {
        return;
    }
    char digits[6] = {0};
    int n = 0;
    for (const char *p = txt; *p && n < 5; p++) {
        if (isdigit((unsigned char)*p)) {
            digits[n++] = *p;
        }
    }
    if (strcmp(txt, digits) != 0) {
        lv_textarea_set_text(w_zip_ta, digits);
    }
}

static void on_zip_changed(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        zip_keep_digits();
        return;
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
        zip_keep_digits();
        const char *txt = lv_textarea_get_text(w_zip_ta);
        if (txt && strlen(txt) == 5) {
            cfg_t c;
            cfg_get(&c);
            strncpy(c.alert_zipcode, txt, sizeof(c.alert_zipcode) - 1);
            c.alert_zipcode[sizeof(c.alert_zipcode) - 1] = '\0';
            cfg_set(&c);
            ESP_LOGI(TAG, "Updated alert zip code to: %s", c.alert_zipcode);
            nws_alerts_refresh();
        }
        if (w_kb) {
            lv_obj_add_flag(w_kb, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_FOCUSED) {
        if (w_kb) {
            lv_obj_clear_flag(w_kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void on_wifi(lv_event_t *e)
{
    (void)e;
    /* Full Wi-Fi UI touches the SDIO radio and has frozen the MIPI panel.
     * When already connected, status in this screen is enough. */
    if (net_is_connected()) {
        return;
    }
    wifi_setup_show();
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
    char buf[16];
    sync_settings_toggles();
    lv_slider_set_value(w_day, c.brightness_day, LV_ANIM_ON);
    lv_slider_set_value(w_night, c.brightness_night, LV_ANIM_ON);
    lv_slider_set_value(w_night_start, c.night_start_hour, LV_ANIM_ON);
    lv_slider_set_value(w_night_end, c.night_end_hour, LV_ANIM_ON);
    lv_slider_set_value(w_windmax, c.wind_scale_max_ms, LV_ANIM_ON);
    lv_slider_set_value(w_ss_idle, c.screensaver_idle_min, LV_ANIM_ON);
    lv_slider_set_value(w_ss_dim, c.screensaver_brightness, LV_ANIM_ON);
    lv_slider_set_value(w_vol_slider, c.alert_volume, LV_ANIM_ON);
    lv_slider_set_value(w_notif_vol_slider, c.notification_volume, LV_ANIM_ON);
    lv_label_set_text_fmt(w_day_val, "%u%%", c.brightness_day);
    lv_label_set_text_fmt(w_night_val, "%u%%", c.brightness_night);
    lv_label_set_text(w_night_start_val, format_hour_12(c.night_start_hour, buf, sizeof(buf)));
    lv_label_set_text(w_night_end_val, format_hour_12(c.night_end_hour, buf, sizeof(buf)));
    lv_label_set_text_fmt(w_vol_val, "%u%%", c.alert_volume);
    lv_label_set_text_fmt(w_notif_vol_val, "%u%%", c.notification_volume);

    if (c.night_dim_enabled) lv_obj_add_state(w_night_en, LV_STATE_CHECKED);
    else                     lv_obj_clear_state(w_night_en, LV_STATE_CHECKED);
    if (c.alert_siren_enabled) lv_obj_add_state(w_alert_siren_sw, LV_STATE_CHECKED);
    else                       lv_obj_clear_state(w_alert_siren_sw, LV_STATE_CHECKED);
    if (c.lightning_alert_sound) lv_obj_add_state(w_ltg_sound_sw, LV_STATE_CHECKED);
    else                         lv_obj_clear_state(w_ltg_sound_sw, LV_STATE_CHECKED);
    if (c.lightning_alert_voice) lv_obj_add_state(w_ltg_voice_sw, LV_STATE_CHECKED);
    else                         lv_obj_clear_state(w_ltg_voice_sw, LV_STATE_CHECKED);
    if (c.animate_forecast) lv_obj_add_state(w_animate, LV_STATE_CHECKED);
    else                    lv_obj_clear_state(w_animate, LV_STATE_CHECKED);
    if (c.screensaver_idle_min == 0) lv_label_set_text(w_ss_idle_val, "Off");
    else lv_label_set_text_fmt(w_ss_idle_val, "%u min", c.screensaver_idle_min);
    lv_label_set_text_fmt(w_ss_dim_val, "%u%%", c.screensaver_brightness);
    ui_forecast_mode_changed();

    audio_set_alert_volume(c.alert_volume);
    audio_set_notification_volume(c.notification_volume);
    display_set_brightness(c.brightness_day);
}

static void on_reboot(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "reboot requested from settings");
    esp_restart();
}

static void on_settings_activity(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        ui_note_user_activity();
    }
}

/* ------------------------------------------------------------------------ */

esp_err_t settings_init(void)
{
    cfg_t c;
    cfg_get(&c);
    audio_set_alert_volume(c.alert_volume);
    audio_set_notification_volume(c.notification_volume);
    char buf[16];

    /* Overlay — never lv_screen_load(); that has blanked the panel repeatedly. */
    s_screen = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_screen, UI_SCR_W, UI_CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);

    /* --- Fixed Header --- */
    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "Settings");
    lv_obj_set_pos(title, 20, 16);

    static const char *tab_names[TAB_COUNT] = { "Display", "Sound", "Network", "System" };
    for (int i = 0; i < TAB_COUNT; i++) {
        s_tab_btns[i] = lv_button_create(s_screen);
        lv_obj_set_size(s_tab_btns[i], 104, 32);
        lv_obj_set_pos(s_tab_btns[i], 168 + i * 112, 16);
        lv_obj_set_style_bg_color(s_tab_btns[i], COL_TRACK, 0);
        lv_obj_set_style_bg_opa(s_tab_btns[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_tab_btns[i], 0, 0);
        lv_obj_set_style_radius(s_tab_btns[i], 8, 0);
        lv_obj_set_style_pad_all(s_tab_btns[i], 0, 0);
        lv_obj_add_event_cb(s_tab_btns[i], on_tab, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *tl = lv_label_create(s_tab_btns[i]);
        lv_label_set_text(tl, tab_names[i]);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(tl, COL_TEXT, 0);
        lv_obj_center(tl);
    }

    lv_obj_t *back = lv_button_create(s_screen);
    lv_obj_set_size(back, 120, 36);
    lv_obj_set_pos(back, 1024 - 120 - 16, 14);
    lv_obj_set_style_bg_color(back, COL_CARD, 0);
    lv_obj_set_style_border_color(back, COL_BORDER, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Done");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_center(bl);

    w_clock = lv_label_create(s_screen);
    lv_obj_set_style_text_font(w_clock, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(w_clock, COL_DIM, 0);
    lv_label_set_text(w_clock, "--:--");
    lv_obj_set_pos(w_clock, 640, 20);

    lv_obj_t *host = lv_obj_create(s_screen);
    lv_obj_set_pos(host, 0, 58);
    lv_obj_set_size(host, 1024, UI_CONTENT_H - 58);
    lv_obj_set_style_bg_opa(host, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(host, 0, 0);
    lv_obj_set_style_pad_all(host, 0, 0);
    lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < TAB_COUNT; i++) {
        s_tabs[i] = make_tab_page(host);
    }

    /* --- Display --- */
    lv_obj_t *left = make_panel(s_tabs[0], 0, 8, PANEL_W, 820);
    int y = 0;

    /* Units */
    row_label(left, y, "Units");
    static const char *unit_map[] = { "Imperial", "Metric", "" };
    w_units = lv_buttonmatrix_create(left);
    style_buttonmatrix(w_units);
    lv_buttonmatrix_set_map(w_units, unit_map);
    lv_obj_set_size(w_units, 220, 40);
    lv_obj_align(w_units, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    lv_buttonmatrix_set_button_ctrl_all(w_units, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(w_units, true);
    buttonmatrix_apply_selection(w_units, (uint32_t)c.units);
    lv_obj_add_event_cb(w_units, on_units, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    /* Timezone */
    row_label(left, y, "Time Zone");
    static const char *tz_map[] = { "ET", "CT", "MT", "AZ", "PT", "AK", "HI", "UTC", "" };
    w_tz = lv_buttonmatrix_create(left);
    style_buttonmatrix(w_tz);
    lv_buttonmatrix_set_map(w_tz, tz_map);
    lv_obj_set_size(w_tz, 300, 40);
    lv_obj_align(w_tz, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    lv_buttonmatrix_set_button_ctrl_all(w_tz, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(w_tz, true);
    buttonmatrix_apply_selection(w_tz, (uint32_t)c.timezone_idx);
    lv_obj_add_event_cb(w_tz, on_tz, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    row_label(left, y, "Start Page");
    static const char *start_map[] = { "LIVE", "SKY", "WEEK", "24H", "ALERTS", "" };
    w_start = lv_buttonmatrix_create(left);
    style_buttonmatrix(w_start);
    lv_buttonmatrix_set_map(w_start, start_map);
    lv_obj_set_size(w_start, 340, 40);
    lv_obj_align(w_start, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    lv_buttonmatrix_set_button_ctrl_all(w_start, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(w_start, true);
    buttonmatrix_apply_selection(w_start, (uint32_t)c.start_page);
    lv_obj_add_event_cb(w_start, on_start_page, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    /* Day Brightness */
    row_label(left, y, "Day Brightness");
    w_day_val = value_label(left, y, "");
    lv_label_set_text_fmt(w_day_val, "%u%%", c.brightness_day);
    w_day = lv_slider_create(left);
    lv_obj_set_size(w_day, PANEL_W - 28, 12);
    lv_obj_set_pos(w_day, 0, y + 36);
    lv_slider_set_range(w_day, 5, 100);
    lv_slider_set_value(w_day, c.brightness_day, LV_ANIM_OFF);
    style_slider(w_day);
    lv_obj_add_event_cb(w_day, on_day_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    /* Night Brightness */
    row_label(left, y, "Night Brightness");
    w_night_val = value_label(left, y, "");
    lv_label_set_text_fmt(w_night_val, "%u%%", c.brightness_night);
    w_night = lv_slider_create(left);
    lv_obj_set_size(w_night, PANEL_W - 28, 12);
    lv_obj_set_pos(w_night, 0, y + 36);
    lv_slider_set_range(w_night, 5, 100);
    lv_slider_set_value(w_night, c.brightness_night, LV_ANIM_OFF);
    style_slider(w_night);
    lv_obj_add_event_cb(w_night, on_night_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    /* Dim at Night Enable */
    row_label(left, y, "Auto-Dim at Night");
    w_night_en = lv_switch_create(left);
    lv_obj_set_size(w_night_en, 64, 34);
    lv_obj_align(w_night_en, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.night_dim_enabled) {
        lv_obj_add_state(w_night_en, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(w_night_en, on_night_enable, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 2;

    /* Night Start Hour */
    row_label(left, y, "Night Dim Start Time");
    w_night_start_val = value_label(left, y, "");
    lv_label_set_text(w_night_start_val, format_hour_12(c.night_start_hour, buf, sizeof(buf)));
    w_night_start = lv_slider_create(left);
    lv_obj_set_size(w_night_start, PANEL_W - 28, 12);
    lv_obj_set_pos(w_night_start, 0, y + 36);
    lv_slider_set_range(w_night_start, 0, 23);
    lv_slider_set_value(w_night_start, c.night_start_hour, LV_ANIM_OFF);
    style_slider(w_night_start);
    lv_obj_add_event_cb(w_night_start, on_night_start, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    /* Night End Hour */
    row_label(left, y, "Night Dim End Time");
    w_night_end_val = value_label(left, y, "");
    lv_label_set_text(w_night_end_val, format_hour_12(c.night_end_hour, buf, sizeof(buf)));
    w_night_end = lv_slider_create(left);
    lv_obj_set_size(w_night_end, PANEL_W - 28, 12);
    lv_obj_set_pos(w_night_end, 0, y + 36);
    lv_slider_set_range(w_night_end, 0, 23);
    lv_slider_set_value(w_night_end, c.night_end_hour, LV_ANIM_OFF);
    style_slider(w_night_end);
    lv_obj_add_event_cb(w_night_end, on_night_end, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    row_label(left, y, "Screensaver Idle (0 = off)");
    w_ss_idle_val = value_label(left, y, "");
    if (c.screensaver_idle_min == 0) {
        lv_label_set_text(w_ss_idle_val, "Off");
    } else {
        lv_label_set_text_fmt(w_ss_idle_val, "%u min", c.screensaver_idle_min);
    }
    w_ss_idle = lv_slider_create(left);
    lv_obj_set_size(w_ss_idle, PANEL_W - 28, 12);
    lv_obj_set_pos(w_ss_idle, 0, y + 36);
    lv_slider_set_range(w_ss_idle, 0, 60);
    lv_slider_set_value(w_ss_idle, c.screensaver_idle_min, LV_ANIM_OFF);
    style_slider(w_ss_idle);
    lv_obj_add_event_cb(w_ss_idle, on_ss_idle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    row_label(left, y, "Screensaver Brightness");
    w_ss_dim_val = value_label(left, y, "");
    lv_label_set_text_fmt(w_ss_dim_val, "%u%%", c.screensaver_brightness);
    w_ss_dim = lv_slider_create(left);
    lv_obj_set_size(w_ss_dim, PANEL_W - 28, 12);
    lv_obj_set_pos(w_ss_dim, 0, y + 36);
    lv_slider_set_range(w_ss_dim, 1, 50);
    lv_slider_set_value(w_ss_dim, c.screensaver_brightness, LV_ANIM_OFF);
    style_slider(w_ss_dim);
    lv_obj_add_event_cb(w_ss_dim, on_ss_dim, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    row_label(left, y, "Night Standby Clock");
    w_night_standby_sw = lv_switch_create(left);
    lv_obj_set_size(w_night_standby_sw, 64, 34);
    lv_obj_align(w_night_standby_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.night_standby_enabled) lv_obj_add_state(w_night_standby_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w_night_standby_sw, on_night_standby_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    row_label(left, y, "Standby Red Tint (else Amber)");
    w_night_standby_red_sw = lv_switch_create(left);
    lv_obj_set_size(w_night_standby_red_sw, 64, 34);
    lv_obj_align(w_night_standby_red_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.night_standby_red) lv_obj_add_state(w_night_standby_red_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w_night_standby_red_sw, on_night_standby_red_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    /* Reset & Reboot Buttons */
    lv_obj_t *reset = lv_button_create(left);
    lv_obj_set_size(reset, (PANEL_W - 36) / 2, 42);
    lv_obj_set_pos(reset, 0, y + 10);
    lv_obj_set_style_bg_color(reset, COL_BG, 0);
    lv_obj_set_style_radius(reset, 8, 0);
    lv_obj_add_event_cb(reset, on_reset, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(reset);
    lv_label_set_text(rl, "Reset Defaults");
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
    lv_obj_center(rl);

    lv_obj_t *reboot = lv_button_create(left);
    lv_obj_set_size(reboot, (PANEL_W - 36) / 2, 42);
    lv_obj_set_pos(reboot, (PANEL_W - 36) / 2 + 8, y + 10);
    lv_obj_set_style_bg_color(reboot, COL_BG, 0);
    lv_obj_set_style_border_color(reboot, COL_ALERT, 0);
    lv_obj_set_style_border_width(reboot, 1, 0);
    lv_obj_set_style_radius(reset, 8, 0);
    lv_obj_add_event_cb(reboot, on_reboot, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rbl = lv_label_create(reboot);
    lv_label_set_text(rbl, "Restart");
    lv_obj_set_style_text_font(rbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rbl, COL_ALERT, 0);
    lv_obj_center(rbl);

    /* --- Sound --- */
    lv_obj_t *right = make_panel(s_tabs[1], 0, 8, PANEL_W, 720);
    y = 0;

    row_label(right, y, "Weather Alert Volume");
    w_vol_val = value_label(right, y, "");
    lv_label_set_text_fmt(w_vol_val, "%u%%", c.alert_volume);
    w_vol_slider = lv_slider_create(right);
    lv_obj_set_size(w_vol_slider, PANEL_W - 28, 12);
    lv_obj_set_pos(w_vol_slider, 0, y + 36);
    lv_slider_set_range(w_vol_slider, 0, 100);
    lv_slider_set_value(w_vol_slider, c.alert_volume, LV_ANIM_OFF);
    style_slider(w_vol_slider);
    lv_obj_add_event_cb(w_vol_slider, on_alert_volume, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    row_label(right, y, "Notification Volume");
    w_notif_vol_val = value_label(right, y, "");
    lv_label_set_text_fmt(w_notif_vol_val, "%u%%", c.notification_volume);
    w_notif_vol_slider = lv_slider_create(right);
    lv_obj_set_size(w_notif_vol_slider, PANEL_W - 28, 12);
    lv_obj_set_pos(w_notif_vol_slider, 0, y + 36);
    lv_slider_set_range(w_notif_vol_slider, 0, 100);
    lv_slider_set_value(w_notif_vol_slider, c.notification_volume, LV_ANIM_OFF);
    style_slider(w_notif_vol_slider);
    lv_obj_add_event_cb(w_notif_vol_slider, on_notification_volume, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    row_label(right, y, "Audible Siren (1050 Hz)");
    w_alert_siren_sw = lv_switch_create(right);
    lv_obj_set_size(w_alert_siren_sw, 64, 34);
    lv_obj_align(w_alert_siren_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.alert_siren_enabled) {
        lv_obj_add_state(w_alert_siren_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(w_alert_siren_sw, on_alert_siren_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    row_label(right, y, "Lightning Proximity Sound");
    w_ltg_sound_sw = lv_switch_create(right);
    lv_obj_set_size(w_ltg_sound_sw, 64, 34);
    lv_obj_align(w_ltg_sound_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.lightning_alert_sound) {
        lv_obj_add_state(w_ltg_sound_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(w_ltg_sound_sw, on_ltg_sound_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    row_label(right, y, "Lightning Voice Alert");
    w_ltg_voice_sw = lv_switch_create(right);
    lv_obj_set_size(w_ltg_voice_sw, 64, 34);
    lv_obj_align(w_ltg_voice_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.lightning_alert_voice) {
        lv_obj_add_state(w_ltg_voice_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(w_ltg_voice_sw, on_ltg_voice_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    lv_obj_t *test_btn = lv_button_create(right);
    lv_obj_set_size(test_btn, (PANEL_W - 36) / 2, 42);
    lv_obj_set_pos(test_btn, 0, y + 4);
    lv_obj_set_style_bg_color(test_btn, COL_BG, 0);
    lv_obj_set_style_border_color(test_btn, COL_CARD, 0);
    lv_obj_set_style_border_width(test_btn, 1, 0);
    lv_obj_set_style_radius(test_btn, 8, 0);
    lv_obj_add_event_cb(test_btn, on_test_siren, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tbl = lv_label_create(test_btn);
    lv_label_set_text(tbl, LV_SYMBOL_VOLUME_MAX " Test Siren");
    lv_obj_set_style_text_font(tbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tbl, COL_ACCENT, 0);
    lv_obj_center(tbl);

    lv_obj_t *voice_btn = lv_button_create(right);
    lv_obj_set_size(voice_btn, (PANEL_W - 36) / 2, 42);
    lv_obj_set_pos(voice_btn, (PANEL_W - 36) / 2 + 8, y + 4);
    lv_obj_set_style_bg_color(voice_btn, COL_BG, 0);
    lv_obj_set_style_border_color(voice_btn, COL_CARD, 0);
    lv_obj_set_style_border_width(voice_btn, 1, 0);
    lv_obj_set_style_radius(voice_btn, 8, 0);
    lv_obj_add_event_cb(voice_btn, on_test_voice_alert, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vbl = lv_label_create(voice_btn);
    lv_label_set_text(vbl, LV_SYMBOL_AUDIO " Test Alert");
    lv_obj_set_style_text_font(vbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(vbl, COL_ACCENT, 0);
    lv_obj_center(vbl);
    y += ROW_H + 8;

    row_label(right, y, "Hourly Chime (8am-8pm)");
    w_hourly_chime_sw = lv_switch_create(right);
    lv_obj_set_size(w_hourly_chime_sw, 64, 34);
    lv_obj_align(w_hourly_chime_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.hourly_chime_enabled) lv_obj_add_state(w_hourly_chime_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w_hourly_chime_sw, on_hourly_chime_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    row_label(right, y, "Morning Voice Briefing");
    w_morning_brief_sw = lv_switch_create(right);
    lv_obj_set_size(w_morning_brief_sw, 64, 34);
    lv_obj_align(w_morning_brief_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.morning_briefing_enabled) lv_obj_add_state(w_morning_brief_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w_morning_brief_sw, on_morning_brief_toggle, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *brief_btn = lv_button_create(right);
    lv_obj_set_size(brief_btn, 108, 34);
    lv_obj_align(brief_btn, LV_ALIGN_TOP_RIGHT, -72, y + 4);
    lv_obj_set_style_bg_color(brief_btn, COL_BG, 0);
    lv_obj_set_style_border_color(brief_btn, COL_CARD, 0);
    lv_obj_set_style_border_width(brief_btn, 1, 0);
    lv_obj_set_style_radius(brief_btn, 8, 0);
    lv_obj_add_event_cb(brief_btn, on_test_morning_briefing, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bbl = lv_label_create(brief_btn);
    lv_label_set_text(bbl, LV_SYMBOL_AUDIO " Test");
    lv_obj_set_style_text_font(bbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bbl, COL_ACCENT, 0);
    lv_obj_center(bbl);
    y += ROW_H + 4;

    row_label(right, y, "Night DND (Critical Alerts Only)");
    w_night_dnd_sw = lv_switch_create(right);
    lv_obj_set_size(w_night_dnd_sw, 64, 34);
    lv_obj_align(w_night_dnd_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.night_alert_dnd) lv_obj_add_state(w_night_dnd_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w_night_dnd_sw, on_night_dnd_toggle, LV_EVENT_VALUE_CHANGED, NULL);

    /* --- Network --- */
    right = make_panel(s_tabs[2], 0, 8, PANEL_W, 480);
    y = 0;

    row_label(right, y, "Wi-Fi");
    w_wifi_sub = lv_label_create(right);
    lv_obj_set_style_text_font(w_wifi_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w_wifi_sub, COL_DIM, 0);
    lv_label_set_text(w_wifi_sub, "");
    lv_obj_set_pos(w_wifi_sub, 0, y + 30);

    lv_obj_t *wifi_btn = lv_button_create(right);
    lv_obj_set_size(wifi_btn, 140, 40);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    lv_obj_set_style_bg_color(wifi_btn, COL_BG, 0);
    lv_obj_set_style_radius(wifi_btn, 8, 0);
    lv_obj_add_event_cb(wifi_btn, on_wifi, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wl = lv_label_create(wifi_btn);
    lv_label_set_text(wl, LV_SYMBOL_WIFI "  Reconnect");
    lv_obj_set_style_text_font(wl, &lv_font_montserrat_16, 0);
    lv_obj_center(wl);
    y += ROW_H + 4;

    row_label(right, y, "NOAA Alert Zip Code");
    w_zip_ta = lv_textarea_create(right);
    lv_obj_set_size(w_zip_ta, 140, 40);
    lv_obj_align(w_zip_ta, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    lv_textarea_set_text(w_zip_ta, c.alert_zipcode[0] ? c.alert_zipcode : "66030");
    lv_textarea_set_max_length(w_zip_ta, 5);
    lv_textarea_set_one_line(w_zip_ta, true);
    lv_obj_set_style_bg_color(w_zip_ta, COL_BG, 0);
    lv_obj_set_style_text_font(w_zip_ta, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(w_zip_ta, COL_TEXT, 0);
    lv_obj_add_event_cb(w_zip_ta, on_zip_changed, LV_EVENT_ALL, NULL);
    y += ROW_H + 4;

    row_label(right, y, "Local Web Dashboard");
    w_web_server_sw = lv_switch_create(right);
    lv_obj_set_size(w_web_server_sw, 64, 34);
    lv_obj_align(w_web_server_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.web_server_enabled) lv_obj_add_state(w_web_server_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w_web_server_sw, on_web_server_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    row_label(right, y, "Home Assistant MQTT");
    w_mqtt_sw = lv_switch_create(right);
    lv_obj_set_size(w_mqtt_sw, 64, 34);
    lv_obj_align(w_mqtt_sw, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.mqtt_enabled) lv_obj_add_state(w_mqtt_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(w_mqtt_sw, on_mqtt_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 4;

    row_label(right, y, "MQTT Broker IP / Host");
    w_mqtt_broker_ta = lv_textarea_create(right);
    lv_obj_set_size(w_mqtt_broker_ta, 180, 40);
    lv_obj_align(w_mqtt_broker_ta, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    lv_textarea_set_text(w_mqtt_broker_ta, c.mqtt_broker[0] ? c.mqtt_broker : "192.168.1.100");
    lv_textarea_set_max_length(w_mqtt_broker_ta, 63);
    lv_textarea_set_one_line(w_mqtt_broker_ta, true);
    lv_obj_set_style_bg_color(w_mqtt_broker_ta, COL_BG, 0);
    lv_obj_set_style_text_font(w_mqtt_broker_ta, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(w_mqtt_broker_ta, on_mqtt_broker_changed, LV_EVENT_ALL, NULL);
    y += ROW_H + 8;

    w_net_status = lv_label_create(right);
    lv_obj_set_style_text_font(w_net_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w_net_status, COL_DIM, 0);
    lv_obj_set_width(w_net_status, PANEL_W - 28);
    lv_label_set_long_mode(w_net_status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(w_net_status, "NWS --\nMQTT --\nOTA --");
    lv_obj_set_pos(w_net_status, 0, y);

    lv_obj_t *alert_panel = make_panel(s_tabs[2], PANEL_W + 16, 8, PANEL_W, 400);
    row_label(alert_panel, 0, "Current Alert");
    w_alert_event = lv_label_create(alert_panel);
    lv_obj_set_style_text_font(w_alert_event, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(w_alert_event, COL_DIM, 0);
    lv_label_set_text(w_alert_event, "All clear");
    lv_obj_set_pos(w_alert_event, 0, 36);
    lv_obj_set_width(w_alert_event, PANEL_W - 28);

    w_alert_body = lv_label_create(alert_panel);
    lv_obj_set_style_text_font(w_alert_body, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w_alert_body, COL_DIM, 0);
    lv_obj_set_width(w_alert_body, PANEL_W - 28);
    lv_label_set_long_mode(w_alert_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(w_alert_body, "No active NWS alert. Tap the ticker on any page to open Alerts.");
    lv_obj_set_pos(w_alert_body, 0, 72);

    /* --- Display extras (right column) --- */
    right = make_panel(s_tabs[0], PANEL_W + 16, 8, PANEL_W, 400);
    y = 0;

    row_label(right, y, "Wind Gauge Full Scale");
    w_windmax_val = value_label(right, y, "");
    lv_label_set_text_fmt(w_windmax_val, "%.0f %s",
                          (double)cfg_wind((float)c.wind_scale_max_ms),
                          cfg_wind_suffix());
    w_windmax = lv_slider_create(right);
    lv_obj_set_size(w_windmax, PANEL_W - 28, 12);
    lv_obj_set_pos(w_windmax, 0, y + 36);
    lv_slider_set_range(w_windmax, 5, 60);
    lv_slider_set_value(w_windmax, c.wind_scale_max_ms, LV_ANIM_OFF);
    style_slider(w_windmax);
    lv_obj_add_event_cb(w_windmax, on_windmax, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    row_label(right, y, "Indoor Temp Trim");
    w_temp_offset_val = value_label(right, y, "");
    float cur_offset_f = c.indoor_temp_offset_c * 1.8f;
    if (c.units == CFG_UNITS_METRIC) {
        lv_label_set_text_fmt(w_temp_offset_val, "%+.1f °C", (double)c.indoor_temp_offset_c);
    } else {
        lv_label_set_text_fmt(w_temp_offset_val, "%+.1f °F", (double)cur_offset_f);
    }
    w_temp_offset = lv_slider_create(right);
    lv_obj_set_size(w_temp_offset, PANEL_W - 28, 12);
    lv_obj_set_pos(w_temp_offset, 0, y + 36);
    lv_slider_set_range(w_temp_offset, -180, 100);
    int32_t init_slider_val = (int32_t)(cur_offset_f * 10.0f + (cur_offset_f >= 0 ? 0.5f : -0.5f));
    lv_slider_set_value(w_temp_offset, init_slider_val, LV_ANIM_OFF);
    style_slider(w_temp_offset);
    lv_obj_add_event_cb(w_temp_offset, on_temp_offset, LV_EVENT_VALUE_CHANGED, NULL);
    y += ROW_H + 8;

    row_label(right, y, "Animate 7-Day Forecast Icons");
    w_animate = lv_switch_create(right);
    lv_obj_set_size(w_animate, 64, 34);
    lv_obj_align(w_animate, LV_ALIGN_TOP_RIGHT, 0, y + 4);
    if (c.animate_forecast) {
        lv_obj_add_state(w_animate, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(w_animate, on_animate_toggle, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *anim_hint = lv_label_create(right);
    lv_obj_set_style_text_font(anim_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(anim_hint, COL_DIM, 0);
    lv_label_set_text(anim_hint, "7-day row is stills; Current Conditions still animates");
    lv_obj_set_pos(anim_hint, 0, y + 42);
    lv_obj_set_width(anim_hint, PANEL_W - 28);

    /* --- System --- */
    lv_obj_t *station = make_panel(s_tabs[3], 0, 8, PANEL_W, 480);
    y = 0;
    row_label(station, y, "Station");
    w_station = lv_label_create(station);
    lv_obj_set_style_text_font(w_station, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w_station, COL_DIM, 0);
    lv_label_set_text(w_station, "checking...");
    lv_obj_set_pos(w_station, 0, y + 36);
    lv_obj_set_width(w_station, PANEL_W - 28);

    right = make_panel(s_tabs[3], PANEL_W + 16, 8, PANEL_W, 400);
    y = 0;

    row_label(right, y, "microSD Card");
    /* Buttons on the label's line, status text on its OWN line underneath.
     * They used to share a line: the status ran full width at y+30 while the
     * buttons occupied y+2..y+36, so the text printed straight through them. */
    lv_obj_t *sd_rescan = lv_button_create(right);
    lv_obj_set_size(sd_rescan, 96, 34);
    lv_obj_align(sd_rescan, LV_ALIGN_TOP_RIGHT, -140, y - 4);
    lv_obj_set_style_bg_color(sd_rescan, COL_ACCENT, 0);
    lv_obj_add_event_cb(sd_rescan, on_sd_remount, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sd_rl = lv_label_create(sd_rescan);
    lv_label_set_text(sd_rl, "Rescan");
    lv_obj_set_style_text_color(sd_rl, COL_BG, 0);
    lv_obj_center(sd_rl);

    w_sd_btn = lv_button_create(right);
    lv_obj_set_size(w_sd_btn, 132, 34);
    lv_obj_align(w_sd_btn, LV_ALIGN_TOP_RIGHT, 0, y - 4);
    lv_obj_set_style_bg_color(w_sd_btn, COL_ACCENT, 0);
    lv_obj_add_event_cb(w_sd_btn, on_sd_format, LV_EVENT_CLICKED, NULL);
    w_sd_btn_lbl = lv_label_create(w_sd_btn);
    lv_label_set_text(w_sd_btn_lbl, "Format");
    lv_obj_set_style_text_color(w_sd_btn_lbl, COL_BG, 0);
    lv_obj_center(w_sd_btn_lbl);

    w_sd = lv_label_create(right);
    lv_obj_set_style_text_font(w_sd, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w_sd, COL_DIM, 0);
    lv_label_set_long_mode(w_sd, LV_LABEL_LONG_DOT);
    lv_obj_set_width(w_sd, PANEL_W - 28);
    lv_label_set_text(w_sd, "checking...");
    lv_obj_set_pos(w_sd, 0, y + 34);

    y += ROW_H + 20;

    row_label(right, y, "Firmware");
    w_diag = lv_label_create(right);
    lv_obj_set_style_text_font(w_diag, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w_diag, COL_DIM, 0);
    lv_label_set_text(w_diag, "checking...");
    lv_obj_set_pos(w_diag, 0, y + 36);

    /* Numeric on-screen keyboard for zip code entry */
    w_kb = lv_keyboard_create(s_screen);
    lv_keyboard_set_mode(w_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(w_kb, w_zip_ta);
    lv_obj_set_size(w_kb, 400, 200);
    lv_obj_align(w_kb, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(w_kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(s_screen, on_settings_activity, LV_EVENT_PRESSED, NULL);
    show_tab(0);

    return ESP_OK;
}

static bool s_settings_force;

void settings_show(void)
{
    if (!s_screen) {
        return;
    }
    s_settings_force = true;
    net_wifi_ui_active(true);
    sync_settings_toggles();
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_screen);
    ui_ticker_raise();
    lv_obj_invalidate(s_screen);
    settings_tick();
}

void settings_hide(void)
{
    if (!s_screen) {
        return;
    }
    if (w_kb) {
        lv_obj_add_flag(w_kb, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    if (!wifi_setup_is_visible()) {
        net_wifi_ui_active(false);
    }
    lv_obj_t *scr = lv_screen_active();
    if (scr) {
        lv_obj_invalidate(scr);
    }
}

bool settings_is_visible(void)
{
    return s_screen && !lv_obj_has_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}

void settings_tick(void)
{
    if (!settings_is_visible()) {
        return;
    }

    if (w_clock) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        char clk[16];
        strftime(clk, sizeof(clk), "%I:%M %p", &lt);
        if (clk[0] == '0') {
            memmove(clk, clk + 1, strlen(clk));
        }
        ui_label_set(w_clock, clk);
    }

    if (w_alert_event && w_alert_body) {
        nws_alert_t alert = {0};
        if (nws_alerts_get_active(&alert) && alert.event[0]) {
            ui_label_set(w_alert_event, alert.event);
            lv_obj_set_style_text_color(w_alert_event, COL_ALERT, 0);
            char until[40];
            nws_format_until(alert.expires_epoch, until, sizeof(until));
            char body[320];
            if (until[0] && alert.instruction[0]) {
                snprintf(body, sizeof(body), "Until %s\n%s", until, alert.instruction);
            } else if (alert.headline[0]) {
                snprintf(body, sizeof(body), "%s", alert.headline);
            } else {
                snprintf(body, sizeof(body), "Active");
            }
            ui_label_set(w_alert_body, body);
        } else {
            nws_forecast_t fc = {0};
            if (nws_forecast_get(&fc) && fc.short_fc[0]) {
                ui_label_set(w_alert_event, fc.period[0] ? fc.period : "NWS forecast");
                lv_obj_set_style_text_color(w_alert_event, COL_ACCENT, 0);
                char body[384];
                snprintf(body, sizeof(body), "%.95s%s%.200s\nOffice %.7s  radar %.7s",
                         fc.short_fc,
                         fc.detailed[0] ? "\n" : "",
                         fc.detailed,
                         fc.office[0] ? fc.office : "--",
                         fc.radar[0] ? fc.radar : "--");
                ui_label_set(w_alert_body, body);
            } else {
                ui_label_set(w_alert_event, "All clear");
                lv_obj_set_style_text_color(w_alert_event, COL_DIM, 0);
                ui_label_set(w_alert_body,
                             "No active NWS alert. Tap the ticker on any page to open Alerts.");
            }
        }
    }

    if (w_wifi_sub) {
        char wifi[80];
        if (net_is_connected()) {
            const char *ssid = net_current_ssid();
            int8_t rssi = net_get_rssi();
            if (rssi != 0) {
                snprintf(wifi, sizeof(wifi), "connected to %s  (%d dBm)",
                         (ssid && ssid[0]) ? ssid : "network", (int)rssi);
            } else {
                snprintf(wifi, sizeof(wifi), "connected to %s",
                         (ssid && ssid[0]) ? ssid : "network");
            }
        } else {
            snprintf(wifi, sizeof(wifi), "not connected");
        }
        ui_label_set(w_wifi_sub, wifi);
    }

    if (w_sd) {
        sdcard_info_t sd;
        sdcard_fmt_state_t fs = sdcard_format_state();

        /* Arming lapses on its own, so a half-pressed Format does not sit
         * there waiting to catch the next person who touches the screen. */
        if (s_fmt_armed && lv_tick_elaps(s_fmt_armed_at) > 5000) {
            s_fmt_armed = false;
        }

        if (fs == SDCARD_FMT_BUSY) {
            ui_label_set(w_sd, "formatting -- do not remove the card");
            ui_label_set(w_sd_btn_lbl, "Working");
        } else if (sdcard_get_info(&sd) == ESP_OK) {
            char sd_s[128];
            snprintf(sd_s, sizeof(sd_s),
                "%s  -  %llu of %llu MB free  -  %lu month%s logged",
                sd.name,
                (unsigned long long)(sd.free_bytes / (1024 * 1024)),
                (unsigned long long)(sd.total_bytes / (1024 * 1024)),
                (unsigned long)sd.log_files,
                sd.log_files == 1 ? "" : "s");
            ui_label_set(w_sd, sd_s);
            ui_label_set(w_sd_btn_lbl, s_fmt_armed ? "Erase all?" : "Format");
        } else if (fs == SDCARD_FMT_FAILED) {
            ui_label_set(w_sd, "format failed -- card may be faulty");
            ui_label_set(w_sd_btn_lbl, "Format");
        } else {
            ui_label_set(w_sd,
                "no card, or not FAT. Insert one and press Rescan.");
            ui_label_set(w_sd_btn_lbl,
                         s_fmt_armed ? "Erase all?" : "Format");
        }

        lv_obj_set_style_bg_color(w_sd_btn,
                                  s_fmt_armed ? COL_ALERT : COL_ACCENT, 0);
        lv_obj_set_style_text_color(w_sd_btn_lbl,
                                    s_fmt_armed ? COL_TEXT : COL_BG, 0);
    }

    static uint32_t s_sys_ms;
    uint32_t t = lv_tick_get();
    if (!s_settings_force && (t - s_sys_ms) < 2000) {
        return;
    }
    s_settings_force = false;
    s_sys_ms = t;

    if (w_station || w_diag) {
        const esp_app_desc_t *app = esp_app_get_description();
        wx_state_t s;
        wx_snapshot(&s);
        uint32_t pkts = tempest_udp_packet_count();
        int64_t now = (int64_t)time(NULL);
        int64_t udp_age = (s.last_udp_epoch > 0) ? (now - s.last_udp_epoch) : -1;
        int64_t obs_age = (s.obs_epoch > 0) ? (now - s.obs_epoch) : -1;
        char udp_a[12], obs_a[12], up[24];
        ui_fmt_age(udp_a, sizeof(udp_a), udp_age);
        ui_fmt_age(obs_a, sizeof(obs_a), obs_age);
        if (s.hub_uptime_s < 3600) {
            snprintf(up, sizeof(up), "%um", (unsigned)(s.hub_uptime_s / 60));
        } else if (s.hub_uptime_s < 86400) {
            snprintf(up, sizeof(up), "%uh %um",
                     (unsigned)(s.hub_uptime_s / 3600),
                     (unsigned)((s.hub_uptime_s % 3600) / 60));
        } else {
            snprintf(up, sizeof(up), "%ud %uh",
                     (unsigned)(s.hub_uptime_s / 86400),
                     (unsigned)((s.hub_uptime_s % 86400) / 3600));
        }

        char web_url[72];
        char ip[16];
        if (net_get_ip(ip, sizeof(ip))) {
            snprintf(web_url, sizeof(web_url), "http://%s:8080", ip);
        } else if (net_is_connected()) {
            snprintf(web_url, sizeof(web_url), "http://tempest.local:8080");
        } else {
            snprintf(web_url, sizeof(web_url), "not available");
        }

        const char *link = wx_obs_is_stale(&s) ? "stale"
                           : (s.obs_valid ? "live" : "waiting");
        const char *indoor = s.indoor_valid
            ? (wx_indoor_is_stale(&s) ? "sensor stale" : "sensor active")
            : "no sensor";

        nws_alert_t nws = {0};
        bool nws_on = nws_alerts_get_active(&nws) && nws.event[0];
        char nws_line[128];
        if (nws_on) {
            nws_format_banner(&nws, nws_line, sizeof(nws_line));
        } else {
            nws_forecast_t nfc = {0};
            if (nws_forecast_get(&nfc) && nfc.short_fc[0]) {
                snprintf(nws_line, sizeof(nws_line), "%.80s  radar %.7s",
                         nfc.short_fc, nfc.radar[0] ? nfc.radar : "--");
            } else {
                snprintf(nws_line, sizeof(nws_line), "All clear");
            }
        }
        char fc_a[12];
        int64_t fc_age = (s.forecast_fetched_epoch > 0)
            ? (now - s.forecast_fetched_epoch) : -1;
        ui_fmt_age(fc_a, sizeof(fc_a), fc_age);

        if (w_net_status) {
            const char *mqtt;
            cfg_t cfg;
            cfg_get(&cfg);
            if (!cfg.mqtt_enabled) {
                mqtt = "off";
            } else if (mqtt_app_is_connected()) {
                mqtt = "connected";
            } else {
                mqtt = "connecting";
            }
            char ota_url[72];
            if (net_get_ip(ip, sizeof(ip))) {
                snprintf(ota_url, sizeof(ota_url), "http://%s:8080/ota", ip);
            } else {
                snprintf(ota_url, sizeof(ota_url), "not available");
            }
            char ns[280];
            snprintf(ns, sizeof(ns),
                     "NWS  %s\nMQTT %s\nOTA  Update at %s",
                     nws_line,
                     mqtt, ota_url);
            ui_label_set(w_net_status, ns);
        }

        if (w_station) {
            char st[720];
            snprintf(st, sizeof(st),
                "link      %s  (obs %s)\n"
                "battery   %.2f V\n"
                "hub rssi  %d dBm  up %s\n"
                "udp       %lu packets  last %s\n"
                "indoor    %s\n"
                "wifi      %s\n"
                "web       %s\n"
                "forecast  %s\n"
                "API token %s\n"
                "NWS       %s",
                link, obs_a,
                (double)s.battery_v, s.hub_rssi, up,
                (unsigned long)pkts, udp_a,
                indoor,
                net_is_connected() ? "connected" : "disconnected",
                web_url,
                fc_a,
                tempest_rest_has_token() ? "yes" : "no",
                nws_line);
            ui_label_set(w_station, st);
        }

        if (w_diag) {
            char dg[200];
            snprintf(dg, sizeof(dg),
                "firmware  %s (%s)\n"
                "memory    %u KB internal / %u KB psram\n"
                "udp age   %s\n"
                "obs age   %s\n"
                "forecast  %s",
                app->version, app->date,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
                udp_a, obs_a, fc_a);
            ui_label_set(w_diag, dg);
        }
    }
}
