#pragma once

namespace delta_progress_guard {

bool is_progress_plausible(double last_progress,
                           double candidate_progress,
                           double total_dist_m,
                           float speed_kmh,
                           double dt_s);

}  // namespace delta_progress_guard
