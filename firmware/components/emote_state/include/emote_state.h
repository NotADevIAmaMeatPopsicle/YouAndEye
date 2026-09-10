#ifndef YOUANDEYE_EMOTE_STATE_H
#define YOUANDEYE_EMOTE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMOTE_UTTERANCE_MAX 128
#define EMOTE_UTTERANCE_UTF8_MAX_BYTES (EMOTE_UTTERANCE_MAX * 4)
#define EMOTE_SOURCE_ID_MAX 64
#define EMOTE_SOURCE_SESSION_MAX 96

typedef enum {
    EMOTE_NEUTRAL,
    EMOTE_HAPPY,
    EMOTE_SURPRISED,
    EMOTE_THINKING,
    EMOTE_SUSPICIOUS,
    EMOTE_SAD,
    EMOTE_EXCITED,
    EMOTE_LOVE,
    EMOTE_ERROR,
    EMOTE_LISTENING,
    EMOTE_SPEAKING,
    EMOTE_WORKING,
    EMOTE_SUCCESS,
    EMOTE_PLAYFUL,
    EMOTE_ENCOURAGING,
    EMOTE_CURIOUS,
    EMOTE_UNCERTAIN,
    EMOTE_CONCERNED,
    EMOTE_DELIGHTED,
    EMOTE_EMBARRASSED,
    EMOTE_REASSURING,
    EMOTE_SHOCKED,
    EMOTE_WEARY,
    EMOTE_CONFUSED,
    EMOTE_BLUSHING,
    EMOTE_NERVOUS,
    EMOTE_MANIACAL,
    EMOTE_STRESSED,
    EMOTE_DETERMINED,
    EMOTE_BORED,
    EMOTE_PANICKED,
    EMOTE_SCHEMING,
    EMOTE_FATIGUED,
    EMOTE_CONTENT,
    EMOTE_PLEADING,
    EMOTE_SICK,
    EMOTE_HYPED,
    EMOTE_BAFFLED,
    EMOTE_AFFECT_COUNT
} emote_affect_t;

typedef enum {
    EMOTE_MODE_IDLE,
    EMOTE_MODE_ATTENTIVE,
    EMOTE_MODE_TRACKING,
    EMOTE_MODE_SPEAKING,
    EMOTE_MODE_SLEEPY
} emote_mode_t;

typedef enum {
    EMOTE_GAZE_USER,
    EMOTE_GAZE_AWAY,
    EMOTE_GAZE_WANDER,
    EMOTE_GAZE_POINT
} emote_gaze_mode_t;

typedef enum {
    EMOTE_DECAY_HOLD,
    EMOTE_DECAY_LINEAR,
    EMOTE_DECAY_EASE_OUT
} emote_decay_t;

typedef enum {
    EMOTE_PRIORITY_AMBIENT = 10,
    EMOTE_PRIORITY_NORMAL = 50,
    EMOTE_PRIORITY_ALERT = 80,
    EMOTE_PRIORITY_CRITICAL = 100
} emote_priority_t;

typedef struct {
    uint64_t seq;
    char source_id[EMOTE_SOURCE_ID_MAX + 1];
    char source_session[EMOTE_SOURCE_SESSION_MAX + 1];
    emote_affect_t affect;
    float intensity;
    float valence;
    float arousal;
    emote_mode_t mode;
    bool autonomy;
    float energy;
    float engagement;
    float speaking;
    emote_gaze_mode_t gaze_mode;
    float gaze_x;
    float gaze_y;
    uint32_t ttl_ms;
    emote_decay_t decay;
    uint32_t decay_duration_ms;
    emote_priority_t priority;
    char utterance[EMOTE_UTTERANCE_UTF8_MAX_BYTES + 1];
} emote_target_t;

typedef struct {
    float open;
    float lower_lid;
    float gaze_x;
    float gaze_y;
    float pupil;
    float brow_y;
    float brow_rotation;
    float asymmetry;
    float arc;
    float intensity;
    uint8_t pupil_shape;
    uint8_t palette;
    uint8_t eye_effect;
} emote_pose_t;

void emote_target_neutral(emote_target_t *target);
bool emote_affect_from_name(const char *name, emote_affect_t *out);
bool emote_mode_from_name(const char *name, emote_mode_t *out);
bool emote_gaze_from_name(const char *name, emote_gaze_mode_t *out);
bool emote_decay_from_name(const char *name, emote_decay_t *out);
bool emote_priority_from_name(const char *name, emote_priority_t *out);
const char *emote_affect_name(emote_affect_t affect);
emote_pose_t emote_pose_for_affect(emote_affect_t affect);
emote_pose_t emote_pose_for_affect_intensity(emote_affect_t affect, float intensity);

#ifdef __cplusplus
}
#endif

#endif
