#include "emote_motion.h"

#include <math.h>
#include <string.h>

#define PENDING_MS UINT32_MAX
#define BLINK_ANTICIPATION_MS 34u
#define BLINK_ANTICIPATION_OPEN 0.04f
#define BLINK_CLOSE_MS 70u
#define BLINK_HOLD_MS 30u
#define BLINK_OPEN_MS 145u
#define BLINK_DURATION_MS \
    (BLINK_ANTICIPATION_MS + BLINK_CLOSE_MS + BLINK_HOLD_MS + BLINK_OPEN_MS)
#define BLINK_RIGHT_LAG_MS 0u
#define GAZE_ARC_CROSS_X_PX 1.5f
#define GAZE_ARC_CROSS_Y_PX 1.1f
#define GAZE_ARC_LIMIT_PX 4.0f
#define GAZE_X_TRAVEL_PX 28.0f
#define GAZE_Y_TRAVEL_PX 24.0f
#define EYE_DRIFT_SPEED 0.83f
#define EYE_DRIFT_AMPLITUDE 0.006f
#define EYE_DRIFT_Y_SCALE 0.55f
#define EYE_DRIFT_LEFT_PHASE 0.7f
#define EYE_DRIFT_RIGHT_PHASE 2.1f

static float blink_value(const emote_motion_t *motion, bool left_eye, uint32_t now_ms);

static float clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

static float smoothstep(float value)
{
    const float t = clampf(value, 0.0f, 1.0f);
    return t * t * (3.0f - (2.0f * t));
}

static float mixf(float from, float to, float amount)
{
    return from + ((to - from) * amount);
}

static float random_unit(emote_motion_t *motion)
{
    motion->rng = (motion->rng * 1664525u) + 1013904223u;
    return (float)(motion->rng >> 8) / 16777215.0f;
}

static uint32_t random_range_ms(emote_motion_t *motion, uint32_t low, uint32_t high)
{
    return low + (uint32_t)(random_unit(motion) * (float)(high - low));
}

static void spring(float *value, float *velocity, float target, float omega, float dt)
{
    if (!isfinite(*value) || !isfinite(*velocity) || !isfinite(target) || !isfinite(dt)) {
        *value = isfinite(target) ? target : 0.0f;
        *velocity = 0.0f;
        return;
    }
    if (dt <= 0.0f) return;

    /*
     * Exact critically damped step for a constant target. The former
     * semi-implicit Euler step became unstable when a 40 MHz dual-panel
     * transfer stretched a frame to roughly 35-41 ms (especially the
     * 26 rad/s saccade spring), eventually producing NaNs. This form remains
     * stable across variable render cadence without weakening the motion.
     */
    const float displacement = *value - target;
    const float slope = *velocity + (omega * displacement);
    const float decay = expf(-omega * dt);
    const float stepped = displacement + (slope * dt);
    *value = target + (stepped * decay);
    *velocity = (slope - (omega * stepped)) * decay;
}

static emote_pose_t pose_mix(emote_pose_t from, emote_pose_t to, float amount)
{
    emote_pose_t result = {
        .open = mixf(from.open, to.open, amount),
        .lower_lid = mixf(from.lower_lid, to.lower_lid, amount),
        .gaze_x = mixf(from.gaze_x, to.gaze_x, amount),
        .gaze_y = mixf(from.gaze_y, to.gaze_y, amount),
        .pupil = mixf(from.pupil, to.pupil, amount),
        .brow_y = mixf(from.brow_y, to.brow_y, amount),
        .brow_rotation = mixf(from.brow_rotation, to.brow_rotation, amount),
        .asymmetry = mixf(from.asymmetry, to.asymmetry, amount),
        .arc = mixf(from.arc, to.arc, amount),
        .intensity = mixf(from.intensity, to.intensity, amount),
        .pupil_shape = amount < 0.5f ? from.pupil_shape : to.pupil_shape,
        .palette = amount < 0.5f ? from.palette : to.palette,
    };
    return result;
}

static emote_pose_t semantic_pose(const emote_target_t *semantic)
{
    emote_pose_t pose = emote_pose_for_affect(semantic->affect);
    pose.intensity = semantic->intensity;
    if (semantic->gaze_mode == EMOTE_GAZE_POINT) {
        pose.gaze_x = semantic->gaze_x;
        pose.gaze_y = semantic->gaze_y;
    } else if (semantic->gaze_mode == EMOTE_GAZE_USER) {
        pose.gaze_x = 0.0f;
        pose.gaze_y = 0.0f;
    }
    return pose;
}

static void schedule_blink(emote_motion_t *motion, uint32_t now_ms)
{
    uint32_t low = 2800, high = 6200;
    if (motion->semantic.affect == EMOTE_ERROR) {
        low = 1200; high = 2800;
    } else if (motion->semantic.affect == EMOTE_THINKING || motion->semantic.affect == EMOTE_WORKING) {
        low = 3600; high = 6200;
    } else if (motion->semantic.mode == EMOTE_MODE_SLEEPY) {
        low = 1400; high = 3300;
    }
    motion->next_blink_ms = now_ms + random_range_ms(motion, low, high);
}

static void schedule_saccade(emote_motion_t *motion, uint32_t now_ms)
{
    uint32_t low = 350, high = 1250;
    if (motion->semantic.affect == EMOTE_THINKING) {
        low = 1200; high = 2800;
    } else if (motion->semantic.mode == EMOTE_MODE_IDLE) {
        low = 1000; high = 2600;
    } else if (motion->semantic.mode == EMOTE_MODE_TRACKING) {
        low = 180; high = 520;
    }
    motion->next_saccade_ms = now_ms + random_range_ms(motion, low, high);
}

void emote_motion_init(emote_motion_t *motion, uint32_t seed, uint32_t now_ms)
{
    if (!motion) return;
    memset(motion, 0, sizeof(*motion));
    emote_target_neutral(&motion->semantic);
    motion->rng = seed ? seed : 0x59e23a17u;
    motion->current = emote_pose_for_affect(EMOTE_NEUTRAL);
    motion->from = motion->current;
    motion->target = motion->current;
    for (int i = 0; i < EMOTE_CHANNEL_COUNT; ++i) motion->telemetry.transition_ms[i] = PENDING_MS;
    schedule_blink(motion, now_ms);
    schedule_saccade(motion, now_ms);
}

void emote_motion_apply(emote_motion_t *motion, const emote_target_t *target, uint32_t now_ms)
{
    if (!motion || !target) return;
    motion->semantic = *target;
    motion->from = motion->current;
    motion->target = semantic_pose(target);
    motion->transition_started_ms = now_ms;
    motion->reaction_ms = target->affect == EMOTE_ERROR ? 70u : random_range_ms(motion, 140u, 210u);
    motion->expires_at_ms = now_ms + target->ttl_ms;
    motion->decay_ends_ms = motion->expires_at_ms + target->decay_duration_ms;
    motion->saccade_x = motion->saccade_y = 0.0f;
    motion->saccade_vx = motion->saccade_vy = 0.0f;
    motion->saccade_target_x = motion->saccade_target_y = 0.0f;

    const bool changed[EMOTE_CHANNEL_COUNT] = {
        fabsf(motion->from.gaze_x - motion->target.gaze_x) > 0.016f ||
            fabsf(motion->from.gaze_y - motion->target.gaze_y) > 0.016f,
        fabsf(motion->from.open - motion->target.open) > 0.006f ||
            fabsf(motion->from.lower_lid - motion->target.lower_lid) > 0.006f ||
            fabsf(motion->from.arc - motion->target.arc) > 0.006f,
        fabsf(motion->from.brow_y - motion->target.brow_y) > 0.006f ||
            fabsf(motion->from.brow_rotation - motion->target.brow_rotation) > 0.006f ||
            fabsf(motion->from.asymmetry - motion->target.asymmetry) > 0.006f,
        /* Below 0.025 the pupil-radius shift is under roughly 1.5 px at the
           concept scale: animate it, but do not claim it as an independent
           visually readable R5 channel. */
        fabsf(motion->from.pupil - motion->target.pupil) > 0.025f,
    };
    for (int i = 0; i < EMOTE_CHANNEL_COUNT; ++i) {
        motion->telemetry.transition_changed[i] = changed[i];
        motion->telemetry.transition_ms[i] = changed[i] ? PENDING_MS : 0;
    }
    schedule_blink(motion, now_ms);
    schedule_saccade(motion, now_ms);
}

void emote_motion_request_blink(emote_motion_t *motion, uint32_t now_ms)
{
    if (!motion || motion->blink_active) return;
    motion->blink_active = true;
    motion->blink_started_ms = now_ms;
    motion->blink_duration_ms = BLINK_DURATION_MS;
    motion->double_blink_pending = false;
}

static emote_pose_t decay_target(const emote_motion_t *motion, uint32_t now_ms)
{
    if (now_ms < motion->expires_at_ms) return motion->target;
    emote_pose_t neutral = emote_pose_for_affect(EMOTE_NEUTRAL);
    if (motion->semantic.decay_duration_ms == 0 || now_ms >= motion->decay_ends_ms) return neutral;
    float progress = (float)(now_ms - motion->expires_at_ms) / (float)motion->semantic.decay_duration_ms;
    float weight = 1.0f;
    if (motion->semantic.decay == EMOTE_DECAY_LINEAR) weight = 1.0f - progress;
    else if (motion->semantic.decay == EMOTE_DECAY_EASE_OUT) weight = (1.0f - progress) * (1.0f - progress);
    return pose_mix(neutral, motion->target, clampf(weight, 0.0f, 1.0f));
}

static float desired(float from, float target, uint32_t elapsed_ms, uint32_t delay_ms, bool anticipate)
{
    if (elapsed_ms < delay_ms) return from;
    if (anticipate && elapsed_ms < delay_ms + 40u) return from - ((target - from) * 0.15f);
    return target;
}

static void update_settle(
    emote_motion_t *motion,
    int channel,
    uint32_t elapsed,
    uint32_t delay,
    float largest_error,
    float largest_velocity,
    float tolerance,
    float velocity_tolerance)
{
    if (!motion->telemetry.transition_changed[channel] || motion->telemetry.transition_ms[channel] != PENDING_MS) return;
    if (elapsed < delay) return;
    if (largest_error < tolerance && largest_velocity < velocity_tolerance) {
        motion->telemetry.transition_ms[channel] = elapsed;
    }
}

void emote_motion_step(emote_motion_t *motion, uint32_t now_ms, float dt_seconds)
{
    if (!motion) return;
    const float dt = clampf(dt_seconds, 0.0f, 0.05f);
    const uint32_t elapsed = now_ms - motion->transition_started_ms;
    const emote_pose_t target = decay_target(motion, now_ms);
    const uint32_t gaze_delay = motion->reaction_ms;
    const uint32_t lid_delay = motion->reaction_ms + 60u;
    const uint32_t brow_delay = motion->reaction_ms + 90u;
    const uint32_t pupil_delay = motion->reaction_ms + 190u;

    if (motion->semantic.autonomy) {
        if (!motion->blink_active && now_ms >= motion->next_blink_ms) {
            motion->blink_active = true;
            motion->blink_started_ms = now_ms;
            motion->blink_duration_ms = BLINK_DURATION_MS;
            motion->double_blink_pending = random_unit(motion) < (motion->semantic.affect == EMOTE_ERROR ? 0.34f : 0.22f);
        }
        if (now_ms >= motion->next_saccade_ms) {
            const bool big = random_unit(motion) < 0.28f;
            const bool glance_back = motion->semantic.affect == EMOTE_THINKING && random_unit(motion) < 0.34f;
            motion->saccade_target_x = glance_back ? 0.36f : ((random_unit(motion) * 2.0f) - 1.0f) * (big ? 0.26f : 0.07f);
            motion->saccade_target_y = ((random_unit(motion) * 2.0f) - 1.0f) * (big ? 0.18f : 0.05f);
            schedule_saccade(motion, now_ms);
        }
        spring(&motion->saccade_x, &motion->saccade_vx, motion->saccade_target_x, 26.0f, dt);
        spring(&motion->saccade_y, &motion->saccade_vy, motion->saccade_target_y, 26.0f, dt);
    } else {
        motion->saccade_x = motion->saccade_y = 0.0f;
        motion->saccade_vx = motion->saccade_vy = 0.0f;
    }
    if (motion->blink_active &&
        now_ms >= motion->blink_started_ms + motion->blink_duration_ms + BLINK_RIGHT_LAG_MS) {
        motion->blink_active = false;
        if (motion->double_blink_pending && motion->semantic.autonomy) {
            motion->double_blink_pending = false;
            motion->next_blink_ms = now_ms + 110u;
        } else {
            motion->double_blink_pending = false;
            schedule_blink(motion, now_ms);
        }
    }
    motion->breathing_phase += dt;
    motion->telemetry.breathing = (sinf(motion->breathing_phase * 1.15f) + sinf(motion->breathing_phase * 0.41f)) * 0.5f;

    float gaze_x_target = desired(motion->from.gaze_x, target.gaze_x, elapsed, gaze_delay, true);
    float gaze_y_target = desired(motion->from.gaze_y, target.gaze_y, elapsed, gaze_delay, true);
    const bool transition_done = motion->telemetry.transition_ms[EMOTE_CHANNEL_GAZE] != PENDING_MS &&
        motion->telemetry.transition_ms[EMOTE_CHANNEL_LIDS] != PENDING_MS &&
        motion->telemetry.transition_ms[EMOTE_CHANNEL_BROWS] != PENDING_MS &&
        motion->telemetry.transition_ms[EMOTE_CHANNEL_PUPIL] != PENDING_MS;
    if (motion->semantic.autonomy && transition_done) {
        gaze_x_target += motion->saccade_x;
        gaze_y_target += motion->saccade_y;
    }
    spring(&motion->current.gaze_x, &motion->velocity.gaze_x, gaze_x_target, 21.0f, dt);
    spring(&motion->current.gaze_y, &motion->velocity.gaze_y, gaze_y_target, 21.0f, dt);
    spring(&motion->current.open, &motion->velocity.open,
           desired(motion->from.open, target.open, elapsed, lid_delay, false), 13.0f, dt);
    spring(&motion->current.lower_lid, &motion->velocity.lower_lid,
           desired(motion->from.lower_lid, target.lower_lid, elapsed, lid_delay, false), 13.0f, dt);
    spring(&motion->current.arc, &motion->velocity.arc,
           desired(motion->from.arc, target.arc, elapsed, lid_delay, false), 13.0f, dt);
    spring(&motion->current.brow_y, &motion->velocity.brow_y,
           desired(motion->from.brow_y, target.brow_y, elapsed, brow_delay, false), 8.5f, dt);
    spring(&motion->current.brow_rotation, &motion->velocity.brow_rotation,
           desired(motion->from.brow_rotation, target.brow_rotation, elapsed, brow_delay, false), 8.5f, dt);
    spring(&motion->current.asymmetry, &motion->velocity.asymmetry,
           desired(motion->from.asymmetry, target.asymmetry, elapsed, brow_delay, false), 8.5f, dt);
    spring(&motion->current.pupil, &motion->velocity.pupil,
           desired(motion->from.pupil, target.pupil, elapsed, pupil_delay, false), 5.5f, dt);
    motion->current.intensity = target.intensity;
    motion->current.pupil_shape = target.pupil_shape;
    motion->current.palette = target.palette;

    const float gaze_error = fmaxf(fabsf(motion->current.gaze_x - target.gaze_x), fabsf(motion->current.gaze_y - target.gaze_y));
    const float gaze_velocity = fmaxf(fabsf(motion->velocity.gaze_x), fabsf(motion->velocity.gaze_y));
    const float lid_error = fmaxf(fabsf(motion->current.open - target.open),
                                  fmaxf(fabsf(motion->current.lower_lid - target.lower_lid), fabsf(motion->current.arc - target.arc)));
    const float lid_velocity = fmaxf(fabsf(motion->velocity.open),
                                     fmaxf(fabsf(motion->velocity.lower_lid), fabsf(motion->velocity.arc)));
    const float brow_error = fmaxf(fabsf(motion->current.brow_y - target.brow_y),
                                   fmaxf(fabsf(motion->current.brow_rotation - target.brow_rotation),
                                         fabsf(motion->current.asymmetry - target.asymmetry)));
    const float brow_velocity = fmaxf(fabsf(motion->velocity.brow_y),
                                      fmaxf(fabsf(motion->velocity.brow_rotation), fabsf(motion->velocity.asymmetry)));
    update_settle(motion, EMOTE_CHANNEL_GAZE, elapsed, gaze_delay, gaze_error, gaze_velocity, 0.016f, 0.035f);
    update_settle(motion, EMOTE_CHANNEL_LIDS, elapsed, lid_delay, lid_error, lid_velocity, 0.006f, 0.018f);
    update_settle(motion, EMOTE_CHANNEL_BROWS, elapsed, brow_delay, brow_error, brow_velocity, 0.006f, 0.015f);
    update_settle(motion, EMOTE_CHANNEL_PUPIL, elapsed, pupil_delay,
                  fabsf(motion->current.pupil - target.pupil), fabsf(motion->velocity.pupil), 0.004f, 0.010f);
    motion->telemetry.gaze_velocity_x = motion->velocity.gaze_x;
    motion->telemetry.gaze_velocity_y = motion->velocity.gaze_y;
    motion->telemetry.gaze_velocity = sqrtf((motion->velocity.gaze_x * motion->velocity.gaze_x) +
                                            (motion->velocity.gaze_y * motion->velocity.gaze_y));
    motion->telemetry.blink_left = blink_value(motion, true, now_ms);
    motion->telemetry.blink_right = blink_value(motion, false, now_ms);
}

static float blink_value(const emote_motion_t *motion, bool left_eye, uint32_t now_ms)
{
    if (!motion->blink_active) return 0.0f;
    const uint32_t lag = left_eye ? 0u : BLINK_RIGHT_LAG_MS;
    if (now_ms < motion->blink_started_ms + lag) return 0.0f;
    const uint32_t elapsed = now_ms - motion->blink_started_ms - lag;
    if (elapsed < BLINK_ANTICIPATION_MS) {
        const float phase = (float)elapsed / (float)BLINK_ANTICIPATION_MS;
        return -BLINK_ANTICIPATION_OPEN * sinf(phase * 3.14159265358979323846f);
    }
    const uint32_t action_elapsed = elapsed - BLINK_ANTICIPATION_MS;
    if (action_elapsed < BLINK_CLOSE_MS) {
        return smoothstep((float)action_elapsed / (float)BLINK_CLOSE_MS);
    }
    if (action_elapsed < BLINK_CLOSE_MS + BLINK_HOLD_MS) return 1.0f;
    if (elapsed < motion->blink_duration_ms) {
        const uint32_t opening_ms = action_elapsed - BLINK_CLOSE_MS - BLINK_HOLD_MS;
        return 1.0f - smoothstep((float)opening_ms / (float)BLINK_OPEN_MS);
    }
    return 0.0f;
}

emote_pose_t emote_motion_render_pose(const emote_motion_t *motion, bool left_eye, uint32_t now_ms)
{
    emote_pose_t pose = motion->current;
    const float blink = blink_value(motion, left_eye, now_ms);
    /* Match the Expression Bench's velocity-perpendicular gaze arc. Convert
       its concept-resolution pixel offset back into normalized pose space so
       every renderer resolution receives the same trajectory. */
    pose.gaze_x += clampf(-motion->velocity.gaze_y * GAZE_ARC_CROSS_X_PX,
                         -GAZE_ARC_LIMIT_PX, GAZE_ARC_LIMIT_PX) / GAZE_X_TRAVEL_PX;
    pose.gaze_y += clampf(motion->velocity.gaze_x * GAZE_ARC_CROSS_Y_PX,
                         -GAZE_ARC_LIMIT_PX, GAZE_ARC_LIMIT_PX) / GAZE_Y_TRAVEL_PX;
    const float eye_drift = sinf((motion->breathing_phase * EYE_DRIFT_SPEED) +
                                 (left_eye ? EYE_DRIFT_LEFT_PHASE : EYE_DRIFT_RIGHT_PHASE)) *
                            EYE_DRIFT_AMPLITUDE;
    pose.gaze_x += eye_drift;
    pose.gaze_y += eye_drift * EYE_DRIFT_Y_SCALE;
    pose.open = clampf(pose.open - (blink * 1.02f), 0.02f, 1.30f);
    pose.pupil = clampf(pose.pupil + (motion->telemetry.breathing * 0.02f), 0.12f, 0.86f);
    return pose;
}

const emote_motion_telemetry_t *emote_motion_telemetry(const emote_motion_t *motion)
{
    return motion ? &motion->telemetry : NULL;
}
