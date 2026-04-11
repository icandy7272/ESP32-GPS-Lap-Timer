#pragma once

#include "types.h"

// Returns true only for GPIO-backed self-locking switches.
// The current hardware uses momentary GPIO buttons for both
// recording and optional future GPIO actions; the latching power switch is
// wired directly in the 5V rail and is not sampled by firmware.
bool button_is_latching(uint8_t button_id);

// Returns true when the current hardware actually populates the GPIO button.
// The present device only wires BUTTON_RECORD; BUTTON_SECTOR remains an
// optional future expansion and should not be polled as an active control.
bool button_is_connected(uint8_t button_id);
