#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "emote_state.h"
#include "eye_renderer.h"

#define CORE_COUNT (sizeof(CORE) / sizeof(CORE[0]))
#define RUBRIC_WIDTH 32

typedef struct {
    emote_affect_t affect;
    const char *name;
} core_state_t;

static const core_state_t CORE[] = {
    {EMOTE_NEUTRAL, "neutral"},
    {EMOTE_THINKING, "thinking"},
    {EMOTE_HAPPY, "happy"},
    {EMOTE_SURPRISED, "surprised"},
    {EMOTE_SUSPICIOUS, "suspicious"},
    {EMOTE_ERROR, "error"},
};

static const uint16_t BACKGROUND_RGB565 = 0x0021;

static int require(int condition, const char *message)
{
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static uint32_t checksum(const uint16_t *pixels, size_t count)
{
    uint32_t value = 2166136261u;
    for (size_t index = 0; index < count; ++index) value = (value ^ pixels[index]) * 16777619u;
    return value;
}

static void rgb565_to_rgb888(uint16_t pixel, uint8_t output[3])
{
    const uint8_t red = (uint8_t)((pixel >> 11) & 0x1f);
    const uint8_t green = (uint8_t)((pixel >> 5) & 0x3f);
    const uint8_t blue = (uint8_t)(pixel & 0x1f);
    output[0] = (uint8_t)((red << 3) | (red >> 2));
    output[1] = (uint8_t)((green << 2) | (green >> 4));
    output[2] = (uint8_t)((blue << 3) | (blue >> 2));
}

static int write_pair_ppm(
    const char *directory,
    const char *name,
    const uint16_t *left,
    const uint16_t *right,
    int width,
    int height)
{
    if (!directory) return 0;
    char path[512];
    if (snprintf(path, sizeof(path), "%s/%s.ppm", directory, name) >= (int)sizeof(path)) return 1;
    FILE *file = fopen(path, "wb");
    if (!file) return 1;
    fprintf(file, "P6\n%d %d\n255\n", width * 2, height);
    for (int y = 0; y < height; ++y) {
        for (int eye = 0; eye < 2; ++eye) {
            const uint16_t *pixels = eye == 0 ? left : right;
            for (int x = 0; x < width; ++x) {
                uint8_t rgb[3];
                rgb565_to_rgb888(pixels[(y * width) + x], rgb);
                fwrite(rgb, sizeof(rgb), 1, file);
            }
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static int write_rubric_pair_ppm(
    const char *directory,
    const char *name,
    const char *suffix,
    const uint8_t *values,
    int eye_height,
    bool binary)
{
    if (!directory) return 0;
    char path[512];
    if (snprintf(path, sizeof(path), "%s/%s-%s.ppm", directory, name, suffix) >=
        (int)sizeof(path)) return 1;
    FILE *file = fopen(path, "wb");
    if (!file) return 1;
    fprintf(file, "P6\n%d %d\n255\n", RUBRIC_WIDTH * 2, eye_height);
    const size_t eye_size = (size_t)RUBRIC_WIDTH * eye_height;
    for (int y = 0; y < eye_height; ++y) {
        for (int eye = 0; eye < 2; ++eye) {
            const uint8_t *plane = values + (eye == 0 ? 0 : eye_size);
            for (int x = 0; x < RUBRIC_WIDTH; ++x) {
                uint8_t shade = plane[(y * RUBRIC_WIDTH) + x];
                if (binary) shade = shade ? 238 : 3;
                const uint8_t rgb[3] = {shade, shade, shade};
                fwrite(rgb, sizeof(rgb), 1, file);
            }
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static size_t count_sclera_scanline_colors(
    const uint16_t *pixels, int width, int height)
{
    const int center_x = width / 2;
    const int center_y = (height * 535) / 1000;
    const int eye_radius_x = (width * 2) / 5;
    const int iris_radius = (int)(57.0 * ((double)width / 240.0 < (double)height / 320.0
                                            ? (double)width / 240.0
                                            : (double)height / 320.0));
    const int start_x = center_x - eye_radius_x + 8;
    const int end_x = center_x - iris_radius - 5;
    uint16_t colors[16] = {0};
    size_t color_count = 0;
    for (int x = start_x; x <= end_x; ++x) {
        const uint16_t color = pixels[(center_y * width) + x];
        bool known = false;
        for (size_t index = 0; index < color_count; ++index) {
            if (colors[index] == color) {
                known = true;
                break;
            }
        }
        if (!known && color_count < sizeof(colors) / sizeof(colors[0])) colors[color_count++] = color;
    }
    return color_count;
}

static double silhouette_difference(
    const uint16_t *left, const uint16_t *right, size_t pixel_count)
{
    size_t different = 0;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const bool left_filled = left[pixel] != BACKGROUND_RGB565;
        const bool right_filled = right[pixel] != BACKGROUND_RGB565;
        if (left_filled != right_filled) ++different;
    }
    return (double)different / (double)pixel_count;
}

static double rgb_difference(
    const uint16_t *left, const uint16_t *right, size_t pixel_count)
{
    size_t different = 0;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        if (left[pixel] != right[pixel]) ++different;
    }
    return (double)different / (double)pixel_count;
}

static int rubric_height(int width, int height)
{
    return (height * RUBRIC_WIDTH + (width / 2)) / width;
}

static void downsample_eye(
    const uint16_t *source,
    int width,
    int height,
    uint8_t *silhouette,
    uint8_t *value)
{
    const int output_height = rubric_height(width, height);
    uint8_t background[3];
    rgb565_to_rgb888(BACKGROUND_RGB565, background);
    for (int output_y = 0; output_y < output_height; ++output_y) {
        const int start_y = (output_y * height) / output_height;
        const int end_y = ((output_y + 1) * height) / output_height;
        for (int output_x = 0; output_x < RUBRIC_WIDTH; ++output_x) {
            const int start_x = (output_x * width) / RUBRIC_WIDTH;
            const int end_x = ((output_x + 1) * width) / RUBRIC_WIDTH;
            uint32_t red = 0;
            uint32_t green = 0;
            uint32_t blue = 0;
            uint32_t count = 0;
            for (int y = start_y; y < end_y; ++y) {
                for (int x = start_x; x < end_x; ++x) {
                    uint8_t rgb[3];
                    rgb565_to_rgb888(source[(y * width) + x], rgb);
                    red += rgb[0];
                    green += rgb[1];
                    blue += rgb[2];
                    ++count;
                }
            }
            const uint8_t average_red = (uint8_t)(red / count);
            const uint8_t average_green = (uint8_t)(green / count);
            const uint8_t average_blue = (uint8_t)(blue / count);
            const int color_delta = abs((int)average_red - background[0]) +
                                    abs((int)average_green - background[1]) +
                                    abs((int)average_blue - background[2]);
            const size_t output_index = (size_t)output_y * RUBRIC_WIDTH + output_x;
            silhouette[output_index] = color_delta > 20 ? 1 : 0;
            value[output_index] = (uint8_t)(((54u * average_red) + (183u * average_green) +
                                             (19u * average_blue)) >> 8);
        }
    }
}

static bool blur_value_plane(uint8_t *values, int height)
{
    const size_t count = (size_t)RUBRIC_WIDTH * height;
    uint8_t *temporary = malloc(count);
    if (!temporary) return false;
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < RUBRIC_WIDTH; ++x) {
                unsigned sum = 0;
                unsigned weight = 0;
                for (int offset_y = -1; offset_y <= 1; ++offset_y) {
                    const int sample_y = y + offset_y < 0 ? 0 :
                        (y + offset_y >= height ? height - 1 : y + offset_y);
                    for (int offset_x = -1; offset_x <= 1; ++offset_x) {
                        const int sample_x = x + offset_x < 0 ? 0 :
                            (x + offset_x >= RUBRIC_WIDTH ? RUBRIC_WIDTH - 1 : x + offset_x);
                        const unsigned sample_weight =
                            (offset_x == 0 ? 2u : 1u) * (offset_y == 0 ? 2u : 1u);
                        sum += values[(sample_y * RUBRIC_WIDTH) + sample_x] * sample_weight;
                        weight += sample_weight;
                    }
                }
                temporary[(y * RUBRIC_WIDTH) + x] = (uint8_t)(sum / weight);
            }
        }
        for (size_t index = 0; index < count; ++index) values[index] = temporary[index];
    }
    free(temporary);
    return true;
}

static int build_rubric_signature(
    const uint16_t *left,
    const uint16_t *right,
    int width,
    int height,
    uint8_t *silhouette,
    uint8_t *squint)
{
    const int output_height = rubric_height(width, height);
    const size_t eye_size = (size_t)RUBRIC_WIDTH * output_height;
    downsample_eye(left, width, height, silhouette, squint);
    downsample_eye(right, width, height, silhouette + eye_size, squint + eye_size);
    if (!blur_value_plane(squint, output_height) ||
        !blur_value_plane(squint + eye_size, output_height)) return -1;
    return output_height;
}

static double byte_difference(
    const uint8_t *left, const uint8_t *right, size_t count, bool absolute_value)
{
    double difference = 0.0;
    for (size_t index = 0; index < count; ++index) {
        if (absolute_value) {
            difference += abs((int)left[index] - (int)right[index]) / 255.0;
        } else if (left[index] != right[index]) {
            difference += 1.0;
        }
    }
    return difference / (double)count;
}

static int run_resolution(int width, int height, const char *output_directory)
{
    const size_t pixel_count = (size_t)width * (size_t)height;
    const int low_height = rubric_height(width, height);
    const size_t low_pair_count = (size_t)RUBRIC_WIDTH * low_height * 2u;
    uint16_t *left_frames[CORE_COUNT] = {0};
    uint16_t *right_frames[CORE_COUNT] = {0};
    uint8_t *silhouettes[CORE_COUNT] = {0};
    uint8_t *squints[CORE_COUNT] = {0};

    int failed = 0;
    for (size_t index = 0; index < CORE_COUNT; ++index) {
        left_frames[index] = malloc(pixel_count * sizeof(uint16_t));
        right_frames[index] = malloc(pixel_count * sizeof(uint16_t));
        silhouettes[index] = malloc(low_pair_count);
        squints[index] = malloc(low_pair_count);
        if (require(left_frames[index] && right_frames[index] && silhouettes[index] && squints[index],
                    "renderer evidence allocation failed")) {
            failed = 1;
            break;
        }
        const emote_pose_t pose = emote_pose_for_affect(CORE[index].affect);
        eye_render_metrics_t metrics = {0};
        if (index == 0) {
            eye_render_metrics_t fast_metrics = {0};
            eye_renderer_render_rgb565(
                left_frames[index], width, height, &pose, true, 0.0f, 0.0f, &fast_metrics);
            if (require(fast_metrics.coverage_blends == 0,
                        "fast compatibility renderer unexpectedly enabled coverage blending")) {
                failed = 1;
                break;
            }
        }
        eye_renderer_render_rgb565_quality(
            left_frames[index], width, height, &pose, true, 0.0f, 0.0f,
            EYE_RENDER_QUALITY_ANTIALIASED, &metrics);
        eye_renderer_render_rgb565_quality(
            right_frames[index], width, height, &pose, false, 0.0f, 0.0f,
            EYE_RENDER_QUALITY_ANTIALIASED, NULL);
        const double left_right_silhouette = silhouette_difference(
            left_frames[index], right_frames[index], pixel_count);
        if (require(metrics.total_pixels == pixel_count && metrics.shaded_pixels > 0,
                    "renderer returned invalid coverage metrics") ||
            require(metrics.coverage_blends > 0,
                    "anti-aliased renderer produced no partial coverage blends") ||
            (index == 0 && require(count_sclera_scanline_colors(left_frames[index], width, height) >= 2,
                                   "high-quality sclera did not retain ordered RGB565 dithering")) ||
            (CORE[index].affect == EMOTE_SUSPICIOUS &&
             require(left_right_silhouette >= 0.10,
                     "suspicious pose lost its eye-aperture asymmetry")) ||
            require(build_rubric_signature(
                        left_frames[index], right_frames[index], width, height,
                        silhouettes[index], squints[index]) == low_height,
                    "could not build low-resolution rubric signature") ||
            require(write_pair_ppm(output_directory, CORE[index].name,
                                   left_frames[index], right_frames[index], width, height) == 0,
                    "could not write render evidence") ||
            require(write_rubric_pair_ppm(
                        output_directory, CORE[index].name, "r2",
                        silhouettes[index], low_height, true) == 0,
                    "could not write R2 evidence") ||
            require(write_rubric_pair_ppm(
                        output_directory, CORE[index].name, "r3",
                        squints[index], low_height, false) == 0,
                    "could not write R3 evidence")) {
            failed = 1;
            break;
        }
        printf("RENDER %dx%d state=%s signature=%08x shaded=%u coverage=%u\n",
               width, height, CORE[index].name, checksum(left_frames[index], pixel_count),
               (unsigned)metrics.shaded_pixels, (unsigned)metrics.coverage_blends);
        if (CORE[index].affect == EMOTE_SUSPICIOUS) {
            printf("ASYMMETRY %dx%d state=suspicious silhouette=%.4f\n",
                   width, height, left_right_silhouette);
        }
    }

    double minimum_rgb_difference = 1.0;
    double minimum_silhouette_difference = 1.0;
    double minimum_r2_difference = 1.0;
    double minimum_r3_difference = 1.0;
    if (!failed) {
        for (size_t left_index = 0; left_index < CORE_COUNT; ++left_index) {
            for (size_t right_index = left_index + 1; right_index < CORE_COUNT; ++right_index) {
                size_t rgb_different = 0;
                size_t silhouette_different = 0;
                for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
                    if (left_frames[left_index][pixel] != left_frames[right_index][pixel]) ++rgb_different;
                    const bool left_filled = left_frames[left_index][pixel] != BACKGROUND_RGB565;
                    const bool right_filled = left_frames[right_index][pixel] != BACKGROUND_RGB565;
                    if (left_filled != right_filled) ++silhouette_different;
                }
                const double rgb_ratio = (double)rgb_different / (double)pixel_count;
                const double silhouette_ratio = (double)silhouette_different / (double)pixel_count;
                if (rgb_ratio < minimum_rgb_difference) minimum_rgb_difference = rgb_ratio;
                if (silhouette_ratio < minimum_silhouette_difference) {
                    minimum_silhouette_difference = silhouette_ratio;
                }
                const double r2_ratio = byte_difference(
                    silhouettes[left_index], silhouettes[right_index], low_pair_count, false);
                const double r3_ratio = byte_difference(
                    squints[left_index], squints[right_index], low_pair_count, true);
                if (r2_ratio < minimum_r2_difference) minimum_r2_difference = r2_ratio;
                if (r3_ratio < minimum_r3_difference) minimum_r3_difference = r3_ratio;
                printf("PAIR %dx%d %s/%s rgb=%.4f silhouette=%.4f r2=%.4f r3=%.4f\n",
                       width, height, CORE[left_index].name, CORE[right_index].name,
                       rgb_ratio, silhouette_ratio, r2_ratio, r3_ratio);
            }
        }
        failed |= require(minimum_rgb_difference >= 0.02, "core RGB outputs are insufficiently distinct");
        failed |= require(minimum_silhouette_difference >= 0.005,
                          "core one-bit silhouettes are insufficiently distinct");
        failed |= require(minimum_r2_difference >= 0.050,
                          "core 32-pixel R2 silhouettes collapsed");
        failed |= require(minimum_r3_difference >= 0.020,
                          "core blurred-value R3 structures collapsed");

        const emote_pose_t neutral = emote_pose_for_affect(EMOTE_NEUTRAL);
        eye_renderer_render_rgb565_quality(
            left_frames[0], width, height, &neutral, true, 8.0f, 0.0f,
            EYE_RENDER_QUALITY_ANTIALIASED, NULL);
        eye_renderer_render_rgb565_quality(
            right_frames[0], width, height, &neutral, true, 0.0f, 8.0f,
            EYE_RENDER_QUALITY_ANTIALIASED, NULL);
        const double directional_difference = rgb_difference(
            left_frames[0], right_frames[0], pixel_count);
        failed |= require(directional_difference >= 0.002,
                          "high-quality iris stretch ignored saccade direction");
        printf("STRETCH %dx%d horizontal/vertical rgb=%.4f\n",
               width, height, directional_difference);
    }

    for (size_t index = 0; index < CORE_COUNT; ++index) {
        free(left_frames[index]);
        free(right_frames[index]);
        free(silhouettes[index]);
        free(squints[index]);
    }
    if (!failed) {
        printf("PASS: renderer %dx%d minRgb=%.4f minSilhouette=%.4f minR2=%.4f minR3=%.4f\n",
               width, height, minimum_rgb_difference, minimum_silhouette_difference,
               minimum_r2_difference, minimum_r3_difference);
    }
    return failed;
}

static int run_fast_compatibility(int width, int height, const char *output_directory)
{
    const size_t pixel_count = (size_t)width * (size_t)height;
    const int low_height = rubric_height(width, height);
    const size_t low_pair_count = (size_t)RUBRIC_WIDTH * low_height * 2u;
    uint16_t *left_frames[CORE_COUNT] = {0};
    uint16_t *right_frames[CORE_COUNT] = {0};
    uint8_t *silhouettes[CORE_COUNT] = {0};
    uint8_t *squints[CORE_COUNT] = {0};
    int failed = 0;

    for (size_t index = 0; index < CORE_COUNT; ++index) {
        left_frames[index] = malloc(pixel_count * sizeof(uint16_t));
        right_frames[index] = malloc(pixel_count * sizeof(uint16_t));
        silhouettes[index] = malloc(low_pair_count);
        squints[index] = malloc(low_pair_count);
        if (require(left_frames[index] && right_frames[index] && silhouettes[index] && squints[index],
                    "fast-render evidence allocation failed")) {
            failed = 1;
            break;
        }
        const emote_pose_t pose = emote_pose_for_affect(CORE[index].affect);
        eye_render_metrics_t left_metrics = {0};
        eye_render_metrics_t right_metrics = {0};
        eye_renderer_render_rgb565(
            left_frames[index], width, height, &pose, true, 0.0f, 0.0f, &left_metrics);
        eye_renderer_render_rgb565(
            right_frames[index], width, height, &pose, false, 0.0f, 0.0f, &right_metrics);
        char evidence_name[64];
        if (snprintf(evidence_name, sizeof(evidence_name), "fast-%s", CORE[index].name) >=
            (int)sizeof(evidence_name) ||
            require(left_metrics.coverage_blends == 0 && right_metrics.coverage_blends == 0,
                    "fast renderer unexpectedly enabled coverage blending") ||
            require(build_rubric_signature(
                        left_frames[index], right_frames[index], width, height,
                        silhouettes[index], squints[index]) == low_height,
                    "could not build fast-render rubric signature") ||
            require(write_pair_ppm(output_directory, evidence_name,
                                   left_frames[index], right_frames[index], width, height) == 0,
                    "could not write fast-render evidence") ||
            require(write_rubric_pair_ppm(
                        output_directory, evidence_name, "r2",
                        silhouettes[index], low_height, true) == 0,
                    "could not write fast-render R2 evidence") ||
            require(write_rubric_pair_ppm(
                        output_directory, evidence_name, "r3",
                        squints[index], low_height, false) == 0,
                    "could not write fast-render R3 evidence")) {
            failed = 1;
            break;
        }
    }

    double minimum_rgb_difference = 1.0;
    double minimum_silhouette_difference = 1.0;
    double minimum_r2_difference = 1.0;
    double minimum_r3_difference = 1.0;
    if (!failed) {
        for (size_t left_index = 0; left_index < CORE_COUNT; ++left_index) {
            for (size_t right_index = left_index + 1; right_index < CORE_COUNT; ++right_index) {
                const double rgb_ratio =
                    (rgb_difference(left_frames[left_index], left_frames[right_index], pixel_count) +
                     rgb_difference(right_frames[left_index], right_frames[right_index], pixel_count)) * 0.5;
                const double silhouette_ratio =
                    (silhouette_difference(left_frames[left_index], left_frames[right_index], pixel_count) +
                     silhouette_difference(right_frames[left_index], right_frames[right_index], pixel_count)) * 0.5;
                const double r2_ratio = byte_difference(
                    silhouettes[left_index], silhouettes[right_index], low_pair_count, false);
                const double r3_ratio = byte_difference(
                    squints[left_index], squints[right_index], low_pair_count, true);
                if (rgb_ratio < minimum_rgb_difference) minimum_rgb_difference = rgb_ratio;
                if (silhouette_ratio < minimum_silhouette_difference) {
                    minimum_silhouette_difference = silhouette_ratio;
                }
                if (r2_ratio < minimum_r2_difference) minimum_r2_difference = r2_ratio;
                if (r3_ratio < minimum_r3_difference) minimum_r3_difference = r3_ratio;
                printf("FAST_PAIR %dx%d %s/%s rgb=%.4f silhouette=%.4f r2=%.4f r3=%.4f\n",
                       width, height, CORE[left_index].name, CORE[right_index].name,
                       rgb_ratio, silhouette_ratio, r2_ratio, r3_ratio);
            }
        }
        failed |= require(minimum_rgb_difference >= 0.02,
                          "fast core RGB outputs are insufficiently distinct");
        failed |= require(minimum_silhouette_difference >= 0.005,
                          "fast core one-bit silhouettes are insufficiently distinct");
        failed |= require(minimum_r2_difference >= 0.050,
                          "fast core 32-pixel R2 silhouettes collapsed");
        failed |= require(minimum_r3_difference >= 0.020,
                          "fast core blurred-value R3 structures collapsed");
    }

    for (size_t index = 0; index < CORE_COUNT; ++index) {
        free(left_frames[index]);
        free(right_frames[index]);
        free(silhouettes[index]);
        free(squints[index]);
    }
    if (!failed) {
        printf("PASS: fast renderer %dx%d minRgb=%.4f minSilhouette=%.4f minR2=%.4f minR3=%.4f\n",
               width, height, minimum_rgb_difference, minimum_silhouette_difference,
               minimum_r2_difference, minimum_r3_difference);
    }
    return failed;
}

int main(int argc, char **argv)
{
    const char *output_directory = argc > 1 ? argv[1] : NULL;
    if (run_resolution(240, 320, output_directory) != 0) return 1;
    if (run_resolution(160, 160, NULL) != 0) return 1;
    if (run_fast_compatibility(160, 160, output_directory) != 0) return 1;
    return 0;
}
