#pragma once

// ============================================================
// Delta Calculation Module — ESP32-S3 GPS Lap Timer
//
// Position-Based Delta using polyline progress projection.
// Projects current GPS position onto the reference lap polyline,
// calculates progress (0.0~1.0), and computes time difference.
//
// Called every 40ms at 25Hz from lap_timer_task.
// Must not block. No dynamic allocation in the hot path.
// Reference lap stored in PSRAM (ps_malloc).
// ============================================================

#include "types.h"
#include <stdint.h>

// --- Public API -----------------------------------------------

/// Initialise delta module. Call once at startup.
void delta_init(void);

/// Set reference lap polyline. Copies points into PSRAM.
/// Replaces any existing reference. Pre-computes cumulative distances.
///   points - array of GpsPoint for the reference lap
///   count  - number of points in the array
void delta_set_reference(const GpsPoint* points, int count);

/// Calculate delta_ms for the current position.
/// Returns positive = slower than reference, negative = faster.
/// Must be called after delta_set_reference().
///   current - the latest GPS fix
int32_t delta_calculate(const GpsPoint* current);

/// Returns true if delta is valid (has reference lap AND on-track).
/// Returns false if off-track (>30m from reference) or no reference set.
bool delta_is_valid(void);

/// Returns true if a reference lap has been set (even if currently off-track).
bool delta_has_reference(void);

/// Returns true if currently off-track (lateral distance > 30m).
bool delta_is_off_track(void);

/// Reset delta state (e.g., at lap start).
/// Clears elapsed time tracking but keeps reference polyline.
void delta_reset_elapsed(void);

/// Set the lap start timestamp for elapsed time calculation.
void delta_set_lap_start(int64_t start_us);

/// Free reference polyline memory. Call when changing tracks.
void delta_free_reference(void);
