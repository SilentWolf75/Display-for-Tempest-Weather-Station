#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_ALERT_NONE,
    AUDIO_ALERT_NOAA_1050HZ,
    AUDIO_ALERT_EAS_DUAL_TONE,
    AUDIO_ALERT_CHIME,
    AUDIO_ALERT_BEEP,
} audio_alert_type_t;

/**
 * Initialize I2S channel and audio amplifier GPIOs.
 */
esp_err_t audio_init(void);

/**
 * Set NOAA weather-alert playback volume (0 to 100).
 */
void audio_set_alert_volume(int percent);

/**
 * Set chime / briefing / notification playback volume (0 to 100).
 */
void audio_set_notification_volume(int percent);

/**
 * @deprecated Use audio_set_alert_volume().
 */
void audio_set_volume(int percent);

/**
 * Returns current alert volume percentage.
 */
int audio_get_volume(void);

/**
 * Play a synthesized alert sound in background task.
 */
void audio_play_alert(audio_alert_type_t type, int duration_ms);

/**
 * Play NOAA 1050 Hz Weather Alert Tone.
 */
void audio_play_noaa_tone(int duration_sec);

/**
 * Play EAS Dual-Tone Siren (853Hz + 960Hz).
 */
void audio_play_eas_siren(int duration_sec);

/**
 * Play a melodic chime.
 */
void audio_play_chime(void);

/**
 * Short keyboard / button click (non-blocking background task).
 */
void audio_play_key_click(void);

/**
 * Short UI notification ping (non-blocking background task).
 */
void audio_play_notify(void);

/**
 * Play morning weather briefing.
 */
void audio_play_morning_briefing(const char *condition_slug);

/**
 * Play cloud or synthesized voice announcement.
 */
void audio_play_tts(const char *text);

/**
 * Full NOAA Weather Alert Broadcast:
 * 1. 1050 Hz Attention Siren (3s)
 * 2. 0.5s pause
 * 3. Clear voice alert announcement
 */
void audio_play_full_noaa_broadcast(const char *alert_text);

/**
 * Audio scheduler tick called periodically from main loop.
 */
void audio_scheduler_tick(int64_t now_epoch, const char *condition_slug);

/**
 * Stop any active audio playback immediately and mute amplifier.
 */
void audio_stop(void);

/**
 * Returns true if audio is currently playing.
 */
bool audio_is_playing(void);

#ifdef __cplusplus
}
#endif
