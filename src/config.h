#pragma once

// ============================================================
// Runtime configuration — ESP32-S3 GPS Lap Timer
// Reads/writes config/settings.json on SD card.
// ============================================================

#include <stdint.h>
#include <stdbool.h>

// --- Application configuration (persisted to SD) ---

struct AppConfig {
    char    wifi_ssid[32];
    char    wifi_pass[32];
    uint8_t brightness;
    uint8_t gps_rate_hz;
};

// Global config instance (written by config_load / POST /api/settings)
extern AppConfig app_config;

// Load config from SD: config/settings.json
// Returns true on success, false on error (defaults remain).
bool config_load();

// Save current app_config to SD: config/settings.json
// Returns true on success, false on write error.
bool config_save();

// Reset app_config to factory defaults.
void config_set_defaults();
