#include "eye_renderer.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

/*
 * The compatibility entry point is an integer analytic rasterizer so a
 * classic ESP32 can animate two panels. The explicit high-quality entry point
 * adds sub-pixel geometry and analytic coverage for the production S3 path.
 */

typedef struct {
    uint16_t outer;
    uint16_t middle;
    uint16_t inner;
    uint16_t limbal;
} iris_palette_t;

static float clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

static int clampi(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static int mini(int a, int b)
{
    return a < b ? a : b;
}

static int maxi(int a, int b)
{
    return a > b ? a : b;
}

static uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static uint16_t rgb565_ordered_dither(int r, int g, int b, int x, int y)
{
    static const int8_t BAYER_4X4[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5,
    };
    const int threshold = BAYER_4X4[((y & 3) << 2) | (x & 3)] - 8;
    return rgb565(
        clampi(r + ((threshold * 8) / 16), 0, 255),
        clampi(g + ((threshold * 4) / 16), 0, 255),
        clampi(b + ((threshold * 8) / 16), 0, 255));
}

static uint16_t mix565(uint16_t from, uint16_t to, int amount)
{
    const int t8 = clampi(amount, 0, 255);
    const int t = t8 + (t8 >> 7);
    const int inverse = 256 - t;
    const int fr = (from >> 11) & 0x1f;
    const int fg = (from >> 5) & 0x3f;
    const int fb = from & 0x1f;
    const int tr = (to >> 11) & 0x1f;
    const int tg = (to >> 5) & 0x3f;
    const int tb = to & 0x1f;
    return (uint16_t)((((fr * inverse) + (tr * t)) >> 8 << 11) |
                      (((fg * inverse) + (tg * t)) >> 8 << 5) |
                      (((fb * inverse) + (tb * t)) >> 8));
}

static iris_palette_t palette_for(uint8_t palette)
{
    switch (palette) {
    case 1:
        return (iris_palette_t){rgb565(122, 20, 74), rgb565(181, 54, 116), rgb565(236, 88, 158), rgb565(60, 8, 38)};
    case 2:
        return (iris_palette_t){rgb565(112, 26, 26), rgb565(170, 56, 48), rgb565(228, 86, 74), rgb565(54, 10, 10)};
    case 3:
        return (iris_palette_t){rgb565(20, 88, 60), rgb565(53, 151, 105), rgb565(86, 214, 150), rgb565(8, 40, 28)};
    default:
        return (iris_palette_t){rgb565(17, 52, 106), rgb565(48, 112, 190), rgb565(86, 174, 244), rgb565(4, 14, 34)};
    }
}

static bool symbolic_pupil_inside(int dx, int dy, int radius, uint8_t shape)
{
    const int radius_sq = radius * radius;
    if (shape == 0) return (dx * dx) + (dy * dy) <= radius_sq;
    if (shape == 1) {
        const int ax = abs(dx);
        const int ay = abs(dy);
        const bool cross = ax <= radius / 3 || ay <= radius / 3;
        const bool diagonal = abs(ax - ay) <= radius / 3;
        return (cross || diagonal) && (ax + ay <= (radius * 3) / 2);
    }

    const int lobe_radius = maxi(2, (radius * 3) / 5);
    const int lobe_y = dy + (radius / 4);
    const int left_x = dx + (radius / 3);
    const int right_x = dx - (radius / 3);
    const bool lobes = ((left_x * left_x) + (lobe_y * lobe_y) <= lobe_radius * lobe_radius) ||
                       ((right_x * right_x) + (lobe_y * lobe_y) <= lobe_radius * lobe_radius);
    const bool point = dy >= -(radius / 5) && dy <= radius &&
                       abs(dx) <= radius - ((dy + radius / 5) * 2 / 3);
    return lobes || point;
}

static void draw_disc(
    uint16_t *destination, int width, int height,
    int cx, int cy, int radius, uint16_t color, uint32_t *shaded)
{
    const int radius_sq = radius * radius;
    const int min_y = maxi(0, cy - radius);
    const int max_y = mini(height - 1, cy + radius);
    const int min_x = maxi(0, cx - radius);
    const int max_x = mini(width - 1, cx + radius);
    for (int y = min_y; y <= max_y; ++y) {
        const int dy = y - cy;
        for (int x = min_x; x <= max_x; ++x) {
            const int dx = x - cx;
            if ((dx * dx) + (dy * dy) <= radius_sq) {
                destination[(y * width) + x] = color;
                if (shaded) ++*shaded;
            }
        }
    }
}

static void draw_curved_tapered_brow_fast(
    uint16_t *destination, int width, int height,
    float cx, float cy, float angle, float half_width, float scale,
    bool left_eye, uint16_t color, uint32_t *shaded)
{
    const int steps = maxi(8, (int)(half_width * 2.0f));
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    for (int step = 0; step <= steps; ++step) {
        const float u = -1.0f + (2.0f * (float)step / (float)steps);
        const float inner_t = clampf((((left_eye ? u : -u) + 1.0f) * 0.5f), 0.0f, 1.0f);
        const float local_x = u * half_width;
        const float local_y = -7.0f * scale * (1.0f - (u * u));
        const float radius = (4.5f + (5.5f * inner_t)) * scale;
        const int x = (int)lroundf(cx + (cosine * local_x) - (sine * local_y));
        const int y = (int)lroundf(cy + (sine * local_x) + (cosine * local_y));
        draw_disc(destination, width, height, x, y, maxi(2, (int)lroundf(radius)), color, shaded);
    }
}

static void render_fast_rgb565(
    uint16_t *destination,
    int width,
    int height,
    const emote_pose_t *pose,
    bool left_eye,
    float gaze_velocity_x,
    float gaze_velocity_y,
    eye_render_metrics_t *metrics)
{
    if (!destination || !pose || width <= 0 || height <= 0) return;

    const uint32_t total = (uint32_t)width * (uint32_t)height;
    const uint16_t background = rgb565(4, 6, 10);
    for (uint32_t pixel = 0; pixel < total; ++pixel) destination[pixel] = background;

    const float x_scale = (float)width / 240.0f;
    const float y_scale = (float)height / 320.0f;
    const float scale = x_scale < y_scale ? x_scale : y_scale;
    const int cx = width / 2;
    const int cy = (height * 535) / 1000;
    /* Wide-open states need a distinct round/tall silhouette and more visible
       sclera to read as surprise rather than an ordinary attentive gaze. */
    const float wide_eye = clampf((pose->open - 1.05f) / 0.23f, 0.0f, 1.0f);
    const int base_rx = maxi(8, (width * 2) / 5);
    const int base_ry = maxi(8, (height * 53) / 160);
    const int rx = maxi(8, (int)((float)base_rx * (1.0f - (wide_eye * 0.10f))));
    const int ry = maxi(8, (int)((float)base_ry * (1.0f + (wide_eye * 0.08f))));
    const int rx_sq = rx * rx;
    const int ry_sq = ry * ry;
    const int ellipse_limit = rx_sq * ry_sq;
    const int outline_band = maxi(1, (ellipse_limit / mini(rx, ry)) * 2);
    const int mirror = left_eye ? -1 : 1;
    const int iris_radius = maxi(4, (int)((57.0f + (pose->intensity * 3.0f)) * scale *
                                          (1.0f - (wide_eye * 0.22f))));
    const float gaze_speed = sqrtf((gaze_velocity_x * gaze_velocity_x) +
                                    (gaze_velocity_y * gaze_velocity_y));
    const int stretch = clampi((int)(gaze_speed * 0.018f * 256.0f), 0, 20);
    const int iris_rx = maxi(3, (iris_radius * (256 + stretch)) / 256);
    const int iris_ry = maxi(3, (iris_radius * (256 - (stretch / 2))) / 256);
    const int iris_x = cx + (int)(pose->gaze_x * 28.0f * x_scale) + (int)(mirror * 1.8f * x_scale);
    const int iris_y = cy + (int)(pose->gaze_y * 24.0f * y_scale);
    const int iris_rx_sq = iris_rx * iris_rx;
    const int iris_ry_sq = iris_ry * iris_ry;
    const int iris_limit = iris_rx_sq * iris_ry_sq;
    const int pupil_radius = maxi(2, (int)(iris_radius * clampf(pose->pupil, 0.12f, 0.86f)));
    const float asymmetry = left_eye ? pose->asymmetry : -pose->asymmetry;
    const float eye_open = clampf(pose->open + (asymmetry * 0.55f), 0.02f, 1.4f);
    const int upper_base = cy - (int)(ry * eye_open * 0.94f) + (int)(pose->gaze_y * 5.0f * y_scale);
    const int lower_base = cy + (int)(ry * ((eye_open * 0.86f) - (pose->lower_lid * 0.5f))) + (int)(pose->gaze_y * 8.0f * y_scale);
    const int upper_curve = (int)((26.0f - (pose->brow_y * 10.0f)) * y_scale);
    const int lower_curve = (int)((16.0f + (pose->lower_lid * 22.0f)) * y_scale);
    const int upper_tilt = (int)(pose->brow_rotation * mirror * 58.0f * y_scale);
    const int arc_amount = clampi((int)(pose->arc * 255.0f), 0, 255);
    /* Quantize the eye-to-arc morph on embedded targets. Blending every pixel
       during the short transition costs a classic ESP32 its 30 fps budget. */
    const bool arc_visible = arc_amount >= 128;
    const int arc_center_y = cy + (int)(76.0f * y_scale);
    const int arc_radius = maxi(4, (int)(108.0f * scale));
    const int arc_thickness = maxi(2, (int)((15.0f + (pose->intensity * 4.0f)) * scale));
    const int arc_outer_sq = (arc_radius + arc_thickness) * (arc_radius + arc_thickness);
    const int arc_inner = maxi(0, arc_radius - arc_thickness);
    const int arc_inner_sq = arc_inner * arc_inner;
    const int arc_outline_band = maxi(2, arc_radius * 4);

    const uint16_t outline = rgb565(9, 16, 28);
    const uint16_t pupil = pose->pupil_shape == 2 ? rgb565(252, 232, 240) : rgb565(6, 11, 20);
    const uint16_t highlight_primary = rgb565(255, 255, 255);
    const uint16_t highlight_secondary = rgb565(214, 235, 255);
    const uint16_t brow = rgb565(72, 84, 104);
    const uint16_t lid = rgb565(102, 132, 174);
    const iris_palette_t iris = palette_for(pose->palette);

    const int h1x = iris_x - ((iris_radius * 40) / 100);
    const int h1y = iris_y - ((iris_radius * 44) / 100);
    const int h1r = maxi(2, (iris_radius * 25) / 100);
    const int h2x = iris_x + ((iris_radius * 34) / 100);
    const int h2y = iris_y + ((iris_radius * 40) / 100);
    const int h2r = maxi(1, (iris_radius * 10) / 100);
    const int h1r_sq = h1r * h1r;
    const int h2r_sq = h2r * h2r;

    const int min_x = maxi(0, cx - rx - (int)(12.0f * x_scale));
    const int max_x = mini(width - 1, cx + rx + (int)(12.0f * x_scale));
    const int min_y = maxi(0, cy - ry);
    const int max_y = mini(height - 1, cy + ry);
    const int iris_limbal_start = (iris_limit * 82) / 100;
    const int iris_outer_start = (iris_limit * 55) / 100;
    const int iris_middle_start = (iris_limit * 24) / 100;
    int upper_edges[width];
    int lower_edges[width];
    for (int x = min_x; x <= max_x; ++x) {
        const int dx = x - cx;
        const int dx_sq = dx * dx;
        upper_edges[x] = upper_base + ((dx_sq * upper_curve) / rx_sq) + ((dx * upper_tilt) / rx);
        lower_edges[x] = lower_base - ((dx_sq * lower_curve) / rx_sq);
    }
    uint32_t shaded = 0;

    for (int y = min_y; y <= max_y; ++y) {
        const int dy = y - cy;
        const int vertical = dy * dy * rx_sq;
        const int row_mix = clampi(((y - (cy - ry)) * 255) / maxi(1, ry * 2), 0, 255);
        const uint16_t sclera = mix565(rgb565(186, 203, 222), rgb565(238, 244, 251), row_mix);
        for (int x = min_x; x <= max_x; ++x) {
            const int dx = x - cx;
            const int dx_sq = dx * dx;
            if (arc_visible) {
                const int arc_dy = y - arc_center_y;
                const int arc_distance_sq = dx_sq + (arc_dy * arc_dy);
                if (arc_dy <= -(int)(2.0f * y_scale) &&
                    arc_distance_sq >= arc_inner_sq && arc_distance_sq <= arc_outer_sq) {
                    const bool arc_outline = arc_distance_sq - arc_inner_sq <= arc_outline_band ||
                                             arc_outer_sq - arc_distance_sq <= arc_outline_band;
                    destination[(y * width) + x] = arc_outline ? outline : iris.inner;
                    ++shaded;
                }
                continue;
            }
            const int ellipse_value = (dx_sq * ry_sq) + vertical;
            const int upper_edge = upper_edges[x];
            const int lower_edge = lower_edges[x];
            const bool inside_eye = ellipse_value <= ellipse_limit && y >= upper_edge && y <= lower_edge;

            if (!inside_eye) continue;

            uint16_t normal_color = sclera;
            if (inside_eye) {
                const int iris_dx = x - iris_x;
                const int iris_dy = y - iris_y;
                const int iris_value = (iris_dx * iris_dx * iris_ry_sq) + (iris_dy * iris_dy * iris_rx_sq);
                if (iris_value <= iris_limit) {
                    if (iris_value > iris_limbal_start) normal_color = iris.limbal;
                    else if (iris_value > iris_outer_start) normal_color = iris.outer;
                    else if (iris_value > iris_middle_start) normal_color = iris.middle;
                    else normal_color = iris.inner;

                    if (symbolic_pupil_inside(iris_dx, iris_dy, pupil_radius, pose->pupil_shape)) normal_color = pupil;
                    const int h1dx = x - h1x;
                    const int h1dy = y - h1y;
                    const int h2dx = x - h2x;
                    const int h2dy = y - h2y;
                    if ((h1dx * h1dx) + (h1dy * h1dy) <= h1r_sq) normal_color = highlight_primary;
                    else if ((h2dx * h2dx) + (h2dy * h2dy) <= h2r_sq) normal_color = highlight_secondary;
                }

                if (ellipse_value >= ellipse_limit - outline_band || y - upper_edge <= 2 || lower_edge - y <= 2) normal_color = outline;
            }

            destination[(y * width) + x] = normal_color;
            ++shaded;
        }
    }

    if (eye_open < 0.42f) {
        const int seam_amount = clampi((int)(((0.42f - eye_open) / 0.36f) * 255.0f), 0, 255);
        const uint16_t seam_color = mix565(background, lid, seam_amount);
        const int seam_radius = maxi(1, (int)(3.2f * scale));
        for (int x = cx - ((rx * 82) / 100); x <= cx + ((rx * 82) / 100); ++x) {
            const int dx = x - cx;
            const int seam_y = cy + (int)(pose->gaze_y * 6.0f * y_scale) + ((dx * dx * (int)(8.0f * y_scale)) / rx_sq) + ((dx * upper_tilt) / (rx * 10));
            draw_disc(destination, width, height, x, seam_y, seam_radius, seam_color, &shaded);
        }
    }

    const int brow_center_y = cy - ry - (int)(18.0f * y_scale) - (int)((pose->brow_y + asymmetry) * 28.0f * y_scale);
    const float brow_angle = (pose->brow_rotation + (asymmetry * 0.65f)) * mirror;
    const int brow_half = maxi(4, (int)(55.0f * x_scale));
    draw_curved_tapered_brow_fast(
        destination, width, height, (float)cx, (float)brow_center_y,
        brow_angle, (float)brow_half, scale, left_eye, brow, &shaded);

    if (metrics) {
        metrics->total_pixels = total;
        metrics->shaded_pixels = shaded > total ? total : shaded;
        metrics->coverage_blends = 0;
    }
}

static int coverage_from_distance(float signed_distance)
{
    const float coverage = clampf(0.5f - signed_distance, 0.0f, 1.0f);
    return clampi((int)((coverage * 255.0f) + 0.5f), 0, 255);
}

static int ellipse_coverage(float dx, float dy, float rx, float ry)
{
    const float nx = dx / rx;
    const float ny = dy / ry;
    const float normalized_sq = (nx * nx) + (ny * ny);
    const float minimum_radius = rx < ry ? rx : ry;
    const float margin = 1.0f / minimum_radius;
    const float definitely_inside = 1.0f - margin;
    const float definitely_outside = 1.0f + margin;
    if (normalized_sq <= definitely_inside * definitely_inside) return 255;
    if (normalized_sq >= definitely_outside * definitely_outside) return 0;
    return coverage_from_distance((sqrtf(normalized_sq) - 1.0f) * minimum_radius);
}

static int circle_coverage(float dx, float dy, float radius)
{
    const float distance_sq = (dx * dx) + (dy * dy);
    const float inside = radius - 0.75f;
    const float outside = radius + 0.75f;
    if (inside > 0.0f && distance_sq <= inside * inside) return 255;
    if (distance_sq >= outside * outside) return 0;
    return coverage_from_distance(sqrtf(distance_sq) - radius);
}

/* Edge blending uses a gamma-2 approximation. It avoids the dark fringe made
   by interpolating gamma-encoded RGB565 channels while keeping the operation
   confined to the one-pixel analytic coverage band. */
static int mix_channel_linear(int from, int to, int amount, int maximum)
{
    const float alpha = (float)amount / 255.0f;
    const float linear = ((float)(from * from) * (1.0f - alpha)) +
                         ((float)(to * to) * alpha);
    return clampi((int)(sqrtf(linear) + 0.5f), 0, maximum);
}

static uint16_t composite_coverage(
    uint16_t under, uint16_t over, int amount, uint32_t *coverage_blends)
{
    const int alpha = clampi(amount, 0, 255);
    if (alpha <= 0) return under;
    if (alpha >= 255) return over;
    if (coverage_blends) ++*coverage_blends;
    const int red = mix_channel_linear((under >> 11) & 0x1f, (over >> 11) & 0x1f, alpha, 0x1f);
    const int green = mix_channel_linear((under >> 5) & 0x3f, (over >> 5) & 0x3f, alpha, 0x3f);
    const int blue = mix_channel_linear(under & 0x1f, over & 0x1f, alpha, 0x1f);
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static bool symbolic_pupil_inside_sample(float dx, float dy, float radius, uint8_t shape)
{
    if (shape == 0) return (dx * dx) + (dy * dy) <= radius * radius;
    if (shape == 1) {
        const float ax = fabsf(dx);
        const float ay = fabsf(dy);
        const bool cross = ax <= radius / 3.0f || ay <= radius / 3.0f;
        const bool diagonal = fabsf(ax - ay) <= radius / 3.0f;
        return (cross || diagonal) && (ax + ay <= radius * 1.5f);
    }

    const float lobe_radius = fmaxf(2.0f, radius * 0.6f);
    const float lobe_y = dy + (radius * 0.25f);
    const float left_x = dx + (radius / 3.0f);
    const float right_x = dx - (radius / 3.0f);
    const bool lobes = ((left_x * left_x) + (lobe_y * lobe_y) <= lobe_radius * lobe_radius) ||
                       ((right_x * right_x) + (lobe_y * lobe_y) <= lobe_radius * lobe_radius);
    const bool point = dy >= -(radius * 0.2f) && dy <= radius &&
                       fabsf(dx) <= radius - ((dy + (radius * 0.2f)) * (2.0f / 3.0f));
    return lobes || point;
}

static int symbolic_pupil_coverage(float dx, float dy, float radius, uint8_t shape)
{
    if (shape == 0) return circle_coverage(dx, dy, radius);
    static const float OFFSETS[4][2] = {
        {-0.25f, -0.25f}, {0.25f, -0.25f}, {-0.25f, 0.25f}, {0.25f, 0.25f},
    };
    int inside = 0;
    for (size_t index = 0; index < 4; ++index) {
        if (symbolic_pupil_inside_sample(
                dx + OFFSETS[index][0], dy + OFFSETS[index][1], radius, shape)) {
            ++inside;
        }
    }
    return (inside * 255) / 4;
}

static void draw_disc_aa(
    uint16_t *destination, int width, int height,
    float cx, float cy, float radius,
    uint16_t color, uint32_t *shaded, uint32_t *coverage_blends)
{
    const int min_x = maxi(0, (int)floorf(cx - radius - 1.0f));
    const int max_x = mini(width - 1, (int)ceilf(cx + radius + 1.0f));
    const int min_y = maxi(0, (int)floorf(cy - radius - 1.0f));
    const int max_y = mini(height - 1, (int)ceilf(cy + radius + 1.0f));
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const int alpha = circle_coverage((float)x - cx, (float)y - cy, radius);
            if (alpha <= 0) continue;
            const size_t pixel = (size_t)y * (size_t)width + (size_t)x;
            destination[pixel] = composite_coverage(destination[pixel], color, alpha, coverage_blends);
            if (shaded) ++*shaded;
        }
    }
}

static void draw_curved_tapered_brow_aa(
    uint16_t *destination, int width, int height,
    float cx, float cy, float angle, float half_width, float scale,
    bool left_eye, uint16_t color, uint32_t *shaded, uint32_t *coverage_blends)
{
    const int steps = maxi(8, (int)(half_width * 2.0f));
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    for (int step = 0; step <= steps; ++step) {
        const float u = -1.0f + (2.0f * (float)step / (float)steps);
        const float inner_t = clampf((((left_eye ? u : -u) + 1.0f) * 0.5f), 0.0f, 1.0f);
        const float local_x = u * half_width;
        const float local_y = -7.0f * scale * (1.0f - (u * u));
        const float radius = fmaxf(2.0f, (4.5f + (5.5f * inner_t)) * scale);
        const float x = cx + (cosine * local_x) - (sine * local_y);
        const float y = cy + (sine * local_x) + (cosine * local_y);
        draw_disc_aa(
            destination, width, height, x, y, radius, color, shaded, coverage_blends);
    }
}

static void draw_seam_aa(
    uint16_t *destination, int width, int height,
    int cx, float cy, int rx, float curve, float tilt, float radius,
    uint16_t color, uint32_t *shaded, uint32_t *coverage_blends)
{
    const int half_width = (rx * 82) / 100;
    const int min_x = maxi(0, cx - half_width);
    const int max_x = mini(width - 1, cx + half_width);
    for (int x = min_x; x <= max_x; ++x) {
        const float dx = (float)(x - cx);
        const float seam_y = cy + ((dx * dx * curve) / (float)(rx * rx)) +
                             ((dx * tilt) / ((float)rx * 10.0f));
        const int min_y = maxi(0, (int)floorf(seam_y - radius - 1.0f));
        const int max_y = mini(height - 1, (int)ceilf(seam_y + radius + 1.0f));
        for (int y = min_y; y <= max_y; ++y) {
            const int alpha = coverage_from_distance(fabsf((float)y - seam_y) - radius);
            if (alpha <= 0) continue;
            const size_t pixel = (size_t)y * (size_t)width + (size_t)x;
            destination[pixel] = composite_coverage(destination[pixel], color, alpha, coverage_blends);
            if (shaded) ++*shaded;
        }
    }
}

static void render_antialiased_rgb565(
    uint16_t *destination,
    int width,
    int height,
    const emote_pose_t *pose,
    bool left_eye,
    float gaze_velocity_x,
    float gaze_velocity_y,
    eye_render_metrics_t *metrics)
{
    if (!destination || !pose || width <= 0 || height <= 0) return;

    const uint32_t total = (uint32_t)width * (uint32_t)height;
    const uint16_t background = rgb565(4, 6, 10);
    for (uint32_t pixel = 0; pixel < total; ++pixel) destination[pixel] = background;

    const float x_scale = (float)width / 240.0f;
    const float y_scale = (float)height / 320.0f;
    const float scale = x_scale < y_scale ? x_scale : y_scale;
    const float cx = (float)width * 0.5f;
    const float cy = (float)height * 0.535f;
    const float wide_eye = clampf((pose->open - 1.05f) / 0.23f, 0.0f, 1.0f);
    const float rx = fmaxf(8.0f, (float)width * 0.4f * (1.0f - (wide_eye * 0.10f)));
    const float ry = fmaxf(8.0f, (float)height * 0.33125f * (1.0f + (wide_eye * 0.08f)));
    const int mirror = left_eye ? -1 : 1;
    const float iris_radius = fmaxf(
        4.0f, (57.0f + (pose->intensity * 3.0f)) * scale * (1.0f - (wide_eye * 0.22f)));
    const float gaze_speed = sqrtf((gaze_velocity_x * gaze_velocity_x) +
                                    (gaze_velocity_y * gaze_velocity_y));
    const float stretch = clampf(gaze_speed * 0.018f, 0.0f, 20.0f / 256.0f);
    const float velocity_axis_x = gaze_speed > 0.001f ? gaze_velocity_x / gaze_speed : 1.0f;
    const float velocity_axis_y = gaze_speed > 0.001f ? gaze_velocity_y / gaze_speed : 0.0f;
    const float iris_x = cx + (pose->gaze_x * 28.0f * x_scale) + ((float)mirror * 1.8f * x_scale);
    const float iris_y = cy + (pose->gaze_y * 24.0f * y_scale);
    const float pupil_radius = fmaxf(2.0f, iris_radius * clampf(pose->pupil, 0.12f, 0.86f));
    const float asymmetry = left_eye ? pose->asymmetry : -pose->asymmetry;
    const float eye_open = clampf(pose->open + (asymmetry * 0.55f), 0.02f, 1.4f);
    const float upper_base = cy - (ry * eye_open * 0.94f) + (pose->gaze_y * 5.0f * y_scale);
    const float lower_base = cy + (ry * ((eye_open * 0.86f) - (pose->lower_lid * 0.5f))) +
                             (pose->gaze_y * 8.0f * y_scale);
    const float upper_curve = (26.0f - (pose->brow_y * 10.0f)) * y_scale;
    const float lower_curve = (16.0f + (pose->lower_lid * 22.0f)) * y_scale;
    const float upper_tilt = pose->brow_rotation * (float)mirror * 58.0f * y_scale;
    const int arc_amount = clampi((int)((pose->arc * 255.0f) + 0.5f), 0, 255);
    const int eye_amount = 255 - arc_amount;
    const float arc_center_y = cy + (76.0f * y_scale);
    const float arc_radius = fmaxf(4.0f, 108.0f * scale);
    const float arc_thickness = fmaxf(2.0f, (15.0f + (pose->intensity * 4.0f)) * scale);
    const float arc_inner = fmaxf(0.0f, arc_radius - arc_thickness);
    const float arc_outer = arc_radius + arc_thickness;

    const uint16_t outline = rgb565(9, 16, 28);
    const uint16_t pupil = pose->pupil_shape == 2 ? rgb565(252, 232, 240) : rgb565(6, 11, 20);
    const uint16_t highlight_primary = rgb565(255, 255, 255);
    const uint16_t highlight_secondary = rgb565(214, 235, 255);
    const uint16_t brow = rgb565(72, 84, 104);
    const uint16_t lid = rgb565(102, 132, 174);
    const iris_palette_t iris = palette_for(pose->palette);

    const float h1x = iris_x - (iris_radius * 0.40f);
    const float h1y = iris_y - (iris_radius * 0.44f);
    const float h1r = fmaxf(2.0f, iris_radius * 0.25f);
    const float h2x = iris_x + (iris_radius * 0.34f);
    const float h2y = iris_y + (iris_radius * 0.40f);
    const float h2r = fmaxf(1.0f, iris_radius * 0.10f);

    const int min_x = maxi(0, (int)floorf(cx - rx - (12.0f * x_scale) - 1.0f));
    const int max_x = mini(width - 1, (int)ceilf(cx + rx + (12.0f * x_scale) + 1.0f));
    const int min_y = maxi(0, (int)floorf(cy - ry - 1.0f));
    const int max_y = mini(height - 1, (int)ceilf(cy + ry + 1.0f));
    uint32_t shaded = 0;
    uint32_t coverage_blends = 0;

    if (eye_amount > 0) {
        for (int y = min_y; y <= max_y; ++y) {
            const float row_mix_f = clampf(((float)y - (cy - ry)) / (ry * 2.0f), 0.0f, 1.0f);
            const int sclera_red = 186 + (int)(52.0f * row_mix_f);
            const int sclera_green = 203 + (int)(41.0f * row_mix_f);
            const int sclera_blue = 222 + (int)(29.0f * row_mix_f);
            for (int x = min_x; x <= max_x; ++x) {
                const uint16_t sclera = rgb565_ordered_dither(
                    sclera_red, sclera_green, sclera_blue, x, y);
                const float dx = (float)x - cx;
                const float normalized_x = dx / rx;
                const float upper_edge = upper_base + (normalized_x * normalized_x * upper_curve) +
                                         (normalized_x * upper_tilt);
                const float lower_edge = lower_base - (normalized_x * normalized_x * lower_curve);
                const int globe_alpha = ellipse_coverage(dx, (float)y - cy, rx, ry);
                const int upper_alpha = coverage_from_distance(upper_edge - (float)y);
                const int lower_alpha = coverage_from_distance((float)y - lower_edge);
                int eye_alpha = mini(globe_alpha, mini(upper_alpha, lower_alpha));
                eye_alpha = (eye_alpha * eye_amount) / 255;
                if (eye_alpha <= 0) continue;

                uint16_t normal_color = sclera;
                const float iris_dx = (float)x - iris_x;
                const float iris_dy = (float)y - iris_y;
                const float iris_along =
                    ((iris_dx * velocity_axis_x) + (iris_dy * velocity_axis_y)) / (1.0f + stretch);
                const float iris_across =
                    (-iris_dx * velocity_axis_y) + (iris_dy * velocity_axis_x);
                const float iris_ratio = ((iris_along * iris_along) + (iris_across * iris_across)) /
                                         (iris_radius * iris_radius);
                const int iris_alpha = ellipse_coverage(
                    iris_along, iris_across, iris_radius, iris_radius);
                if (iris_alpha > 0) {
                    uint16_t iris_color;
                    if (iris_ratio > 0.82f) iris_color = iris.limbal;
                    else if (iris_ratio > 0.55f) iris_color = iris.outer;
                    else if (iris_ratio > 0.24f) iris_color = iris.middle;
                    else iris_color = iris.inner;

                    const int pupil_alpha = pose->pupil_shape == 0
                        ? circle_coverage(iris_along, iris_across, pupil_radius)
                        : symbolic_pupil_coverage(
                              iris_dx, iris_dy, pupil_radius, pose->pupil_shape);
                    iris_color = composite_coverage(iris_color, pupil, pupil_alpha, &coverage_blends);
                    iris_color = composite_coverage(
                        iris_color, highlight_primary,
                        circle_coverage((float)x - h1x, (float)y - h1y, h1r), &coverage_blends);
                    iris_color = composite_coverage(
                        iris_color, highlight_secondary,
                        circle_coverage((float)x - h2x, (float)y - h2y, h2r), &coverage_blends);
                    normal_color = composite_coverage(
                        normal_color, iris_color, iris_alpha, &coverage_blends);
                }

                const float ellipse_ratio = (dx * dx) / (rx * rx) +
                                            (((float)y - cy) * ((float)y - cy)) / (ry * ry);
                const float ellipse_inside = (1.0f - ellipse_ratio) * fminf(rx, ry) * 0.5f;
                const float inside_distance = fminf(
                    ellipse_inside, fminf((float)y - upper_edge, lower_edge - (float)y));
                const float outline_width = fmaxf(1.5f, 3.0f * scale);
                const int outline_alpha = coverage_from_distance(inside_distance - outline_width);
                normal_color = composite_coverage(
                    normal_color, outline, outline_alpha, &coverage_blends);

                const size_t pixel = (size_t)y * (size_t)width + (size_t)x;
                destination[pixel] = composite_coverage(
                    destination[pixel], normal_color, eye_alpha, &coverage_blends);
                ++shaded;
            }
        }
    }

    if (arc_amount > 0) {
        const int arc_min_x = maxi(0, (int)floorf(cx - arc_outer - 1.0f));
        const int arc_max_x = mini(width - 1, (int)ceilf(cx + arc_outer + 1.0f));
        const int arc_min_y = maxi(0, (int)floorf(arc_center_y - arc_outer - 1.0f));
        const int arc_max_y = mini(height - 1, (int)ceilf(arc_center_y - (2.0f * y_scale) + 1.0f));
        for (int y = arc_min_y; y <= arc_max_y; ++y) {
            for (int x = arc_min_x; x <= arc_max_x; ++x) {
                const float dx = (float)x - cx;
                const float dy = (float)y - arc_center_y;
                const int outer_alpha = circle_coverage(dx, dy, arc_outer);
                const int inner_alpha = circle_coverage(dx, dy, arc_inner);
                const int clip_alpha = coverage_from_distance(
                    (float)y - (arc_center_y - (2.0f * y_scale)));
                int arc_alpha = mini(outer_alpha, 255 - inner_alpha);
                arc_alpha = (arc_alpha * clip_alpha * arc_amount) / (255 * 255);
                if (arc_alpha <= 0) continue;
                const float radial_distance = sqrtf((dx * dx) + (dy * dy));
                const float edge_distance = fminf(
                    radial_distance - arc_inner, arc_outer - radial_distance);
                const int outline_alpha = coverage_from_distance(edge_distance - fmaxf(1.0f, 2.0f * scale));
                const uint16_t arc_color = composite_coverage(
                    iris.inner, outline, outline_alpha, &coverage_blends);
                const size_t pixel = (size_t)y * (size_t)width + (size_t)x;
                destination[pixel] = composite_coverage(
                    destination[pixel], arc_color, arc_alpha, &coverage_blends);
                ++shaded;
            }
        }
    }

    if (eye_open < 0.42f && arc_amount < 230) {
        const int seam_amount = clampi((int)(((0.42f - eye_open) / 0.36f) * 255.0f), 0, 255);
        const uint16_t seam_color = mix565(background, lid, seam_amount);
        draw_seam_aa(
            destination, width, height, (int)cx,
            cy + (pose->gaze_y * 6.0f * y_scale), (int)rx, 8.0f * y_scale, upper_tilt,
            fmaxf(1.0f, 3.2f * scale), seam_color, &shaded, &coverage_blends);
    }

    const float brow_center_y = cy - ry - (18.0f * y_scale) -
                                ((pose->brow_y + asymmetry) * 28.0f * y_scale);
    const float brow_angle = (pose->brow_rotation + (asymmetry * 0.65f)) * (float)mirror;
    const float brow_half = fmaxf(4.0f, 55.0f * x_scale);
    draw_curved_tapered_brow_aa(
        destination, width, height, cx, brow_center_y, brow_angle,
        brow_half, scale, left_eye, brow, &shaded, &coverage_blends);

    if (metrics) {
        metrics->total_pixels = total;
        metrics->shaded_pixels = shaded > total ? total : shaded;
        metrics->coverage_blends = coverage_blends;
    }
}

void eye_renderer_render_rgb565_quality(
    uint16_t *destination,
    int width,
    int height,
    const emote_pose_t *pose,
    bool left_eye,
    float gaze_velocity_x,
    float gaze_velocity_y,
    eye_render_quality_t quality,
    eye_render_metrics_t *metrics)
{
    if (quality == EYE_RENDER_QUALITY_ANTIALIASED) {
        render_antialiased_rgb565(
            destination, width, height, pose, left_eye,
            gaze_velocity_x, gaze_velocity_y, metrics);
        return;
    }
    render_fast_rgb565(
        destination, width, height, pose, left_eye,
        gaze_velocity_x, gaze_velocity_y, metrics);
}

void eye_renderer_render_rgb565(
    uint16_t *destination,
    int width,
    int height,
    const emote_pose_t *pose,
    bool left_eye,
    float gaze_velocity_x,
    float gaze_velocity_y,
    eye_render_metrics_t *metrics)
{
    render_fast_rgb565(
        destination, width, height, pose, left_eye,
        gaze_velocity_x, gaze_velocity_y, metrics);
}
