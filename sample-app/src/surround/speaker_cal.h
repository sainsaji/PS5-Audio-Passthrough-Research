// Ported from EVO Player (GPL-3.0), Surround Sound Studio (#106).
#pragma once

#define EVO_SPEAKER_CAL_CHANNELS 8
/* 30 ms at 48 kHz: well past any in-room path difference (~10 m). */
#define EVO_SPEAKER_CAL_MAX_DELAY_MS 30.0f
#define EVO_SPEAKER_CAL_MAX_TRIM_DB  12.0f

typedef struct {
    int   enabled;
    float gain_db[EVO_SPEAKER_CAL_CHANNELS];   /* trim, clamped to +/-12 dB */
    float delay_ms[EVO_SPEAKER_CAL_CHANNELS];  /* 0..30 ms                  */
} evo_speaker_cal_t;
