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
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "audio";

#define DEFAULT_SAMPLE_RATE 24000
#define CHUNK_SAMPLES       256
#define TASK_STACK_SIZE     (8 * 1024)
#define TASK_PRIO           5

static i2s_chan_handle_t  s_tx_handle = NULL;
static TaskHandle_t       s_play_task = NULL;
static volatile bool      s_stop_requested = false;
static volatile bool      s_is_playing = false;
static int                s_volume_pct = 90;

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
    s_volume_pct = cfg.alert_volume;
    if (s_volume_pct <= 0) s_volume_pct = 90;

    clear_i2s_dma();
    mute_amplifier();

    ESP_LOGI(TAG, "I2S Audio ready (muted): Rate=%d Hz, Vol=%d%%", DEFAULT_SAMPLE_RATE, s_volume_pct);
    return ESP_OK;
}

void audio_set_volume(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    s_volume_pct = percent;
}

int audio_get_volume(void)
{
    return s_volume_pct;
}

/* Synthesizer Tone Player */
static void play_tone_internal(audio_alert_type_t type, int duration_ms)
{
    unmute_amplifier();

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
        freq1 = 2000.0f;
        freq2 = 0.0f;
    }

    float delta1 = (2.0f * (float)M_PI * freq1) / (float)DEFAULT_SAMPLE_RATE;
    float delta2 = (2.0f * (float)M_PI * freq2) / (float)DEFAULT_SAMPLE_RATE;
    float vol_scale = (float)s_volume_pct / 100.0f * 0.90f;
    int samples_sent = 0;

    while (samples_sent < total_samples && !s_stop_requested) {
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

/* Stream WAV audio file chunk-by-chunk from SPIFFS without buffer allocation */
static bool play_spiffs_wav(const char *rel_name)
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

    ESP_LOGI(TAG, "Streaming audio broadcast: %s", path);
    unmute_amplifier();

    int16_t mono_buf[CHUNK_SAMPLES];
    int16_t stereo_out[CHUNK_SAMPLES * 2];
    float vol_scale = (float)s_volume_pct / 100.0f * 0.95f;

    while (!s_stop_requested) {
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

static void play_voice_announcement(const char *text)
{
    if (!text || text[0] == '\0' || s_stop_requested) return;

    /* Direct WAV file name playback */
    if (strstr(text, ".wav") != NULL) {
        if (play_spiffs_wav(text)) return;
    }

    if (strstr(text, "chime") != NULL || strstr(text, "Chime") != NULL) {
        if (play_spiffs_wav("chime.wav")) return;
    }
    if (strstr(text, "test") != NULL || strstr(text, "Test") != NULL) {
        if (play_spiffs_wav("test.wav")) return;
    }
    if (strstr(text, "Tornado") != NULL || strstr(text, "tornado") != NULL) {
        if (play_spiffs_wav("tornado.wav")) return;
    }
    if (strstr(text, "Thunderstorm") != NULL || strstr(text, "thunderstorm") != NULL) {
        if (play_spiffs_wav("thunderstorm.wav")) return;
    }
    if (strstr(text, "Flood") != NULL || strstr(text, "flood") != NULL) {
        if (play_spiffs_wav("flood.wav")) return;
    }
    if (play_spiffs_wav("general.wav")) {
        return;
    }
}

typedef struct {
    audio_alert_type_t type;
    int                duration_ms;
    char              *tts_text;
    bool               is_broadcast;
} audio_task_params_t;

static void audio_master_task(void *arg)
{
    audio_task_params_t *p = (audio_task_params_t *)arg;
    s_is_playing = true;
    s_stop_requested = false;

    if (p->is_broadcast) {
        /* 1. Play 1050 Hz Siren for 3.0 seconds */
        play_tone_internal(AUDIO_ALERT_NOAA_1050HZ, 3000);

        /* 2. Short pause */
        vTaskDelay(pdMS_TO_TICKS(400));

        /* 3. Play Broadcast Announcement */
        if (p->tts_text && !s_stop_requested) {
            play_voice_announcement(p->tts_text);
        }
    } else if (p->tts_text) {
        play_voice_announcement(p->tts_text);
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
    vTaskDelete(NULL);
}

static void start_audio_task(audio_task_params_t *p)
{
    if (s_is_playing) {
        audio_stop();
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    xTaskCreate(audio_master_task, "audio_task", TASK_STACK_SIZE, p, TASK_PRIO, &s_play_task);
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
    audio_play_tts("chime.wav");
}

void audio_play_tts(const char *text)
{
    if (!text || text[0] == '\0') return;
    audio_task_params_t *p = calloc(1, sizeof(audio_task_params_t));
    if (!p) return;
    p->tts_text = strdup(text);
    start_audio_task(p);
}

void audio_play_full_noaa_broadcast(const char *alert_text)
{
    if (!alert_text || alert_text[0] == '\0') {
        audio_play_noaa_tone(4);
        return;
    }
    audio_task_params_t *p = calloc(1, sizeof(audio_task_params_t));
    if (!p) return;
    p->is_broadcast = true;
    p->tts_text = strdup(alert_text);
    start_audio_task(p);
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
    audio_play_tts(wav_file);
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
