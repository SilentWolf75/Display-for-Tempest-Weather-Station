#include "wifi_setup.h"
#include "config.h"
#include "net.h"
#include "display.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "wifi_ui";

#define COL_BG        lv_color_hex(0x0B0E13)
#define COL_CARD      lv_color_hex(0x151A22)
#define COL_TEXT      lv_color_hex(0xE8EDF2)
#define COL_DIM       lv_color_hex(0x7E8B99)
#define COL_ACCENT    lv_color_hex(0x4FC3F7)
#define COL_OK        lv_color_hex(0x66BB6A)
#define COL_ALERT     lv_color_hex(0xEF5350)

#define MAX_APS       24
#define SCAN_STACK    4096

static lv_obj_t *s_screen;
static lv_obj_t *s_prev;
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_scan_btn;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_connect_btn;
static lv_obj_t *s_chosen_lbl;

static char       s_chosen_ssid[CFG_SSID_LEN];
static net_ap_t   s_aps[MAX_APS];
static int        s_ap_count;
static volatile bool s_scanning;

/* ------------------------------------------------------------------------ */

static void set_status(const char *text, lv_color_t colour)
{
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, colour, 0);
}

/* Signal strength as something a person can act on, rather than a dBm number
 * that means nothing to most people. */
static const char *signal_words(int8_t rssi)
{
    if (rssi >= -55) return "excellent";
    if (rssi >= -67) return "good";
    if (rssi >= -75) return "fair";
    return "weak";
}

static void on_ap_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_event_get_user_data(e);

    strncpy(s_chosen_ssid, ssid, sizeof(s_chosen_ssid) - 1);
    s_chosen_ssid[sizeof(s_chosen_ssid) - 1] = '\0';
    (void)btn;

    lv_label_set_text_fmt(s_chosen_lbl, "Password for %s", s_chosen_ssid);
    lv_obj_clear_flag(s_chosen_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pass_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_connect_btn, LV_OBJ_FLAG_HIDDEN);

    lv_textarea_set_text(s_pass_ta, "");
    lv_keyboard_set_textarea(s_keyboard, s_pass_ta);
    set_status("enter the password, then Connect", COL_DIM);
}

/* Runs outside the LVGL task: net_scan() blocks for about two seconds. */
static void scan_task(void *arg)
{
    (void)arg;
    int n = net_scan(s_aps, MAX_APS);

    if (display_lock(-1)) {
        lv_obj_clean(s_list);
        s_ap_count = (n > 0) ? n : 0;

        if (n < 0) {
            set_status("scan failed -- is the radio up?", COL_ALERT);
        } else if (n == 0) {
            set_status("no networks found", COL_ALERT);
        } else {
            for (int i = 0; i < n; i++) {
                char row[80];
                snprintf(row, sizeof(row), "%s   (%s%s)",
                         s_aps[i].ssid, signal_words(s_aps[i].rssi),
                         s_aps[i].secure ? "" : ", open");
                lv_obj_t *btn = lv_list_add_button(
                    s_list, s_aps[i].secure ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_OPEN,
                    row);
                lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0);
                /* s_aps outlives the buttons, so pointing at it is safe. */
                lv_obj_add_event_cb(btn, on_ap_clicked, LV_EVENT_CLICKED,
                                    s_aps[i].ssid);
            }
            set_status("pick a network", COL_DIM);
        }
        lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
        display_unlock();
    }

    s_scanning = false;
    vTaskDelete(NULL);
}

static void on_scan(lv_event_t *e)
{
    (void)e;
    if (s_scanning) {
        return;
    }
    s_scanning = true;
    lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    set_status("scanning...", COL_ACCENT);

    if (xTaskCreate(scan_task, "wifi_scan", SCAN_STACK, NULL, 4, NULL)
            != pdPASS) {
        s_scanning = false;
        lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
        set_status("could not start scan", COL_ALERT);
    }
}

static void on_connect(lv_event_t *e)
{
    (void)e;
    if (s_chosen_ssid[0] == '\0') {
        set_status("pick a network first", COL_ALERT);
        return;
    }
    const char *pass = lv_textarea_get_text(s_pass_ta);

    esp_err_t err = net_apply_credentials(s_chosen_ssid, pass);
    if (err == ESP_OK) {
        lv_label_set_text_fmt(s_status, "connecting to %s...", s_chosen_ssid);
        lv_obj_set_style_text_color(s_status, COL_ACCENT, 0);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    } else {
        set_status("could not apply credentials", COL_ALERT);
        ESP_LOGE(TAG, "net_apply_credentials: %s", esp_err_to_name(err));
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    wifi_setup_hide();
}

/* ------------------------------------------------------------------------ */

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h,
                             const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, COL_CARD, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_center(l);
    return b;
}

esp_err_t wifi_setup_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_pos(title, 24, 18);

    make_button(s_screen, 1024 - 150 - 24, 14, 150, 50,
                LV_SYMBOL_LEFT "  Done", on_back);
    s_scan_btn = make_button(s_screen, 150, 16, 130, 46, "Scan", on_scan);

    s_status = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_status, COL_DIM, 0);
    lv_label_set_text(s_status, "");
    lv_obj_set_pos(s_status, 300, 30);

    /* Left: the network list. Right: password entry. */
    s_list = lv_list_create(s_screen);
    lv_obj_set_pos(s_list, 24, 78);
    lv_obj_set_size(s_list, 470, 480);
    lv_obj_set_style_bg_color(s_list, COL_CARD, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 12, 0);

    s_chosen_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_chosen_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_chosen_lbl, COL_TEXT, 0);
    lv_label_set_text(s_chosen_lbl, "");
    lv_obj_set_pos(s_chosen_lbl, 516, 84);
    lv_obj_add_flag(s_chosen_lbl, LV_OBJ_FLAG_HIDDEN);

    s_pass_ta = lv_textarea_create(s_screen);
    lv_obj_set_pos(s_pass_ta, 516, 116);
    lv_obj_set_size(s_pass_ta, 484, 52);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, "password");
    lv_obj_set_style_text_font(s_pass_ta, &lv_font_montserrat_18, 0);
    lv_obj_add_flag(s_pass_ta, LV_OBJ_FLAG_HIDDEN);

    s_connect_btn = make_button(s_screen, 516, 180, 160, 48,
                                "Connect", on_connect);
    lv_obj_set_style_bg_color(s_connect_btn, COL_ACCENT, 0);
    lv_obj_add_flag(s_connect_btn, LV_OBJ_FLAG_HIDDEN);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_obj_set_pos(s_keyboard, 516, 240);
    lv_obj_set_size(s_keyboard, 484, 318);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "wi-fi setup screen built");
    return ESP_OK;
}

void wifi_setup_tick(void)
{
    if (!wifi_setup_is_visible() || s_scanning) {
        return;
    }
    /* Only overwrite the status line once a connection attempt has settled,
     * so it does not stamp on "scanning..." or a validation message. */
    if (net_is_connected()) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  connected to %s",
                 net_current_ssid());
        set_status(buf, COL_OK);
    }
}

void wifi_setup_show(void)
{
    if (!s_screen) {
        return;
    }
    s_prev = lv_screen_active();

    if (net_is_connected()) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  connected to %s",
                 net_current_ssid());
        set_status(buf, COL_OK);
    } else if (net_current_ssid()[0]) {
        char buf[64];
        snprintf(buf, sizeof(buf), "not connected (configured: %s)",
                 net_current_ssid());
        set_status(buf, COL_DIM);
    } else {
        set_status("no network configured -- tap Scan", COL_DIM);
    }

    lv_screen_load(s_screen);
}

void wifi_setup_hide(void)
{
    if (s_prev) {
        lv_screen_load(s_prev);
    }
}

bool wifi_setup_is_visible(void)
{
    return s_screen && lv_screen_active() == s_screen;
}
