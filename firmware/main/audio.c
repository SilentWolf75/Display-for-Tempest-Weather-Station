#include "audio.h"
#include "board_pins.h"
#include "config.h"

#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "audio";

#define DEFAULT_SAMPLE_RATE 24000
#define CHUNK_SAMPLES       256
#define TASK_STACK_SIZE     (8 * 1024)
#define TASK_PRIO           5
#define CLICK_QUEUE_LEN     8
#define CLICK_TASK_STACK    (4 * 1024)

static i2s_chan_handle_t  s_tx_handle = NULL;
static TaskHandle_t       s_play_task = NULL;
static TaskHandle_t       s_click_task = NULL;
static QueueHandle_t      s_click_queue = NULL;
static SemaphoreHandle_t  s_i2s_mutex = NULL;
static volatile bool      s_stop_requested = false;
static volatile bool      s_is_playing = false;
static int                s_alert_volume = 80;
static int                s_notification_volume = 70;

#define KEYCLICK_VOL_SCALE  0.95f   /* keyboard — always full, ignores sliders */

/* Reused by the click task — keeps 3 KB off its stack. */
static int16_t s_click_mono_buf[CHUNK_SAMPLES];
static int16_t s_click_stereo_buf[CHUNK_SAMPLES * 2];

static void click_task(void *arg);
static void click_once_task(void *arg);
static void play_key_click_sound(void);
static bool play_spiffs_wav_ex(const char *rel_name, float vol_mul);

typedef enum {
    AUDIO_VOL_ALERT = 0,
    AUDIO_VOL_NOTIFICATION,
} audio_vol_class_t;

static float alert_vol_scale(void)
{
    return (float)s_alert_volume / 100.0f * 0.90f;
}

static float notification_vol_scale(void)
{
    return (float)s_notification_volume / 100.0f * 0.95f;
}

static float vol_scale_for(audio_vol_class_t vol)
{
    return vol == AUDIO_VOL_NOTIFICATION ? notification_vol_scale() : alert_vol_scale();
}

static void mute_amplifier(void)
{
    if (BOARD_AUDIO_PA_GPIO >= 0) {
        /* NS4168: 1 = Mute/Disable */
        gpio_set_level(BOARD_AUDIO_PA_GPIO, 1);
    }
}

static void unmute_amplifier(void)
{
    if (BOARD_AUDIO_PA_GPIO >= 0) {
        /* NS4168: 0 = Active/Enable */
        gpio_set_level(BOARD_AUDIO_PA_GPIO, 0);
    }
}

static void clear_i2s_dma(void)
{
    if (!s_tx_handle) return;
    int16_t zeros[CHUNK_SAMPLES * 2] = {0};
    size_t written = 0;
    for (int i = 0; i < 4; i++) {
        i2s_channel_write(s_tx_handle, zeros, sizeof(zeros), &written, 50);
    }
}

static esp_err_t init_amplifier_gpio(void)
{
    if (BOARD_AUDIO_PA_GPIO < 0) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_AUDIO_PA_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure PA GPIO %d: %s", BOARD_AUDIO_PA_GPIO, esp_err_to_name(err));
        return err;
    }

    mute_amplifier();
    ESP_LOGI(TAG, "Audio amplifier (PA_EN GPIO %d) initialized (muted)", BOARD_AUDIO_PA_GPIO);
    return ESP_OK;
}

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "initializing NS4168 I2S audio driver (24 kHz stereo)...");

    init_amplifier_gpio();

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_AUDIO_I2S_BCLK,
            .ws   = BOARD_AUDIO_I2S_WS,
            .dout = BOARD_AUDIO_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(s_tx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
        return err;
    }

    cfg_t cfg;
    cfg_get(&cfg);
    s_alert_volume = cfg.alert_volume;
    s_notification_volume = cfg.notification_volume;
    if (s_alert_volume <= 0) {
        s_alert_volume = 80;
    }
    if (s_notification_volume <= 0) {
        s_notification_volume = 70;
    }

    clear_i2s_dma();
    mute_amplifier();

    s_i2s_mutex = xSemaphoreCreateMutex();
    s_click_queue = xQueueCreate(CLICK_QUEUE_LEN, sizeof(uint8_t));
    if (s_click_queue) {
        if (xTaskCreate(click_task, "audio_click", CLICK_TASK_STACK, NULL, TASK_PRIO + 1,
                        &s_click_task) != pdPASS) {
            ESP_LOGW(TAG, "key-click task create failed");
        }
    } else {
        ESP_LOGW(TAG, "key-click queue unavailable");
    }

    ESP_LOGI(TAG, "I2S Audio ready (muted): alert=%d%% notify=%d%%",
             s_alert_volume, s_notification_volume);
    return ESP_OK;
}

void audio_set_alert_volume(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    s_alert_volume = percent;
}

void audio_set_notification_volume(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    s_notification_volume = percent;
}

void audio_set_volume(int percent)
{
    audio_set_alert_volume(percent);
}

int audio_get_volume(void)
{
    return s_alert_volume;
}

/* Synthesizer Tone Player */
static void play_tone_internal_ex(audio_alert_type_t type, int duration_ms,
                                  bool honour_stop, float vol_scale)
{
    unmute_amplifier();
    vTaskDelay(pdMS_TO_TICKS(12));

    int total_samples = (int)((int64_t)duration_ms * DEFAULT_SAMPLE_RATE / 1000);
    int16_t buffer[CHUNK_SAMPLES * 2];

    float phase1 = 0.0f;
    float phase2 = 0.0f;
    float freq1 = 1050.0f;
    float freq2 = 0.0f;

    if (type == AUDIO_ALERT_NOAA_1050HZ) {
        freq1 = 1050.0f;
        freq2 = 0.0f;
    } else if (type == AUDIO_ALERT_EAS_DUAL_TONE) {
        freq1 = 853.0f;
        freq2 = 960.0f;
    } else if (type == AUDIO_ALERT_CHIME) {
        freq1 = 587.33f;
        freq2 = 880.00f;
    } else if (type == AUDIO_ALERT_BEEP) {
        freq1 = 1400.0f;
        freq2 = 0.0f;
    }

    float delta1 = (2.0f * (float)M_PI * freq1) / (float)DEFAULT_SAMPLE_RATE;
    float delta2 = (2.0f * (float)M_PI * freq2) / (float)DEFAULT_SAMPLE_RATE;
    int samples_sent = 0;

    while (samples_sent < total_samples && (!honour_stop || !s_stop_requested)) {
        int chunk = CHUNK_SAMPLES;
        if (samples_sent + chunk > total_samples) {
            chunk = total_samples - samples_sent;
        }

        for (int i = 0; i < chunk; i++) {
            float sample_f = 0.0f;
            if (freq2 > 0.0f) {
                sample_f = (sinf(phase1) + sinf(phase2)) * 0.5f;
                phase2 += delta2;
                if (phase2 >= 2.0f * (float)M_PI) phase2 -= 2.0f * (float)M_PI;
            } else {
                sample_f = sinf(phase1);
            }
            phase1 += delta1;
            if (phase1 >= 2.0f * (float)M_PI) phase1 -= 2.0f * (float)M_PI;

            if (type == AUDIO_ALERT_CHIME) {
                float env = 1.0f - ((float)(samples_sent + i) / (float)total_samples);
                sample_f *= env * env;
            }

            int16_t s16 = (int16_t)(sample_f * vol_scale * 32767.0f);
            buffer[i * 2]     = s16;
            buffer[i * 2 + 1] = s16;
        }

        size_t bytes_written = 0;
        if (s_tx_handle) {
            i2s_channel_write(s_tx_handle, buffer, chunk * 4, &bytes_written, portMAX_DELAY);
        }
        samples_sent += chunk;
    }

    clear_i2s_dma();
    mute_amplifier();
}

static void play_tone_internal(audio_alert_type_t type, int duration_ms)
{
    play_tone_internal_ex(type, duration_ms, true, alert_vol_scale());
}

static void play_tone_notification(audio_alert_type_t type, int duration_ms)
{
    play_tone_internal_ex(type, duration_ms, true, notification_vol_scale());
}

/* Stream WAV audio file chunk-by-chunk from SPIFFS without buffer allocation */
static bool play_spiffs_wav_ex2(const char *rel_name, float vol_scale, bool honour_stop,
                                int16_t *mono_buf, int16_t *stereo_out)
{
    char path[128];
    snprintf(path, sizeof(path), "/icons/audio/%s", rel_name);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "failed to open WAV file: %s", path);
        return false;
    }

    /* Skip standard 44-byte RIFF WAVE header */
    uint8_t header[44];
    if (fread(header, 1, 44, f) != 44) {
        fclose(f);
        return false;
    }

    unmute_amplifier();
    vTaskDelay(pdMS_TO_TICKS(12));

    while (!honour_stop || !s_stop_requested) {
        size_t read_samples = fread(mono_buf, sizeof(int16_t), CHUNK_SAMPLES, f);
        if (read_samples == 0) {
            break;
        }

        for (size_t i = 0; i < read_samples; i++) {
            int16_t val = (int16_t)(mono_buf[i] * vol_scale);
            stereo_out[i * 2]     = val;
            stereo_out[i * 2 + 1] = val;
        }

        size_t written = 0;
        if (s_tx_handle) {
            i2s_channel_write(s_tx_handle, stereo_out, read_samples * 4, &written, portMAX_DELAY);
        }
    }

    fclose(f);
    clear_i2s_dma();
    mute_amplifier();
    return true;
}

static bool play_spiffs_wav_ex(const char *rel_name, float vol_mul)
{
    int16_t mono_buf[CHUNK_SAMPLES];
    int16_t stereo_out[CHUNK_SAMPLES * 2];
    float scale = alert_vol_scale() * vol_mul;
    return play_spiffs_wav_ex2(rel_name, scale, true, mono_buf, stereo_out);
}

static bool play_spiffs_wav_vol(const char *rel_name, audio_vol_class_t vol)
{
    int16_t mono_buf[CHUNK_SAMPLES];
    int16_t stereo_out[CHUNK_SAMPLES * 2];
    return play_spiffs_wav_ex2(rel_name, vol_scale_for(vol), true, mono_buf, stereo_out);
}

static void play_key_click_sound(void)
{
    if (s_i2s_mutex) {
        xSemaphoreTake(s_i2s_mutex, portMAX_DELAY);
    }

    /* Clicks must not inherit a stale stop flag left by alert playback. */
    s_stop_requested = false;

    if (!play_spiffs_wav_ex2("keyclick.wav", KEYCLICK_VOL_SCALE, false,
                             s_click_mono_buf, s_click_stereo_buf)) {
        ESP_LOGW(TAG, "keyclick.wav missing, using tone fallback");
        play_tone_internal_ex(AUDIO_ALERT_BEEP, 70, false, KEYCLICK_VOL_SCALE);
    }

    if (s_i2s_mutex) {
        xSemaphoreGive(s_i2s_mutex);
    }
}

static void click_once_task(void *arg)
{
    (void)arg;
    play_key_click_sound();
    vTaskDelete(NULL);
}

static void click_task(void *arg)
{
    (void)arg;
    uint8_t drop;
    for (;;) {
        if (xQueueReceive(s_click_queue, &drop, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        play_key_click_sound();
    }
}

static void play_voice_announcement(const char *text, audio_vol_class_t vol)
{
    if (!text || text[0] == '\0' || s_stop_requested) {
        return;
    }

    if (strstr(text, ".wav") != NULL) {
        play_spiffs_wav_vol(text, vol);
        return;
    }
    if (strstr(text, "chime") != NULL || strstr(text, "Chime") != NULL) {
        play_spiffs_wav_vol("chime.wav", AUDIO_VOL_NOTIFICATION);
        return;
    }
    if (strstr(text, "morning_") != NULL) {
        play_spiffs_wav_vol(text, AUDIO_VOL_NOTIFICATION);
        return;
    }
    if (strstr(text, "test") != NULL || strstr(text, "Test") != NULL) {
        play_spiffs_wav_vol("test.wav", vol);
        return;
    }
    if (strstr(text, "Tornado") != NULL || strstr(text, "tornado") != NULL) {
        play_spiffs_wav_vol("tornado.wav", AUDIO_VOL_ALERT);
        return;
    }
    if (strstr(text, "Thunderstorm") != NULL || strstr(text, "thunderstorm") != NULL) {
        play_spiffs_wav_vol("thunderstorm.wav", AUDIO_VOL_ALERT);
        return;
    }
    if (strstr(text, "Flood") != NULL || strstr(text, "flood") != NULL) {
        play_spiffs_wav_vol("flood.wav", AUDIO_VOL_ALERT);
        return;
    }
    play_spiffs_wav_vol("general.wav", AUDIO_VOL_ALERT);
}

typedef struct {
    audio_alert_type_t type;
    int                duration_ms;
    char              *tts_text;
    bool               is_broadcast;
    bool               is_key_click;
    bool               is_notify;
    audio_vol_class_t  vol_class;
} audio_task_params_t;

static void audio_master_task(void *arg)
{
    audio_task_params_t *p = (audio_task_params_t *)arg;
    if (s_i2s_mutex) {
        xSemaphoreTake(s_i2s_mutex, portMAX_DELAY);
    }
    s_is_playing = true;
    s_stop_requested = false;

    if (p->is_broadcast) {
        /* 1. Play 1050 Hz Siren for 3.0 seconds */
        play_tone_internal(AUDIO_ALERT_NOAA_1050HZ, 3000);

        /* 2. Short pause */
        vTaskDelay(pdMS_TO_TICKS(400));

        /* 3. Play Broadcast Announcement */
        if (p->tts_text && !s_stop_requested) {
            play_voice_announcement(p->tts_text, AUDIO_VOL_ALERT);
        }
    } else if (p->is_key_click) {
        play_key_click_sound();
    } else if (p->is_notify) {
        if (!play_spiffs_wav_vol("notify.wav", AUDIO_VOL_NOTIFICATION) &&
            !s_stop_requested) {
            play_tone_notification(AUDIO_ALERT_CHIME, 180);
        }
    } else if (p->tts_text) {
        play_voice_announcement(p->tts_text, p->vol_class);
    } else {
        play_tone_internal(p->type, p->duration_ms);
    }

    if (p->tts_text) {
        free(p->tts_text);
    }
    free(p);

    clear_i2s_dma();
    mute_amplifier();

    s_is_playing = false;
    s_play_task = NULL;
    if (s_i2s_mutex) {
        xSemaphoreGive(s_i2s_mutex);
    }
    vTaskDelete(NULL);
}

static bool start_audio_task(audio_task_params_t *p)
{
    if (s_is_playing) {
        audio_stop();
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    if (xTaskCreate(audio_master_task, "audio_task", TASK_STACK_SIZE, p, TASK_PRIO,
                    &s_play_task) != pdPASS) {
        ESP_LOGW(TAG, "audio task create failed");
        if (p->tts_text) {
            free(p->tts_text);
        }
        free(p);
        return false;
    }
    return true;
}

void audio_play_alert(audio_alert_type_t type, int duration_ms)
{
    audio_task_params_t *p = calloc(1, sizeof(audio_task_params_t));
    if (!p) return;
    p->type = type;
    p->duration_ms = duration_ms;
    start_audio_task(p);
}

void audio_play_noaa_tone(int duration_sec)
{
    audio_play_alert(AUDIO_ALERT_NOAA_1050HZ, duration_sec * 1000);
}

void audio_play_eas_siren(int duration_sec)
{
    audio_play_alert(AUDIO_ALERT_EAS_DUAL_TONE, duration_sec * 1000);
}

void audio_play_chime(void)
{
    audio_task_params_t *p = calloc(1, sizeof(audio_task_params_t));
    if (!p) {
        return;
    }
    p->vol_class = AUDIO_VOL_NOTIFICATION;
    p->tts_text = strdup("chime.wav");
    start_audio_task(p);
}

void audio_play_key_click(void)
{
    if (!s_click_queue) {
        xTaskCreate(click_once_task, "audio_click_once", CLICK_TASK_STACK, NULL,
                    TASK_PRIO + 1, NULL);
        return;
    }
    uint8_t tick = 1;
    if (xQueueSend(s_click_queue, &tick, 0) != pdTRUE) {
        /* Queue full — drop oldest and retry once. */
        xQueueReceive(s_click_queue, &tick, 0);
        xQueueSend(s_click_queue, &tick, 0);
    }
}

void audio_play_notify(void)
{
    audio_task_params_t *p = calloc(1, sizeof(audio_task_params_t));
    if (!p) {
        return;
    }
    p->is_notify = true;
    start_audio_task(p);
}

void audio_play_tts(const char *text)
{
    if (!text || text[0] == '\0') {
        return;
    }
    audio_task_params_t *p = calloc(1, sizeof(audio_task_params_t));
    if (!p) {
        return;
    }
    p->vol_class = AUDIO_VOL_ALERT;
    p->tts_text = strdup(text);
    start_audio_task(p);
}

bool audio_play_full_noaa_broadcast(const char *alert_text)
{
    audio_task_params_t *p = calloc(1, sizeof(audio_task_params_t));
    if (!p) return false;
    if (alert_text && alert_text[0]) {
        p->is_broadcast = true;
        p->tts_text = strdup(alert_text);
        if (!p->tts_text) { free(p); return false; }
    } else {
        p->type = AUDIO_ALERT_NOAA_1050HZ;
        p->duration_ms = 4000;
    }
    return start_audio_task(p);
}

void audio_stop(void)
{
    s_stop_requested = true;
    clear_i2s_dma();
    mute_amplifier();
}

bool audio_is_playing(void)
{
    return s_is_playing;
}

static int s_last_chime_hour = -1;
static int s_last_briefing_day = -1;

void audio_play_morning_briefing(const char *condition_slug)
{
    const char *wav_file = "morning_general.wav";
    if (condition_slug) {
        if (strstr(condition_slug, "clear") || strstr(condition_slug, "sun")) {
            wav_file = "morning_sunny.wav";
        } else if (strstr(condition_slug, "cloud")) {
            wav_file = "morning_clouds.wav";
        } else if (strstr(condition_slug, "rain") || strstr(condition_slug, "thunder") || strstr(condition_slug, "sleet") || strstr(condition_slug, "snow")) {
            wav_file = "morning_rain.wav";
        }
    }
    audio_task_params_t *p = calloc(1, sizeof(audio_task_params_t));
    if (!p) {
        return;
    }
    p->vol_class = AUDIO_VOL_NOTIFICATION;
    p->tts_text = strdup(wav_file);
    start_audio_task(p);
}

void audio_scheduler_tick(int64_t now_epoch, const char *condition_slug)
{
    if (now_epoch < 1700000000LL) return;

    time_t t = (time_t)now_epoch;
    struct tm lt;
    localtime_r(&t, &lt);

    cfg_t cfg;
    cfg_get(&cfg);

    /* 1. Hourly Daytime Chime (8:00 AM - 8:00 PM at minute :00) */
    if (cfg.hourly_chime_enabled && lt.tm_min == 0 && lt.tm_sec < 5) {
        if (lt.tm_hour >= 8 && lt.tm_hour <= 20 && s_last_chime_hour != lt.tm_hour) {
            s_last_chime_hour = lt.tm_hour;
            ESP_LOGI(TAG, "Triggering Hourly Daytime Chime (%d:00)", lt.tm_hour);
            audio_play_chime();
        }
    }

    /* 2. Morning Spoken Weather Briefing */
    if (cfg.morning_briefing_enabled && lt.tm_hour == cfg.night_end_hour && lt.tm_min == 0 && lt.tm_sec < 5) {
        if (s_last_briefing_day != lt.tm_yday) {
            s_last_briefing_day = lt.tm_yday;
            ESP_LOGI(TAG, "Triggering Morning Weather Briefing (%d:00)", lt.tm_hour);
            audio_play_morning_briefing(condition_slug);
        }
    }
}
