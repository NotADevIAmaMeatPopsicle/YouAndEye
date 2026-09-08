#include "emote_state.h"

#include <string.h>

typedef struct {
    const char *name;
    emote_pose_t pose;
} pose_entry_t;

#define POSE(o, l, x, y, p, by, br, a, arc_, i, shape_, palette_) \
    {o, l, x, y, p, by, br, a, arc_, i, shape_, palette_}

static const pose_entry_t POSES[EMOTE_AFFECT_COUNT] = {
    [EMOTE_NEUTRAL] = {"neutral", POSE(1.00f, 0.00f, 0.00f, 0.00f, 0.42f, 0.00f, 0.00f, 0.02f, 0.00f, 0.70f, 0, 0)},
    [EMOTE_HAPPY] = {"happy", POSE(0.95f, 0.00f, 0.00f, 0.00f, 0.44f, 0.20f, 0.06f, 0.03f, 1.00f, 0.78f, 0, 0)},
    [EMOTE_SURPRISED] = {"surprised", POSE(1.28f, 0.00f, 0.00f, -0.10f, 0.27f, 0.46f, 0.02f, 0.02f, 0.00f, 0.92f, 0, 0)},
    [EMOTE_THINKING] = {"thinking", POSE(0.90f, 0.10f, -0.42f, -0.34f, 0.34f, 0.08f, -0.12f, 0.13f, 0.00f, 0.72f, 0, 0)},
    [EMOTE_SUSPICIOUS] = {"suspicious", POSE(0.55f, 0.34f, -0.72f, 0.08f, 0.28f, -0.22f, -0.30f, 0.25f, 0.00f, 0.84f, 0, 0)},
    [EMOTE_SAD] = {"sad", POSE(0.78f, 0.14f, 0.00f, 0.34f, 0.52f, -0.02f, -0.34f, 0.04f, 0.00f, 0.65f, 0, 0)},
    [EMOTE_EXCITED] = {"excited", POSE(1.16f, 0.00f, 0.00f, -0.04f, 0.62f, 0.40f, 0.00f, 0.08f, 0.00f, 0.96f, 1, 0)},
    [EMOTE_LOVE] = {"love", POSE(1.02f, 0.00f, 0.00f, 0.02f, 0.70f, 0.22f, 0.05f, 0.03f, 0.00f, 0.94f, 2, 1)},
    [EMOTE_ERROR] = {"error", POSE(0.70f, 0.24f, 0.00f, 0.00f, 0.26f, -0.24f, -0.42f, 0.01f, 0.00f, 0.92f, 0, 2)},
    [EMOTE_LISTENING] = {"listening", POSE(1.06f, 0.00f, 0.00f, -0.06f, 0.46f, 0.16f, 0.02f, 0.03f, 0.00f, 0.74f, 0, 0)},
    [EMOTE_SPEAKING] = {"speaking", POSE(1.00f, 0.02f, 0.00f, -0.02f, 0.43f, 0.12f, 0.03f, 0.04f, 0.10f, 0.78f, 0, 0)},
    [EMOTE_WORKING] = {"working", POSE(0.94f, 0.06f, 0.18f, -0.10f, 0.36f, 0.02f, -0.06f, 0.03f, 0.00f, 0.68f, 0, 0)},
    [EMOTE_SUCCESS] = {"success", POSE(0.98f, 0.02f, 0.00f, 0.00f, 0.44f, 0.26f, 0.07f, 0.04f, 1.00f, 0.88f, 0, 3)},
    [EMOTE_PLAYFUL] = {"playful", POSE(0.86f, 0.22f, 0.46f, 0.14f, 0.40f, 0.10f, 0.30f, 0.20f, 0.00f, 0.82f, 0, 0)},
    [EMOTE_ENCOURAGING] = {"encouraging", POSE(0.95f, 0.00f, 0.00f, 0.04f, 0.44f, 0.24f, 0.08f, 0.05f, 1.00f, 0.82f, 0, 0)},
    [EMOTE_CURIOUS] = {"curious", POSE(1.02f, 0.02f, 0.28f, -0.12f, 0.50f, 0.24f, 0.20f, 0.18f, 0.00f, 0.74f, 0, 0)},
    [EMOTE_UNCERTAIN] = {"uncertain", POSE(0.84f, 0.12f, -0.34f, 0.22f, 0.39f, 0.06f, -0.24f, 0.22f, 0.00f, 0.62f, 0, 0)},
    [EMOTE_CONCERNED] = {"concerned", POSE(0.88f, 0.10f, -0.08f, 0.18f, 0.46f, 0.10f, -0.38f, 0.08f, 0.00f, 0.68f, 0, 0)},
    [EMOTE_DELIGHTED] = {"delighted", POSE(0.84f, 0.00f, 0.00f, 0.02f, 0.54f, 0.28f, 0.10f, 0.06f, 0.88f, 0.72f, 0, 0)},
    [EMOTE_EMBARRASSED] = {"embarrassed", POSE(0.72f, 0.16f, 0.46f, 0.34f, 0.48f, 0.04f, -0.22f, 0.16f, 0.16f, 0.66f, 0, 1)},
    [EMOTE_REASSURING] = {"reassuring", POSE(0.96f, 0.04f, 0.00f, 0.06f, 0.48f, 0.16f, 0.12f, 0.05f, 0.48f, 0.70f, 0, 0)},
};

void emote_target_neutral(emote_target_t *target)
{
    if (!target) return;
    memset(target, 0, sizeof(*target));
    target->affect = EMOTE_NEUTRAL;
    target->intensity = 0.45f;
    target->arousal = 0.25f;
    target->mode = EMOTE_MODE_IDLE;
    target->autonomy = true;
    target->energy = 0.30f;
    target->engagement = 0.40f;
    target->gaze_mode = EMOTE_GAZE_WANDER;
    target->ttl_ms = 600000;
    target->decay = EMOTE_DECAY_EASE_OUT;
    target->decay_duration_ms = 1200;
    target->priority = EMOTE_PRIORITY_AMBIENT;
    memcpy(target->utterance, "HELLO", 6);
}

bool emote_affect_from_name(const char *name, emote_affect_t *out)
{
    if (!name || !out) return false;
    for (int i = 0; i < EMOTE_AFFECT_COUNT; ++i) {
        if (strcmp(name, POSES[i].name) == 0) {
            *out = (emote_affect_t)i;
            return true;
        }
    }
    return false;
}

const char *emote_affect_name(emote_affect_t affect)
{
    return affect >= 0 && affect < EMOTE_AFFECT_COUNT ? POSES[affect].name : "neutral";
}

emote_pose_t emote_pose_for_affect(emote_affect_t affect)
{
    if (affect < 0 || affect >= EMOTE_AFFECT_COUNT) affect = EMOTE_NEUTRAL;
    return POSES[affect].pose;
}

emote_pose_t emote_pose_for_affect_intensity(emote_affect_t affect, float intensity)
{
    if (affect < 0 || affect >= EMOTE_AFFECT_COUNT) affect = EMOTE_NEUTRAL;
    const emote_pose_t neutral = POSES[EMOTE_NEUTRAL].pose;
    const emote_pose_t authored = POSES[affect].pose;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    const float reference = authored.intensity > 0.05f ? authored.intensity : 0.70f;
    float weight = affect == EMOTE_NEUTRAL ? 1.0f : intensity / reference;
    if (weight > 1.15f) weight = 1.15f;

    emote_pose_t pose = {
        .open = neutral.open + ((authored.open - neutral.open) * weight),
        .lower_lid = neutral.lower_lid + ((authored.lower_lid - neutral.lower_lid) * weight),
        .gaze_x = neutral.gaze_x + ((authored.gaze_x - neutral.gaze_x) * weight),
        .gaze_y = neutral.gaze_y + ((authored.gaze_y - neutral.gaze_y) * weight),
        .pupil = neutral.pupil + ((authored.pupil - neutral.pupil) * weight),
        .brow_y = neutral.brow_y + ((authored.brow_y - neutral.brow_y) * weight),
        .brow_rotation = neutral.brow_rotation + ((authored.brow_rotation - neutral.brow_rotation) * weight),
        .asymmetry = neutral.asymmetry + ((authored.asymmetry - neutral.asymmetry) * weight),
        .arc = neutral.arc + ((authored.arc - neutral.arc) * weight),
        .intensity = intensity,
        .pupil_shape = weight >= 0.55f ? authored.pupil_shape : neutral.pupil_shape,
        .palette = weight >= 0.55f ? authored.palette : neutral.palette,
    };
    return pose;
}

static bool parse_name(const char *name, const char *const *names, size_t count, int *value)
{
    if (!name || !value) return false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(name, names[i]) == 0) {
            *value = (int)i;
            return true;
        }
    }
    return false;
}

bool emote_mode_from_name(const char *name, emote_mode_t *out)
{
    static const char *const names[] = {"idle", "attentive", "tracking", "speaking", "sleepy"};
    int value = 0;
    if (!out || !parse_name(name, names, sizeof(names) / sizeof(names[0]), &value)) return false;
    *out = (emote_mode_t)value;
    return true;
}

bool emote_gaze_from_name(const char *name, emote_gaze_mode_t *out)
{
    static const char *const names[] = {"user", "away", "wander", "point"};
    int value = 0;
    if (!out || !parse_name(name, names, sizeof(names) / sizeof(names[0]), &value)) return false;
    *out = (emote_gaze_mode_t)value;
    return true;
}

bool emote_decay_from_name(const char *name, emote_decay_t *out)
{
    static const char *const names[] = {"hold", "linear", "ease_out"};
    int value = 0;
    if (!out || !parse_name(name, names, sizeof(names) / sizeof(names[0]), &value)) return false;
    *out = (emote_decay_t)value;
    return true;
}

bool emote_priority_from_name(const char *name, emote_priority_t *out)
{
    static const char *const names[] = {"ambient", "normal", "alert", "critical"};
    int index = 0;
    if (!parse_name(name, names, sizeof(names) / sizeof(names[0]), &index)) return false;
    static const emote_priority_t values[] = {
        EMOTE_PRIORITY_AMBIENT, EMOTE_PRIORITY_NORMAL, EMOTE_PRIORITY_ALERT, EMOTE_PRIORITY_CRITICAL};
    *out = values[index];
    return true;
}
