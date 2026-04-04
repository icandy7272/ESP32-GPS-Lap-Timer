#pragma once

// ============================================================
// GPS Module — ESP32-S3 GPS Lap Timer
// UART2 NMEA reception, PPS interrupt, GpsPoint generation.
// See docs/ARCHITECTURE.md for task scheduling details.
// ============================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

// PPS timestamp (microseconds) — written by ISR, read by gps_task.
// Access via pps_read() for safe 64-bit reads on dual-core ESP32-S3.
int64_t pps_read();

// Initialise UART2, attach PPS interrupt, create and start gps_task.
// The caller must create gps_queue (depth 4, sizeof GpsPoint) before calling.
void gps_init(QueueHandle_t gps_queue);

// FreeRTOS task function — pinned to Core 0, priority 22, 4096 stack.
// Do not call directly; gps_init() creates the task.
void gps_task(void* param);
