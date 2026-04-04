#pragma once

// ============================================================
// TFT Display Module — ESP32-S3 GPS Lap Timer
// Renders three screens: Driving, Status, Lap List.
// Runs on Core 1, priority 10, 8192 byte stack.
// See docs/ARCHITECTURE.md and docs/PRD.md section 6.
// ============================================================

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// --- Public API ---

// Initialise TFT hardware, backlight, and start the display task.
// btn_display_q: receives ButtonEvent from button task
// spi_mtx:       shared SPI bus mutex (SD + TFT)
// session_mtx:   protects SessionState reads
void display_init(QueueHandle_t     btn_display_q,
                  SemaphoreHandle_t spi_mtx,
                  SemaphoreHandle_t session_mtx);

// FreeRTOS task entry point (Core 1, priority 10, stack 8192).
// param is unused — dependencies injected via display_init().
void display_task(void* param);
