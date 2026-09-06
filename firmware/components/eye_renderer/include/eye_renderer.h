#ifndef YOUANDEYE_EYE_RENDERER_H
#define YOUANDEYE_EYE_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "emote_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t total_pixels;
    uint32_t shaded_pixels;
    uint32_t coverage_blends;
} eye_render_metrics_t;

typedef enum {
    EYE_RENDER_QUALITY_FAST = 0,
    EYE_RENDER_QUALITY_ANTIALIASED = 1,
} eye_render_quality_t;

void eye_renderer_render_rgb565(
    uint16_t *destination,
    int width,
    int height,
    const emote_pose_t *pose,
    bool left_eye,
    float gaze_velocity_x,
    float gaze_velocity_y,
    eye_render_metrics_t *metrics);

void eye_renderer_render_rgb565_quality(
    uint16_t *destination,
    int width,
    int height,
    const emote_pose_t *pose,
    bool left_eye,
    float gaze_velocity_x,
    float gaze_velocity_y,
    eye_render_quality_t quality,
    eye_render_metrics_t *metrics);

#ifdef __cplusplus
}
#endif

#endif
