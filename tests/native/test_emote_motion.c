#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "emote_motion.h"

static const emote_affect_t CORE[] = {
    EMOTE_NEUTRAL,
    EMOTE_THINKING,
    EMOTE_HAPPY,
    EMOTE_SURPRISED,
    EMOTE_SUSPICIOUS,
    EMOTE_ERROR,
};

static int require(int condition, const char *message)
{
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int pose_is_finite(const emote_pose_t *pose)
{
    return isfinite(pose->open) && isfinite(pose->lower_lid) &&
           isfinite(pose->gaze_x) && isfinite(pose->gaze_y) &&
           isfinite(pose->pupil) && isfinite(pose->brow_y) &&
           isfinite(pose->brow_rotation) && isfinite(pose->asymmetry) &&
           isfinite(pose->arc) && isfinite(pose->intensity);
}

static float pose_delta(const emote_pose_t *left, const emote_pose_t *right)
{
    return fabsf(left->open - right->open) +
           fabsf(left->lower_lid - right->lower_lid) +
           fabsf(left->gaze_x - right->gaze_x) +
           fabsf(left->gaze_y - right->gaze_y) +
           fabsf(left->pupil - right->pupil) +
           fabsf(left->brow_y - right->brow_y) +
           fabsf(left->brow_rotation - right->brow_rotation) +
           fabsf(left->asymmetry - right->asymmetry) +
           fabsf(left->arc - right->arc);
}

static int test_blink_timing(void)
{
    emote_motion_t motion;
    const uint32_t started_ms = 1000u;
    emote_motion_init(&motion, 0x51a7u, started_ms);
    emote_motion_request_blink(&motion, started_ms);

    emote_motion_step(&motion, started_ms + 17u, 0.017f);
    if (require(emote_motion_telemetry(&motion)->blink_left < -0.039f &&
                    emote_motion_telemetry(&motion)->blink_right < -0.038f,
                "both eyes did not begin with the 34 ms four-percent anticipation widen")) return 1;

    emote_motion_step(&motion, started_ms + 103u, 0.050f);
    const float left_closing = emote_motion_telemetry(&motion)->blink_left;
    const float right_closing = emote_motion_telemetry(&motion)->blink_right;
    if (require(left_closing > 0.99f && right_closing >= 0.97f &&
                    right_closing <= left_closing &&
                    fabsf(right_closing - (left_closing * motion.blink_right_scale)) < 0.0001f,
                "blink did not keep synchronized timing with restrained per-eye amplitude")) return 1;

    emote_motion_step(&motion, started_ms + 119u, 0.016f);
    if (require(emote_motion_telemetry(&motion)->blink_left == 1.0f &&
                    emote_motion_telemetry(&motion)->blink_right >= 0.97f &&
                    emote_motion_telemetry(&motion)->blink_right <= 1.0f,
                "blink hold lost its synchronized, subtly uneven closure")) return 1;

    emote_motion_step(&motion, started_ms + 206u, 0.050f);
    const float left_opening = emote_motion_telemetry(&motion)->blink_left;
    const float right_opening = emote_motion_telemetry(&motion)->blink_right;
    if (require(left_opening > 0.45f && left_opening < 0.55f &&
                    right_opening >= left_opening * 0.97f && right_opening <= left_opening &&
                    fabsf(right_opening - (left_opening * motion.blink_right_scale)) < 0.0001f,
                "blink opening lost synchronized timing or restrained asymmetry")) return 1;

    emote_motion_step(&motion, started_ms + 279u, 0.050f);
    if (require(!motion.blink_active &&
                    emote_motion_telemetry(&motion)->blink_left == 0.0f &&
                    emote_motion_telemetry(&motion)->blink_right == 0.0f,
                "synchronized blink did not finish after both eye curves completed")) return 1;

    return 0;
}

static int test_intensity_scales_expression_geometry(void)
{
    const emote_pose_t neutral = emote_pose_for_affect(EMOTE_NEUTRAL);
    const emote_pose_t authored = emote_pose_for_affect(EMOTE_SUSPICIOUS);
    const emote_pose_t muted = emote_pose_for_affect_intensity(EMOTE_SUSPICIOUS, 0.0f);
    const emote_pose_t half = emote_pose_for_affect_intensity(
        EMOTE_SUSPICIOUS, authored.intensity * 0.5f);
    const emote_pose_t full = emote_pose_for_affect_intensity(
        EMOTE_SUSPICIOUS, authored.intensity);

    if (require(pose_delta(&muted, &neutral) < 0.0001f,
                "zero intensity did not return expression geometry to neutral")) return 1;
    if (require(pose_delta(&full, &authored) < 0.0001f,
                "authored intensity did not reproduce the authored pose")) return 1;
    if (require(fabsf(half.gaze_x - ((neutral.gaze_x + authored.gaze_x) * 0.5f)) < 0.0001f &&
                    fabsf(half.open - ((neutral.open + authored.open) * 0.5f)) < 0.0001f,
                "half intensity did not produce a halfway geometric expression")) return 1;
    return 0;
}

static int test_nuanced_affects_and_acting_profiles(void)
{
    for (int affect = EMOTE_CURIOUS; affect < EMOTE_AFFECT_COUNT; ++affect) {
        const emote_pose_t pose = emote_pose_for_affect((emote_affect_t)affect);
        emote_affect_t parsed = EMOTE_NEUTRAL;
        if (require(pose_is_finite(&pose), "a nuanced affect produced a non-finite pose") ||
            require(emote_affect_from_name(emote_affect_name((emote_affect_t)affect), &parsed),
                    "a nuanced affect name did not parse") ||
            require(parsed == (emote_affect_t)affect, "a nuanced affect failed name round-trip")) {
            return 1;
        }
    }

    const uint32_t started_ms = 1000u;
    emote_motion_t motion;
    emote_target_t target;
    emote_motion_init(&motion, 0x51a7u, started_ms);
    emote_target_neutral(&target);
    target.autonomy = false;
    target.affect = EMOTE_THINKING;
    target.intensity = emote_pose_for_affect(EMOTE_THINKING).intensity;
    emote_motion_apply(&motion, &target, started_ms);
    if (require(motion.reaction_ms >= 230u && motion.reaction_ms < 300u,
                "thinking lost its brief hesitation window")) return 1;
    const emote_pose_t thinking = emote_motion_render_pose(
        &motion, true, started_ms + (motion.reaction_ms / 2u));
    if (require(thinking.open > motion.current.open && thinking.pupil < motion.current.pupil,
                "thinking hesitation did not visibly gather before the transition")) return 1;

    target.affect = EMOTE_LISTENING;
    target.intensity = emote_pose_for_affect(EMOTE_LISTENING).intensity;
    emote_motion_apply(&motion, &target, started_ms);
    if (require(motion.reaction_ms >= 105u && motion.reaction_ms < 150u,
                "listening lost its quick attention lead")) return 1;
    const emote_pose_t listening = emote_motion_render_pose(&motion, true, started_ms + 350u);
    if (require(listening.open > motion.current.open && listening.pupil > motion.current.pupil,
                "listening attention cue did not open toward the user")) return 1;

    target.affect = EMOTE_SUCCESS;
    target.intensity = emote_pose_for_affect(EMOTE_SUCCESS).intensity;
    emote_motion_apply(&motion, &target, started_ms);
    const emote_pose_t success = emote_motion_render_pose(&motion, true, started_ms + 1050u);
    if (require(success.open < motion.current.open && success.arc > motion.current.arc,
                "success did not include the small post-event relief release")) return 1;

    return 0;
}

static int test_gaze_arc(void)
{
    emote_motion_t motion;
    emote_motion_init(&motion, 0x51a7u, 1000u);
    motion.current.gaze_x = 0.0f;
    motion.current.gaze_y = 0.0f;
    motion.velocity.gaze_x = 0.0f;
    motion.velocity.gaze_y = 0.0f;
    const emote_pose_t baseline_left = emote_motion_render_pose(&motion, true, 1000u);
    motion.velocity.gaze_x = 1.0f;
    motion.velocity.gaze_y = 0.5f;

    emote_pose_t pose = emote_motion_render_pose(&motion, true, 1000u);
    if (require(fabsf((pose.gaze_x - baseline_left.gaze_x) - (-0.75f / 28.0f)) < 0.0001f &&
                    fabsf((pose.gaze_y - baseline_left.gaze_y) - (1.1f / 24.0f)) < 0.0001f,
                "render pose did not add the specified velocity-perpendicular gaze arc")) return 1;

    motion.velocity.gaze_x = 0.0f;
    motion.velocity.gaze_y = 0.0f;
    const emote_pose_t baseline_right = emote_motion_render_pose(&motion, false, 1000u);
    motion.velocity.gaze_x = 100.0f;
    motion.velocity.gaze_y = 100.0f;
    pose = emote_motion_render_pose(&motion, false, 1000u);
    if (require(fabsf((pose.gaze_x - baseline_right.gaze_x) - (-4.0f / 28.0f)) < 0.0001f &&
                    fabsf((pose.gaze_y - baseline_right.gaze_y) - (4.0f / 24.0f)) < 0.0001f,
                "gaze arc did not clamp to the four-pixel concept-space limit")) return 1;

    return 0;
}

static int test_independent_eye_drift(void)
{
    emote_motion_t motion;
    emote_motion_init(&motion, 0x51a7u, 1000u);
    motion.current.gaze_x = 0.0f;
    motion.current.gaze_y = 0.0f;
    motion.velocity.gaze_x = 0.0f;
    motion.velocity.gaze_y = 0.0f;
    motion.breathing_phase = 0.0f;

    const emote_pose_t left = emote_motion_render_pose(&motion, true, 1000u);
    const emote_pose_t right = emote_motion_render_pose(&motion, false, 1000u);
    const float expected_left = sinf(0.7f) * 0.006f;
    const float expected_right = sinf(2.1f) * 0.006f;
    if (require(fabsf(left.gaze_x - expected_left) < 0.0001f &&
                    fabsf(right.gaze_x - expected_right) < 0.0001f &&
                    fabsf(left.gaze_y - (expected_left * 0.55f)) < 0.0001f &&
                    fabsf(right.gaze_y - (expected_right * 0.55f)) < 0.0001f,
                "per-eye drift no longer matches the simulator profile")) return 1;
    if (require(fabsf(left.gaze_x - right.gaze_x) > 0.0005f,
                "per-eye drift collapsed into mirrored lockstep")) return 1;
    return 0;
}

static int test_sixty_second_idle_life(void)
{
    emote_motion_t motion;
    uint32_t now_ms = 1000u;
    emote_motion_init(&motion, 0x51a7u, now_ms);
    emote_target_t target;
    emote_target_neutral(&target);
    target.ttl_ms = 120000u;
    emote_motion_apply(&motion, &target, now_ms);

    emote_pose_t previous_left = emote_motion_render_pose(&motion, true, now_ms);
    emote_pose_t previous_right = emote_motion_render_pose(&motion, false, now_ms);
    unsigned static_frames = 0;
    unsigned blink_samples = 0;
    unsigned gaze_samples = 0;
    unsigned samples = 0;

    while (now_ms < 61000u) {
        now_ms += 33u;
        emote_motion_step(&motion, now_ms, 0.033f);
        const emote_pose_t left = emote_motion_render_pose(&motion, true, now_ms);
        const emote_pose_t right = emote_motion_render_pose(&motion, false, now_ms);
        const emote_motion_telemetry_t *telemetry = emote_motion_telemetry(&motion);
        const float delta = pose_delta(&left, &previous_left) + pose_delta(&right, &previous_right);
        if (delta <= 1e-7f) ++static_frames;
        if (telemetry->blink_left != 0.0f || telemetry->blink_right != 0.0f) ++blink_samples;
        if (telemetry->gaze_velocity > 0.005f) ++gaze_samples;
        previous_left = left;
        previous_right = right;
        ++samples;
    }

    if (require(samples >= 1800u, "idle audit did not cover sixty seconds") ||
        require(static_frames == 0u, "idle motion produced a fully static rendered frame") ||
        require(blink_samples > 0u, "idle motion scheduled no blink samples") ||
        require(gaze_samples > 0u, "idle motion scheduled no gaze samples")) return 1;
    printf("IDLE duration=60s samples=%u static=%u blink=%u gaze=%u\n",
           samples, static_frames, blink_samples, gaze_samples);
    return 0;
}

static int test_attention_decays_and_recovers(void)
{
    emote_motion_t motion;
    emote_target_t target;
    const uint32_t started_ms = 1000u;
    emote_motion_init(&motion, 0x51a7u, started_ms);
    emote_target_neutral(&target);
    emote_motion_apply(&motion, &target, started_ms);

    const emote_pose_t fresh = emote_motion_render_pose(&motion, true, started_ms + 1000u);
    const emote_pose_t rested = emote_motion_render_pose(&motion, true, started_ms + 150000u);
    if (require(rested.open < fresh.open - 0.09f,
                "long-idle neutral did not soften the upper lids") ||
        require(rested.lower_lid > fresh.lower_lid + 0.035f,
                "long-idle neutral did not raise the lower lids") ||
        require(rested.pupil < fresh.pupil - 0.025f,
                "long-idle neutral did not relax the pupil") ||
        require(rested.gaze_y > fresh.gaze_y + 0.045f,
                "long-idle neutral did not settle its gaze")) return 1;

    emote_motion_step(&motion, started_ms + 150000u, 0.033f);
    if (require(motion.telemetry.attention_decay > 0.99f,
                "attention-decay telemetry did not report full idle age")) return 1;

    target.affect = EMOTE_LISTENING;
    target.intensity = emote_pose_for_affect(EMOTE_LISTENING).intensity;
    emote_motion_apply(&motion, &target, started_ms + 150001u);
    if (require(emote_motion_telemetry(&motion)->attention_decay == 0.0f,
                "new interaction did not immediately clear attention decay")) return 1;
    return 0;
}

static int test_personality_is_bounded_and_preserves_core_geometry(void)
{
    emote_motion_t baseline;
    emote_motion_t styled;
    emote_target_t target;
    const uint32_t started_ms = 1000u;
    emote_motion_init(&baseline, 0x51a7u, started_ms);
    emote_motion_init(&styled, 0x51a7u, started_ms);
    emote_target_neutral(&target);
    target.affect = EMOTE_SUSPICIOUS;
    target.intensity = emote_pose_for_affect(EMOTE_SUSPICIOUS).intensity;
    target.autonomy = false;

    emote_personality_t personality = {
        EMOTE_BLINK_LIVELY,
        EMOTE_GAZE_CURIOUS,
        EMOTE_IDLE_PLAYFUL,
        0.65f,
    };
    emote_motion_set_personality(&styled, &personality, started_ms);
    emote_motion_apply(&baseline, &target, started_ms);
    emote_motion_apply(&styled, &target, started_ms);
    if (require(pose_delta(&baseline.target, &styled.target) < 0.0001f,
                "personality controls changed authored core-expression geometry")) return 1;

    emote_motion_request_blink(&baseline, started_ms);
    emote_motion_request_blink(&styled, started_ms);
    if (require(styled.blink_duration_ms < baseline.blink_duration_ms,
                "lively blink style did not shorten the bounded blink")) return 1;
    emote_motion_step(&styled, started_ms + 90u, 0.05f);
    if (require(styled.telemetry.blink_left != 0.0f &&
                    styled.telemetry.blink_right != 0.0f,
                "styled blink lost synchronized two-eye motion")) return 1;

    personality.blink_style = (emote_blink_style_t)99;
    personality.gaze_style = (emote_gaze_style_t)99;
    personality.idle_style = (emote_idle_style_t)99;
    personality.energy = 4.0f;
    emote_motion_set_personality(&styled, &personality, started_ms);
    if (require(styled.personality.blink_style == EMOTE_BLINK_NATURAL &&
                    styled.personality.gaze_style == EMOTE_GAZE_ATTENTIVE &&
                    styled.personality.idle_style == EMOTE_IDLE_CALM &&
                    fabsf(styled.personality.energy - 0.85f) < 0.0001f,
                "invalid personality values were not normalized and bounded")) return 1;
    return 0;
}

static int test_semantic_modifiers_are_bounded(void)
{
    emote_motion_t baseline;
    emote_motion_t urgent;
    emote_target_t target;
    const uint32_t started_ms = 1000u;
    emote_motion_init(&baseline, 0x51a7u, started_ms);
    emote_motion_init(&urgent, 0x51a7u, started_ms);
    emote_target_neutral(&target);
    target.affect = EMOTE_THINKING;
    target.intensity = emote_pose_for_affect(EMOTE_THINKING).intensity;

    const emote_modifiers_t default_modifiers = {0.5f, 0.5f, 0.3f};
    const emote_modifiers_t unsafe_modifiers = {-1.0f, 2.0f, 2.0f};
    emote_motion_set_modifiers(&baseline, &default_modifiers);
    emote_motion_set_modifiers(&urgent, &unsafe_modifiers);
    emote_motion_apply(&baseline, &target, started_ms);
    emote_motion_apply(&urgent, &target, started_ms);
    if (require(urgent.modifiers.warmth == 0.0f &&
                    urgent.modifiers.confidence == 1.0f &&
                    urgent.modifiers.urgency == 1.0f,
                "semantic modifiers were not clamped to the safe range") ||
        require(urgent.reaction_ms < baseline.reaction_ms,
                "urgency did not shorten the local reaction timing")) return 1;
    return 0;
}

int main(void)
{
    if (test_blink_timing()) return 1;
    if (test_intensity_scales_expression_geometry()) return 1;
    if (test_nuanced_affects_and_acting_profiles()) return 1;
    if (test_gaze_arc()) return 1;
    if (test_independent_eye_drift()) return 1;
    if (test_sixty_second_idle_life()) return 1;
    if (test_attention_decays_and_recovers()) return 1;
    if (test_personality_is_bounded_and_preserves_core_geometry()) return 1;
    if (test_semantic_modifiers_are_bounded()) return 1;

    emote_motion_t motion;
    uint32_t now_ms = 1000u;
    emote_motion_init(&motion, 0x51a7u, now_ms);

    for (size_t state_index = 0; state_index < sizeof(CORE) / sizeof(CORE[0]); ++state_index) {
        emote_target_t target;
        emote_target_neutral(&target);
        target.affect = CORE[state_index];
        target.mode = EMOTE_MODE_ATTENTIVE;
        target.autonomy = false;
        target.gaze_mode = EMOTE_GAZE_AWAY;
        target.ttl_ms = 600000u;
        emote_motion_apply(&motion, &target, now_ms);

        for (int step_index = 0; step_index < 200; ++step_index) {
            now_ms += 16u;
            emote_motion_step(&motion, now_ms, 0.016f);
        }

        const emote_motion_telemetry_t *telemetry = emote_motion_telemetry(&motion);
        if (require(telemetry != NULL, "motion telemetry unavailable") ||
            require(pose_is_finite(&motion.current), "motion produced a non-finite pose")) {
            return 1;
        }

        uint32_t previous = 0u;
        bool have_previous = false;
        uint32_t minimum_gap = UINT32_MAX;
        for (int channel = 0; channel < EMOTE_CHANNEL_COUNT; ++channel) {
            if (!telemetry->transition_changed[channel]) continue;
            const uint32_t settled = telemetry->transition_ms[channel];
            if (require(settled != UINT32_MAX, "a changed channel did not settle")) return 1;
            if (have_previous) {
                if (require(settled > previous, "changed channels settled out of order")) return 1;
                const uint32_t gap = settled - previous;
                if (gap < minimum_gap) minimum_gap = gap;
                if (require(gap >= 40u, "changed channels settled within the 40 ms R5 floor")) return 1;
            }
            previous = settled;
            have_previous = true;
        }

        printf("MOTION state=%s", emote_affect_name(CORE[state_index]));
        for (int channel = 0; channel < EMOTE_CHANNEL_COUNT; ++channel) {
            if (telemetry->transition_changed[channel]) {
                printf(" c%d=%u", channel, (unsigned)telemetry->transition_ms[channel]);
            } else {
                printf(" c%d=n/a", channel);
            }
        }
        if (minimum_gap == UINT32_MAX) printf(" minGap=n/a\n");
        else printf(" minGap=%u\n", (unsigned)minimum_gap);
    }

    puts("PASS: core-six motion is finite and satisfies ordered R5 settling");
    return 0;
}
