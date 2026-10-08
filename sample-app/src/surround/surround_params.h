// Ported from EVO Player (GPL-3.0), Surround Sound Studio (#106).
#pragma once

/* What the Studio's view draws, filled once per frame by the screen.
 * Channel indices are the AudioOut S16_8CH interleave FL FR FC LFE BL BR SL SR. */

#define EVO_RMLUI_SURROUND_SPEAKERS 8
#define EVO_RMLUI_SURROUND_ACTIONS  8
#define EVO_RMLUI_SURROUND_TRAIL    5

typedef struct {
    const char* name;   /* "FRONT LEFT" */
    const char* label;  /* "FL" */
    double      hz;     /* test tone frequency */
    int         dx;
    int         dy;
    int         ch;         /* channel index, matches active_channel */
    int         item_idx;   /* matches selected_item when this node has the cursor */
    int         hidden;     /* not part of the current layout (6/7 in 5.1) */
} evo_rmlui_surround_speaker_t;

/* view_mode */
#define EVO_SURROUND_VIEW_STAGE       0   /* actions list / 2D speaker cursor */
#define EVO_SURROUND_VIEW_ORB         1   /* the spatial orb has the controls */
#define EVO_SURROUND_VIEW_CALIBRATION 2   /* AUTO CALIBRATION (MIC)            */

/* cal_phase - mirrors SpeakerCalibrationService::Phase, plus the intro */
#define EVO_SURROUND_CAL_INTRO     0
#define EVO_SURROUND_CAL_MIC_CHECK 1
#define EVO_SURROUND_CAL_MEASURING 2
#define EVO_SURROUND_CAL_ANALYZING 3
#define EVO_SURROUND_CAL_COMPLETE  4
#define EVO_SURROUND_CAL_ERROR     5

typedef struct {
    int         rail_focused;
    int         is_51_layout;
    int         selected_item;   /* 0-7 actions, 8-15 speakers (8 + channel) */
    int         active_channel;  /* -1 = none; driven by the audio test thread */
    int         surround_mode;   /* 0 idle, 1 sweep / auto test running, 2 orb */

    evo_rmlui_surround_speaker_t speakers[EVO_RMLUI_SURROUND_SPEAKERS];
    int         speaker_count;
    float       anim_time;       /* seconds, drives the ripples */
    float       orb_x;           /* orb ground position, stage dp */
    float       orb_y;
    int         orb_active;      /* 1 = the orb has the controls */
    float       proximity[EVO_RMLUI_SURROUND_SPEAKERS]; /* DBAP gain 0..1 per channel */

    int         view_mode;       /* EVO_SURROUND_VIEW_* */
    int         flight_mode;     /* 0 MANUAL, 1 ORBIT, 2 FLYBY */
    int         tone_follow;     /* 3D SOUND FIELD: music panned by the gain matrix */
    int         field_playing;   /* that music is playing right now */
    int         sweep_rotation;  /* the 360 sweep is running (field-panned) */
    int         nearest_channel; /* loudest channel in the gain matrix, -1 none */
    float       orb_z_m;         /* elevation above ear height, metres */
    float       azimuth_deg;     /* 0..360 clockwise from the front */
    float       elevation_deg;
    float       distance_m;      /* listener -> source */
    float       x_m, y_m;        /* +x right, +y FRONT (telemetry convention) */
    float       source_dbfs;     /* test-tone peak level */
    float       px_per_m;        /* stage scale, for the distance rings */
    float       trail_x[EVO_RMLUI_SURROUND_TRAIL];   /* recent ground positions, newest first */
    float       trail_y[EVO_RMLUI_SURROUND_TRAIL];
    int         trail_count;
    int         order[EVO_RMLUI_SURROUND_SPEAKERS];  /* physical order FL C FR SL SR SBL SBR LFE */
    int         order_count;

    /* AUTO CALIBRATION (MIC) */
    int         cal_phase;       /* EVO_SURROUND_CAL_* */
    int         cal_step;        /* 1-based speaker being measured */
    int         cal_total;
    int         cal_channel;     /* channel being measured, -1 none */
    int         cal_verifying;   /* closing FRONT LEFT drift check */
    int         cal_applied;     /* this run's result is the active playback profile */
    int         cal_profile_active; /* a saved calibration trims playback right now */
    float       cal_noise_db;
    float       cal_mic_level;   /* live 0..1 */
    const char* cal_message;
    int         cal_measured[EVO_RMLUI_SURROUND_SPEAKERS];
    int         cal_detected[EVO_RMLUI_SURROUND_SPEAKERS];
    float       cal_trim_db[EVO_RMLUI_SURROUND_SPEAKERS];
    float       cal_path_m[EVO_RMLUI_SURROUND_SPEAKERS];
    float       cal_delay_ms[EVO_RMLUI_SURROUND_SPEAKERS];
} evo_rmlui_surround_params_t;
