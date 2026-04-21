#pragma once

// ============================================================
// GPS Module — ESP32-S3 GPS Lap Timer
// UART2 NMEA reception, GpsPoint generation. Timestamps use UART
// arrival time; BK-880 does not output a PPS signal (datasheet
// §2.1.3), so the PPS ISR attached on GPIO16 never fires in
// practice — the pps_* symbols are retained for historical
// compatibility but read back 0.
// See docs/ARCHITECTURE.md for task scheduling details.
// ============================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

// Dormant on this hardware — BK-880 has no PPS output, so the ISR
// that would write this value never triggers. Always returns 0.
// Kept so gps_fix.cpp's defensive "if (pps_ok)" branch compiles.
int64_t pps_read();

// Initialise UART2, attach a (dormant) PPS interrupt on GPIO16,
// create and start gps_task. Caller must create gps_queue
// (depth 4, sizeof GpsPoint) before calling.
void gps_init(QueueHandle_t gps_queue);

// FreeRTOS task function — pinned to Core 0, priority 22, 4096 stack.
// Do not call directly; gps_init() creates the task.
void gps_task(void* param);

// USB serial live-map stream control. `0` disables the stream; 1-25 Hz
// emits compact [gps-live] lines from the GPS fix path.
void gps_set_live_stream_rate(uint8_t rate_hz);
uint8_t gps_get_live_stream_rate();
