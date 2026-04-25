#include "delta_progress_guard.h"

#include <assert.h>

using namespace delta_progress_guard;

static void test_uninitialized_progress_is_always_allowed() {
    assert(is_progress_plausible(-1.0, 0.75, 400.0, 60.0f, 0.04));
}

static void test_plausible_forward_progress_is_allowed() {
    assert(is_progress_plausible(0.100, 0.105, 400.0, 60.0f, 0.04));
}

static void test_excessive_forward_progress_is_rejected() {
    assert(!is_progress_plausible(0.100, 0.300, 400.0, 80.0f, 0.04));
}

static void test_small_backward_jitter_is_allowed() {
    assert(is_progress_plausible(0.500, 0.490, 400.0, 40.0f, 0.04));
}

static void test_large_backward_jump_is_rejected() {
    assert(!is_progress_plausible(0.500, 0.420, 400.0, 40.0f, 0.04));
}

static void test_short_lap_wrap_is_allowed_near_boundary() {
    assert(is_progress_plausible(0.990, 0.010, 400.0, 80.0f, 0.04));
}

static void test_large_apparent_wrap_is_rejected() {
    assert(!is_progress_plausible(0.800, 0.100, 400.0, 80.0f, 0.04));
}

int main() {
    test_uninitialized_progress_is_always_allowed();
    test_plausible_forward_progress_is_allowed();
    test_excessive_forward_progress_is_rejected();
    test_small_backward_jitter_is_allowed();
    test_large_backward_jump_is_rejected();
    test_short_lap_wrap_is_allowed_near_boundary();
    test_large_apparent_wrap_is_rejected();
    return 0;
}
