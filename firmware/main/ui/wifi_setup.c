#include "wifi_setup.h"
#include "ui.h"
#include "config.h"
#include "net.h"
#include "audio.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "wifi_ui";

bool settings_is_visible(void);

/* Match the dashboard palette - dark surfaces, bright text, muted accents. */
#define COL_BG          lv_color_hex(0x06090E)
#define COL_CARD        lv_color_hex(0x0F141C)
#define COL_ROW         lv_color_hex(0x141B26)
#define COL_BORDER      lv_color_hex(0x2A3544)
#define COL_TEXT        lv_color_hex(0xF8FAFC)
#define COL_DIM         lv_color_hex(0x94A3B8)
#define COL_FAINT       lv_color_hex(0x64748B)
#define COL_BTN         lv_color_hex(0x1D4ED8)
#define COL_BTN_DIM     lv_color_hex(0x1E293B)
/* Android-like keyboard palette */
#define COL_KB_BG       lv_color_hex(0x000000)
#define COL_KEY         lv_color_hex(0x464646)
#define COL_KEY_FN      lv_color_hex(0x363636)
#define COL_KEY_SHIFT   lv_color_hex(0x1A73E8)
#define COL_KEY_PRESS   lv_color_hex(0x5F6368)
#define COL_FOCUS       lv_color_hex(0x38BDF8)
#define COL_OK          lv_color_hex(0x4ADE80)
#define COL_ALERT       lv_color_hex(0xF87171)
#define COL_SELECTED_BG lv_color_hex(0x172554)
#define COL_SELECTED_BD lv_color_hex(0x3B82F6)

#define MAX_APS       24
#define SCAN_STACK    8192
#define SCAN_PRIO     2
#define CONNECT_STACK 8192
#define CONNECT_PRIO  2
#define CONNECT_WAIT_S  20
#define SHIFT_DBL_MS    400
#define SHIFT_BTN_SCAN  48
#define KB_SHIFT_BTN    21

/* Android/Gboard shift glyph: arrow with baseline (Font Awesome upload icon). */
#define KB_SHIFT_SYM    LV_SYMBOL_UPLOAD

typedef enum {
    SHIFT_OFF = 0,
    SHIFT_ONE,
    SHIFT_LOCK,
} shift_state_t;

static shift_state_t s_shift;
static uint32_t        s_shift_tap_ms;

/* Android-style QWERTY + number row (password-friendly). */
static const char *const s_kb_lower_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    KB_SHIFT_SYM, "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "&@#", "z", "x", "c", "v", "b", "n", "m", ".", "-", "_", "\n",
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

static const char *const s_kb_upper_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    KB_SHIFT_SYM, "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "&@#", "Z", "X", "C", "V", "B", "N", "M", ".", "-", "_", "\n",
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

#define KB_W1  ((lv_buttonmatrix_ctrl_t)1)
#define KB_W2  ((lv_buttonmatrix_ctrl_t)2)
#define KB_W6  ((lv_buttonmatrix_ctrl_t)6)
#define KB_W7  ((lv_buttonmatrix_ctrl_t)7)
#define KB_BS  ((lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_CHECKED | 2))
#define KB_OK  ((lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_CHECKED | 2))

static const lv_buttonmatrix_ctrl_t s_kb_lower_ctrl[] = {
    KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_BS,
    KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1,
    KB_W2, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1,
    KB_W2, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1,
    KB_W2, KB_W6, KB_OK,
};

static const char *const s_kb_sym_map[] = {
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", LV_SYMBOL_BACKSPACE, "\n",
    "-", "_", "+", "=", "[", "]", "{", "}", "\\", "|", "\n",
    "abc", "'", "\"", ";", ":", ",", ".", "/", "?", "\n",
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_kb_sym_ctrl[] = {
    KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_BS,
    KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1,
    KB_W2, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1, KB_W1,
    KB_W2, KB_W6, KB_OK,
};

static lv_obj_t *s_screen;
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_scan_btn;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_connect_btn;
static lv_obj_t *s_chosen_lbl;
static lv_obj_t *s_pass_hint;
static lv_obj_t *s_show_pass_btn;
static lv_obj_t *s_kb_close_btn;
static lv_obj_t *s_selected_btn;

static char       s_chosen_ssid[CFG_SSID_LEN];
static net_ap_t   s_aps[MAX_APS];
static int        s_ap_count;
static bool       s_chosen_open;
static bool       s_pass_visible;
static bool       s_connecting;
static int64_t    s_connect_deadline;
static volatile bool s_scanning;
static volatile bool s_scan_ready;
static volatile int  s_scan_result;

/* ------------------------------------------------------------------------ */

static void on_connect(lv_event_t *e);
static void on_kb_ready(lv_event_t *e);
static void on_kb_custom(lv_event_t *e);
static void start_scan(void);
static void style_keyboard(lv_obj_t *kb);
static void style_pass_field(void);
static void set_connect_enabled(bool en);
static void flash_pass_border(void);
static void kb_click(void);
static void kb_show(void);
static void kb_hide(void);
static void shift_reset(void);
static void shift_update_visual(void);
static void kb_set_letter_case(void);
static void kb_add_char(const char *txt);
static void update_pass_hint(void);

static void set_status(const char *text, lv_color_t colour)
{
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, colour, 0);
}

static void preload_saved_network(void)
{
    const char *saved = net_current_ssid();
    if (!saved || saved[0] == '\0') {
        return;
    }
    strncpy(s_chosen_ssid, saved, sizeof(s_chosen_ssid) - 1);
    s_chosen_ssid[sizeof(s_chosen_ssid) - 1] = '\0';
    s_chosen_open = false;
    lv_label_set_text_fmt(s_chosen_lbl, "Password for %s", s_chosen_ssid);
    lv_textarea_set_text(s_pass_ta, "");
    lv_textarea_set_password_mode(s_pass_ta, !s_pass_visible);
    lv_obj_clear_state(s_pass_ta, LV_STATE_DISABLED);
    lv_textarea_set_placeholder_text(s_pass_ta, "tap here to type password");
    set_connect_enabled(true);
    update_pass_hint();
}

static void scan_deferred_cb(lv_timer_t *timer)
{
    start_scan();
    lv_timer_delete(timer);
}

static const char *signal_words(int8_t rssi)
{
    if (rssi >= -55) return "excellent";
    if (rssi >= -67) return "good";
    if (rssi >= -75) return "fair";
    return "weak";
}

static void update_pass_hint(void)
{
    if (!s_pass_hint) {
        return;
    }
    if (s_chosen_ssid[0] == '\0') {
        lv_label_set_text(s_pass_hint, "");
        return;
    }
    if (s_chosen_open) {
        lv_label_set_text(s_pass_hint, "Open network - no password needed");
        lv_obj_set_style_text_color(s_pass_hint, COL_OK, 0);
        return;
    }
    uint32_t n = lv_textarea_get_text(s_pass_ta)
                     ? strlen(lv_textarea_get_text(s_pass_ta))
                     : 0;
    lv_label_set_text_fmt(s_pass_hint, "%lu character%s entered",
                          (unsigned long)n, n == 1 ? "" : "s");
    lv_obj_set_style_text_color(s_pass_hint,
                                n >= 8 ? COL_OK : COL_DIM, 0);
}

static void clear_ap_highlight(void)
{
    if (!s_selected_btn) {
        return;
    }
    lv_obj_set_style_bg_color(s_selected_btn, COL_ROW, 0);
    lv_obj_set_style_border_color(s_selected_btn, COL_BORDER, 0);
    lv_obj_set_style_border_width(s_selected_btn, 1, 0);
    s_selected_btn = NULL;
}

static void highlight_ap(lv_obj_t *btn)
{
    clear_ap_highlight();
    s_selected_btn = btn;
    lv_obj_set_style_bg_color(btn, COL_SELECTED_BG, 0);
    lv_obj_set_style_border_color(btn, COL_SELECTED_BD, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
}

static void style_btn_label(lv_obj_t *btn, lv_color_t colour)
{
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_obj_set_style_text_color(lbl, colour, 0);
    }
}

static void set_connect_enabled(bool en)
{
    if (!s_connect_btn) {
        return;
    }
    if (en) {
        lv_obj_clear_state(s_connect_btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_connect_btn, COL_BTN, 0);
        lv_obj_set_style_bg_opa(s_connect_btn, LV_OPA_COVER, 0);
        style_btn_label(s_connect_btn, COL_TEXT);
    } else {
        lv_obj_add_state(s_connect_btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_connect_btn, COL_BTN_DIM, 0);
        lv_obj_set_style_bg_opa(s_connect_btn, LV_OPA_70, 0);
        style_btn_label(s_connect_btn, COL_FAINT);
    }
}

static void kb_show(void)
{
    if (!s_keyboard || s_chosen_open) {
        return;
    }
    shift_reset();
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    if (s_kb_close_btn) {
        lv_obj_clear_flag(s_kb_close_btn, LV_OBJ_FLAG_HIDDEN);
    }
    lv_keyboard_set_textarea(s_keyboard, s_pass_ta);
    lv_obj_move_foreground(s_keyboard);
    if (s_kb_close_btn) {
        lv_obj_move_foreground(s_kb_close_btn);
    }
}

static void kb_hide(void)
{
    if (!s_keyboard) {
        return;
    }
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    if (s_kb_close_btn) {
        lv_obj_add_flag(s_kb_close_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_pass_ta) {
        lv_obj_clear_state(s_pass_ta, LV_STATE_FOCUSED);
    }
    shift_reset();
}

static void kb_set_letter_case(void)
{
    if (!s_keyboard) {
        return;
    }
    if (lv_keyboard_get_mode(s_keyboard) != LV_KEYBOARD_MODE_TEXT_LOWER) {
        return;
    }
    const char *const *map = (s_shift == SHIFT_OFF) ? s_kb_lower_map : s_kb_upper_map;
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, map, s_kb_lower_ctrl);
    lv_obj_invalidate(s_keyboard);
}

static void shift_reset(void)
{
    s_shift = SHIFT_OFF;
    s_shift_tap_ms = 0;
    shift_update_visual();
}

static void shift_update_visual(void)
{
    if (!s_keyboard) {
        return;
    }
    kb_set_letter_case();
    if (s_shift == SHIFT_OFF) {
        lv_buttonmatrix_clear_button_ctrl(s_keyboard, KB_SHIFT_BTN,
                                          LV_BUTTONMATRIX_CTRL_CHECKED);
    } else {
        lv_buttonmatrix_set_button_ctrl(s_keyboard, KB_SHIFT_BTN,
                                        LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    lv_obj_invalidate(s_keyboard);
}

static void shift_on_press(void)
{
    uint32_t now = lv_tick_get();
    if (s_shift == SHIFT_LOCK) {
        s_shift = SHIFT_OFF;
    } else if (s_shift == SHIFT_ONE && (now - s_shift_tap_ms) < SHIFT_DBL_MS) {
        s_shift = SHIFT_LOCK;
    } else {
        s_shift = SHIFT_ONE;
    }
    s_shift_tap_ms = now;
    shift_update_visual();
}

static void shift_after_char(void)
{
    if (s_shift == SHIFT_ONE) {
        s_shift = SHIFT_OFF;
        shift_update_visual();
    }
}

static void kb_add_char(const char *txt)
{
    if (!s_pass_ta || !txt || txt[0] == '\0') {
        return;
    }
    char out[8];
    size_t n = strlen(txt);
    if (n >= sizeof(out)) {
        n = sizeof(out) - 1;
    }
    for (size_t i = 0; i < n; i++) {
        char c = txt[i];
        if (c >= 'a' && c <= 'z' &&
            (s_shift == SHIFT_ONE || s_shift == SHIFT_LOCK)) {
            c = (char)toupper((unsigned char)c);
        }
        out[i] = c;
    }
    out[n] = '\0';
    lv_textarea_add_text(s_pass_ta, out);
    shift_after_char();
    kb_click();
    flash_pass_border();
    update_pass_hint();
}

static void on_kb_close(lv_event_t *e)
{
    (void)e;
    kb_hide();
    kb_click();
}

static void flash_pass_border(void)
{
    lv_obj_set_style_border_color(s_pass_ta, COL_FOCUS, 0);
    lv_obj_set_style_border_width(s_pass_ta, 2, 0);
}

static void kb_click(void)
{
    audio_play_key_click();
}

static void on_pass_focus(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        style_pass_field();
        lv_obj_set_style_border_color(s_pass_ta, COL_FOCUS, 0);
        lv_obj_set_style_border_width(s_pass_ta, 2, 0);
        kb_show();
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_border_color(s_pass_ta, COL_BORDER, 0);
        lv_obj_set_style_border_width(s_pass_ta, 1, 0);
    }
}

static void on_pass_changed(lv_event_t *e)
{
    (void)e;
    update_pass_hint();
    flash_pass_border();
}

static void on_show_pass(lv_event_t *e)
{
    (void)e;
    s_pass_visible = !s_pass_visible;
    lv_textarea_set_password_mode(s_pass_ta, !s_pass_visible);
    lv_obj_t *lbl = lv_obj_get_child(s_show_pass_btn, 0);
    if (lbl) {
        lv_label_set_text(lbl, s_pass_visible ? LV_SYMBOL_EYE_CLOSE
                                              : LV_SYMBOL_EYE_OPEN);
    }
    kb_click();
}

static void on_kb_custom(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    uint32_t btn_id = lv_buttonmatrix_get_selected_button(kb);
    if (btn_id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }

    const char *txt = lv_buttonmatrix_get_button_text(kb, btn_id);
    if (!txt) {
        return;
    }

    if (btn_id == KB_SHIFT_BTN) {
        shift_on_press();
        kb_click();
        return;
    }
    if (strcmp(txt, "&@#") == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
        kb_click();
        return;
    }
    if (strcmp(txt, "abc") == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        kb_set_letter_case();
        shift_update_visual();
        kb_click();
        return;
    }
    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_delete_char(s_pass_ta);
        kb_click();
        update_pass_hint();
        return;
    }
    if (strcmp(txt, LV_SYMBOL_LEFT) == 0) {
        lv_textarea_cursor_left(s_pass_ta);
        kb_click();
        return;
    }
    if (strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        lv_textarea_cursor_right(s_pass_ta);
        kb_click();
        return;
    }
    if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        on_kb_ready(e);
        return;
    }
    if (strcmp(txt, LV_SYMBOL_KEYBOARD) == 0 || strcmp(txt, LV_SYMBOL_CLOSE) == 0) {
        kb_hide();
        kb_click();
        return;
    }
    if (strcmp(txt, " ") == 0) {
        lv_textarea_add_char(s_pass_ta, ' ');
        kb_click();
        update_pass_hint();
        return;
    }

    /* Ordinary character key */
    if (txt[0] != '\0' && txt[1] == '\0') {
        kb_add_char(txt);
    } else if (txt[0] != '\0') {
        kb_add_char(txt);
    }
}

static bool ap_is_open(const char *ssid)
{
    for (int i = 0; i < s_ap_count; i++) {
        if (strcmp(s_aps[i].ssid, ssid) == 0) {
            return !s_aps[i].secure;
        }
    }
    return false;
}

static void on_ap_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_event_get_user_data(e);

    strncpy(s_chosen_ssid, ssid, sizeof(s_chosen_ssid) - 1);
    s_chosen_ssid[sizeof(s_chosen_ssid) - 1] = '\0';
    s_chosen_open = ap_is_open(ssid);

    ESP_LOGI(TAG, "network selected: %s%s", s_chosen_ssid,
             s_chosen_open ? " (open)" : "");

    highlight_ap(btn);
    lv_label_set_text_fmt(s_chosen_lbl, "Password for %s", s_chosen_ssid);
    lv_textarea_set_text(s_pass_ta, "");
    lv_textarea_set_password_mode(s_pass_ta, !s_pass_visible);

    if (s_chosen_open) {
        lv_obj_add_state(s_pass_ta, LV_STATE_DISABLED);
        lv_textarea_set_placeholder_text(s_pass_ta, "not required");
        kb_hide();
        set_status("open network - tap Connect", COL_DIM);
    } else {
        lv_obj_clear_state(s_pass_ta, LV_STATE_DISABLED);
        lv_textarea_set_placeholder_text(s_pass_ta,
                                         "tap here to type password");
        set_status("tap password field or Connect when ready", COL_DIM);
    }

    set_connect_enabled(true);
    update_pass_hint();
    kb_click();
}

static void scan_task(void *arg)
{
    (void)arg;
    s_scan_result = net_scan(s_aps, MAX_APS);
    s_scan_ready  = true;
    s_scanning    = false;
    vTaskDelete(NULL);
}

static void publish_scan_results(void)
{
    int n = s_scan_result;

    clear_ap_highlight();
    s_chosen_ssid[0] = '\0';
    s_chosen_open = false;
    set_connect_enabled(false);

    lv_obj_clean(s_list);
    s_ap_count = (n > 0) ? n : 0;

    if (n < 0) {
        set_status("scan failed - is the radio up?", COL_ALERT);
    } else if (n == 0) {
        set_status("no networks found - tap Scan to retry", COL_ALERT);
    } else {
        const char *saved = net_current_ssid();
        for (int i = 0; i < n; i++) {
            char row[96];
            snprintf(row, sizeof(row), "%s  %.32s   (%s%s)",
                     s_aps[i].secure ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_OPEN,
                     s_aps[i].ssid, signal_words(s_aps[i].rssi),
                     s_aps[i].secure ? "" : ", open");

            lv_obj_t *btn = lv_button_create(s_list);
            if (!btn) {
                ESP_LOGE(TAG, "out of LVGL memory after %d rows", i);
                set_status("too many networks to list", COL_ALERT);
                break;
            }
            lv_obj_set_width(btn, LV_PCT(100));
            lv_obj_set_height(btn, 46);
            lv_obj_set_style_bg_color(btn, COL_ROW, 0);
            lv_obj_set_style_border_color(btn, COL_BORDER, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_radius(btn, 8, 0);

            lv_obj_t *lbl = lv_label_create(btn);
            if (lbl) {
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
                lv_label_set_text(lbl, row);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
                lv_obj_set_width(lbl, LV_PCT(100));
                lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
            }
            lv_obj_add_event_cb(btn, on_ap_clicked, LV_EVENT_CLICKED,
                                s_aps[i].ssid);

            /* Pre-select the network we already have saved, if visible. */
            if (saved && saved[0] && strcmp(saved, s_aps[i].ssid) == 0) {
                strncpy(s_chosen_ssid, saved, sizeof(s_chosen_ssid) - 1);
                s_chosen_ssid[sizeof(s_chosen_ssid) - 1] = '\0';
                s_chosen_open = !s_aps[i].secure;
                highlight_ap(btn);
                lv_label_set_text_fmt(s_chosen_lbl, "Password for %s", saved);
                set_connect_enabled(true);
            }
        }
        if (s_chosen_ssid[0]) {
            if (s_chosen_open) {
                set_status("saved network found - tap Connect", COL_DIM);
            } else {
                set_status("saved network found - enter password", COL_DIM);
            }
        } else {
            set_status("pick a network", COL_DIM);
        }
    }
    lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
    update_pass_hint();
}

static void start_scan(void)
{
    if (s_scanning) {
        return;
    }
    s_scanning   = true;
    s_scan_ready = false;
    s_connecting = false;
    lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    set_status("scanning...", COL_FOCUS);

    if (xTaskCreate(scan_task, "wifi_scan", SCAN_STACK, NULL, SCAN_PRIO, NULL)
            != pdPASS) {
        s_scanning = false;
        lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
        set_status("could not start scan", COL_ALERT);
    }
}

static void on_scan(lv_event_t *e)
{
    (void)e;
    if (s_scanning || net_wifi_scan_busy()) {
        return;
    }
    set_status("scanning...", COL_FOCUS);
    lv_timer_create(scan_deferred_cb, 150, NULL);
}

typedef struct {
    char ssid[64];
    char pass[64];
} connect_req_t;

static void connect_worker_task(void *pv)
{
    connect_req_t *req = (connect_req_t *)pv;
    net_apply_credentials(req->ssid, req->pass);
    free(req);
    vTaskDelete(NULL);
}

static void on_connect(lv_event_t *e)
{
    (void)e;
    if (s_chosen_ssid[0] == '\0') {
        set_status("pick a network first", COL_ALERT);
        kb_click();
        return;
    }
    if (s_scanning || net_wifi_scan_busy()) {
        set_status("wait for scan to finish", COL_ALERT);
        kb_click();
        return;
    }
    const char *pass = lv_textarea_get_text(s_pass_ta);
    if (!s_chosen_open && (!pass || pass[0] == '\0')) {
        set_status("enter the network password", COL_ALERT);
        flash_pass_border();
        kb_click();
        return;
    }

    connect_req_t *req = calloc(1, sizeof(connect_req_t));
    if (req) {
        strncpy(req->ssid, s_chosen_ssid, sizeof(req->ssid) - 1);
        if (!s_chosen_open && pass) {
            snprintf(req->pass, sizeof(req->pass), "%.*s", (int)sizeof(req->pass) - 1, pass);
        }
        xTaskCreate(connect_worker_task, "wifi_conn", CONNECT_STACK, req, CONNECT_PRIO, NULL);
    }

    s_connecting = true;
    s_connect_deadline = (int64_t)time(NULL) + CONNECT_WAIT_S;
    lv_label_set_text_fmt(s_status, "connecting to %s...", s_chosen_ssid);
    lv_obj_set_style_text_color(s_status, COL_FOCUS, 0);
    set_connect_enabled(false);
    lv_obj_add_state(s_connect_btn, LV_STATE_DISABLED);
}

static void on_kb_ready(lv_event_t *e)
{
    (void)e;
    /* The checkmark dismisses the keyboard; Connect is its own button. */
    kb_hide();
    kb_click();
}

static void on_back(lv_event_t *e)
{
    (void)e;
    kb_hide();
    wifi_setup_hide();
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h,
                             const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, COL_BTN_DIM, 0);
    lv_obj_set_style_border_color(b, COL_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, COL_TEXT, 0);
    lv_obj_center(l);
    return b;
}

static void style_pass_field(void)
{
    lv_obj_set_style_bg_color(s_pass_ta, COL_ROW, 0);
    lv_obj_set_style_border_color(s_pass_ta, COL_BORDER, 0);
    lv_obj_set_style_border_width(s_pass_ta, 1, 0);
    lv_obj_set_style_radius(s_pass_ta, 8, 0);
    lv_obj_set_style_pad_hor(s_pass_ta, 12, 0);
    lv_obj_set_style_text_color(s_pass_ta, COL_TEXT, 0);
    lv_obj_set_style_text_color(s_pass_ta, COL_FAINT,
                                LV_PART_TEXTAREA_PLACEHOLDER);
}

static void style_keyboard(lv_obj_t *kb)
{
    lv_obj_set_style_bg_color(kb, COL_KB_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(kb, 6, LV_PART_MAIN);

    lv_obj_set_style_bg_color(kb, COL_KEY, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, COL_KEY_PRESS, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, COL_KEY_FN, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COL_TEXT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, COL_KEY_SHIFT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
}

esp_err_t wifi_setup_init(void)
{
    /* Overlay on lv_layer_top() - never lv_screen_load() here. Switching screens
     * while the SDIO radio is active has repeatedly left the MIPI panel with
     * backlight on and no framebuffer (light-blue edge glow). */
    s_screen = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_screen, UI_SCR_W, UI_CONTENT_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_pos(title, 24, 18);

    lv_obj_t *done_btn = make_button(s_screen, 1024 - 150 - 24, 14, 150, 50,
                                     LV_SYMBOL_LEFT "  Done", on_back);
    s_scan_btn = make_button(s_screen, 150, 16, 130, 46, "Scan", on_scan);
    lv_obj_add_flag(s_scan_btn, LV_OBJ_FLAG_HIDDEN);

    s_status = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_status, COL_DIM, 0);
    lv_label_set_text(s_status, "");
    lv_obj_set_pos(s_status, 300, 30);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status, 420);

    s_list = lv_list_create(s_screen);
    lv_obj_set_pos(s_list, 24, 78);
    lv_obj_set_size(s_list, 470, 430);
    lv_obj_set_style_bg_color(s_list, COL_CARD, 0);
    lv_obj_set_style_border_color(s_list, COL_BORDER, 0);
    lv_obj_set_style_border_width(s_list, 1, 0);
    lv_obj_set_style_radius(s_list, 12, 0);

    s_chosen_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_chosen_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_chosen_lbl, COL_TEXT, 0);
    lv_label_set_text(s_chosen_lbl, "Tap a network, then type its password");
    lv_obj_set_pos(s_chosen_lbl, 516, 84);
    lv_label_set_long_mode(s_chosen_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_chosen_lbl, 484);

    s_pass_ta = lv_textarea_create(s_screen);
    lv_obj_set_pos(s_pass_ta, 516, 116);
    lv_obj_set_size(s_pass_ta, 430, 52);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta,
                                    "tap to type password");
    lv_obj_set_style_text_font(s_pass_ta, &lv_font_montserrat_18, 0);
    style_pass_field();
    lv_obj_add_event_cb(s_pass_ta, on_pass_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_pass_ta, on_pass_focus, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(s_pass_ta, on_pass_changed, LV_EVENT_VALUE_CHANGED, NULL);

    s_show_pass_btn = lv_button_create(s_screen);
    lv_obj_set_pos(s_show_pass_btn, 952, 116);
    lv_obj_set_size(s_show_pass_btn, 48, 52);
    lv_obj_set_style_bg_color(s_show_pass_btn, COL_CARD, 0);
    lv_obj_set_style_radius(s_show_pass_btn, 8, 0);
    lv_obj_add_event_cb(s_show_pass_btn, on_show_pass, LV_EVENT_CLICKED, NULL);
    lv_obj_t *eye = lv_label_create(s_show_pass_btn);
    lv_label_set_text(eye, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(eye, COL_TEXT, 0);
    lv_obj_center(eye);

    s_pass_hint = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_pass_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_pass_hint, COL_DIM, 0);
    lv_label_set_text(s_pass_hint, "");
    lv_obj_set_pos(s_pass_hint, 516, 172);

    s_connect_btn = make_button(s_screen, 516, 200, 160, 48,
                                "Connect", on_connect);
    lv_obj_set_style_bg_color(s_connect_btn, COL_BTN, 0);
    style_btn_label(s_connect_btn, COL_TEXT);
    set_connect_enabled(false);

    s_keyboard = lv_keyboard_create(s_screen);
    lv_obj_set_size(s_keyboard, 484, 252);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_RIGHT, -24, -16);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        s_kb_lower_map, s_kb_lower_ctrl);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_USER_1,
                        s_kb_sym_map, s_kb_sym_ctrl);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_keyboard, s_pass_ta);
    lv_obj_remove_event_cb(s_keyboard, lv_keyboard_def_event_cb);
    style_keyboard(s_keyboard);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, on_kb_custom, LV_EVENT_VALUE_CHANGED, NULL);
    shift_reset();

    s_kb_close_btn = make_button(s_screen, 0, 0, 484, 36,
                                 LV_SYMBOL_DOWN "  Hide keyboard", on_kb_close);
    lv_obj_align_to(s_kb_close_btn, s_keyboard, LV_ALIGN_OUT_TOP_MID, 0, -4);
    lv_obj_add_flag(s_kb_close_btn, LV_OBJ_FLAG_HIDDEN);

    /* Header controls must stay tappable even when the keyboard is open. */
    lv_obj_move_foreground(done_btn);
    lv_obj_move_foreground(s_scan_btn);
    lv_obj_move_foreground(title);

    lv_obj_update_layout(s_screen);
    ESP_LOGI(TAG, "wi-fi setup screen built");
    return ESP_OK;
}

void wifi_setup_tick(void)
{
    if (!wifi_setup_is_visible()) {
        return;
    }

    if (s_scan_ready) {
        s_scan_ready = false;
        publish_scan_results();
        return;
    }
    if (s_scanning) {
        return;
    }

    int64_t now = (int64_t)time(NULL);

    if (s_connecting) {
        if (net_is_connected()) {
            s_connecting = false;
            char buf[72];
            snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  connected to %s",
                     net_current_ssid());
            set_status(buf, COL_OK);
            set_connect_enabled(true);
            return;
        }
        if (now >= s_connect_deadline) {
            s_connecting = false;
            set_status("could not connect - check password and try again",
                       COL_ALERT);
            set_connect_enabled(true);
        }
        return;
    }

    if (net_is_connected()) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  connected to %s",
                 net_current_ssid());
        set_status(buf, COL_OK);
    }

    /* Keep the overlay painted if something else invalidated the layer. */
    static uint32_t s_keepalive_ms;
    uint32_t t = lv_tick_get();
    if (t - s_keepalive_ms >= 1000) {
        s_keepalive_ms = t;
        lv_obj_invalidate(s_screen);
    }
}

void wifi_setup_show(void)
{
    if (!s_screen) {
        return;
    }
    net_wifi_ui_active(true);
    s_connecting = false;
    s_pass_visible = false;
    s_chosen_ssid[0] = '\0';
    s_chosen_open = false;
    clear_ap_highlight();
    set_connect_enabled(false);
    kb_hide();
    lv_textarea_set_text(s_pass_ta, "");
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_obj_t *eye = lv_obj_get_child(s_show_pass_btn, 0);
    if (eye) {
        lv_label_set_text(eye, LV_SYMBOL_EYE_OPEN);
    }

    if (net_is_connected()) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  connected to %s",
                 net_current_ssid());
        set_status(buf, COL_OK);
    } else if (net_current_ssid()[0]) {
        preload_saved_network();
        set_status("saved network loaded - tap Scan or Connect", COL_DIM);
    } else {
        set_status("tap Scan to find networks", COL_DIM);
    }

    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_screen);
    ui_ticker_raise();
    lv_obj_invalidate(s_screen);

    /* One-shot reconnect using saved NVS credentials - no scan, no keyboard. */
    if (!net_is_connected() && net_current_ssid()[0] != '\0') {
        connect_req_t *req = calloc(1, sizeof(connect_req_t));
        if (req) {
            strncpy(req->ssid, net_current_ssid(), sizeof(req->ssid) - 1);
            cfg_t c;
            cfg_get(&c);
            snprintf(req->pass, sizeof(req->pass), "%.*s", (int)sizeof(req->pass) - 1, c.wifi_password);
            xTaskCreate(connect_worker_task, "wifi_conn", CONNECT_STACK, req,
                        CONNECT_PRIO, NULL);
            s_connecting = true;
            s_connect_deadline = (int64_t)time(NULL) + CONNECT_WAIT_S;
            set_status("reconnecting...", COL_FOCUS);
        }
    }
}

void wifi_setup_hide(void)
{
    if (!s_screen) {
        return;
    }
    kb_hide();
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
    if (!settings_is_visible()) {
        net_wifi_ui_active(false);
    }
    lv_obj_t *scr = lv_screen_active();
    if (scr) {
        lv_obj_invalidate(scr);
    }
}

bool wifi_setup_is_visible(void)
{
    return s_screen && !lv_obj_has_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
}
