#ifndef YOUANDEYE_EMOTE_MOTION_H
#define YOUANDEYE_EMOTE_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#include "emote_state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EMOTE_CHANNEL_GAZE,
    EMOTE_CHANNEL_LIDS,
    EMOTE_CHANNEL_BROWS,
    EMOTE_CHANNEL_PUPIL,
    EMOTE_CHANNEL_COUNT
};

typedef enum {
    EMOTE_BLINK_GENTLE,
    EMOTE_BLINK_NATURAL,
    EMOTE_BLINK_LIVELY
} emote_blink_style_t;

typedef enum {
    EMOTE_GAZE_SOFT,
    EMOTE_GAZE_ATTENTIVE,
    EMOTE_GAZE_CURIOUS,
    EMOTE_GAZE_DIRECT
} emote_gaze_style_t;

typedef enum {
    EMOTE_IDLE_CALM,
    EMOTE_IDLE_CURIOUS,
    EMOTE_IDLE_PLAYFUL,
    EMOTE_IDLE_FOCUSED
} emote_idle_style_t;

typedef struct {
    emote_blink_style_t blink_style;
    emote_gaze_style_t gaze_style;
    emote_idle_style_t idle_style;
    float energy;
} emote_personality_t;

typedef struct {
    float warmth;
    float confidence;
    float urgency;
} emote_modifiers_t;

typedef struct {
    uint32_t transition_ms[EMOTE_CHANNEL_COUNT];
    bool transition_changed[EMOTE_CHANNEL_COUNT];
    float blink_left;
    float blink_right;
    float gaze_velocity_x;
    float gaze_velocity_y;
    float gaze_velocity;
    float breathing;
    float attention_decay;
} emote_motion_telemetry_t;

typedef struct {
    emote_target_t semantic;
    emote_pose_t current;
    emote_pose_t velocity;
    emote_pose_t from;
    emote_pose_t target;
    uint32_t transition_started_ms;
    uint32_t reaction_ms;
    uint32_t expires_at_ms;
    uint32_t decay_ends_ms;
    uint32_t rng;
    uint32_t next_blink_ms;
    uint32_t blink_started_ms;
    uint32_t blink_duration_ms;
    float blink_right_scale;
    uint32_t next_saccade_ms;
    bool blink_active;
    bool double_blink_pending;
    float saccade_x;
    float saccade_y;
    float saccade_vx;
    float saccade_vy;
    float saccade_target_x;
    float saccade_target_y;
    float breathing_phase;
    emote_personality_t personality;
    emote_modifiers_t modifiers;
    emote_motion_telemetry_t telemetry;
} emote_motion_t;

void emote_motion_init(emote_motion_t *motion, uint32_t seed, uint32_t now_ms);
void emote_motion_set_personality(
    emote_motion_t *motion,
    const emote_personality_t *personality,
    uint32_t now_ms);
void emote_motion_set_modifiers(emote_motion_t *motion, const emote_modifiers_t *modifiers);
void emote_motion_apply(emote_motion_t *motion, const emote_target_t *target, uint32_t now_ms);
void emote_motion_request_blink(emote_motion_t *motion, uint32_t now_ms);
void emote_motion_step(emote_motion_t *motion, uint32_t now_ms, float dt_seconds);
emote_pose_t emote_motion_render_pose(const emote_motion_t *motion, bool left_eye, uint32_t now_ms);
const emote_motion_telemetry_t *emote_motion_telemetry(const emote_motion_t *motion);

#ifdef __cplusplus
}
#endif

#endif
