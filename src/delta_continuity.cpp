#include "delta_continuity.h"

namespace delta_continuity {

namespace {

inline double abs_d(double x) { return x < 0.0 ? -x : x; }

}  // namespace

double folded_progress_delta(double prev_progress, double cand_progress) {
    if (prev_progress < 0.0) {
        return 0.0;
    }
    double raw = cand_progress - prev_progress;
    // Fold lap-wrap into small positive delta.  A -0.92 step becomes
    // +0.08; a +0.92 step becomes -0.08.  This is the shortest-way
    // interpretation of "how far did progress actually move".
    if (raw > 0.5) {
        raw -= 1.0;
    } else if (raw < -0.5) {
        raw += 1.0;
    }
    return raw;
}

StepKind classify_step(double prev_progress,
                       double cand_progress,
                       double wrap_threshold,
                       double big_forward_threshold,
                       double big_backward_threshold) {
    if (prev_progress < 0.0) {
        return StepKind::Normal;
    }
    double raw = cand_progress - prev_progress;
    // Explicit lap wrap: jumped strongly negative by more than the
    // wrap threshold (e.g. 0.95 → 0.03 is -0.92).  Only one direction
    // is valid here; going +0.92 (e.g. 0.03 → 0.95) would be a
    // backwards skip, not a wrap.
    if (raw < -wrap_threshold) {
        return StepKind::Wrap;
    }
    // Large forward jump (suspicious mismatch).
    if (raw > big_forward_threshold) {
        return StepKind::Suspicious;
    }
    // Large backward jump.  Small negative is GPS jitter; big
    // negative (but not enough for wrap) is a mismatch.
    if (raw < -big_backward_threshold) {
        return StepKind::Suspicious;
    }
    return StepKind::Normal;
}

double continuity_penalty_m(double prev_progress,
                            double cand_progress,
                            double lap_length_m,
                            double weight) {
    if (prev_progress < 0.0 || weight <= 0.0 || lap_length_m <= 0.0) {
        return 0.0;
    }
    double folded = folded_progress_delta(prev_progress, cand_progress);
    double abs_m = abs_d(folded) * lap_length_m;
    return abs_m * weight;
}

}  // namespace delta_continuity
