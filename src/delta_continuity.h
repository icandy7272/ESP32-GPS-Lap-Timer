#pragma once

// Pure helpers for reasoning about how the delta engine's "progress
// along the reference polyline" should evolve between consecutive GPS
// fixes.  Extracted here so tests_host can pin the classification
// without dragging in FreeRTOS / Arduino / PSRAM.
//
// Why this exists: project_to_polyline() in src/delta.cpp picks the
// reference segment whose geometry best matches the current point.  At
// crossovers or near-return geometry the best geometric match can
// legitimately be a different segment from where the runner "really"
// is — the one-shot minimum-lateral-distance rule can then flick
// between two locations at the same track-cross pattern, producing the
// classic "delta jumped" artifact.  Preferring a locally continuous
// match (one near the previous progress) disambiguates these cases
// without breaking the first fix after a reset, lap wrap, or a
// genuine "I was lost" recovery.
//
// All inputs are in normalised progress units (0.0 = start of
// reference, 1.0 = end).  Helpers are side-effect free.

namespace delta_continuity {

// Classification of one progress step against the last good match.
// Used by both the matching loop (to bias towards continuity) and
// downstream diagnostics (to flag unexpected jumps).
enum class StepKind {
    // Small forward step, or a small GPS-jitter backward step.  Safe
    // to accept directly.
    Normal,

    // Large negative step that crosses the lap boundary (e.g. 0.95 →
    // 0.03).  Legitimate on lap completion; caller usually resets the
    // progress tracker and accepts.
    Wrap,

    // Big forward or large backward jump with no plausible physical
    // cause.  Likely a projection mis-match (crossover, reversal of
    // side).  Caller should either fall back to frozen delta or
    // downweight the candidate.
    Suspicious,
};

// If `prev_progress` is negative, treats the step as a first-sample
// case and always returns Normal (nothing to compare against).
// `wrap_threshold` defines how negative the step must be before it is
// classified as a lap wrap (typical 0.5 = jumped back more than half
// the reference).
// `big_forward_threshold` caps legitimate forward steps per sample
// (typical 0.05 = 5 % of lap per fix).
// `big_backward_threshold` caps legitimate backward jitter (typical
// 0.05).
StepKind classify_step(double prev_progress,
                       double cand_progress,
                       double wrap_threshold,
                       double big_forward_threshold,
                       double big_backward_threshold);

// Signed progress delta from prev to cand, folded so lap wrap is
// represented as a small positive value (e.g. 0.95 → 0.03 returns
// +0.08 instead of -0.92).  If prev_progress is negative, returns 0.
// Useful as a tiebreak penalty in geometric match scoring.
double folded_progress_delta(double prev_progress, double cand_progress);

// Scoring penalty, in metres-equivalent, to add to a candidate
// segment's lateral distance so the match loop biases towards
// continuity.  Scales the folded progress delta by
// `lap_length_m * weight` — both caller-provided so the function
// stays dependency-free.  weight = 0 disables the bias (falls back
// to pure min-lateral-distance matching).
double continuity_penalty_m(double prev_progress,
                            double cand_progress,
                            double lap_length_m,
                            double weight);

}  // namespace delta_continuity
