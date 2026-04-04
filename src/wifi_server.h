#pragma once

// ============================================================
// WiFi AP + HTTP server — ESP32-S3 GPS Lap Timer
// Provides web dashboard, VBO download, track/settings API.
// See docs/ARCHITECTURE.md for task scheduling details.
// ============================================================

// Initialise WiFi AP mode and register HTTP routes.
// Call once from setup(), before creating the WiFi task.
void wifi_init();

// FreeRTOS task entry point.
// Core 1, priority 5, stack 8192 bytes.
void wifi_task(void* param);
