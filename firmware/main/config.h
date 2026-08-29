#pragma once
/*
 * Runtime settings, persisted in NVS.
 *
 * Units used to be a compile-time Kconfig choice. They are runtime now because
 * a settings screen that requires a reflash is not a settings screen. The
 * conversion helpers below keep the same names the UI already used, so call
 * sites did not change -- only what the macros expand to.
 *
 * wx_state still stores SI exclusively. These convert at render time.
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

typedef enum {
    CFG_UNITS_IMPERIAL = 0,
    CFG_UNITS_METRIC   = 1,
} cfg_units_t;

#define CFG_SSID_LEN      33     /* 32 + NUL, per 802.11 */
#define CFG_PASSWORD_LEN  65     /* 64 + NUL, WPA2 max   */

typedef struct {
    /* Wi-Fi credentials entered on the settings screen. Kept here rather
     * than in Kconfig so the network can be changed without a reflash;
     * secrets.h still seeds them on a first boot. */
    char        wifi_ssid[CFG_SSID_LEN];
    char        wifi_password[CFG_PASSWORD_LEN];

    cfg_units_t units;
    uint8_t     timezone_idx;        /* 0=ET, 1=CT, 2=MT, 3=AZ, 4=PT, 5=AK, 6=HI, 7=UTC */
    uint8_t     brightness_day;      /* 5..100 */
    uint8_t     brightness_night;    /* 0..100, 0 = backlight off */
    uint8_t     night_start_hour;    /* local hour dimming begins */
    uint8_t     night_end_hour;      /* local hour full brightness resumes */
    bool        night_dim_enabled;
    bool        animate_forecast;    /* animate all 7 icons, not just header */
    uint8_t     wind_scale_max_ms;   /* full-scale on the wind ring */
    char        alert_zipcode[10];   /* 5-digit US Zip Code for NOAA alerts */
    uint8_t     alert_volume;        /* 0..100 % — NOAA siren & alert voice */
    uint8_t     notification_volume; /* 0..100 % — chimes, briefings, UI pings */
    bool        alert_siren_enabled; /* Audible 1050 Hz siren on active warning */
    bool        hourly_chime_enabled;/* Soft chime at top of the hour (8am-8pm) */
    bool        morning_briefing_enabled; /* Spoken forecast brief at wakeup */
    bool        night_alert_dnd;     /* Silence non-critical alerts at night */
    bool        night_standby_enabled; /* Minimal clock during sleep hours */
    bool        night_standby_red;   /* false = amber, true = red night tint */
    bool        web_server_enabled;  /* Local Web Dashboard (http://tempest.local) */
    bool        mqtt_enabled;        /* Home Assistant MQTT Auto-Discovery */
    char        mqtt_broker[64];     /* MQTT broker host/IP */
    uint8_t     screensaver_idle_min; /* 0 = off; minutes of no touch before dim */
    uint8_t     screensaver_brightness; /* backlight % while screensaver active */
    bool        lightning_alert_sound; /* Short siren when strike within 6 mi */
    bool        lightning_alert_voice; /* Spoken proximity alert for lightning */
} cfg_t;

esp_err_t cfg_init(void);

/* Snapshot. Cheap; copies a small struct under a lock. */
void cfg_get(cfg_t *out);

/* Applies and persists. Only fields that actually changed are written to NVS,
 * so dragging a slider does not burn flash on every pixel. */
esp_err_t cfg_set(const cfg_t *in);

/* Restores defaults and persists them. */
esp_err_t cfg_reset(void);

/* Stores credentials and persists them. Does not reconnect -- call
 * net_apply_credentials() for that. */
esp_err_t cfg_set_wifi(const char *ssid, const char *password);

/* True once a real SSID has been set, from either NVS or secrets.h. */
bool cfg_has_wifi(void);

/* Rejects empty strings and the Kconfig "changeme" placeholder. */
bool cfg_wifi_ssid_usable(const char *ssid);

/* Day or night brightness from the current local hour. Falls back to day
 * brightness when the clock has not synced yet. */
uint8_t cfg_brightness_now(void);

/* True when the local hour falls inside the configured night window.
 * Handles windows that wrap past midnight, which is the normal case. */
bool cfg_is_night(int local_hour);

/* ---- unit conversion, reading the live setting ----
 * The macro names match what ui.c already used, so switching from the old
 * compile-time Kconfig choice was a one-block change. */
float       cfg_temp(float celsius);
const char *cfg_temp_suffix(void);
float       cfg_wind(float ms);
const char *cfg_wind_suffix(void);
float       cfg_pressure(float mb);
const char *cfg_pressure_suffix(void);
const char *cfg_pressure_fmt(void);
float       cfg_rain(float mm);
const char *cfg_rain_suffix(void);
const char *cfg_rain_fmt(void);
float       cfg_distance(float km);
const char *cfg_distance_suffix(void);
