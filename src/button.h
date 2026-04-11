#pragma once

// ============================================================
// Button debounce module — ESP32-S3 GPS Lap Timer
// Polls the populated GPIO buttons for the current hardware revision.
// Today that means PIN_BTN_RECORD (GPIO4, momentary); PIN_BTN_SECTOR
// (GPIO5) is kept as an optional future expansion and may be absent.
// Fan-out: each press event is sent to BOTH btn_session_queue
// AND btn_display_queue.
// See docs/ARCHITECTURE.md section 5.
// ============================================================

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Setup GPIO pins (INPUT_PULLUP), store queue handles, create task.
void button_init(QueueHandle_t btn_session_q, QueueHandle_t btn_display_q);

// FreeRTOS task entry — Core 1, priority 8, stack 2048.
// 10 ms polling loop with 50 ms debounce.
void button_task(void* param);
