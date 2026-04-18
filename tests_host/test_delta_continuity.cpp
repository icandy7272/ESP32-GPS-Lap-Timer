// Host tests for src/delta_continuity.cpp — the pure helpers that
// bias delta polyline matching towards temporally-continuous
// candidates.
//
// We do not test project_to_polyline() directly here because that
// function pulls in PSRAM allocation, Arduino headers, and the full
// reference-lap state machine.  Instead, we pin the scoring and
// classification primitives so future changes to the matcher can't
// silently break "delta should not jump across the lap" assumptions.

#include "delta_continuity.h"

#include <assert.h>
#include <cmath>
#include <stdio.h>

using namespace delta_continuity;

static int fail_count = 0;

static void check_eq(const char* desc, StepKind got, StepKind want) {
    if (got != want) {
        fprintf(stderr, "FAIL: %s — got %d, want %d\n",
                desc, (int)got, (int)want);
        fail_count++;
    }
}

static void check_near(const char* desc, double got, double want, double tol) {
    if (std::fabs(got - want) > tol) {
        fprintf(stderr, "FAIL: %s — got %.6f, want %.6f (tol %.6f)\n",
                desc, got, want, tol);
        fail_count++;
    }
}

int main() {
    // Tuning used by the caller.
    const double WRAP_TH     = 0.5;
    const double FWD_TH      = 0.05;  // 5 % of lap per fix
    const double BACK_TH     = 0.05;
    const double LAP_LEN     = 300.0; // m, typical short kart track
    const double WEIGHT      = 0.25;  // arbitrary — test values are
                                      // consistent-relative only

    // --- classify_step ---

    // Negative prev_progress: always Normal (first-fix semantics).
    check_eq("first fix (no prev)",
             classify_step(-1.0, 0.1, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Normal);

    // Tiny forward step → Normal.
    check_eq("small forward",
             classify_step(0.40, 0.41, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Normal);

    // Tiny backward jitter → Normal.
    check_eq("small backward (GPS jitter)",
             classify_step(0.40, 0.39, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Normal);

    // 4 % forward step (just under threshold) → Normal.
    check_eq("4% forward (within threshold)",
             classify_step(0.30, 0.34, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Normal);

    // 10 % forward step → Suspicious.
    check_eq("10% forward (over threshold)",
             classify_step(0.30, 0.40, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Suspicious);

    // Large backward but not a wrap → Suspicious.
    check_eq("20% backward (not a wrap)",
             classify_step(0.60, 0.40, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Suspicious);

    // Classic lap wrap.
    check_eq("lap wrap (95% → 3%)",
             classify_step(0.95, 0.03, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Wrap);

    // Just-under-wrap negative step is NOT a wrap.  0.60 → 0.20 is
    // raw -0.40, which is less severe than -WRAP_TH (-0.5).
    check_eq("big backward but below wrap threshold → Suspicious",
             classify_step(0.60, 0.20, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Suspicious);

    // A big positive raw delta is NOT a wrap.  0.03 → 0.95 is +0.92
    // — the runner did not time-travel; this is a backwards skip.
    check_eq("big forward near endpoints is not a wrap",
             classify_step(0.03, 0.95, WRAP_TH, FWD_TH, BACK_TH),
             StepKind::Suspicious);

    // --- folded_progress_delta ---

    check_near("folded: no prev returns 0",
               folded_progress_delta(-1.0, 0.7),
               0.0, 1e-12);

    check_near("folded: small forward",
               folded_progress_delta(0.40, 0.42),
               0.02, 1e-12);

    check_near("folded: small backward",
               folded_progress_delta(0.40, 0.38),
               -0.02, 1e-12);

    // Wrap: 0.95 → 0.03 raw = -0.92, folded = +0.08.
    check_near("folded: lap wrap collapses to small positive",
               folded_progress_delta(0.95, 0.03),
               0.08, 1e-12);

    // Backwards skip: 0.03 → 0.95 raw = +0.92, folded = -0.08.
    check_near("folded: backwards skip collapses to small negative",
               folded_progress_delta(0.03, 0.95),
               -0.08, 1e-12);

    // Boundary case: exactly +0.5 stays positive.
    check_near("folded: exactly +0.5 raw",
               folded_progress_delta(0.25, 0.75),
               0.5, 1e-12);

    // --- continuity_penalty_m ---

    // No prev, weight irrelevant.
    check_near("penalty: no prev returns 0",
               continuity_penalty_m(-1.0, 0.5, LAP_LEN, WEIGHT),
               0.0, 1e-9);

    // Zero weight disables the bias.
    check_near("penalty: zero weight returns 0",
               continuity_penalty_m(0.40, 0.42, LAP_LEN, 0.0),
               0.0, 1e-9);

    // Zero lap length disables the bias.
    check_near("penalty: zero lap length returns 0",
               continuity_penalty_m(0.40, 0.42, 0.0, WEIGHT),
               0.0, 1e-9);

    // 0.02 progress delta on 300 m lap = 6 m folded.  With weight
    // 0.25, penalty = 6 * 0.25 = 1.5 m.
    check_near("penalty: 2% step on 300m lap with weight 0.25",
               continuity_penalty_m(0.40, 0.42, LAP_LEN, WEIGHT),
               1.5, 1e-9);

    // Backward also produces positive penalty (uses abs).
    check_near("penalty: backward jitter is symmetric",
               continuity_penalty_m(0.40, 0.38, LAP_LEN, WEIGHT),
               1.5, 1e-9);

    // Lap wrap: folded delta is small, so penalty stays small.
    // 0.95 → 0.03 folded = +0.08 → 24 m × 0.25 = 6 m.
    check_near("penalty: lap wrap stays small (via folding)",
               continuity_penalty_m(0.95, 0.03, LAP_LEN, WEIGHT),
               6.0, 1e-9);

    // Backwards skip looks just like a wrap from magnitude standpoint
    // — the penalty alone will not distinguish them.  classify_step
    // is the helper that does (see cases above).
    check_near("penalty: backwards skip same magnitude as wrap",
               continuity_penalty_m(0.03, 0.95, LAP_LEN, WEIGHT),
               6.0, 1e-9);

    if (fail_count == 0) {
        printf("test_delta_continuity: OK\n");
        return 0;
    }
    return fail_count;
}
