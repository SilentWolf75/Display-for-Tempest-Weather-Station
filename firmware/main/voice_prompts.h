#pragma once
#include <stdint.h>
#include <stddef.h>

/* Pre-defined weather alert types for voice playback */
typedef enum {
    VOICE_MSG_TEST = 0,
    VOICE_MSG_TORNADO,
    VOICE_MSG_THUNDERSTORM,
    VOICE_MSG_FLOOD,
    VOICE_MSG_WINTER,
    VOICE_MSG_GENERAL,
} voice_msg_id_t;
